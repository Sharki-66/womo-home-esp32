#include "sdkconfig.h"
#include "walter_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "lwip/ip4_addr.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "cJSON.h"

#include <atomic>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#if (defined(configGENERATE_RUN_TIME_STATS) && (configGENERATE_RUN_TIME_STATS == 1) && \
    defined(configUSE_TRACE_FACILITY) && (configUSE_TRACE_FACILITY == 1))
#define WALTER_HAS_RUNTIME_STATS 1
#else
#define WALTER_HAS_RUNTIME_STATS 0
#endif

#include "womo_analog.h"
#include "womo_hx711.h"
#include "womo_rs485.h"
#include "WalterModem.h"

#if WALTER_ENABLE_GPS
#include "womo_gps.h"
#endif

#if WALTER_ENABLE_BME680
#include "bme680.h"
#endif

#if WALTER_ENABLE_BNO055
#include "bno055.h"
#endif

WalterModem modem;
static const char *TAG = "walter_main";
static constexpr size_t WIFI_SSID_MAX_LEN = 32;

static void configure_logging(void)
{
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_log_level_set("womo_analog", ESP_LOG_INFO);
    esp_log_level_set("womo_hx711", ESP_LOG_INFO);
    esp_log_level_set("womo_rs485", ESP_LOG_INFO);
    esp_log_level_set("womo_gps", ESP_LOG_INFO);
    esp_log_level_set("lte_tcp", ESP_LOG_INFO);
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

static float wrap_angle_deg(float angle)
{
    while (angle < -180.0f) {
        angle += 360.0f;
    }
    while (angle >= 180.0f) {
        angle -= 360.0f;
    }
    return angle;
}

static float wrap_angle_0_360(float angle)
{
    while (angle < 0.0f) {
        angle += 360.0f;
    }
    while (angle >= 360.0f) {
        angle -= 360.0f;
    }
    return angle;
}

static bool limit_angle_delta(float &current,
                              float previous,
                              float max_step_deg,
                              bool wrap_to_360)
{
    if (max_step_deg <= 0.0f) {
        return false;
    }

    float diff = current - previous;
    while (diff > 180.0f) {
        diff -= 360.0f;
    }
    while (diff < -180.0f) {
        diff += 360.0f;
    }

    if (fabsf(diff) <= max_step_deg) {
        return false;
    }

    float limited = previous + copysignf(max_step_deg, diff);
    current = wrap_to_360 ? wrap_angle_0_360(limited) : wrap_angle_deg(limited);
    return true;
}

static const char* heading_to_compass(float heading);

#if WALTER_ENABLE_LTE
static uint8_t lte_signal_strength_percent(float rsrp_dbm)
{
    if (!isfinite(rsrp_dbm)) {
        return 0;
    }
    constexpr float kMin = -120.0f;  // very weak
    constexpr float kMax = -80.0f;   // excellent
    float pct = ((rsrp_dbm - kMin) / (kMax - kMin)) * 100.0f;
    pct = clampf(pct, 0.0f, 100.0f);
    return static_cast<uint8_t>(pct + 0.5f);
}
#endif

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

#if WALTER_ENABLE_BNO055
static constexpr const char *BNO055_NVS_NAMESPACE = "bno055";
static constexpr const char *BNO055_NVS_KEY = "calib";
static constexpr uint32_t BNO055_CAL_VERSION = 1;

typedef struct {
    uint32_t version;
    sensor_offset_t offsets;
} bno055_calibration_blob_t;

static bool s_bno055_offset_cache_valid = false;
static sensor_offset_t s_bno055_cached_offsets = {};
static bool s_bno055_restore_attempted = false;
static bool s_bno055_capture_pending = true;
static int64_t s_bno055_last_log_ts_us = 0;

enum class BnoPersistResult {
    kNoAction = 0,
    kSaved,
    kError,
};

static esp_err_t bno055_apply_axis_map(bno055_t *imu)
{
    if (!imu) {
        return ESP_ERR_INVALID_ARG;
    }

    // Tatsächliche Montage: Sensor +X nach oben, +Y nach hinten, +Z nach rechts.
    // Gewünscht (Fahrzeugkoordinaten): X→vorne, Y→links, Z→oben.
    // Beobachtet: Display-Kippen vorne/hinten beeinflusst Pitch, links/rechts Roll.
    // Daraus folgt: Fahrzeug X = -Sensor Y, Fahrzeug Y = POSITIVE_Z, Fahrzeug Z = POSITIVE_X.
    bno055_axes_t axes = {
        .x = NEGATIVE_Y,
        .y = POSITIVE_Z,
        .z = POSITIVE_X,
    };
    return bno055_remap_axis(imu, &axes);
}

static bool bno055_offsets_equal(const sensor_offset_t &lhs, const sensor_offset_t &rhs)
{
    return memcmp(&lhs, &rhs, sizeof(sensor_offset_t)) == 0;
}

static bool bno055_restore_calibration(bno055_t *imu)
{
    if (!imu) {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(BNO055_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "BNO055 calibration NVS open failed: %s", esp_err_to_name(err));
        }
        return false;
    }

    bno055_calibration_blob_t blob = {};
    size_t blob_size = sizeof(blob);
    err = nvs_get_blob(handle, BNO055_NVS_KEY, &blob, &blob_size);
    nvs_close(handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "BNO055 calibration read failed: %s", esp_err_to_name(err));
        }
        return false;
    }

    if (blob_size != sizeof(blob) || blob.version != BNO055_CAL_VERSION) {
        ESP_LOGW(TAG,
                 "BNO055 calibration blob invalid (size=%u, version=%u)",
                 static_cast<unsigned>(blob_size),
                 static_cast<unsigned>(blob.version));
        return false;
    }

    imu->config.offsets = blob.offsets;
    if (bno055_set_offsets(imu) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply stored BNO055 offsets");
        return false;
    }

    (void)bno055_get_calibration_status(imu);
    s_bno055_cached_offsets = blob.offsets;
    s_bno055_offset_cache_valid = true;
    ESP_LOGI(TAG, "BNO055 calibration restored from NVS");
    return true;
}

static BnoPersistResult bno055_persist_calibration_if_needed(bno055_t *imu)
{
    if (!imu || !imu->config.is_calibrated) {
        return BnoPersistResult::kNoAction;
    }

    if (bno055_get_offsets(imu) != ESP_OK) {
        ESP_LOGW(TAG, "BNO055 get_offsets failed, skip persist");
        return BnoPersistResult::kError;
    }

    if (s_bno055_offset_cache_valid && bno055_offsets_equal(s_bno055_cached_offsets, imu->config.offsets)) {
        return BnoPersistResult::kNoAction;
    }

    bno055_calibration_blob_t blob = {
        .version = BNO055_CAL_VERSION,
        .offsets = imu->config.offsets,
    };

    nvs_handle_t handle;
    esp_err_t err = nvs_open(BNO055_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BNO055 calibration save open failed: %s", esp_err_to_name(err));
        return BnoPersistResult::kError;
    }

    err = nvs_set_blob(handle, BNO055_NVS_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "BNO055 calibration save failed: %s", esp_err_to_name(err));
        return BnoPersistResult::kError;
    }

    s_bno055_cached_offsets = blob.offsets;
    s_bno055_offset_cache_valid = true;
    ESP_LOGI(TAG, "BNO055 calibration saved to NVS");
    return BnoPersistResult::kSaved;
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
    bno055_t imu = {};
    bool initialised = false;
    bno055_measurement_t last_measurement = {};
    int64_t last_timestamp_us = 0;
    bool have_last = false;

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = ms_to_ticks(WALTER_BNO055_POLL_INTERVAL_MS);

    while (true) {
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

            esp_err_t axis_err = bno055_apply_axis_map(&imu);
            if (axis_err != ESP_OK) {
                ESP_LOGW(TAG, "BNO055 axis remap failed: %s", esp_err_to_name(axis_err));
            } else {
                ESP_LOGI(TAG, "BNO055 axis remap applied (FzgX=-Y, FzgY=+Z, FzgZ=+X)");
            }

            ESP_LOGI(TAG, "BNO055 initialised (mode=NDOF)");
            sensor_state_mark_bno055_configured(WALTER_BNO055_I2C_ADDR);
            initialised = true;
            if (s_bno055_offset_cache_valid) {
                imu.config.offsets = s_bno055_cached_offsets;
                if (bno055_set_offsets(&imu) == ESP_OK) {
                    (void)bno055_get_calibration_status(&imu);
                    ESP_LOGI(TAG, "BNO055 calibration re-applied from cache");
                } else {
                    ESP_LOGW(TAG, "Failed to re-apply cached BNO055 calibration");
                }
            } else if (!s_bno055_restore_attempted) {
                s_bno055_restore_attempted = true;
                if (!bno055_restore_calibration(&imu)) {
                    ESP_LOGI(TAG, "No stored BNO055 calibration found – will persist after calibration");
                }
            }
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

        if (imu.config.is_calibrated) {
            if (s_bno055_capture_pending) {
                BnoPersistResult persist_result = bno055_persist_calibration_if_needed(&imu);
                if (persist_result != BnoPersistResult::kError) {
                    s_bno055_capture_pending = false;
                }
            }
        } else {
            s_bno055_capture_pending = true;
        }

        bno055_measurement_t measurement = {};
        int64_t now_us = esp_timer_get_time();
        bool publish = false;
        bool fallback = false;
        int64_t publish_ts = now_us;

        if (ok) {
            measurement.yaw_deg = imu.euler_angle.yaw;
            measurement.pitch_deg = wrap_angle_deg(imu.euler_angle.pitch - 180.0f);
            measurement.roll_deg = wrap_angle_deg(imu.euler_angle.roll);
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

            bool plausibility_adjusted = false;
            if (have_last && (WALTER_BNO055_MAX_EULER_STEP_DEG > 0.0f)) {
                plausibility_adjusted |= limit_angle_delta(measurement.yaw_deg,
                                                          last_measurement.yaw_deg,
                                                          WALTER_BNO055_MAX_EULER_STEP_DEG,
                                                          true);
                plausibility_adjusted |= limit_angle_delta(measurement.pitch_deg,
                                                          last_measurement.pitch_deg,
                                                          WALTER_BNO055_MAX_EULER_STEP_DEG,
                                                          false);
                plausibility_adjusted |= limit_angle_delta(measurement.roll_deg,
                                                          last_measurement.roll_deg,
                                                          WALTER_BNO055_MAX_EULER_STEP_DEG,
                                                          false);
                if (plausibility_adjusted) {
                    ESP_LOGW(TAG,
                             "BNO055 Euler-Sprung begrenzt (yaw=%.1f°, pitch=%.1f°, roll=%.1f°)",
                             measurement.yaw_deg,
                             measurement.pitch_deg,
                             measurement.roll_deg);
                }
            }

            last_measurement = measurement;
            last_timestamp_us = now_us;
            have_last = true;
            publish = true;
            if (plausibility_adjusted) {
                fallback = true;
            }
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
            float delta_ms = 0.0f;
            if (s_bno055_last_log_ts_us > 0 && publish_ts > s_bno055_last_log_ts_us) {
                delta_ms = (float)(publish_ts - s_bno055_last_log_ts_us) / 1000.0f;
            }
            s_bno055_last_log_ts_us = publish_ts;
            ESP_LOGI(TAG,
                     "BNO055 dT=%.1f ms cal=%u/%u/%u/%u heading=%.1f° (%s) roll=%.1f° pitch=%.1f°%s",
                     delta_ms,
                     measurement.calibration_sys,
                     measurement.calibration_gyro,
                     measurement.calibration_accel,
                     measurement.calibration_mag,
                     measurement.yaw_deg,
                     heading_label,
                     measurement.roll_deg,
                     measurement.pitch_deg,
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
static wifi_config_t s_wifi_ap_config = {};
static wifi_config_t s_wifi_sta_config = {};
static bool s_wifi_sta_configured = false;
static bool s_wifi_driver_started = false;
static bool s_wifi_ap_active = false;
static bool s_wifi_sta_active = false;
static std::atomic<bool> s_wifi_ap_target{true};
static std::atomic<bool> s_wifi_sta_target{false};
static SemaphoreHandle_t s_wifi_runtime_mutex = nullptr;
static bool s_wifi_sta_ip_valid = false;
static esp_ip4_addr_t s_wifi_sta_ip = {};
static esp_ip4_addr_t s_wifi_sta_gateway = {};
static esp_ip4_addr_t s_wifi_sta_netmask = {};
static bool s_wifi_last_error_valid = false;
static esp_err_t s_wifi_last_error = ESP_OK;
static esp_err_t wifi_runtime_apply(bool ap_enable, bool sta_enable);
static esp_err_t wifi_runtime_apply_targets(void);
static void sensor_state_publish_wifi_state(void);
static void wifi_set_last_error(esp_err_t err);

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
#if WALTER_ENABLE_LTE
    struct {
        bool registered;
        bool info_valid;
        char operator_name[WALTER_MODEM_OPERATOR_MAX_SIZE];
        uint16_t mcc;
        uint8_t mnc;
        uint16_t tac;
        uint32_t cell_id;
        float rsrp_dbm;
        float rsrq_db;
        float rssi_dbm;
        uint8_t band;
        int64_t timestamp_us;
    } lte;
#endif
#if WALTER_ENABLE_GPS
    struct {
        bool valid;
        double latitude;
        double longitude;
        double altitude_m;
        float speed_kmh;
        float heading_deg;
        uint8_t satellites;
        float confidence_m;
        int64_t timestamp;
        uint32_t time_to_fix_ms;
        uint32_t fix_count;
        int64_t last_fix_attempt_us;
    } gps;
#endif
    struct {
        bool ap_target;
        bool sta_target;
        bool ap_active;
        bool sta_active;
        bool sta_connected;
        bool driver_started;
        bool nat_enabled;
        bool default_route_set;
        bool sta_configured;
        bool sta_ip_valid;
        char ap_ssid[WIFI_SSID_MAX_LEN + 1];
        char sta_ssid[WIFI_SSID_MAX_LEN + 1];
        esp_ip4_addr_t sta_ip;
        esp_ip4_addr_t sta_gateway;
        esp_ip4_addr_t sta_netmask;
        bool last_error_valid;
        esp_err_t last_error;
    } wifi;
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

#if WALTER_ENABLE_LTE
static void sensor_state_publish_lte_registration(bool registered)
{
    portENTER_CRITICAL(&s_sensor_state_mux);
    s_sensor_state.lte.registered = registered;
    if (!registered) {
        s_sensor_state.lte.info_valid = false;
    }
    s_sensor_state.lte.timestamp_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_sensor_state_mux);
}

static void sensor_state_publish_lte_cellinfo(const WalterModemCellInformation &cell)
{
    portENTER_CRITICAL(&s_sensor_state_mux);
    s_sensor_state.lte.registered = true;
    s_sensor_state.lte.info_valid = true;
    strncpy(s_sensor_state.lte.operator_name, cell.netName, sizeof(s_sensor_state.lte.operator_name));
    s_sensor_state.lte.operator_name[sizeof(s_sensor_state.lte.operator_name) - 1] = '\0';
    s_sensor_state.lte.mcc = cell.cc;
    s_sensor_state.lte.mnc = cell.nc;
    s_sensor_state.lte.tac = cell.tac;
    s_sensor_state.lte.cell_id = cell.cid;
    s_sensor_state.lte.rsrp_dbm = cell.rsrp;
    s_sensor_state.lte.rsrq_db = cell.rsrq;
    s_sensor_state.lte.rssi_dbm = cell.rssi;
    s_sensor_state.lte.band = cell.band;
    s_sensor_state.lte.timestamp_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_sensor_state_mux);
}
#endif

static void sensor_state_publish_wifi_state(void)
{
    portENTER_CRITICAL(&s_sensor_state_mux);
    auto &wifi = s_sensor_state.wifi;
    wifi.ap_target = s_wifi_ap_target.load();
    wifi.sta_target = s_wifi_sta_target.load();
    wifi.ap_active = s_wifi_ap_active;
    wifi.sta_active = s_wifi_sta_active;
    wifi.sta_connected = s_wifi_sta_connected.load();
    wifi.driver_started = s_wifi_driver_started;
    wifi.nat_enabled = s_nat_enabled;
    wifi.default_route_set = s_default_route_set;
    wifi.sta_configured = s_wifi_sta_configured;
    wifi.sta_ip_valid = s_wifi_sta_ip_valid;
    if (wifi.sta_ip_valid) {
        wifi.sta_ip = s_wifi_sta_ip;
        wifi.sta_gateway = s_wifi_sta_gateway;
        wifi.sta_netmask = s_wifi_sta_netmask;
    } else {
        wifi.sta_ip.addr = 0;
        wifi.sta_gateway.addr = 0;
        wifi.sta_netmask.addr = 0;
    }
    strlcpy(wifi.ap_ssid,
            reinterpret_cast<const char *>(s_wifi_ap_config.ap.ssid),
            sizeof(wifi.ap_ssid));
    if (s_wifi_sta_configured) {
        strlcpy(wifi.sta_ssid,
                reinterpret_cast<const char *>(s_wifi_sta_config.sta.ssid),
                sizeof(wifi.sta_ssid));
    } else {
        wifi.sta_ssid[0] = '\0';
    }
    wifi.last_error_valid = s_wifi_last_error_valid;
    wifi.last_error = s_wifi_last_error;
    portEXIT_CRITICAL(&s_sensor_state_mux);
}

static void wifi_set_last_error(esp_err_t err)
{
    s_wifi_last_error = err;
    s_wifi_last_error_valid = (err != ESP_OK);
    sensor_state_publish_wifi_state();
}

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

#if WALTER_ENABLE_GPS
static void sensor_state_publish_gps(const womo_gps_data_t *data, uint32_t fix_count)
{
    if (!data) {
        return;
    }
    portENTER_CRITICAL(&s_sensor_state_mux);
    auto &gps = s_sensor_state.gps;
    gps.valid = data->valid;
    gps.latitude = data->latitude;
    gps.longitude = data->longitude;
    gps.altitude_m = data->altitude_m;
    gps.speed_kmh = data->speed_kmh;
    gps.heading_deg = data->heading_deg;
    gps.satellites = data->satellites;
    gps.confidence_m = data->confidence_m;
    gps.timestamp = data->timestamp;
    gps.time_to_fix_ms = data->time_to_fix_ms;
    gps.fix_count = fix_count;
    gps.last_fix_attempt_us = esp_timer_get_time();
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
#if WALTER_ENABLE_LTE
static esp_err_t lte_control_for_gps(bool enable);
#endif
#if WALTER_ENABLE_RS485
static esp_err_t command_set_lte_enabled(bool enable);
static esp_err_t command_set_wifi_ap_enabled(bool enable);
static esp_err_t command_set_wifi_sta_enabled(bool enable);
static esp_err_t command_start_wifi_scan(void);
static esp_err_t command_set_wifi_credentials(const char *ssid, const char *password);
static void rs485_report_command_result(const char *cmd, esp_err_t err);
#endif
static float system_cpu_load_percent(void);
#if WALTER_HAS_RUNTIME_STATS
static float system_cpu_load_percent(void)
{
    UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
    if (num_tasks == 0) {
        return -1.0f;
    }

    TaskStatus_t *task_states = static_cast<TaskStatus_t *>(pvPortMalloc(num_tasks * sizeof(TaskStatus_t)));
    if (!task_states) {
        ESP_LOGW(TAG, "Run-time stats allocation failed");
        return -1.0f;
    }

    uint32_t total_runtime = 0;
    UBaseType_t filled = uxTaskGetSystemState(task_states, num_tasks, &total_runtime);
    uint64_t idle_runtime = 0;

    for (UBaseType_t i = 0; i < filled; ++i) {
        const char *name = task_states[i].pcTaskName;
        if (name != nullptr && strncmp(name, "IDLE", 4) == 0) {
            idle_runtime += task_states[i].ulRunTimeCounter;
        }
    }

    vPortFree(task_states);

    if (total_runtime == 0) {
        return -1.0f;
    }

    float idle_ratio = static_cast<float>(idle_runtime) / static_cast<float>(total_runtime);
    float load_pct = (1.0f - idle_ratio) * 100.0f;
    if (load_pct < 0.0f) {
        load_pct = 0.0f;
    } else if (load_pct > 100.0f) {
        load_pct = 100.0f;
    }
    return load_pct;
}
#else
static float system_cpu_load_percent(void)
{
    return -1.0f;
}
#endif
#if WALTER_ENABLE_LTE
typedef enum {
    LTE_CMD_ENABLE,
    LTE_CMD_DISABLE,
    LTE_CMD_RESTART,
} lte_runtime_cmd_t;

static QueueHandle_t s_lte_command_queue = nullptr;
static std::atomic<bool> s_lte_target_enabled{true};
static std::atomic<bool> s_lte_runtime_active{false};
static esp_err_t s_lte_last_error = ESP_OK;
#endif

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
        sensor_state_publish_wifi_state();
        break;
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "Connected to upstream AP");
        s_wifi_sta_connected.store(true);
        sensor_state_publish_wifi_state();
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Disconnected from upstream AP (reason=%d), retrying", event->reason);
        s_wifi_sta_connected.store(false);
        s_default_route_set = false;
        s_wifi_sta_ip_valid = false;
        sensor_state_publish_wifi_state();
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
        s_wifi_sta_ip_valid = true;
        s_wifi_sta_ip = event->ip_info.ip;
        s_wifi_sta_gateway = event->ip_info.gw;
        s_wifi_sta_netmask = event->ip_info.netmask;
        configure_routing_after_connect();
        sensor_state_publish_wifi_state();
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

    if (!s_wifi_runtime_mutex) {
        s_wifi_runtime_mutex = xSemaphoreCreateMutex();
        if (!s_wifi_runtime_mutex) {
            ESP_LOGE(TAG, "Failed to create WiFi runtime mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    s_wifi_ap_config = ap_config;
    if (connect_sta) {
        s_wifi_sta_config = sta_config;
        s_wifi_sta_configured = true;
    } else {
        memset(&s_wifi_sta_config, 0, sizeof(s_wifi_sta_config));
        s_wifi_sta_configured = false;
    }

    s_wifi_ap_target.store(true);
    s_wifi_sta_target.store(connect_sta);
    sensor_state_publish_wifi_state();

    esp_err_t runtime_err = wifi_runtime_apply_targets();
    if (runtime_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi runtime state: %s", esp_err_to_name(runtime_err));
        return runtime_err;
    }

#if WALTER_ENABLE_NAT
#else
    ESP_LOGW(TAG, "NAT support disabled in config (WALTER_ENABLE_NAT not set)");
#endif

    ESP_LOGI(TAG, "WiFi init complete (AP on, STA %s)", connect_sta ? "enabled" : "disabled");
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

    sensor_state_publish_wifi_state();
}

static esp_err_t wifi_runtime_apply(bool ap_enable, bool sta_enable)
{
    if (!s_wifi_runtime_mutex) {
        wifi_set_last_error(ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    if (ap_enable && s_wifi_ap_config.ap.max_connection == 0) {
        ESP_LOGW(TAG, "WiFi AP config missing, cannot enable SoftAP");
        wifi_set_last_error(ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }
    if (sta_enable && !s_wifi_sta_configured) {
        ESP_LOGW(TAG, "WiFi STA credentials missing, cannot enable station");
        wifi_set_last_error(ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_wifi_runtime_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        wifi_set_last_error(ESP_ERR_TIMEOUT);
        return ESP_ERR_TIMEOUT;
    }

    bool prev_ap = s_wifi_ap_active;
    bool prev_sta = s_wifi_sta_active;
    esp_err_t err = ESP_OK;

    do {
        if (!ap_enable && !sta_enable) {
            if (s_wifi_driver_started) {
                err = esp_wifi_stop();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to stop WiFi driver: %s", esp_err_to_name(err));
                    break;
                }
                s_wifi_driver_started = false;
            }
            if (prev_sta) {
                ESP_LOGI(TAG, "WiFi STA disabled");
            }
            if (prev_ap) {
                ESP_LOGI(TAG, "SoftAP disabled");
            }
            ESP_LOGI(TAG, "WiFi driver stopped");
            s_wifi_sta_active = false;
            s_wifi_ap_active = false;
            s_wifi_sta_connected.store(false);
            s_wifi_sta_ip_valid = false;
            break;
        }

        wifi_mode_t mode = WIFI_MODE_NULL;
        if (ap_enable && sta_enable) {
            mode = WIFI_MODE_APSTA;
        } else if (ap_enable) {
            mode = WIFI_MODE_AP;
        } else {
            mode = WIFI_MODE_STA;
        }

        err = esp_wifi_set_mode(mode);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set WiFi mode %d: %s", mode, esp_err_to_name(err));
            break;
        }

        err = esp_wifi_set_config(WIFI_IF_AP, &s_wifi_ap_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update SoftAP config: %s", esp_err_to_name(err));
            break;
        }

        if (sta_enable) {
            err = esp_wifi_set_config(WIFI_IF_STA, &s_wifi_sta_config);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to update STA config: %s", esp_err_to_name(err));
                break;
            }
        } else if (prev_sta) {
            esp_wifi_disconnect();
            s_wifi_sta_ip_valid = false;
        }

        if (!s_wifi_driver_started) {
            err = esp_wifi_start();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start WiFi driver: %s", esp_err_to_name(err));
                break;
            }
            s_wifi_driver_started = true;
        }

        if (sta_enable) {
            err = esp_wifi_connect();
            if (err == ESP_ERR_WIFI_STATE || err == ESP_ERR_WIFI_CONN) {
                ESP_LOGW(TAG, "WiFi connect pending: %s", esp_err_to_name(err));
                err = ESP_OK;
            } else if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start WiFi connection: %s", esp_err_to_name(err));
                break;
            } else {
                ESP_LOGI(TAG, "WiFi STA connect requested (SSID=%s)", s_wifi_sta_config.sta.ssid);
            }
        }

        if (!prev_ap && ap_enable) {
            ESP_LOGI(TAG, "SoftAP enabled (SSID=%s, channel=%u)",
                     s_wifi_ap_config.ap.ssid,
                     s_wifi_ap_config.ap.channel);
        } else if (prev_ap && !ap_enable) {
            ESP_LOGI(TAG, "SoftAP disabled");
        }

        if (!prev_sta && sta_enable) {
            ESP_LOGI(TAG, "WiFi STA enabled");
        } else if (prev_sta && !sta_enable) {
            ESP_LOGI(TAG, "WiFi STA disabled");
            s_wifi_sta_connected.store(false);
        }

        s_wifi_ap_active = ap_enable;
        s_wifi_sta_active = sta_enable;
        if (!sta_enable) {
            s_wifi_sta_connected.store(false);
            s_wifi_sta_ip_valid = false;
        }
    } while (false);

    xSemaphoreGive(s_wifi_runtime_mutex);
    wifi_set_last_error(err);
    return err;
}

static esp_err_t wifi_runtime_apply_targets(void)
{
    return wifi_runtime_apply(s_wifi_ap_target.load(), s_wifi_sta_target.load());
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
            float cpu_load = system_cpu_load_percent();
            if (cpu_load < 0.0f) {
                static bool s_cpu_warned = false;
                if (!s_cpu_warned) {
                    ESP_LOGW(TAG, "CPU load not available (run-time stats disabled)");
                    s_cpu_warned = true;
                }
            } else {
                ESP_LOGI(TAG, "System CPU load: %.1f%%", cpu_load);
            }
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
typedef struct {
    char cmd[32];
    esp_err_t err;
} rs485_cmd_result_msg_t;

static QueueHandle_t s_rs485_cmd_result_queue = nullptr;

static void rs485_send_json(const char *context, cJSON *root);
static void rs485_queue_command_result(const char *cmd, esp_err_t err);

// RS485 TX Task - sends sensor data as JSON
static void rs485_tx_task(void *arg)
{
    ESP_LOGI(TAG, "RS485 TX task started");
    
    const TickType_t full_interval = pdMS_TO_TICKS(1000);  // full payload every 1 second for testing
    TickType_t last_full_send = xTaskGetTickCount() - full_interval;
#if WALTER_ENABLE_BNO055
    const TickType_t imu_interval = pdMS_TO_TICKS(300);    // interleave IMU snapshots alle 0,3 s
    TickType_t last_imu_send = last_full_send;
#endif
    const TickType_t loop_delay = pdMS_TO_TICKS(100);

    while (true) {
        TickType_t now = xTaskGetTickCount();
        bool send_full = ((now - last_full_send) >= full_interval);
    #if WALTER_ENABLE_BNO055
        bool send_imu = !send_full && ((now - last_imu_send) >= imu_interval);
    #endif

        if (s_rs485_cmd_result_queue) {
            rs485_cmd_result_msg_t cmd_msg;
            while (xQueueReceive(s_rs485_cmd_result_queue, &cmd_msg, 0) == pdPASS) {
                cJSON *ack = cJSON_CreateObject();
                cJSON_AddStringToObject(ack, "type", "cmd_ack");
                cJSON_AddStringToObject(ack, "cmd", cmd_msg.cmd);
                bool ok = (cmd_msg.err == ESP_OK);
                cJSON_AddBoolToObject(ack, "ok", ok);
                if (!ok) {
                    cJSON_AddStringToObject(ack, "err", esp_err_to_name(cmd_msg.err));
                }
                cJSON_AddNumberToObject(ack, "ts", (double)(esp_timer_get_time() / 1000));
                rs485_send_json("cmd_ack", ack);
                cJSON_Delete(ack);
            }
        }

        if (send_full) {
            sensor_shared_state_t snapshot = sensor_state_snapshot();
            last_full_send = now;
    #if WALTER_ENABLE_BNO055
            last_imu_send = now;
    #endif

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

#if WALTER_ENABLE_LTE
            if (snapshot.lte.registered || snapshot.lte.info_valid) {
                cJSON *lte = cJSON_CreateObject();
                cJSON_AddBoolToObject(lte, "registered", snapshot.lte.registered);
                if (snapshot.lte.info_valid && snapshot.lte.operator_name[0] != '\0') {
                    cJSON_AddStringToObject(lte, "operator", snapshot.lte.operator_name);
                }
                if (snapshot.lte.info_valid) {
                    cJSON_AddNumberToObject(lte, "rsrp_dbm", snapshot.lte.rsrp_dbm);
                    cJSON_AddNumberToObject(lte,
                                             "signal_pct",
                                             static_cast<double>(lte_signal_strength_percent(snapshot.lte.rsrp_dbm)));
                }
                cJSON_AddItemToObject(root, "lte", lte);
            }
#endif

            {
                const auto &wifi = snapshot.wifi;
                cJSON *wifi_json = cJSON_CreateObject();
                cJSON_AddBoolToObject(wifi_json, "ap_target", wifi.ap_target);
                cJSON_AddBoolToObject(wifi_json, "sta_target", wifi.sta_target);
                cJSON_AddBoolToObject(wifi_json, "ap_active", wifi.ap_active);
                cJSON_AddBoolToObject(wifi_json, "sta_active", wifi.sta_active);
                cJSON_AddBoolToObject(wifi_json, "sta_connected", wifi.sta_connected);
                cJSON_AddBoolToObject(wifi_json, "driver_started", wifi.driver_started);
                cJSON_AddBoolToObject(wifi_json, "nat", wifi.nat_enabled);
                cJSON_AddBoolToObject(wifi_json, "def_route", wifi.default_route_set);
                cJSON_AddBoolToObject(wifi_json, "sta_cfg", wifi.sta_configured);
                if (wifi.ap_ssid[0] != '\0') {
                    cJSON_AddStringToObject(wifi_json, "ap_ssid", wifi.ap_ssid);
                }
                if (wifi.sta_ssid[0] != '\0') {
                    cJSON_AddStringToObject(wifi_json, "sta_ssid", wifi.sta_ssid);
                }
                if (wifi.sta_ip_valid) {
                    char ipbuf[16];
                    snprintf(ipbuf, sizeof(ipbuf), IPSTR, IP2STR(&wifi.sta_ip));
                    cJSON_AddStringToObject(wifi_json, "sta_ip", ipbuf);
                    snprintf(ipbuf, sizeof(ipbuf), IPSTR, IP2STR(&wifi.sta_gateway));
                    cJSON_AddStringToObject(wifi_json, "sta_gw", ipbuf);
                    snprintf(ipbuf, sizeof(ipbuf), IPSTR, IP2STR(&wifi.sta_netmask));
                    cJSON_AddStringToObject(wifi_json, "sta_mask", ipbuf);
                }
                if (wifi.last_error_valid) {
                    cJSON_AddStringToObject(wifi_json, "last_err", esp_err_to_name(wifi.last_error));
                }
                cJSON_AddItemToObject(root, "wifi", wifi_json);
            }

#if WALTER_ENABLE_GPS
            if (snapshot.gps.valid) {
                cJSON *gps = cJSON_CreateObject();
                cJSON_AddNumberToObject(gps, "lat", snapshot.gps.latitude);
                cJSON_AddNumberToObject(gps, "lon", snapshot.gps.longitude);
                cJSON_AddNumberToObject(gps, "alt_m", snapshot.gps.altitude_m);
                cJSON_AddNumberToObject(gps, "speed_kmh", snapshot.gps.speed_kmh);
                cJSON_AddNumberToObject(gps, "heading_deg", snapshot.gps.heading_deg);
                cJSON_AddStringToObject(gps, "hdg", heading_to_compass(snapshot.gps.heading_deg));
                cJSON_AddNumberToObject(gps, "sats", snapshot.gps.satellites);
                cJSON_AddNumberToObject(gps, "conf_m", snapshot.gps.confidence_m);
                cJSON_AddNumberToObject(gps, "ttf_ms", snapshot.gps.time_to_fix_ms);
                cJSON_AddNumberToObject(gps, "fix_count", snapshot.gps.fix_count);
                cJSON_AddNumberToObject(gps, "ts", (double)snapshot.gps.timestamp);
                cJSON_AddItemToObject(root, "gps", gps);
            }
#endif

            rs485_send_json("full", root);
            cJSON_Delete(root);
        }
#if WALTER_ENABLE_BNO055
        else if (send_imu) {
            sensor_shared_state_t snapshot = sensor_state_snapshot();
            last_imu_send = now;

            if (snapshot.bno055.configured && snapshot.bno055.valid) {
                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "type", "imu");
                cJSON_AddNumberToObject(root, "ts", (double)(esp_timer_get_time() / 1000));

                cJSON *imu = cJSON_CreateObject();
                cJSON_AddNumberToObject(imu, "yaw_deg", snapshot.bno055.yaw_deg);
                cJSON_AddNumberToObject(imu, "pitch_deg", snapshot.bno055.pitch_deg);
                cJSON_AddNumberToObject(imu, "roll_deg", snapshot.bno055.roll_deg);
                cJSON_AddStringToObject(imu, "hdg", heading_to_compass(snapshot.bno055.yaw_deg));

                cJSON *cal = cJSON_CreateObject();
                cJSON_AddNumberToObject(cal, "sys", snapshot.bno055.calibration_sys);
                cJSON_AddNumberToObject(cal, "gyro", snapshot.bno055.calibration_gyro);
                cJSON_AddNumberToObject(cal, "acc", snapshot.bno055.calibration_accel);
                cJSON_AddNumberToObject(cal, "mag", snapshot.bno055.calibration_mag);
                cJSON_AddItemToObject(imu, "cal", cal);

                if (snapshot.bno055.fallback) {
                    cJSON_AddBoolToObject(imu, "fallback", true);
                }
                cJSON_AddNumberToObject(imu, "ts_us", (double)snapshot.bno055.timestamp_us);
                cJSON_AddItemToObject(root, "imu", imu);

                rs485_send_json("imu", root);
                cJSON_Delete(root);
            }
        }
#endif

        vTaskDelay(loop_delay);  // Small delay to not hog CPU
    }
}

static void rs485_send_json(const char *context, cJSON *root)
{
    if (!root) {
        return;
    }
    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to encode RS485 %s payload", context ? context : "payload");
        return;
    }
    size_t len = strlen(json_str);
    json_str[len] = '\n';
    ESP_LOGI(TAG, "Sending RS485 %s: %zu bytes", context ? context : "payload", len + 1);
    esp_err_t write_err = womo_rs485_write((uint8_t *)json_str, len + 1, pdMS_TO_TICKS(100));
    if (write_err != ESP_OK) {
        ESP_LOGW(TAG, "RS485 write (%s) failed: %s", context ? context : "payload", esp_err_to_name(write_err));
    }
    cJSON_free(json_str);
}

static void rs485_queue_command_result(const char *cmd, esp_err_t err)
{
    if (!cmd) {
        return;
    }
    if (!s_rs485_cmd_result_queue) {
        ESP_LOGW(TAG, "RS485 command result queue not ready (cmd=%s)", cmd);
        return;
    }
    rs485_cmd_result_msg_t msg = {};
    strlcpy(msg.cmd, cmd, sizeof(msg.cmd));
    msg.err = err;
    if (xQueueSend(s_rs485_cmd_result_queue, &msg, 0) != pdPASS) {
        ESP_LOGW(TAG, "RS485 command result queue full (cmd=%s)", cmd);
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
        bool handled = false;
        esp_err_t cmd_err = ESP_OK;

        if (strcmp(cmd_str, "lte_enable") == 0) {
            handled = true;
            cmd_err = command_set_lte_enabled(true);
        } else if (strcmp(cmd_str, "lte_disable") == 0) {
            handled = true;
            cmd_err = command_set_lte_enabled(false);
        } else if (strcmp(cmd_str, "wifi_enable_ap") == 0) {
            handled = true;
            cmd_err = command_set_wifi_ap_enabled(true);
        } else if (strcmp(cmd_str, "wifi_disable_ap") == 0) {
            handled = true;
            cmd_err = command_set_wifi_ap_enabled(false);
        } else if (strcmp(cmd_str, "wifi_enable_sta") == 0) {
            handled = true;
            cmd_err = command_set_wifi_sta_enabled(true);
        } else if (strcmp(cmd_str, "wifi_disable_sta") == 0) {
            handled = true;
            cmd_err = command_set_wifi_sta_enabled(false);
        } else if (strcmp(cmd_str, "wifi_scan_start") == 0) {
            handled = true;
            cmd_err = command_start_wifi_scan();
        } else if (strcmp(cmd_str, "wifi_set_credentials") == 0) {
            handled = true;
            const cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
            const cJSON *password = cJSON_GetObjectItem(root, "password");
            if (!ssid || !password || !cJSON_IsString(ssid) || !cJSON_IsString(password)) {
                cmd_err = ESP_ERR_INVALID_ARG;
            } else {
                cmd_err = command_set_wifi_credentials(ssid->valuestring, password->valuestring);
            }
#if WALTER_ENABLE_HX711
        } else if (strcmp(cmd_str, "tare_a") == 0) {
            handled = true;
            ESP_LOGI(TAG, "Tare Platform A requested");
            // TODO: Implement tare functionality
        } else if (strcmp(cmd_str, "tare_b") == 0) {
            handled = true;
            ESP_LOGI(TAG, "Tare Platform B requested");
            // TODO: Implement tare functionality
#endif
        }

        if (handled) {
            rs485_report_command_result(cmd_str, cmd_err);
        } else {
            ESP_LOGW(TAG, "Unknown command: %s", cmd_str);
        }
    } else {
        ESP_LOGW(TAG, "RS485 command missing string field 'cmd'");
    }

    cJSON_Delete(root);
}
#endif // WALTER_ENABLE_RS485

#if WALTER_ENABLE_LTE
/**
 * @brief Internal LTE control function for GPS module
 */
static esp_err_t lte_control_for_gps(bool enable)
{
    ESP_LOGI(TAG, "GPS module requesting LTE %s", enable ? "enable" : "disable");
    
    bool previous = s_lte_target_enabled.exchange(enable);
    if (previous == enable) {
        return ESP_OK;
    }
    
    if (!s_lte_command_queue) {
        ESP_LOGW(TAG, "LTE command queue not ready");
        s_lte_target_enabled.store(previous);
        return ESP_ERR_INVALID_STATE;
    }
    
    lte_runtime_cmd_t cmd = enable ? LTE_CMD_ENABLE : LTE_CMD_DISABLE;
    if (xQueueSend(s_lte_command_queue, &cmd, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGW(TAG, "LTE command queue full");
        s_lte_target_enabled.store(previous);
        return ESP_ERR_TIMEOUT;
    }
    
    // Wait a bit for the command to take effect
    if (enable) {
        // Enabling - wait for LTE to come back up
        vTaskDelay(pdMS_TO_TICKS(5000));
    } else {
        // Disabling - wait for LTE to shut down
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    return ESP_OK;
}
#endif

#if WALTER_ENABLE_RS485
static esp_err_t command_set_lte_enabled(bool enable)
{
#if WALTER_ENABLE_LTE
    return lte_control_for_gps(enable);
#else
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
#else
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t command_set_wifi_ap_enabled(bool enable)
{
    ESP_LOGI(TAG, "Command: WiFi AP %s", enable ? "enable" : "disable");
    bool previous = s_wifi_ap_target.exchange(enable);
    if (previous == enable) {
        return ESP_OK;
    }

    esp_err_t err = wifi_runtime_apply_targets();
    if (err != ESP_OK) {
        s_wifi_ap_target.store(previous);
    }
    return err;
}

static esp_err_t command_set_wifi_sta_enabled(bool enable)
{
    ESP_LOGI(TAG, "Command: WiFi STA %s", enable ? "enable" : "disable");
    if (enable && !s_wifi_sta_configured) {
        ESP_LOGW(TAG, "WiFi STA credentials missing, cannot enable STA");
        return ESP_ERR_INVALID_STATE;
    }

    bool previous = s_wifi_sta_target.exchange(enable);
    if (previous == enable) {
        return ESP_OK;
    }

    esp_err_t err = wifi_runtime_apply_targets();
    if (err != ESP_OK) {
        s_wifi_sta_target.store(previous);
    }
    return err;
}

static esp_err_t command_start_wifi_scan(void)
{
    ESP_LOGI(TAG, "Command: WiFi scan start");
    ESP_LOGW(TAG, "WiFi scan runtime control not implemented yet");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t command_set_wifi_credentials(const char *ssid, const char *password)
{
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG,
             "Command: WiFi credentials update (SSID length %u)",
             static_cast<unsigned>(strlen(ssid)));
    ESP_LOGW(TAG, "WiFi credential updates not implemented yet");
    return ESP_ERR_NOT_SUPPORTED;
}

static void rs485_report_command_result(const char *cmd, esp_err_t err)
{
    if (!cmd) {
        return;
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Command %s executed", cmd);
    } else {
        ESP_LOGW(TAG, "Command %s failed: %s", cmd, esp_err_to_name(err));
    }
    rs485_queue_command_result(cmd, err);
}
#endif // WALTER_ENABLE_RS485

#if WALTER_ENABLE_GPS
/**
 * @brief GPS task - periodically executes GNSS fix cycles
 */
static void gps_task(void *arg)
{
    (void)arg;
    const char *GPS_TAG = "womo_gps_task";
    
    ESP_LOGI(GPS_TAG, "GPS task started");
    
    // Initialize GPS subsystem
    esp_err_t init_err = womo_gps_init();
    if (init_err != ESP_OK) {
        ESP_LOGE(GPS_TAG, "Failed to initialize GPS subsystem: %s", esp_err_to_name(init_err));
        ESP_LOGE(GPS_TAG, "GPS task terminating");
        vTaskDelete(nullptr);
        return;
    }
    
#if WALTER_ENABLE_LTE
    // Register LTE control callback
    esp_err_t reg_err = womo_gps_register_lte_control(lte_control_for_gps);
    if (reg_err != ESP_OK) {
        ESP_LOGW(GPS_TAG, "Failed to register LTE control callback: %s", esp_err_to_name(reg_err));
        ESP_LOGW(GPS_TAG, "GPS will manage LTE manually");
    }
#else
    ESP_LOGW(GPS_TAG, "LTE not enabled, GPS will operate without LTE coordination");
#endif
    
    uint32_t fix_count = 0;
    const TickType_t interval_ticks = pdMS_TO_TICKS(WALTER_GPS_FIX_INTERVAL_MS);
    
    // First fix attempt immediately after startup delay
    vTaskDelay(pdMS_TO_TICKS(10000)); // Wait 10 seconds for system to stabilize
    
    while (true) {
        ESP_LOGI(GPS_TAG, "=== Starting GPS fix cycle #%u ===", fix_count + 1);
        
        esp_err_t fix_err = womo_gps_execute_fix_cycle();
        
        if (fix_err == ESP_OK) {
            // Retrieve and publish the fix
            womo_gps_data_t fix_data = {};
            if (womo_gps_get_last_fix(&fix_data) == ESP_OK) {
                fix_count++;
                sensor_state_publish_gps(&fix_data, fix_count);
                
                ESP_LOGI(GPS_TAG, "GPS fix #%u successful: lat=%.6f, lon=%.6f, alt=%.1f m, "
                         "sats=%u, conf=%.1f m, speed=%.1f km/h, heading=%.1f°",
                         fix_count,
                         fix_data.latitude,
                         fix_data.longitude,
                         fix_data.altitude_m,
                         fix_data.satellites,
                         fix_data.confidence_m,
                         fix_data.speed_kmh,
                         fix_data.heading_deg);
            } else {
                ESP_LOGW(GPS_TAG, "GPS fix succeeded but could not retrieve data");
            }
        } else {
            ESP_LOGW(GPS_TAG, "GPS fix cycle failed: %s", esp_err_to_name(fix_err));
        }
        
        ESP_LOGI(GPS_TAG, "Next GPS fix in %u seconds", WALTER_GPS_FIX_INTERVAL_MS / 1000);
        vTaskDelay(interval_ticks);
    }
}
#endif // WALTER_ENABLE_GPS

#if WALTER_ENABLE_LTE

static const char *LTE_TAG = "lte_tcp";
static WalterModemRsp s_lte_rsp = {};
static uint8_t s_lte_mac[6] = {0};
static bool s_lte_mac_ready = false;
static uint16_t s_lte_counter = 0;
static uint8_t s_lte_socket_id = 0;

static void lte_runtime_notify(bool target_enabled, bool link_ready, esp_err_t last_err)
{
    static bool prev_target = true;
    static bool prev_link = false;
    static esp_err_t prev_err = ESP_OK;

    s_lte_runtime_active.store(target_enabled && link_ready);
    s_lte_last_error = last_err;

    if (prev_target == target_enabled && prev_link == link_ready && prev_err == last_err) {
        return;
    }

    prev_target = target_enabled;
    prev_link = link_ready;
    prev_err = last_err;

    ESP_LOGI(LTE_TAG,
             "Runtime state: target=%s link_ready=%s last_err=%s",
             target_enabled ? "on" : "off",
             link_ready ? "yes" : "no",
             (last_err == ESP_OK) ? "OK" : esp_err_to_name(last_err));
    // TODO: Publish LTE runtime state towards RS485 consumer.
}

static bool lte_is_registered(void)
{
    WalterModemNetworkRegState state = modem.getNetworkRegState();
    return state == WALTER_MODEM_NETWORK_REG_REGISTERED_HOME ||
           state == WALTER_MODEM_NETWORK_REG_REGISTERED_ROAMING;
}

static bool lte_wait_for_registration(int timeout_sec)
{
    ESP_LOGI(LTE_TAG, "Waiting for LTE registration (timeout %d s)", timeout_sec);
    int elapsed = 0;
    while (!lte_is_registered()) {
        if (!s_lte_target_enabled.load()) {
            ESP_LOGW(LTE_TAG, "Registration aborted (LTE disabled)");
            sensor_state_publish_lte_registration(false);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        ++elapsed;
        if (elapsed >= timeout_sec) {
            ESP_LOGW(LTE_TAG, "LTE registration timed out");
            sensor_state_publish_lte_registration(false);
            return false;
        }
    }
    ESP_LOGI(LTE_TAG, "LTE registered");
    sensor_state_publish_lte_registration(true);
    return true;
}

static bool lte_configure_pdp(void)
{
    const char *apn = (*WALTER_LTE_APN != '\0') ? WALTER_LTE_APN : nullptr;
    if (!modem.definePDPContext(1, apn, &s_lte_rsp)) {
        ESP_LOGE(LTE_TAG, "definePDPContext failed (result=%d)", (int)s_lte_rsp.result);
        return false;
    }
    return true;
}

static bool lte_attach_network(void)
{
    sensor_state_publish_lte_registration(false);
    if (!modem.setOpState(WALTER_MODEM_OPSTATE_NO_RF)) {
        ESP_LOGE(LTE_TAG, "setOpState(NO_RF) failed");
        return false;
    }

    if (!lte_configure_pdp()) {
        return false;
    }

    if (!modem.setOpState(WALTER_MODEM_OPSTATE_FULL)) {
        ESP_LOGE(LTE_TAG, "setOpState(FULL) failed");
        return false;
    }

    if (!modem.setNetworkSelectionMode(WALTER_MODEM_NETWORK_SEL_MODE_AUTOMATIC)) {
        ESP_LOGE(LTE_TAG, "setNetworkSelectionMode failed");
        return false;
    }

    return lte_wait_for_registration(WALTER_LTE_ATTACH_TIMEOUT_SEC);
}

static void lte_socket_event_handler(WalterModemSocketEvent ev,
                                     int socket_id,
                                     uint16_t data_received,
                                     uint8_t *data,
                                     void *args)
{
    (void)args;
    (void)data;
    if (ev == WALTER_MODEM_SOCKET_EVENT_RING) {
        ESP_LOGI(LTE_TAG,
                 "Incoming data on socket %d (%u bytes)",
                 socket_id,
                 static_cast<unsigned>(data_received));
    }
}

static bool lte_update_cellinfo(void)
{
    if (!WalterModem::getCellInformation(WALTER_MODEM_SQNMONI_REPORTS_SERVING_CELL, &s_lte_rsp)) {
        ESP_LOGW(LTE_TAG, "getCellInformation failed (result=%d)", (int)s_lte_rsp.result);
        return false;
    }

    const WalterModemCellInformation &cell = s_lte_rsp.data.cellInformation;
    ESP_LOGI(LTE_TAG,
             "Operator %s (MCC %u, MNC %u, TAC %u, CID %u, RSRP %.1f dBm, RSRQ %.1f dB)",
             cell.netName,
             static_cast<unsigned>(cell.cc),
             static_cast<unsigned>(cell.nc),
             static_cast<unsigned>(cell.tac),
             static_cast<unsigned>(cell.cid),
             cell.rsrp,
             cell.rsrq);
    sensor_state_publish_lte_cellinfo(cell);
    return true;
}

static bool lte_open_socket(void)
{
    modem.socketSetEventHandler(lte_socket_event_handler, nullptr);

    if (!modem.socketConfig(&s_lte_rsp)) {
        ESP_LOGE(LTE_TAG, "socketConfig failed (result=%d)", (int)s_lte_rsp.result);
        return false;
    }
    s_lte_socket_id = s_lte_rsp.data.socketId;

    if (!modem.socketConfigSecure(WALTER_LTE_TCP_SECURE != 0)) {
        ESP_LOGE(LTE_TAG, "socketConfigSecure(%d) failed", WALTER_LTE_TCP_SECURE);
        return false;
    }

    if (!modem.socketDial(WALTER_LTE_TCP_HOST,
                          WALTER_LTE_TCP_PORT,
                          0,
                          nullptr,
                          nullptr,
                          nullptr,
                          WALTER_MODEM_SOCKET_PROTO_TCP)) {
        ESP_LOGE(LTE_TAG, "socketDial %s:%d failed", WALTER_LTE_TCP_HOST, WALTER_LTE_TCP_PORT);
        return false;
    }

    ESP_LOGI(LTE_TAG, "TCP socket connected to %s:%d", WALTER_LTE_TCP_HOST, WALTER_LTE_TCP_PORT);
    lte_update_cellinfo();
    return true;
}

static void lte_close_socket(void)
{
    if (s_lte_socket_id != 0) {
        modem.socketClose(nullptr, nullptr, nullptr, s_lte_socket_id);
        s_lte_socket_id = 0;
    }
}

static void lte_set_low_power(void)
{
    modem.setOpState(WALTER_MODEM_OPSTATE_MINIMUM);
    sensor_state_publish_lte_registration(false);
}

static bool lte_send_heartbeat(void)
{
    if (!s_lte_mac_ready) {
        esp_err_t err = esp_read_mac(s_lte_mac, ESP_MAC_WIFI_STA);
        if (err != ESP_OK) {
            ESP_LOGE(LTE_TAG, "esp_read_mac failed: %s", esp_err_to_name(err));
            return false;
        }
        s_lte_mac_ready = true;
    }

    uint8_t payload[8] = {0};
    memcpy(payload, s_lte_mac, sizeof(s_lte_mac));
    payload[6] = static_cast<uint8_t>(s_lte_counter >> 8);
    payload[7] = static_cast<uint8_t>(s_lte_counter & 0xFF);

    if (!modem.socketSend(payload, sizeof(payload))) {
        ESP_LOGE(LTE_TAG, "socketSend failed");
        return false;
    }

    ESP_LOGI(LTE_TAG, "Heartbeat %u sent", static_cast<unsigned>(s_lte_counter));
    ++s_lte_counter;
    return true;
}

static void lte_tcp_task(void *arg)
{
    (void)arg;
    ESP_LOGI(LTE_TAG, "LTE TCP task started");

    sensor_state_publish_lte_registration(false);

    if (!WalterModem::begin(static_cast<uart_port_t>(WALTER_LTE_UART_PORT))) {
        ESP_LOGE(LTE_TAG, "WalterModem::begin failed on UART%d", WALTER_LTE_UART_PORT);
        lte_runtime_notify(false, false, ESP_FAIL);
        vTaskDelete(nullptr);
        return;
    }

    if (strlen(WALTER_LTE_PIN) > 0) {
        if (!WalterModem::unlockSIM(&s_lte_rsp, nullptr, nullptr, WALTER_LTE_PIN)) {
            ESP_LOGE(LTE_TAG, "SIM unlock failed (result=%d)", (int)s_lte_rsp.result);
            lte_runtime_notify(false, false, ESP_FAIL);
            vTaskDelete(nullptr);
            return;
        }
    }

    const TickType_t interval_ticks = ms_to_ticks(WALTER_LTE_SEND_INTERVAL_MS);
    bool link_ready = false;
    bool runtime_enabled = s_lte_target_enabled.load();
    lte_runtime_notify(runtime_enabled, link_ready, ESP_OK);

    while (true) {
        QueueHandle_t queue = s_lte_command_queue;
        lte_runtime_cmd_t cmd;

        if (!runtime_enabled) {
            lte_runtime_notify(runtime_enabled, link_ready, ESP_OK);
            if (link_ready) {
                lte_close_socket();
                lte_set_low_power();
                link_ready = false;
                lte_runtime_notify(runtime_enabled, link_ready, ESP_OK);
            }

            if (queue && xQueueReceive(queue, &cmd, portMAX_DELAY) == pdPASS) {
                if (cmd == LTE_CMD_ENABLE || cmd == LTE_CMD_RESTART) {
                    runtime_enabled = true;
                    s_lte_target_enabled.store(true);
                    lte_runtime_notify(runtime_enabled, link_ready, ESP_OK);
                }
            }
            continue;
        }

        if (queue && xQueueReceive(queue, &cmd, 0) == pdPASS) {
            if (cmd == LTE_CMD_DISABLE) {
                runtime_enabled = false;
                s_lte_target_enabled.store(false);
                continue;
            } else if (cmd == LTE_CMD_RESTART) {
                if (link_ready) {
                    lte_close_socket();
                    lte_set_low_power();
                    link_ready = false;
                    lte_runtime_notify(runtime_enabled, link_ready, ESP_OK);
                }
            }
        }

        if (!link_ready) {
            if (!lte_attach_network()) {
                lte_runtime_notify(runtime_enabled, link_ready, ESP_FAIL);
                if (queue && xQueueReceive(queue, &cmd, ms_to_ticks(WALTER_LTE_RETRY_DELAY_MS)) == pdPASS) {
                    if (cmd == LTE_CMD_DISABLE) {
                        runtime_enabled = false;
                        s_lte_target_enabled.store(false);
                    }
                }
                continue;
            }

            if (!lte_open_socket()) {
                lte_close_socket();
                lte_set_low_power();
                lte_runtime_notify(runtime_enabled, link_ready, ESP_FAIL);
                if (queue && xQueueReceive(queue, &cmd, ms_to_ticks(WALTER_LTE_RETRY_DELAY_MS)) == pdPASS) {
                    if (cmd == LTE_CMD_DISABLE) {
                        runtime_enabled = false;
                        s_lte_target_enabled.store(false);
                    }
                }
                continue;
            }

            link_ready = true;
            lte_runtime_notify(runtime_enabled, link_ready, ESP_OK);
        }

        if (!lte_send_heartbeat()) {
            lte_close_socket();
            lte_set_low_power();
            link_ready = false;
            lte_runtime_notify(runtime_enabled, link_ready, ESP_FAIL);
            if (queue && xQueueReceive(queue, &cmd, ms_to_ticks(WALTER_LTE_RETRY_DELAY_MS)) == pdPASS) {
                if (cmd == LTE_CMD_DISABLE) {
                    runtime_enabled = false;
                    s_lte_target_enabled.store(false);
                }
            }
            continue;
        }

        lte_update_cellinfo();

        if (queue && xQueueReceive(queue, &cmd, interval_ticks) == pdPASS) {
            if (cmd == LTE_CMD_DISABLE) {
                runtime_enabled = false;
                s_lte_target_enabled.store(false);
            } else if (cmd == LTE_CMD_RESTART) {
                if (link_ready) {
                    lte_close_socket();
                    lte_set_low_power();
                    link_ready = false;
                    lte_runtime_notify(runtime_enabled, link_ready, ESP_OK);
                }
            }
        }
    }
}

#endif // WALTER_ENABLE_LTE

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
        if (!s_rs485_cmd_result_queue) {
            s_rs485_cmd_result_queue = xQueueCreate(8, sizeof(rs485_cmd_result_msg_t));
            if (!s_rs485_cmd_result_queue) {
                ESP_LOGE(TAG, "Failed to create RS485 command result queue");
            }
        }
        
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

#if WALTER_ENABLE_LTE
    if (!s_lte_command_queue) {
        s_lte_command_queue = xQueueCreate(8, sizeof(lte_runtime_cmd_t));
        if (!s_lte_command_queue) {
            ESP_LOGE(TAG, "Failed to create LTE command queue");
        }
    }

    if (s_lte_command_queue) {
        BaseType_t lte_created = xTaskCreate(
            lte_tcp_task,
            "lte_tcp",
            WALTER_LTE_TASK_STACK,
            nullptr,
            WALTER_LTE_TASK_PRIORITY,
            nullptr);
        if (lte_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create LTE TCP task");
        }
    }
#endif

#if WALTER_ENABLE_GPS
    // Start GPS task
    BaseType_t gps_created = xTaskCreate(
        gps_task,
        "gps",
        WALTER_GPS_TASK_STACK,
        nullptr,
        WALTER_GPS_TASK_PRIORITY,
        nullptr);
    if (gps_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GPS task");
    } else {
        ESP_LOGI(TAG, "GPS task created successfully");
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
