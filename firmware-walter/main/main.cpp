#include "sdkconfig.h"
#include "walter_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "lwip/ip4_addr.h"

#include "nvs_flash.h"

#include "cJSON.h"

#include <atomic>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "womo_analog.h"
#include "womo_hx711.h"
#include "womo_rs485.h"

#if WALTER_ENABLE_BME680
#include "bme680.h"
#endif

#if WALTER_ENABLE_BNO055
#include "bno055.h"
#endif

static const char *TAG = "walter_main";

static void configure_logging(void)
{
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_log_level_set("womo_analog", ESP_LOG_INFO);
    esp_log_level_set("womo_hx711", ESP_LOG_INFO);
    esp_log_level_set("womo_rs485", ESP_LOG_INFO);
    esp_log_level_set("womo_gps", ESP_LOG_INFO);
    esp_log_level_set("WalterModem", ESP_LOG_INFO);
}

static TickType_t ms_to_ticks(uint32_t ms)
{
    if (ms == 0) {
        return 1;
    }
    TickType_t ticks = pdMS_TO_TICKS(ms);
    return (ticks == 0) ? 1 : ticks;
}

static float clampf(float value, float min_val, float max_val)
{
    if (value < min_val) {
        return min_val;
    }
    if (value > max_val) {
        return max_val;
    }
    return value;
}

static const char* heading_to_compass(float heading);

#if WALTER_ENABLE_BME680 || WALTER_ENABLE_BNO055
static i2c_master_bus_handle_t s_sensor_i2c_bus = nullptr;

static esp_err_t ensure_sensor_i2c_bus(void)
{
    if (s_sensor_i2c_bus != nullptr) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.i2c_port = static_cast<i2c_port_t>(WALTER_SENSOR_I2C_PORT);
    bus_cfg.sda_io_num = static_cast<gpio_num_t>(WALTER_SENSOR_I2C_SDA_GPIO);
    bus_cfg.scl_io_num = static_cast<gpio_num_t>(WALTER_SENSOR_I2C_SCL_GPIO);
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = (WALTER_SENSOR_I2C_ENABLE_INTERNAL_PULLUPS != 0);

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_sensor_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialise sensor I2C bus: %s", esp_err_to_name(err));
        s_sensor_i2c_bus = nullptr;
        return err;
    }

    ESP_LOGI(TAG,
             "Sensor I2C bus initialised (port=%d, SDA=%d, SCL=%d, %lu Hz)",
             WALTER_SENSOR_I2C_PORT,
             WALTER_SENSOR_I2C_SDA_GPIO,
             WALTER_SENSOR_I2C_SCL_GPIO,
             static_cast<unsigned long>(WALTER_SENSOR_I2C_SPEED_HZ));
    return ESP_OK;
}
#endif

#if WALTER_ENABLE_BME680
static constexpr size_t kBme680MaxSensors = 2;

typedef struct {
    float temperature_c;
    float humidity_pct;
    float pressure_hpa;
    float dewpoint_c;
    float gas_res_kohm;
    uint16_t iaq_score;
    bool gas_valid;
    bool heater_stable;
    uint8_t gas_range;
    uint8_t gas_index;
} bme680_measurement_t;

typedef struct {
    uint8_t address;
    size_t index;
    const char *log_tag;
} bme680_task_params_t;

static void sensor_state_mark_bme680_configured(size_t index, uint8_t address);
static void sensor_state_publish_bme680(size_t index,
                                        const bme680_measurement_t &measurement,
                                        bool fallback,
                                        int64_t timestamp_us);

static bme680_task_params_t s_bme680_task_params[kBme680MaxSensors] = {};
static char s_bme680_log_tags[kBme680MaxSensors][16] = {};
static const char *const s_bme680_task_names[kBme680MaxSensors] = {
    "bme680_0",
    "bme680_1",
};

typedef struct {
    bool have_value;
    bme680_measurement_t last;
    int64_t last_timestamp_us;
} bme680_plausibility_state_t;

static bme680_plausibility_state_t s_bme680_plausibility[kBme680MaxSensors] = {};

static bool bme680_get_last_valid(size_t index,
                                  bme680_measurement_t *out_measurement,
                                  int64_t *out_timestamp_us)
{
    if (index >= kBme680MaxSensors || out_measurement == nullptr) {
        return false;
    }

    const bme680_plausibility_state_t &state = s_bme680_plausibility[index];
    if (!state.have_value) {
        return false;
    }

    *out_measurement = state.last;
    if (out_timestamp_us) {
        *out_timestamp_us = state.last_timestamp_us;
    }
    return true;
}

static bool bme680_apply_plausibility(size_t index,
                                      bme680_measurement_t &measurement,
                                      int64_t now_us,
                                      const char *log_tag,
                                      bool *out_used_fallback)
{
    if (index >= kBme680MaxSensors) {
        return false;
    }

    bool fallback_used = false;
    bme680_plausibility_state_t &state = s_bme680_plausibility[index];
    const bool have_last = state.have_value;
    const bme680_measurement_t previous = state.last;
    const float dt_s = (have_last && now_us > state.last_timestamp_us)
                           ? static_cast<float>(now_us - state.last_timestamp_us) / 1000000.0f
                           : 0.0f;

    auto handle_non_finite = [&](float &value, const char *field_name, float last_value) {
        if (!isfinite(value)) {
            if (have_last) {
                ESP_LOGW(TAG, "%s %s non-finite, using previous %.2f", log_tag, field_name, last_value);
                value = last_value;
                fallback_used = true;
            } else {
                ESP_LOGW(TAG, "%s %s non-finite, clamping to 0", log_tag, field_name);
                value = 0.0f;
                fallback_used = true;
            }
        }
    };

    auto handle_range = [&](float &value, float min_val, float max_val, const char *field_name, float last_value) {
        if (value < min_val || value > max_val) {
            if (have_last) {
                ESP_LOGW(TAG, "%s %s %.2f out of bounds (%.2f..%.2f), using previous %.2f",
                         log_tag, field_name, value, min_val, max_val, last_value);
                value = last_value;
                fallback_used = true;
            } else {
                ESP_LOGW(TAG, "%s %s %.2f out of bounds (%.2f..%.2f), clamping",
                         log_tag, field_name, value, min_val, max_val);
                value = clampf(value, min_val, max_val);
                fallback_used = true;
            }
        }
    };

    auto handle_delta = [&](float &value, float last_value, float max_delta_per_sec, const char *field_name) {
        if (have_last && dt_s > 0.0f && max_delta_per_sec > 0.0f) {
            float delta_per_sec = fabsf(value - last_value) / dt_s;
            if (delta_per_sec > max_delta_per_sec) {
                ESP_LOGW(TAG,
                         "%s %s delta %.2f per second exceeds %.2f, using previous %.2f",
                         log_tag,
                         field_name,
                         delta_per_sec,
                         max_delta_per_sec,
                         last_value);
                value = last_value;
                fallback_used = true;
            }
        }
    };

    handle_non_finite(measurement.temperature_c, "temperature", previous.temperature_c);
    handle_range(measurement.temperature_c,
                 WALTER_BME680_TEMP_MIN_C,
                 WALTER_BME680_TEMP_MAX_C,
                 "temperature",
                 previous.temperature_c);
    handle_delta(measurement.temperature_c,
                 previous.temperature_c,
                 WALTER_BME680_TEMP_MAX_DELTA_PER_SEC,
                 "temperature");

    handle_non_finite(measurement.humidity_pct, "humidity", previous.humidity_pct);
    handle_range(measurement.humidity_pct,
                 WALTER_BME680_HUM_MIN_PCT,
                 WALTER_BME680_HUM_MAX_PCT,
                 "humidity",
                 previous.humidity_pct);
    handle_delta(measurement.humidity_pct,
                 previous.humidity_pct,
                 WALTER_BME680_HUM_MAX_DELTA_PER_SEC,
                 "humidity");

    handle_non_finite(measurement.pressure_hpa, "pressure", previous.pressure_hpa);
    handle_range(measurement.pressure_hpa,
                 WALTER_BME680_PRESS_MIN_HPA,
                 WALTER_BME680_PRESS_MAX_HPA,
                 "pressure",
                 previous.pressure_hpa);
    handle_delta(measurement.pressure_hpa,
                 previous.pressure_hpa,
                 WALTER_BME680_PRESS_MAX_DELTA_PER_SEC,
                 "pressure");

    if (!isfinite(measurement.dewpoint_c)) {
        if (have_last) {
            ESP_LOGW(TAG, "%s dewpoint non-finite, using previous %.2f", log_tag, previous.dewpoint_c);
            measurement.dewpoint_c = previous.dewpoint_c;
        } else {
            measurement.dewpoint_c = 0.0f;
        }
        fallback_used = true;
    }

    if (!measurement.gas_valid) {
        if (have_last && previous.gas_valid) {
            ESP_LOGW(TAG, "%s gas measurement invalid, using previous sample", log_tag);
            measurement.gas_valid = previous.gas_valid;
            measurement.gas_res_kohm = previous.gas_res_kohm;
            measurement.gas_range = previous.gas_range;
            measurement.gas_index = previous.gas_index;
            measurement.heater_stable = previous.heater_stable;
            fallback_used = true;
        }
    } else {
        handle_non_finite(measurement.gas_res_kohm, "gas", previous.gas_res_kohm);
    }

    if (have_last) {
        if (!isfinite(measurement.temperature_c) || !isfinite(measurement.humidity_pct)) {
            measurement.dewpoint_c = previous.dewpoint_c;
        }
    }

    state.have_value = true;
    state.last = measurement;
    state.last_timestamp_us = now_us;

    if (out_used_fallback) {
        *out_used_fallback = fallback_used;
    }

    return true;
}

static void bme680_task(void *arg)
{
    const bme680_task_params_t *params = static_cast<const bme680_task_params_t *>(arg);
    const size_t index = params->index;
    const char *log_tag = params->log_tag;

    ESP_LOGI(TAG, "%s task started (index=%u, addr=0x%02X)", log_tag, (unsigned)index, params->address);

    bme680_handle_t handle = nullptr;
    bool initialised = false;
    bool startup_delay_done = false;

    const TickType_t period = ms_to_ticks(WALTER_BME680_POLL_INTERVAL_MS);
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        if (!initialised) {
            if (!startup_delay_done && WALTER_BME680_STARTUP_DELAY_MS > 0) {
                vTaskDelay(ms_to_ticks(WALTER_BME680_STARTUP_DELAY_MS));
                startup_delay_done = true;
            }
            if (ensure_sensor_i2c_bus() != ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            bme680_config_t cfg = {};
            cfg.i2c_address = params->address;
            cfg.i2c_clock_speed = WALTER_SENSOR_I2C_SPEED_HZ;
            cfg.power_mode = BME680_POWER_MODE_FORCED;
            cfg.iir_filter = BME680_IIR_FILTER_3;
            cfg.standby_time = BME680_STANDBY_TIME_500MS;
            cfg.pressure_oversampling = BME680_PRESSURE_OVERSAMPLING_8X;
            cfg.temperature_oversampling = BME680_TEMPERATURE_OVERSAMPLING_8X;
            cfg.humidity_oversampling = BME680_HUMIDITY_OVERSAMPLING_8X;
            cfg.gas_enabled = true;
            cfg.heater_temperature = WALTER_BME680_HEATER_TEMP_C;
            cfg.heater_duration = WALTER_BME680_HEATER_DURATION_MS;
            cfg.heater_temperature_profile[0] = WALTER_BME680_HEATER_TEMP_C;
            cfg.heater_duration_profile[0] = WALTER_BME680_HEATER_DURATION_MS;
            cfg.heater_profile_size = 1;
            cfg.heater_shared_duration = WALTER_BME680_HEATER_DURATION_MS;

            esp_err_t init_err = bme680_init(s_sensor_i2c_bus, &cfg, &handle);
            if (init_err == ESP_OK) {
                ESP_LOGI(TAG, "%s initialised", log_tag);
                initialised = true;
            } else {
                ESP_LOGW(TAG, "%s init failed: %s", log_tag, esp_err_to_name(init_err));
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
        }

        bme680_measurement_t measurement = {};
        bool publish = false;
        bool fallback = false;
        int64_t timestamp_us = esp_timer_get_time();

        bme680_data_t data = {};
        esp_err_t data_err = bme680_get_data(handle, &data);
        if (data_err == ESP_OK) {
            measurement.temperature_c = data.air_temperature;
            measurement.humidity_pct = data.relative_humidity;
            measurement.pressure_hpa = data.barometric_pressure / 100.0f;  // driver returns Pa
            measurement.dewpoint_c = data.dewpoint_temperature;
            measurement.gas_res_kohm = data.gas_resistance / 1000.0f;
            measurement.iaq_score = data.iaq_score;
            measurement.gas_valid = data.gas_valid;
            measurement.heater_stable = data.heater_stable;
            measurement.gas_range = data.gas_range;
            measurement.gas_index = data.gas_index;

            bool used_fallback = false;
            if (bme680_apply_plausibility(index, measurement, timestamp_us, log_tag, &used_fallback)) {
                fallback = used_fallback;
                publish = true;
            }
        } else {
            ESP_LOGW(TAG, "%s read failed: %s", log_tag, esp_err_to_name(data_err));
            if (bme680_get_last_valid(index, &measurement, &timestamp_us)) {
                fallback = true;
                publish = true;
            } else {
                initialised = false;
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }

        if (publish) {
            sensor_state_publish_bme680(index, measurement, fallback, timestamp_us);
#if WALTER_SENSOR_LOG_BME680
            ESP_LOGI(TAG,
                     "%s T=%.1f°C RH=%.1f%% P=%.1f hPa gas=%.1f kΩ%s",
                     log_tag,
                     measurement.temperature_c,
                     measurement.humidity_pct,
                     measurement.pressure_hpa,
                     measurement.gas_res_kohm,
                     fallback ? " (fallback)" : "");
#endif
        }

        vTaskDelayUntil(&last_wake, period);
    }
}
#endif  // WALTER_ENABLE_BME680

#if WALTER_ENABLE_BNO055
typedef struct {
    float yaw_deg;
    float pitch_deg;
    float roll_deg;
    float linear_accel_mps2[3];
    float gravity_mps2[3];
    float temperature_c;
    uint8_t calibration_sys;
    uint8_t calibration_gyro;
    uint8_t calibration_accel;
    uint8_t calibration_mag;
    bool calibrated;
} bno055_measurement_t;

static void sensor_state_mark_bno055_configured(uint8_t address);
static void sensor_state_publish_bno055(const bno055_measurement_t &measurement,
                                        bool fallback,
                                        int64_t timestamp_us);

static i2c_master_dev_handle_t s_bno055_device = nullptr;

static esp_err_t ensure_bno055_device(void)
{
    esp_err_t err = ensure_sensor_i2c_bus();
    if (err != ESP_OK) {
        return err;
    }

    if (s_bno055_device != nullptr) {
        return ESP_OK;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = WALTER_BNO055_I2C_ADDR;
    dev_cfg.scl_speed_hz = WALTER_SENSOR_I2C_SPEED_HZ;
    dev_cfg.flags.disable_ack_check = false;
    dev_cfg.scl_wait_us = 0xffff;

    err = i2c_master_bus_add_device(s_sensor_i2c_bus, &dev_cfg, &s_bno055_device);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add BNO055 (addr=0x%02X): %s",
                 WALTER_BNO055_I2C_ADDR, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "BNO055 device registered on sensor I2C bus (addr=0x%02X)",
                 WALTER_BNO055_I2C_ADDR);
    }
    return err;
}

static void bno055_task(void *arg)
{
    (void)arg;

    const TickType_t period = ms_to_ticks(WALTER_BNO055_POLL_INTERVAL_MS);
    TickType_t last_wake = xTaskGetTickCount();

    bno055_t imu = {};
    bool initialised = false;
    bno055_measurement_t last_measurement = {};
    bool have_last = false;
    int64_t last_timestamp_us = 0;

    while (true) {
        if (ensure_bno055_device() != ESP_OK) {
            vTaskDelay(ms_to_ticks(2000));
            continue;
        }

        if (imu.config.slave_handle != s_bno055_device) {
            memset(&imu, 0, sizeof(imu));
            imu.config.slave_handle = s_bno055_device;
            initialised = false;
        }

        if (!initialised) {
            vTaskDelay(ms_to_ticks(WALTER_BNO055_STARTUP_DELAY_MS));

            esp_err_t init_err = bno055_initialize(&imu);
            if (init_err != ESP_OK) {
                ESP_LOGE(TAG, "BNO055 init failed: %s", esp_err_to_name(init_err));
                vTaskDelay(ms_to_ticks(2000));
                continue;
            }

            vTaskDelay(ms_to_ticks(WALTER_BNO055_POST_RESET_DELAY_MS));

            esp_err_t cfg_err = bno055_configure(&imu, NDOF_MODE, (ACC_M_S2 | GY_DPS | EUL_DEG | TEMP_C));
            if (cfg_err != ESP_OK) {
                ESP_LOGE(TAG, "BNO055 configure failed: %s", esp_err_to_name(cfg_err));
                initialised = false;
                vTaskDelay(ms_to_ticks(2000));
                continue;
            }

            ESP_LOGI(TAG, "BNO055 initialised (mode=NDOF)");
            sensor_state_mark_bno055_configured(WALTER_BNO055_I2C_ADDR);
            initialised = true;
        }

        esp_err_t cal_err = bno055_get_calibration_status(&imu);
        if (cal_err != ESP_OK) {
            ESP_LOGW(TAG, "BNO055 calibration read failed: %s", esp_err_to_name(cal_err));
        }

        esp_err_t euler_err = bno055_get_readings(&imu, EULER_ANGLE);
        esp_err_t lin_err = bno055_get_readings(&imu, LINEAR_ACCELERATION);
        esp_err_t grav_err = bno055_get_readings(&imu, GRAVITY);
        esp_err_t temp_err = bno055_get_readings(&imu, TEMPERATURE);

        bool ok = (euler_err == ESP_OK) && (lin_err == ESP_OK) && (grav_err == ESP_OK) && (temp_err == ESP_OK);

        bno055_measurement_t measurement = {};
        int64_t now_us = esp_timer_get_time();
        bool publish = false;
        bool fallback = false;
        int64_t publish_ts = now_us;

        if (ok) {
            measurement.yaw_deg = imu.euler_angle.yaw;
            measurement.pitch_deg = imu.euler_angle.pitch;
            measurement.roll_deg = imu.euler_angle.roll;
            measurement.linear_accel_mps2[0] = imu.linear_acceleration.x;
            measurement.linear_accel_mps2[1] = imu.linear_acceleration.y;
            measurement.linear_accel_mps2[2] = imu.linear_acceleration.z;
            measurement.gravity_mps2[0] = imu.gravity.x;
            measurement.gravity_mps2[1] = imu.gravity.y;
            measurement.gravity_mps2[2] = imu.gravity.z;
            measurement.temperature_c = imu.temperature;
            measurement.calibration_sys = imu.config.calibration.sys;
            measurement.calibration_gyro = imu.config.calibration.gyro;
            measurement.calibration_accel = imu.config.calibration.xl;
            measurement.calibration_mag = imu.config.calibration.mag;
            measurement.calibrated = imu.config.is_calibrated;

            last_measurement = measurement;
            last_timestamp_us = now_us;
            have_last = true;
            publish = true;
        } else {
            ESP_LOGW(TAG, "BNO055 read failed (Euler=%s, Lin=%s, Grav=%s, Temp=%s)",
                     esp_err_to_name(euler_err),
                     esp_err_to_name(lin_err),
                     esp_err_to_name(grav_err),
                     esp_err_to_name(temp_err));
            if (have_last) {
                measurement = last_measurement;
                publish_ts = last_timestamp_us;
                fallback = true;
                publish = true;
            } else {
                initialised = false;
            }
        }

        if (publish) {
            sensor_state_publish_bno055(measurement, fallback, publish_ts);
#if WALTER_SENSOR_LOG_BNO055
            const char *heading_label = heading_to_compass(measurement.yaw_deg);
            ESP_LOGI(TAG,
                     "BNO055 heading=%.1f° (%s) pitch=%.1f° roll=%.1f° cal=%u/%u/%u/%u%s",
                     measurement.yaw_deg,
                     heading_label,
                     measurement.pitch_deg,
                     measurement.roll_deg,
                     measurement.calibration_sys,
                     measurement.calibration_gyro,
                     measurement.calibration_accel,
                     measurement.calibration_mag,
                     fallback ? " (fallback)" : "");
#endif
        }

        vTaskDelayUntil(&last_wake, period);
    }
}
#endif

static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;
static bool s_nat_enabled = false;
static bool s_default_route_set = false;
static std::atomic<bool> s_wifi_sta_connected{false};

static constexpr gpio_num_t SENSOR_RAIL_GPIO = GPIO_NUM_0;

#define DHCPS_OFFER_DNS 0x02

#if WALTER_ENABLE_HX711
static womo_hx711_t s_hx711 = {};

static constexpr int HX_IDX_PLATFORM_A = 0;
static constexpr int HX_IDX_PLATFORM_B = 1;

typedef struct {
    bool has_value;
    float last_value;
    int64_t last_timestamp_us;
} hx_plausibility_state_t;

static hx_plausibility_state_t s_hx_plaus[2];
static portMUX_TYPE s_hx_plaus_mux = portMUX_INITIALIZER_UNLOCKED;

static void hx_reset_plausibility(void)
{
    portENTER_CRITICAL(&s_hx_plaus_mux);
    memset(s_hx_plaus, 0, sizeof(s_hx_plaus));
    portEXIT_CRITICAL(&s_hx_plaus_mux);
}

static bool hx_get_last_valid(int index, float *out_kg, int64_t *out_timestamp_us = nullptr)
{
    if (!out_kg) {
        return false;
    }

    bool have = false;
    float value = 0.0f;
    int64_t timestamp = 0;

    portENTER_CRITICAL(&s_hx_plaus_mux);
    if (index >= 0 && index < 2 && s_hx_plaus[index].has_value) {
        have = true;
        value = s_hx_plaus[index].last_value;
        timestamp = s_hx_plaus[index].last_timestamp_us;
    }
    portEXIT_CRITICAL(&s_hx_plaus_mux);

    if (have) {
        *out_kg = value;
        if (out_timestamp_us) {
            *out_timestamp_us = timestamp;
        }
    }
    return have;
}

static bool hx_plausibility_check(int index, float value, const char *label)
{
    if (!isfinite(value)) {
        ESP_LOGW(TAG, "%s non-finite (%.3f)", label, value);
        return false;
    }

    if (value < WALTER_HX711_MIN_KG || value > WALTER_HX711_MAX_KG) {
        ESP_LOGW(TAG, "%s out of bounds: %.3f kg (%.3f..%.3f)", label, value,
                 WALTER_HX711_MIN_KG, WALTER_HX711_MAX_KG);
        return false;
    }

    int64_t now = esp_timer_get_time();
    float last = 0.0f;
    float delta = 0.0f;
    float allowed_delta = 0.0f;
    bool had_previous = false;
    bool ok = true;

    portENTER_CRITICAL(&s_hx_plaus_mux);
    if (index >= 0 && index < 2 && s_hx_plaus[index].has_value) {
        had_previous = true;
        last = s_hx_plaus[index].last_value;
        float dt = (float)(now - s_hx_plaus[index].last_timestamp_us) / 1000000.0f;
        if (dt > 0.0f && WALTER_HX711_MAX_DELTA_PER_SEC > 0.0f) {
            allowed_delta = WALTER_HX711_MAX_DELTA_PER_SEC * dt + 1e-3f;
            delta = fabsf(value - last);
            if (delta > allowed_delta) {
                ok = false;
            }
        }
    }

    if (ok && index >= 0 && index < 2) {
        s_hx_plaus[index].has_value = true;
        s_hx_plaus[index].last_value = value;
    }
    if (index >= 0 && index < 2) {
        s_hx_plaus[index].last_timestamp_us = now;
    }
    portEXIT_CRITICAL(&s_hx_plaus_mux);

    if (!ok) {
        if (had_previous) {
            ESP_LOGW(TAG,
                     "%s jump rejected: now=%.3f kg, last=%.3f kg, delta=%.3f kg (max %.3f kg)",
                     label, value, last, delta, allowed_delta);
        } else {
            ESP_LOGW(TAG, "%s jump rejected: now=%.3f kg (no previous)", label, value);
        }
    }

    return ok;
}
#endif

typedef struct {
#if WALTER_ENABLE_HX711
    struct {
        bool valid_a;
        bool valid_b;
        bool fallback_a;
        bool fallback_b;
        float kg_a;
        float kg_b;
        int64_t timestamp_us;
    } hx711;
#endif
#if WALTER_ENABLE_ANALOG
    struct {
        bool valid;
        womo_analog_data_t data;
        int64_t timestamp_us;
    } analog;
#endif
#if WALTER_ENABLE_BNO055
    struct {
        bool configured;
        bool valid;
        bool fallback;
        uint8_t address;
        float yaw_deg;
        float pitch_deg;
        float roll_deg;
        float linear_accel_mps2[3];
        float gravity_mps2[3];
        float temperature_c;
        uint8_t calibration_sys;
        uint8_t calibration_gyro;
        uint8_t calibration_accel;
        uint8_t calibration_mag;
        bool calibrated;
        int64_t timestamp_us;
    } bno055;
#endif
#if WALTER_ENABLE_BME680
    struct {
        bool configured;
        bool valid;
        bool fallback;
        uint8_t address;
        float temperature_c;
        float humidity_pct;
        float pressure_hpa;
        float dewpoint_c;
        float gas_res_kohm;
        uint16_t iaq_score;
        bool gas_valid;
        bool heater_stable;
        uint8_t gas_range;
        uint8_t gas_index;
        int64_t timestamp_us;
    } bme680[kBme680MaxSensors];
    size_t bme680_active;
#endif
} sensor_shared_state_t;

static sensor_shared_state_t s_sensor_state = {};
static portMUX_TYPE s_sensor_state_mux = portMUX_INITIALIZER_UNLOCKED;

static sensor_shared_state_t sensor_state_snapshot(void)
{
    sensor_shared_state_t copy;
    portENTER_CRITICAL(&s_sensor_state_mux);
    copy = s_sensor_state;
    portEXIT_CRITICAL(&s_sensor_state_mux);
    return copy;
}

#if WALTER_ENABLE_HX711
static void sensor_state_publish_hx711(bool have_a, float kg_a, bool fallback_a,
                                       bool have_b, float kg_b, bool fallback_b,
                                       int64_t timestamp_us)
{
    portENTER_CRITICAL(&s_sensor_state_mux);
    s_sensor_state.hx711.valid_a = have_a;
    s_sensor_state.hx711.valid_b = have_b;
    s_sensor_state.hx711.fallback_a = fallback_a;
    s_sensor_state.hx711.fallback_b = fallback_b;
    s_sensor_state.hx711.kg_a = kg_a;
    s_sensor_state.hx711.kg_b = kg_b;
    s_sensor_state.hx711.timestamp_us = timestamp_us;
    portEXIT_CRITICAL(&s_sensor_state_mux);
}
#endif

#if WALTER_ENABLE_ANALOG
static void sensor_state_publish_analog(const womo_analog_data_t *analog, int64_t timestamp_us)
{
    if (!analog) {
        return;
    }
    portENTER_CRITICAL(&s_sensor_state_mux);
    s_sensor_state.analog.valid = true;
    s_sensor_state.analog.data = *analog;
    s_sensor_state.analog.timestamp_us = timestamp_us;
    portEXIT_CRITICAL(&s_sensor_state_mux);
}
#endif

#if WALTER_ENABLE_BNO055
static void sensor_state_mark_bno055_configured(uint8_t address)
{
    portENTER_CRITICAL(&s_sensor_state_mux);
    auto &slot = s_sensor_state.bno055;
    slot.configured = true;
    slot.address = address;
    slot.valid = false;
    slot.fallback = false;
    slot.calibrated = false;
    memset(slot.linear_accel_mps2, 0, sizeof(slot.linear_accel_mps2));
    memset(slot.gravity_mps2, 0, sizeof(slot.gravity_mps2));
    slot.temperature_c = 0.0f;
    slot.yaw_deg = 0.0f;
    slot.pitch_deg = 0.0f;
    slot.roll_deg = 0.0f;
    slot.calibration_sys = 0;
    slot.calibration_gyro = 0;
    slot.calibration_accel = 0;
    slot.calibration_mag = 0;
    slot.timestamp_us = 0;
    portEXIT_CRITICAL(&s_sensor_state_mux);
}

static void sensor_state_publish_bno055(const bno055_measurement_t &measurement,
                                        bool fallback,
                                        int64_t timestamp_us)
{
    portENTER_CRITICAL(&s_sensor_state_mux);
    auto &slot = s_sensor_state.bno055;
    slot.valid = true;
    slot.fallback = fallback;
    slot.yaw_deg = measurement.yaw_deg;
    slot.pitch_deg = measurement.pitch_deg;
    slot.roll_deg = measurement.roll_deg;
    memcpy(slot.linear_accel_mps2, measurement.linear_accel_mps2, sizeof(slot.linear_accel_mps2));
    memcpy(slot.gravity_mps2, measurement.gravity_mps2, sizeof(slot.gravity_mps2));
    slot.temperature_c = measurement.temperature_c;
    slot.calibration_sys = measurement.calibration_sys;
    slot.calibration_gyro = measurement.calibration_gyro;
    slot.calibration_accel = measurement.calibration_accel;
    slot.calibration_mag = measurement.calibration_mag;
    slot.calibrated = measurement.calibrated;
    slot.timestamp_us = timestamp_us;
    portEXIT_CRITICAL(&s_sensor_state_mux);
}
#endif

#if WALTER_ENABLE_BME680
static void sensor_state_mark_bme680_configured(size_t index, uint8_t address)
{
    if (index >= kBme680MaxSensors) {
        return;
    }
    portENTER_CRITICAL(&s_sensor_state_mux);
    auto &slot = s_sensor_state.bme680[index];
    slot.configured = true;
    slot.address = address;
    slot.valid = false;
    slot.fallback = false;
    if (s_sensor_state.bme680_active < (index + 1)) {
        s_sensor_state.bme680_active = index + 1;
    }
    portEXIT_CRITICAL(&s_sensor_state_mux);
}

static void sensor_state_publish_bme680(size_t index, const bme680_measurement_t &measurement,
                                        bool fallback, int64_t timestamp_us)
{
    if (index >= kBme680MaxSensors) {
        return;
    }
    portENTER_CRITICAL(&s_sensor_state_mux);
    auto &slot = s_sensor_state.bme680[index];
    slot.valid = true;
    slot.fallback = fallback;
    slot.temperature_c = measurement.temperature_c;
    slot.humidity_pct = measurement.humidity_pct;
    slot.pressure_hpa = measurement.pressure_hpa;
    slot.dewpoint_c = measurement.dewpoint_c;
    slot.gas_res_kohm = measurement.gas_res_kohm;
    slot.iaq_score = measurement.iaq_score;
    slot.gas_valid = measurement.gas_valid;
    slot.heater_stable = measurement.heater_stable;
    slot.gas_range = measurement.gas_range;
    slot.gas_index = measurement.gas_index;
    slot.timestamp_us = timestamp_us;
    portEXIT_CRITICAL(&s_sensor_state_mux);
}
#endif

// Kompassrichtungen: 16-teilige Rose
static const char* heading_to_compass(float heading) {
    static const char* directions[16] = {
        "N", "NNO", "NO", "ONO", "O", "OSO", "SO", "SSO",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
    };
    // Normalisiere auf 0-360°
    while (heading < 0) heading += 360.0f;
    while (heading >= 360.0f) heading -= 360.0f;
    // Berechne Index: 16 Richtungen = 360°/16 = 22.5° pro Sektor
    // +11.25° für Zentrierung (N = 348.75° - 11.25°)
    int index = (int)((heading + 11.25f) / 22.5f) % 16;
    return directions[index];
}

#if WALTER_ENABLE_HX711
static void hx711_task(void *arg);
#endif
#if WALTER_ENABLE_ANALOG
static void analog_task(void *arg);
#endif
#if WALTER_ENABLE_BNO055
static void bno055_task(void *arg);
#endif
#if WALTER_ENABLE_BME680
static void bme680_task(void *arg);
#endif
#if WALTER_ENABLE_RS485
static void rs485_tx_task(void *arg);
static void rs485_rx_task(void *arg);
static void handle_rs485_command(const char *cmd_json);
#endif
static esp_err_t sensor_subsystem_init(void);
static void configure_routing_after_connect(void);
static void configure_softap_dns(void);
static void enable_switched_3v3(void);

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base != WIFI_EVENT) {
        return;
    }

    switch (event_id) {
    case WIFI_EVENT_AP_STACONNECTED: {
        const wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "SoftAP client %02x:%02x:%02x:%02x:%02x:%02x joined, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5], event->aid);
        break;
    }
    case WIFI_EVENT_AP_STADISCONNECTED: {
        const wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "SoftAP client %02x:%02x:%02x:%02x:%02x:%02x left, AID=%d",
                 event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5], event->aid);
        break;
    }
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "Station interface started");
        s_wifi_sta_connected.store(false);
        break;
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "Connected to upstream AP");
        s_wifi_sta_connected.store(true);
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Disconnected from upstream AP (reason=%d), retrying", event->reason);
        s_wifi_sta_connected.store(false);
        s_default_route_set = false;
        esp_wifi_connect();
        break;
    }
    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Upstream IP acquired: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_sta_connected.store(true);
        configure_routing_after_connect();
    }
}

static esp_err_t wifi_init_apsta(void)
{
    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL, NULL));

    wifi_config_t ap_config = {};
    strlcpy((char *)ap_config.ap.ssid, WALTER_AP_SSID, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(WALTER_AP_SSID);
    strlcpy((char *)ap_config.ap.password, WALTER_AP_PASSWORD, sizeof(ap_config.ap.password));
    ap_config.ap.channel = WALTER_AP_CHANNEL;
    ap_config.ap.max_connection = WALTER_AP_MAX_CONNECTIONS;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.pmf_cfg.required = false;

    if (strlen(WALTER_AP_PASSWORD) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    bool connect_sta = strlen(WALTER_STA_SSID) > 0;
    wifi_config_t sta_config = {};
    if (connect_sta) {
        strlcpy((char *)sta_config.sta.ssid, WALTER_STA_SSID, sizeof(sta_config.sta.ssid));
        strlcpy((char *)sta_config.sta.password, WALTER_STA_PASSWORD, sizeof(sta_config.sta.password));
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta_config.sta.pmf_cfg.capable = true;
        sta_config.sta.pmf_cfg.required = false;

        if (strlen(WALTER_STA_PASSWORD) == 0) {
            sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        }
    }

    wifi_mode_t mode = connect_sta ? WIFI_MODE_APSTA : WIFI_MODE_AP;
    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    if (connect_sta) {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "SoftAP ready, SSID=%s, channel=%u", ap_config.ap.ssid, ap_config.ap.channel);

    if (connect_sta) {
        ESP_LOGI(TAG, "Connecting to upstream SSID=%s", sta_config.sta.ssid);
        ESP_ERROR_CHECK(esp_wifi_connect());
    }

#if WALTER_ENABLE_NAT
#else
    ESP_LOGW(TAG, "NAT support disabled in config (WALTER_ENABLE_NAT not set)");
#endif

    return ESP_OK;
}

static void configure_softap_dns(void)
{
    if (!s_ap_netif) {
        return;
    }

    esp_netif_dns_info_t dns_info = {};
    esp_err_t dns_from_sta = ESP_FAIL;
    if (s_sta_netif) {
        dns_from_sta = esp_netif_get_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns_info);
    }

    bool have_valid_dns = (dns_from_sta == ESP_OK &&
                           dns_info.ip.type == ESP_IPADDR_TYPE_V4 &&
                           dns_info.ip.u_addr.ip4.addr != 0);

    if (!have_valid_dns) {
        ip4_addr_t fallback = {};
        if (ip4addr_aton(WALTER_AP_DNS_PRIMARY, &fallback)) {
            dns_info.ip.type = ESP_IPADDR_TYPE_V4;
            dns_info.ip.u_addr.ip4.addr = fallback.addr;
            have_valid_dns = true;
            ESP_LOGI(TAG, "Using fallback DNS %s for SoftAP clients", WALTER_AP_DNS_PRIMARY);
        } else if (ip4addr_aton(WALTER_AP_DNS_SECONDARY, &fallback)) {
            dns_info.ip.type = ESP_IPADDR_TYPE_V4;
            dns_info.ip.u_addr.ip4.addr = fallback.addr;
            have_valid_dns = true;
            ESP_LOGI(TAG, "Using secondary fallback DNS %s for SoftAP clients", WALTER_AP_DNS_SECONDARY);
        }
    }

    if (!have_valid_dns) {
        ESP_LOGW(TAG, "No valid DNS information available for SoftAP clients");
        return;
    }

    uint8_t dhcps_offer_option = DHCPS_OFFER_DNS;
    esp_err_t err = esp_netif_dhcps_stop(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, "Failed to stop DHCP server for DNS update: %s", esp_err_to_name(err));
        return;
    }

    err = esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET,
                                 ESP_NETIF_DOMAIN_NAME_SERVER,
                                 &dhcps_offer_option,
                                 sizeof(dhcps_offer_option));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable DNS offer on DHCP server: %s", esp_err_to_name(err));
    }

    err = esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set SoftAP DNS: %s", esp_err_to_name(err));
    }

    err = esp_netif_dhcps_start(s_ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        ESP_LOGW(TAG, "Failed to restart DHCP server after DNS update: %s", esp_err_to_name(err));
        return;
    }

    esp_ip4_addr_t dns_ip = dns_info.ip.u_addr.ip4;
    ESP_LOGI(TAG, "SoftAP DHCP advertises DNS " IPSTR, IP2STR(&dns_ip));
}

static void configure_routing_after_connect(void)
{
    if (!s_sta_netif) {
        return;
    }

    if (!s_default_route_set) {
        if (esp_netif_set_default_netif(s_sta_netif) == ESP_OK) {
            s_default_route_set = true;
            ESP_LOGI(TAG, "Station interface set as default route");
        } else {
            ESP_LOGW(TAG, "Failed to set station interface as default route");
        }
    }

    configure_softap_dns();

#if WALTER_ENABLE_NAT
    if (s_ap_netif && !s_nat_enabled) {
        esp_err_t nat_err = esp_netif_napt_enable(s_ap_netif);
        if (nat_err == ESP_OK) {
            s_nat_enabled = true;
            ESP_LOGI(TAG, "NAT between SoftAP and STA enabled");
        } else if (nat_err == ESP_ERR_NOT_SUPPORTED || nat_err == ESP_ERR_INVALID_ARG) {
            ESP_LOGW(TAG, "NAT not enabled (err=%d). Ensure NAT is enabled in config", nat_err);
        } else {
            ESP_LOGE(TAG, "Failed to enable NAT (err=%d)", nat_err);
        }
    }
#endif
}
#if WALTER_ENABLE_HX711
static void hx711_task(void *arg)
{
    (void)arg;

    const TickType_t period = ms_to_ticks(WALTER_HX711_POLL_INTERVAL_MS);
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        int32_t raw_b = 0;
        float kg_b = 0.0f;
        bool have_b = false;
        bool fallback_b = false;
        bool read_b_ok = false;
        int64_t fallback_b_ts = 0;

        if (womo_hx711_set_gain(&s_hx711, WOMO_HX711_GAIN_A_128) == ESP_OK &&
            womo_hx711_read_average(&s_hx711, WALTER_HX711_AVG_SAMPLES, &raw_b) == ESP_OK) {
            read_b_ok = true;
            float grams_b = (raw_b - (int32_t)WALTER_HX711_OFFSET_B) * WALTER_HX711_SCALE_B;
            kg_b = grams_b / 1000.0f;
            have_b = hx_plausibility_check(HX_IDX_PLATFORM_B, kg_b, "HX711.B");
        }
        if (!have_b && hx_get_last_valid(HX_IDX_PLATFORM_B, &kg_b, &fallback_b_ts)) {
            have_b = true;
            fallback_b = true;
            const char *reason = read_b_ok ? "new sample rejected" : "read failed";
            ESP_LOGW(TAG, "HX711.B using last valid value %.2f kg (%s)", kg_b, reason);
        }

#if WALTER_HX711_ENABLE_CHANNEL_B
        int32_t raw_a = 0;
        float kg_a = 0.0f;
        bool have_a = false;
        bool fallback_a = false;
        bool read_a_ok = false;
        int64_t fallback_a_ts = 0;
        if (womo_hx711_set_gain(&s_hx711, WOMO_HX711_GAIN_B_32) == ESP_OK &&
            womo_hx711_read_average(&s_hx711, WALTER_HX711_AVG_SAMPLES, &raw_a) == ESP_OK) {
            read_a_ok = true;
#if WALTER_HX711_INVERT_A
            float grams_a = ((int32_t)WALTER_HX711_OFFSET_A - raw_a) * WALTER_HX711_SCALE_A;
#else
            float grams_a = (raw_a - (int32_t)WALTER_HX711_OFFSET_A) * WALTER_HX711_SCALE_A;
#endif
            kg_a = grams_a / 1000.0f;
            have_a = hx_plausibility_check(HX_IDX_PLATFORM_A, kg_a, "HX711.A");
        }
        if (!have_a && hx_get_last_valid(HX_IDX_PLATFORM_A, &kg_a, &fallback_a_ts)) {
            have_a = true;
            fallback_a = true;
            const char *reason = read_a_ok ? "new sample rejected" : "read failed";
            ESP_LOGW(TAG, "HX711.A using last valid value %.2f kg (%s)", kg_a, reason);
        }

        if (have_a && have_b) {
            ESP_LOGI(TAG, "HX711: A=%.2fkg%s (raw=%ld) B=%.2fkg%s (raw=%ld)",
                     kg_a, fallback_a ? "*" : "", (long)raw_a,
                     kg_b, fallback_b ? "*" : "", (long)raw_b);
        } else if (have_b) {
            ESP_LOGI(TAG, "HX711: B=%.2fkg%s (raw=%ld)",
                     kg_b, fallback_b ? "*" : "", (long)raw_b);
        } else if (have_a) {
            ESP_LOGI(TAG, "HX711: A=%.2fkg%s (raw=%ld)",
                     kg_a, fallback_a ? "*" : "", (long)raw_a);
        }
#else
        const bool have_a = false;
        const bool fallback_a = false;
        const float kg_a = 0.0f;
        const int64_t fallback_a_ts = 0;
        if (have_b) {
            ESP_LOGI(TAG, "HX711 Channel A: raw=%ld → %.2f kg%s",
                     (long)raw_b, kg_b, fallback_b ? "*" : "");
        }
#endif

        int64_t timestamp_us = esp_timer_get_time();
#if WALTER_HX711_ENABLE_CHANNEL_B
        if (fallback_a && fallback_b) {
            timestamp_us = (fallback_a_ts > fallback_b_ts) ? fallback_a_ts : fallback_b_ts;
        } else if (fallback_a) {
            timestamp_us = fallback_a_ts;
        } else if (fallback_b) {
            timestamp_us = fallback_b_ts;
        }
#else
        if (fallback_b) {
            timestamp_us = fallback_b_ts;
        }
#endif

        sensor_state_publish_hx711(have_a, kg_a, fallback_a,
                                   have_b, kg_b, fallback_b,
                                   timestamp_us);

        vTaskDelayUntil(&last_wake, period);
    }
}
#endif

#if WALTER_ENABLE_ANALOG
static void analog_task(void *arg)
{
    (void)arg;

    const TickType_t period = ms_to_ticks(WALTER_ANALOG_POLL_INTERVAL_MS);
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        womo_analog_data_t analog = {};
        esp_err_t err = womo_analog_read(&analog);
        if (err == ESP_OK) {
            int64_t now_us = esp_timer_get_time();
            sensor_state_publish_analog(&analog, now_us);
#if WALTER_SENSOR_LOG_ANALOG
            if (analog.battery_valid[0]) {
                ESP_LOGI(TAG, "Battery1: %.2f V (%d mV)", analog.battery_v[0], analog.battery_mv[0]);
            }
            if (analog.battery_valid[1]) {
                ESP_LOGI(TAG, "Battery2: %.2f V (%d mV)", analog.battery_v[1], analog.battery_mv[1]);
            }
            if (analog.tank_valid[0]) {
                ESP_LOGI(TAG, "Tank1: %.2f V -> %u%%", analog.tank_v[0], analog.tank_percent[0]);
            }
            if (analog.tank_valid[1]) {
                ESP_LOGI(TAG, "Tank2: %.2f V -> %u%%", analog.tank_v[1], analog.tank_percent[1]);
            }
#endif
        } else {
            ESP_LOGW(TAG, "Analog read failed: %s", esp_err_to_name(err));
        }

        vTaskDelayUntil(&last_wake, period);
    }
}
#endif

#if WALTER_ENABLE_RS485
// RS485 TX Task - sends sensor data as JSON
static void rs485_tx_task(void *arg)
{
    ESP_LOGI(TAG, "RS485 TX task started");
    
    TickType_t last_full_send = 0;
    const TickType_t full_interval = pdMS_TO_TICKS(10000);  // 10 seconds

    while (true) {
        TickType_t now = xTaskGetTickCount();
        bool send_full = false;

        if ((now - last_full_send) >= full_interval) {
            send_full = true;
            last_full_send = now;
        }

        if (send_full) {
            sensor_shared_state_t snapshot = sensor_state_snapshot();

            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "type", "full");
            cJSON_AddNumberToObject(root, "ts", (double)(esp_timer_get_time() / 1000));

#if WALTER_ENABLE_HX711
            if (snapshot.hx711.valid_a || snapshot.hx711.valid_b) {
                cJSON *hx = cJSON_CreateObject();
                if (snapshot.hx711.valid_a) {
                    cJSON_AddNumberToObject(hx, "a", snapshot.hx711.kg_a);
                    if (snapshot.hx711.fallback_a) {
                        cJSON_AddBoolToObject(hx, "a_fb", true);
                    }
                }
                if (snapshot.hx711.valid_b) {
                    cJSON_AddNumberToObject(hx, "b", snapshot.hx711.kg_b);
                    if (snapshot.hx711.fallback_b) {
                        cJSON_AddBoolToObject(hx, "b_fb", true);
                    }
                }
                if (snapshot.hx711.valid_a && snapshot.hx711.valid_b) {
                    cJSON_AddNumberToObject(hx, "sum", snapshot.hx711.kg_a + snapshot.hx711.kg_b);
                }
                cJSON_AddNumberToObject(hx, "ts_us", (double)snapshot.hx711.timestamp_us);
                cJSON_AddItemToObject(root, "hx", hx);
            }
#endif

#if WALTER_ENABLE_ANALOG
            if (snapshot.analog.valid) {
                const womo_analog_data_t &analog = snapshot.analog.data;
                cJSON *bat = cJSON_CreateObject();
                if (analog.battery_valid[0]) cJSON_AddNumberToObject(bat, "b1", analog.battery_v[0]);
                if (analog.battery_valid[1]) cJSON_AddNumberToObject(bat, "b2", analog.battery_v[1]);
                if (analog.battery_valid[0] || analog.battery_valid[1]) {
                    cJSON_AddItemToObject(root, "bat", bat);
                } else {
                    cJSON_Delete(bat);
                }

                cJSON *tank = cJSON_CreateObject();
                if (analog.tank_valid[0]) cJSON_AddNumberToObject(tank, "t1", analog.tank_percent[0]);
                if (analog.tank_valid[1]) cJSON_AddNumberToObject(tank, "t2", analog.tank_percent[1]);
                if (analog.tank_valid[0] || analog.tank_valid[1]) {
                    cJSON_AddItemToObject(root, "tank", tank);
                } else {
                    cJSON_Delete(tank);
                }

                cJSON_AddNumberToObject(root, "analog_ts_us", (double)snapshot.analog.timestamp_us);
            }
#endif

#if WALTER_ENABLE_BNO055
            if (snapshot.bno055.configured && snapshot.bno055.valid) {
                cJSON *imu = cJSON_CreateObject();
                cJSON_AddNumberToObject(imu, "yaw_deg", snapshot.bno055.yaw_deg);
                cJSON_AddNumberToObject(imu, "pitch_deg", snapshot.bno055.pitch_deg);
                cJSON_AddNumberToObject(imu, "roll_deg", snapshot.bno055.roll_deg);
                cJSON_AddStringToObject(imu, "hdg", heading_to_compass(snapshot.bno055.yaw_deg));

                cJSON *lin = cJSON_CreateObject();
                cJSON_AddNumberToObject(lin, "x", snapshot.bno055.linear_accel_mps2[0]);
                cJSON_AddNumberToObject(lin, "y", snapshot.bno055.linear_accel_mps2[1]);
                cJSON_AddNumberToObject(lin, "z", snapshot.bno055.linear_accel_mps2[2]);
                cJSON_AddItemToObject(imu, "lin", lin);

                cJSON *grav = cJSON_CreateObject();
                cJSON_AddNumberToObject(grav, "x", snapshot.bno055.gravity_mps2[0]);
                cJSON_AddNumberToObject(grav, "y", snapshot.bno055.gravity_mps2[1]);
                cJSON_AddNumberToObject(grav, "z", snapshot.bno055.gravity_mps2[2]);
                cJSON_AddItemToObject(imu, "grav", grav);

                cJSON_AddNumberToObject(imu, "temp_c", snapshot.bno055.temperature_c);
                cJSON_AddBoolToObject(imu, "calibrated", snapshot.bno055.calibrated);

                cJSON *cal = cJSON_CreateObject();
                cJSON_AddNumberToObject(cal, "sys", snapshot.bno055.calibration_sys);
                cJSON_AddNumberToObject(cal, "gyro", snapshot.bno055.calibration_gyro);
                cJSON_AddNumberToObject(cal, "acc", snapshot.bno055.calibration_accel);
                cJSON_AddNumberToObject(cal, "mag", snapshot.bno055.calibration_mag);
                cJSON_AddItemToObject(imu, "cal", cal);

                cJSON_AddNumberToObject(imu, "ts_us", (double)snapshot.bno055.timestamp_us);
                if (snapshot.bno055.fallback) {
                    cJSON_AddBoolToObject(imu, "fallback", true);
                }

                cJSON_AddItemToObject(root, "imu", imu);
            }
#endif

#if WALTER_ENABLE_BME680
            if (snapshot.bme680_active > 0) {
                cJSON *bme = cJSON_CreateObject();
                for (size_t idx = 0; idx < snapshot.bme680_active; ++idx) {
                    const auto &entry = snapshot.bme680[idx];
                    if (!entry.configured || !entry.valid) {
                        continue;
                    }
                    char key[8];
                    snprintf(key, sizeof(key), "0x%02X", entry.address);
                    cJSON *node = cJSON_CreateObject();
                    cJSON_AddNumberToObject(node, "temp_c", entry.temperature_c);
                    cJSON_AddNumberToObject(node, "rh_pct", entry.humidity_pct);
                    cJSON_AddNumberToObject(node, "press_hpa", entry.pressure_hpa);
                    cJSON_AddNumberToObject(node, "dew_c", entry.dewpoint_c);
                    if (entry.gas_valid) {
                        cJSON_AddNumberToObject(node, "gas_kohm", entry.gas_res_kohm);
                        cJSON_AddBoolToObject(node, "heater", entry.heater_stable);
                        cJSON_AddNumberToObject(node, "gas_range", entry.gas_range);
                        cJSON_AddNumberToObject(node, "gas_idx", entry.gas_index);
                    }
                    cJSON_AddNumberToObject(node, "iaq", entry.iaq_score);
                    cJSON_AddNumberToObject(node, "ts_us", (double)entry.timestamp_us);
                    if (entry.fallback) {
                        cJSON_AddBoolToObject(node, "fallback", true);
                    }
                    cJSON_AddItemToObject(bme, key, node);
                }
                if (cJSON_GetArraySize(bme) > 0) {
                    cJSON_AddItemToObject(root, "bme", bme);
                } else {
                    cJSON_Delete(bme);
                }
            }
#endif

            char *json_str = cJSON_PrintUnformatted(root);
            if (json_str) {
                size_t len = strlen(json_str);
                json_str[len] = '\n';  // Add newline
                ESP_LOGI(TAG, "Sending RS485 full: %zu bytes", len + 1);
                womo_rs485_write((uint8_t*)json_str, len + 1, pdMS_TO_TICKS(100));
                cJSON_free(json_str);
            }
            cJSON_Delete(root);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));  // Small delay to not hog CPU
    }
}

// RS485 RX Task - receives commands from AMOLED
static void rs485_rx_task(void *arg)
{
    ESP_LOGI(TAG, "RS485 RX task started");
    
    uint8_t buffer[512];
    char line_buffer[512];
    size_t line_pos = 0;
    
    while (true) {
        int len = womo_rs485_read(buffer, sizeof(buffer) - 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            buffer[len] = '\0';
            
            // Process byte by byte to find complete JSON lines
            for (int i = 0; i < len; i++) {
                char c = (char)buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        handle_rs485_command(line_buffer);
                        line_pos = 0;
                    }
                } else if (line_pos < sizeof(line_buffer) - 1) {
                    line_buffer[line_pos++] = c;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Handle commands received from AMOLED
static void handle_rs485_command(const char *cmd_json)
{
    ESP_LOGI(TAG, "RS485 RX: %s", cmd_json);

    cJSON *root = cJSON_Parse(cmd_json);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse JSON command");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (cmd && cJSON_IsString(cmd)) {
        const char *cmd_str = cmd->valuestring;

#if WALTER_ENABLE_HX711
        bool handled = false;

        if (strcmp(cmd_str, "tare_a") == 0) {
            ESP_LOGI(TAG, "Tare Platform A requested");
            // TODO: Implement tare functionality
            handled = true;
        } else if (strcmp(cmd_str, "tare_b") == 0) {
            ESP_LOGI(TAG, "Tare Platform B requested");
            // TODO: Implement tare functionality
            handled = true;
        }

        if (!handled) {
            ESP_LOGW(TAG, "Unknown command: %s", cmd_str);
        }
#else
        ESP_LOGW(TAG, "Unknown command: %s", cmd_str);
#endif
    } else {
        ESP_LOGW(TAG, "RS485 command missing string field 'cmd'");
    }

    cJSON_Delete(root);
}
#endif // WALTER_ENABLE_RS485

static esp_err_t sensor_subsystem_init(void)
{
    bool any_sensor = false;
    esp_err_t first_error = ESP_OK;

#if WALTER_ENABLE_HX711
    esp_err_t hx_err = womo_hx711_init(&s_hx711,
                                       (gpio_num_t)WALTER_HX711_DOUT_GPIO,
                                       (gpio_num_t)WALTER_HX711_SCK_GPIO,
                                       (womo_hx711_gain_t)WALTER_HX711_GAIN_SETTING);
    if (hx_err == ESP_OK) {
        hx_reset_plausibility();
        BaseType_t task_created = xTaskCreate(
            hx711_task,
            "hx711",
            WALTER_SENSOR_TASK_STACK,
            nullptr,
            WALTER_SENSOR_TASK_PRIORITY,
            nullptr);
        if (task_created == pdPASS) {
            any_sensor = true;
        } else {
            ESP_LOGE(TAG, "Failed to create HX711 task");
            first_error = (first_error == ESP_OK) ? ESP_ERR_NO_MEM : first_error;
        }
    } else {
        ESP_LOGW(TAG, "HX711 init failed: %s", esp_err_to_name(hx_err));
        first_error = (first_error == ESP_OK) ? hx_err : first_error;
    }
#endif

#if WALTER_ENABLE_ANALOG
    esp_err_t analog_err = womo_analog_init();
    if (analog_err == ESP_OK) {
        BaseType_t task_created = xTaskCreate(
            analog_task,
            "analog",
            WALTER_SENSOR_TASK_STACK,
            nullptr,
            WALTER_SENSOR_TASK_PRIORITY,
            nullptr);
        if (task_created == pdPASS) {
            any_sensor = true;
        } else {
            ESP_LOGE(TAG, "Failed to create analog task");
            first_error = (first_error == ESP_OK) ? ESP_ERR_NO_MEM : first_error;
        }
    } else if (analog_err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Analog monitor not supported in this build");
    } else {
        ESP_LOGW(TAG, "Analog monitor init failed: %s", esp_err_to_name(analog_err));
        first_error = (first_error == ESP_OK) ? analog_err : first_error;
    }
#endif

#if WALTER_ENABLE_BNO055
    {
        esp_err_t imu_err = ensure_bno055_device();
        if (imu_err == ESP_OK) {
            sensor_state_mark_bno055_configured(WALTER_BNO055_I2C_ADDR);
            BaseType_t task_created = xTaskCreate(
                bno055_task,
                "bno055",
                WALTER_SENSOR_TASK_STACK,
                nullptr,
                WALTER_SENSOR_TASK_PRIORITY,
                nullptr);
            if (task_created == pdPASS) {
                any_sensor = true;
            } else {
                ESP_LOGE(TAG, "Failed to create BNO055 task");
                first_error = (first_error == ESP_OK) ? ESP_ERR_NO_MEM : first_error;
            }
        } else {
            ESP_LOGW(TAG, "BNO055 setup failed: %s", esp_err_to_name(imu_err));
            first_error = (first_error == ESP_OK) ? imu_err : first_error;
        }
    }
#endif

#if WALTER_ENABLE_BME680
    if (WALTER_BME680_SENSOR_COUNT > 0) {
        esp_err_t bus_err = ensure_sensor_i2c_bus();
        if (bus_err == ESP_OK) {
            const size_t requested = WALTER_BME680_SENSOR_COUNT;
            const size_t count = (requested > kBme680MaxSensors) ? kBme680MaxSensors : requested;
            const uint8_t addresses[kBme680MaxSensors] = {
                WALTER_BME680_ADDR_0,
                WALTER_BME680_ADDR_1,
            };

            for (size_t idx = 0; idx < count; ++idx) {
                const uint8_t addr = addresses[idx];
                sensor_state_mark_bme680_configured(idx, addr);
                s_bme680_task_params[idx].address = addr;
                s_bme680_task_params[idx].index = idx;
                snprintf(s_bme680_log_tags[idx], sizeof(s_bme680_log_tags[idx]), "BME680@0x%02X", addr);
                s_bme680_task_params[idx].log_tag = s_bme680_log_tags[idx];
                esp_log_level_set(s_bme680_log_tags[idx], ESP_LOG_INFO);

                BaseType_t task_created = xTaskCreate(
                    bme680_task,
                    s_bme680_task_names[idx],
                    WALTER_SENSOR_TASK_STACK,
                    &s_bme680_task_params[idx],
                    WALTER_SENSOR_TASK_PRIORITY,
                    nullptr);

                if (task_created == pdPASS) {
                    any_sensor = true;
                } else {
                    ESP_LOGE(TAG, "Failed to create task for BME680 address 0x%02x", addr);
                    first_error = (first_error == ESP_OK) ? ESP_ERR_NO_MEM : first_error;
                }
            }
        } else {
            first_error = (first_error == ESP_OK) ? bus_err : first_error;
        }
    }
#endif

    if (!any_sensor) {
        ESP_LOGW(TAG, "Sensor subsystem configured but no sensors initialised");
        return (first_error == ESP_OK) ? ESP_FAIL : first_error;
    }

    ESP_LOGI(TAG, "Sensor subsystem started");
    return first_error;
}


extern "C" void app_main(void)
{
    configure_logging();
    ESP_LOGI(TAG, "Walter sensor hub starting up");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    enable_switched_3v3();

    ESP_ERROR_CHECK(wifi_init_apsta());

    esp_err_t sensor_err = sensor_subsystem_init();
    if (sensor_err != ESP_OK) {
        ESP_LOGW(TAG, "Sensor subsystem init reported: %s", esp_err_to_name(sensor_err));
    }

#if WALTER_ENABLE_RS485
    esp_err_t rs485_err = womo_rs485_init();
    ESP_LOGI(TAG, "womo_rs485_init() returned: %s (0x%x)", esp_err_to_name(rs485_err), rs485_err);
    if (rs485_err == ESP_OK) {
        ESP_LOGI(TAG, "RS485 initialized, starting communication tasks");
        
        // Start RS485 TX task (sends sensor data)
        BaseType_t tx_created = xTaskCreate(
            rs485_tx_task,
            "rs485_tx",
            4096,
            NULL,
            5,
            NULL
        );
        if (tx_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create RS485 TX task");
        }
        
        // Start RS485 RX task (receives commands)
        BaseType_t rx_created = xTaskCreate(
            rs485_rx_task,
            "rs485_rx",
            3072,
            NULL,
            5,
            NULL
        );
        if (rx_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create RS485 RX task");
        }
    } else {
        ESP_LOGW(TAG, "RS485 init failed: %s", esp_err_to_name(rs485_err));
    }
#endif

    ESP_LOGI(TAG, "App main initialization complete; deleting main task");
    vTaskDelete(NULL);
}

static void enable_switched_3v3(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = BIT64(SENSOR_RAIL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_set_level(SENSOR_RAIL_GPIO, 0));
    ESP_LOGI(TAG, "3V3 switched rail enabled via GPIO%u (active low)", (unsigned)SENSOR_RAIL_GPIO);
}
