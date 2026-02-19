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
#include "esp_app_desc.h"
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
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
#include <stdlib.h>

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
#include "modem_manager.h"
#endif
#if WALTER_ENABLE_WEBUI
#include "web/womo_web.h"
#endif

#if WALTER_ENABLE_BME680
#include "bme680.h"
#include "bsec_interface.h"
#endif

#if WALTER_ENABLE_BNO055
#include "bno055.h"
#endif

WalterModem modem;
static const char *TAG = "walter_main";
static constexpr size_t WIFI_SSID_MAX_LEN = 32;

#if WALTER_ENABLE_GPS
static SemaphoreHandle_t s_modem_ready_sem = nullptr;
#endif

static void configure_logging(void)
{
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_log_level_set("womo_analog", ESP_LOG_INFO);
    esp_log_level_set("womo_hx711", ESP_LOG_INFO);
    esp_log_level_set("womo_rs485", ESP_LOG_INFO);
    esp_log_level_set("womo_gps", ESP_LOG_INFO);
    // Höheres Logging für LTE-Diagnose
    esp_log_level_set("lte_tcp", ESP_LOG_DEBUG);
    esp_log_level_set("modem_mgr", ESP_LOG_DEBUG);
    esp_log_level_set("WalterModem", ESP_LOG_DEBUG);
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

static __attribute__((unused)) float wrap_angle_deg(float angle)
{
    while (angle < -180.0f) {
        angle += 360.0f;
    }
    while (angle >= 180.0f) {
        angle -= 360.0f;
    }
    return angle;
}

static __attribute__((unused)) float wrap_angle_0_360(float angle)
{
    while (angle < 0.0f) {
        angle += 360.0f;
    }
    while (angle >= 360.0f) {
        angle -= 360.0f;
    }
    return angle;
}

// Begrenze Winkelsprünge und optionaler Wrap um ±180°
static bool limit_angle_delta(float &value, float last, float max_step_deg, bool wrap_around)
{
    if (max_step_deg <= 0.0f) {
        return false;
    }

    float delta = value - last;
    if (wrap_around) {
        delta = wrap_angle_deg(delta);
    }

    bool adjusted = false;
    if (fabsf(delta) > max_step_deg) {
        delta = copysignf(max_step_deg, delta);
        adjusted = true;
    }

    value = wrap_around ? wrap_angle_deg(last + delta) : last + delta;
    return adjusted;
}

// remove unused helpers to silence warnings

static const char* heading_to_compass(float heading);

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
// LTE Runtime-Steuerung (früh platziert, damit GPS-Task darauf zugreifen kann)
QueueHandle_t s_lte_command_queue = nullptr;
std::atomic<bool> s_lte_target_enabled{true};
std::atomic<bool> s_lte_runtime_active{false};
esp_err_t s_lte_last_error = ESP_OK;
std::atomic<bool> s_lte_quiet_active{false};

// Vorwärtsdeklaration für LTE-Steuerung, genutzt im GPS-Task
esp_err_t lte_runtime_set_enabled(bool enable);
typedef enum {
    LTE_CMD_ENABLE,
    LTE_CMD_DISABLE,
    LTE_CMD_RESTART,
    LTE_CMD_QUIET_ON,
    LTE_CMD_QUIET_OFF,
} lte_runtime_cmd_t;

esp_err_t lte_runtime_set_enabled(bool enable)
{
#if WALTER_ENABLE_LTE
    ESP_LOGI(TAG, "Command: LTE %s (caller=%s)", enable ? "enable" : "disable", pcTaskGetName(NULL));
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
    return ESP_OK;
#else
    (void)enable;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t lte_runtime_set_quiet(bool quiet)
{
#if WALTER_ENABLE_LTE
    ESP_LOGI(TAG, "Command: LTE quiet %s (caller=%s)", quiet ? "on" : "off", pcTaskGetName(NULL));

    if (!s_lte_command_queue) {
        ESP_LOGW(TAG, "LTE command queue not ready");
        return ESP_ERR_INVALID_STATE;
    }

    lte_runtime_cmd_t cmd = quiet ? LTE_CMD_QUIET_ON : LTE_CMD_QUIET_OFF;
    if (xQueueSend(s_lte_command_queue, &cmd, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGW(TAG, "LTE quiet command queue full");
        return ESP_ERR_TIMEOUT;
    }

    // Optimistisch setzen, damit Aufrufer nicht auf Queue-Verarbeitung warten muss
    s_lte_quiet_active.store(quiet);

    TickType_t start = xTaskGetTickCount();
    const TickType_t wait_ticks = pdMS_TO_TICKS(3000);
    while ((xTaskGetTickCount() - start) < wait_ticks) {
        if (s_lte_quiet_active.load() == quiet) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGW(TAG, "LTE quiet transition timeout (requested=%d active=%d)",
             quiet ? 1 : 0,
             s_lte_quiet_active.load() ? 1 : 0);
    return ESP_ERR_TIMEOUT;
#else
    (void)quiet;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t lte_runtime_restart(void)
{
#if WALTER_ENABLE_LTE
    ESP_LOGI(TAG, "Command: LTE restart (caller=%s)", pcTaskGetName(NULL));

    if (!s_lte_command_queue) {
        ESP_LOGW(TAG, "LTE command queue not ready");
        return ESP_ERR_INVALID_STATE;
    }

    lte_runtime_cmd_t cmd = LTE_CMD_RESTART;
    if (xQueueSend(s_lte_command_queue, &cmd, pdMS_TO_TICKS(100)) != pdPASS) {
        ESP_LOGW(TAG, "LTE restart command queue full");
        return ESP_ERR_TIMEOUT;
    }

    s_lte_target_enabled.store(true);
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

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
static constexpr const char *GAS_NVS_NAMESPACE = "gas_hist";
static constexpr const char *GAS_NVS_KEY = "hist";
static constexpr const char *GAS_NVS_TARA_KEY = "tara";
static constexpr uint32_t GAS_TARA_BLOB_VERSION = 1;
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
    uint8_t iaq_accuracy;
    float co2_eq_ppm;
    float bvoc_ppm;
    bool gas_valid;
    bool heater_stable;
    uint8_t gas_range;
    uint8_t gas_index;
} bme680_measurement_t;

enum class PressureTrendState : uint8_t {
    kUnknown = 0,
    kSteady,
    kRiseSlow,
    kRiseFast,
    kFallSlow,
    kFallFast,
};

enum class PressureDiffState : uint8_t {
    kUnknown = 0,
    kOk,
    kWarn,
    kHigh,
};

typedef struct {
    int64_t ts_us;
    float pressure_hpa;
    bool valid;
} pressure_sample_t;

typedef struct {
    pressure_sample_t samples[1200];  // 3h window at worst-case cadence
    size_t head;
    size_t count;
} pressure_history_t;

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
static pressure_history_t s_bme_pressure_history[kBme680MaxSensors] = {};
static PressureTrendState s_bme_pressure_trend[kBme680MaxSensors] = {
    PressureTrendState::kUnknown,
    PressureTrendState::kUnknown,
};

static constexpr int kPressTrendWindowSec = 3 * 3600;      // 3h Fenster
static constexpr int kPressTrendMinWindowSec = 3600;       // mindestens 1h Daten
static constexpr uint16_t kPressTrendMinSamples = 20;      // Mindestanzahl Samples
static constexpr float kPressTrendSlow = 0.3f;             // hPa/h
static constexpr float kPressTrendFast = 1.0f;             // hPa/h
static constexpr float kPressTrendHys = 0.2f;              // Hysterese

static constexpr int kPressDiffWindowSec = 300;            // 5 Minuten Outdoor-Mittel
static constexpr float kPressDiffWarn = 1.0f;              // hPa
static constexpr float kPressDiffHigh = 2.0f;              // hPa

static const char *pressure_trend_state_str(PressureTrendState state)
{
    switch (state) {
        case PressureTrendState::kSteady:
            return "steady";
        case PressureTrendState::kRiseSlow:
            return "rise_slow";
        case PressureTrendState::kRiseFast:
            return "rise_fast";
        case PressureTrendState::kFallSlow:
            return "fall_slow";
        case PressureTrendState::kFallFast:
            return "fall_fast";
        case PressureTrendState::kUnknown:
        default:
            return "unknown";
    }
}

static const char *pressure_diff_state_str(PressureDiffState state)
{
    switch (state) {
        case PressureDiffState::kOk:
            return "ok";
        case PressureDiffState::kWarn:
            return "warn";
        case PressureDiffState::kHigh:
            return "high";
        case PressureDiffState::kUnknown:
        default:
            return "unknown";
    }
}

static void pressure_history_add(size_t index, int64_t ts_us, float pressure_hpa)
{
    if (index >= kBme680MaxSensors) {
        return;
    }
    auto &hist = s_bme_pressure_history[index];
    hist.samples[hist.head] = {ts_us, pressure_hpa, true};
    hist.head = (hist.head + 1) % (sizeof(hist.samples) / sizeof(hist.samples[0]));
    if (hist.count < (sizeof(hist.samples) / sizeof(hist.samples[0]))) {
        hist.count++;
    }
}

static bool pressure_history_trend(size_t index, int64_t now_us, float *out_slope_hpa_h,
                                   uint16_t *out_samples, uint16_t *out_window_min,
                                   PressureTrendState *io_state)
{
    if (index >= kBme680MaxSensors) {
        return false;
    }
    const auto &hist = s_bme_pressure_history[index];
    if (hist.count == 0) {
        return false;
    }

    const int64_t window_us = static_cast<int64_t>(kPressTrendWindowSec) * 1000000LL;
    const int64_t min_window_us = static_cast<int64_t>(kPressTrendMinWindowSec) * 1000000LL;
    const size_t cap = sizeof(hist.samples) / sizeof(hist.samples[0]);

    // Collect samples inside window (newest to oldest)
    float sum_t = 0.0f;
    float sum_p = 0.0f;
    float sum_t2 = 0.0f;
    float sum_tp = 0.0f;
    uint16_t n = 0;
    int64_t newest_ts = 0;
    int64_t oldest_ts = 0;

    // Use the newest sample timestamp as reference to keep numbers small
    const pressure_sample_t *latest = nullptr;
    for (size_t i = 0; i < hist.count; ++i) {
        size_t idx = (hist.head + cap - 1 - i) % cap;
        const auto &s = hist.samples[idx];
        if (!s.valid) {
            continue;
        }
        if (latest == nullptr) {
            latest = &s;
        }
        int64_t age = latest->ts_us - s.ts_us;
        if (age > window_us) {
            break;
        }
        float t = static_cast<float>(-(age) / 1e6);  // seconds relative to latest (0 at newest)
        sum_t += t;
        sum_p += s.pressure_hpa;
        sum_t2 += t * t;
        sum_tp += t * s.pressure_hpa;
        if (n == 0) {
            newest_ts = s.ts_us;
        }
        oldest_ts = s.ts_us;
        ++n;
    }

    if (n < kPressTrendMinSamples) {
        return false;
    }
    int64_t span_us = newest_ts - oldest_ts;
    if (span_us < min_window_us) {
        return false;
    }

    float denom = (n * sum_t2) - (sum_t * sum_t);
    if (denom == 0.0f) {
        return false;
    }
    float slope_hpa_per_s = ((n * sum_tp) - (sum_t * sum_p)) / denom;
    float slope_hpa_per_h = slope_hpa_per_s * 3600.0f;

    PressureTrendState prev = io_state ? *io_state : PressureTrendState::kUnknown;
    PressureTrendState state = PressureTrendState::kSteady;
    float slow_up = kPressTrendSlow + kPressTrendHys;
    float slow_down = kPressTrendSlow - kPressTrendHys;
    float fast_up = kPressTrendFast + kPressTrendHys;
    float fast_down = kPressTrendFast - kPressTrendHys;

    auto classify = [&](float s, PressureTrendState last) {
        if (last == PressureTrendState::kRiseFast || last == PressureTrendState::kRiseSlow) {
            if (s > fast_down) return PressureTrendState::kRiseFast;
            if (s > slow_down) return PressureTrendState::kRiseSlow;
        }
        if (last == PressureTrendState::kFallFast || last == PressureTrendState::kFallSlow) {
            if (s < -fast_down) return PressureTrendState::kFallFast;
            if (s < -slow_down) return PressureTrendState::kFallSlow;
        }
        if (s >= fast_up) return PressureTrendState::kRiseFast;
        if (s >= slow_up) return PressureTrendState::kRiseSlow;
        if (s <= -fast_up) return PressureTrendState::kFallFast;
        if (s <= -slow_up) return PressureTrendState::kFallSlow;
        return PressureTrendState::kSteady;
    };

    state = classify(slope_hpa_per_h, prev);

    if (out_slope_hpa_h) {
        *out_slope_hpa_h = slope_hpa_per_h;
    }
    if (out_samples) {
        *out_samples = n;
    }
    if (out_window_min) {
        *out_window_min = static_cast<uint16_t>(span_us / 60000000LL);
    }
    if (io_state) {
        *io_state = state;
    }
    return true;
}

static bool pressure_history_avg(size_t index, int64_t now_us, int window_sec, float *out_avg,
                                 uint16_t *out_samples)
{
    if (index >= kBme680MaxSensors) {
        return false;
    }
    const auto &hist = s_bme_pressure_history[index];
    const int64_t window_us = static_cast<int64_t>(window_sec) * 1000000LL;
    const size_t cap = sizeof(hist.samples) / sizeof(hist.samples[0]);
    float sum = 0.0f;
    uint16_t n = 0;
    for (size_t i = 0; i < hist.count; ++i) {
        size_t idx = (hist.head + cap - 1 - i) % cap;
        const auto &s = hist.samples[idx];
        if (!s.valid) {
            continue;
        }
        if ((now_us - s.ts_us) > window_us) {
            break;
        }
        sum += s.pressure_hpa;
        ++n;
    }
    if (n == 0) {
        return false;
    }
    if (out_avg) {
        *out_avg = sum / static_cast<float>(n);
    }
    if (out_samples) {
        *out_samples = n;
    }
    return true;
}

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

    (void)log_tag;  // Validation disabled; keep signature stable.

    bme680_plausibility_state_t &state = s_bme680_plausibility[index];
    state.have_value = true;
    state.last = measurement;
    state.last_timestamp_us = now_us;

    if (out_used_fallback) {
        *out_used_fallback = false;
    }

    return true;
}

static void bme680_task(void *arg)
{
    const bme680_task_params_t *params = static_cast<const bme680_task_params_t *>(arg);
    const size_t index = params->index;
    const char *log_tag = params->log_tag;

    const bool use_bsec = (params->address == WALTER_BME680_ADDR_1);
    const TickType_t period = ms_to_ticks(use_bsec ? 3000U : WALTER_BME680_POLL_INTERVAL_MS);
    uint32_t bsec_publish_divider = use_bsec ? (WALTER_BME680_POLL_INTERVAL_MS / 3000U) : 1U;
    if (bsec_publish_divider == 0U) {
        bsec_publish_divider = 1U;
    }
    uint32_t bsec_publish_countdown = bsec_publish_divider;

    ESP_LOGI(TAG, "%s task started (index=%u, addr=0x%02X)", log_tag, (unsigned)index, params->address);

    bme680_handle_t handle = nullptr;
    bool initialised = false;
    bool startup_delay_done = false;

    bsec_library_return_t bsec_status = BSEC_OK;
    bool bsec_ready = false;

    if (use_bsec) {
        bsec_status = bsec_init();
        if (bsec_status != BSEC_OK) {
            ESP_LOGW(TAG, "%s bsec_init failed: %d", log_tag, (int)bsec_status);
        } else {
            bsec_sensor_configuration_t requested_virtual_sensors[] = {
                {BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_IAQ},
                {BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_CO2_EQUIVALENT},
                {BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_BREATH_VOC_EQUIVALENT},
                {BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE},
                {BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY},
                {BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_RAW_PRESSURE},
                {BSEC_SAMPLE_RATE_LP, BSEC_OUTPUT_RAW_GAS},
            };
            bsec_sensor_configuration_t required_sensor_settings[BSEC_MAX_PHYSICAL_SENSOR] = {};
            uint8_t n_required = BSEC_MAX_PHYSICAL_SENSOR;
            bsec_status = bsec_update_subscription(requested_virtual_sensors,
                                                   sizeof(requested_virtual_sensors) / sizeof(requested_virtual_sensors[0]),
                                                   required_sensor_settings,
                                                   &n_required);
            if (bsec_status != BSEC_OK) {
                ESP_LOGW(TAG, "%s bsec_update_subscription failed: %d", log_tag, (int)bsec_status);
            } else {
                bsec_ready = true;
                ESP_LOGI(TAG, "%s BSEC enabled (LP 3s, publish/30s)", log_tag);
            }
        }
    }

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
            measurement.iaq_accuracy = 0;
            measurement.co2_eq_ppm = 0.0f;
            measurement.bvoc_ppm = 0.0f;
            measurement.gas_valid = data.gas_valid;
            measurement.heater_stable = data.heater_stable;
            measurement.gas_range = data.gas_range;
            measurement.gas_index = data.gas_index;

            if (use_bsec && bsec_ready) {
                const int64_t ts_ns = timestamp_us * 1000;  // BSEC expects ns
                bsec_input_t inputs[4] = {};
                uint8_t n_inputs = 0;

                inputs[n_inputs++] = {ts_ns, data.air_temperature, 1, BSEC_INPUT_TEMPERATURE};
                inputs[n_inputs++] = {ts_ns, data.barometric_pressure, 1, BSEC_INPUT_PRESSURE};
                inputs[n_inputs++] = {ts_ns, data.relative_humidity, 1, BSEC_INPUT_HUMIDITY};
                inputs[n_inputs++] = {ts_ns, data.gas_resistance, 1, BSEC_INPUT_GASRESISTOR};

                bsec_output_t outputs[10] = {};
                uint8_t n_outputs = sizeof(outputs) / sizeof(outputs[0]);
                bsec_status = bsec_do_steps(inputs, n_inputs, outputs, &n_outputs);
                if (bsec_status != BSEC_OK) {
                    ESP_LOGW(TAG, "%s bsec_do_steps failed: %d", log_tag, (int)bsec_status);
                } else {
                    for (uint8_t i = 0; i < n_outputs; ++i) {
                        const bsec_output_t &out = outputs[i];
                        switch (out.sensor_id) {
                        case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
                            measurement.temperature_c = out.signal;
                            break;
                        case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
                            measurement.humidity_pct = out.signal;
                            break;
                        case BSEC_OUTPUT_RAW_PRESSURE:
                            measurement.pressure_hpa = out.signal / 100.0f;
                            break;
                        case BSEC_OUTPUT_IAQ:
                            measurement.iaq_score = static_cast<uint16_t>(out.signal);
                            measurement.iaq_accuracy = out.accuracy;
                            break;
                        case BSEC_OUTPUT_CO2_EQUIVALENT:
                            measurement.co2_eq_ppm = out.signal;
                            break;
                        case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
                            measurement.bvoc_ppm = out.signal;
                            break;
                        default:
                            break;
                        }
                    }
                }
            }

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

        bool do_publish = publish;
        if (use_bsec && publish) {
            if (bsec_publish_countdown > 0) {
                bsec_publish_countdown -= 1;
            }
            if (bsec_publish_countdown > 0) {
                do_publish = false;
            } else {
                bsec_publish_countdown = bsec_publish_divider;
                do_publish = true;
            }
        }

        if (do_publish) {
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
#if WALTER_ENABLE_WEBUI
            womo_web_imu_sample_t web_sample = {
                .valid = true,
                .fallback = fallback,
                .calibrated = measurement.calibrated,
                .yaw_deg = measurement.yaw_deg,
                .pitch_deg = measurement.pitch_deg,
                .roll_deg = measurement.roll_deg,
                .temperature_c = measurement.temperature_c,
                .cal_sys = measurement.calibration_sys,
                .cal_gyro = measurement.calibration_gyro,
                .cal_accel = measurement.calibration_accel,
                .cal_mag = measurement.calibration_mag,
                .timestamp_us = publish_ts,
            };
            womo_web_publish_imu(&web_sample);
#endif
#if WALTER_SENSOR_LOG_BNO055
            static const int64_t kLogIntervalUs = (int64_t)WALTER_ANALOG_POLL_INTERVAL_MS * 1000;
            bool should_log = false;
            if (s_bno055_last_log_ts_us == 0) {
                should_log = true;
            } else if (publish_ts > s_bno055_last_log_ts_us) {
                int64_t delta_us = publish_ts - s_bno055_last_log_ts_us;
                if (delta_us >= kLogIntervalUs) {
                    should_log = true;
                }
            }

            if (should_log) {
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
            }
#endif
        }

        vTaskDelayUntil(&last_wake, period);
    }
}
#endif

#if WALTER_ENABLE_GPS
static void sensor_state_note_gps_request_result(esp_err_t status);
static void sensor_state_publish_gps_fix(const womo_gps_data_t &fix);
static bool gps_fix_plausible(const womo_gps_data_t &fix);

static void gps_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "GPS task started");

#if !WALTER_ENABLE_LTE
    ESP_LOGE(TAG, "GPS task requires LTE modem support; enable WALTER_ENABLE_LTE");
    vTaskDelete(nullptr);
    return;
#else
    if (!s_modem_ready_sem) {
        ESP_LOGE(TAG, "GPS task missing modem-ready semaphore; aborting");
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "GPS waiting for modem initialisation");
    while (xSemaphoreTake(s_modem_ready_sem, ms_to_ticks(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "GPS still waiting for modem...");
    }
    xSemaphoreGive(s_modem_ready_sem);

    esp_err_t init_err = womo_gps_init();
    sensor_state_note_gps_request_result(init_err);
    if (init_err != ESP_OK) {
        ESP_LOGE(TAG, "GPS init failed: %s", esp_err_to_name(init_err));
        vTaskDelete(nullptr);
        return;
    }

    const TickType_t first_delay = ms_to_ticks(WALTER_GPS_FIRST_DELAY_MS);
    const TickType_t request_interval = ms_to_ticks(WALTER_GPS_REQUEST_INTERVAL_MS);
    const TickType_t poll_delay = ms_to_ticks(WALTER_GPS_POLL_INTERVAL_MS);
    const TickType_t gnss_timeout = ms_to_ticks(WALTER_GPS_FIX_TIMEOUT_MS);
    TickType_t last_request = xTaskGetTickCount();
    bool first_cycle = true;
    int64_t last_fix_timestamp = 0;
    const TickType_t wait_log_interval = ms_to_ticks(10000);
    TickType_t last_wait_log = xTaskGetTickCount();

    while (true) {
        TickType_t now = xTaskGetTickCount();
        TickType_t wait = first_cycle ? first_delay : request_interval;
        if ((now - last_request) >= wait) {
            uint32_t waited_ms = (uint32_t)((now - last_request) * portTICK_PERIOD_MS);
            ESP_LOGI(TAG, "GPS cycle trigger after %u ms (first=%s)", waited_ms, first_cycle ? "yes" : "no");
            womo_gps_data_t fix = {};
            esp_err_t req_err = modem_manager_run_gnss_cycle(gnss_timeout, &fix);
            sensor_state_note_gps_request_result(req_err);
            last_request = now;
            first_cycle = false;

            if (req_err == ESP_OK) {
                const bool plausible = gps_fix_plausible(fix);
                if (plausible) {
                    sensor_state_publish_gps_fix(fix);
                    if (fix.timestamp != 0 && fix.timestamp != last_fix_timestamp) {
                        last_fix_timestamp = fix.timestamp;
                        ESP_LOGI(TAG,
                                 "GPS fix lat=%.6f lon=%.6f alt=%.1f m speed=%.1f km/h sats=%u conf=%.1f m",
                                 fix.latitude,
                                 fix.longitude,
                                 fix.altitude_m,
                                 fix.speed_kmh,
                                 fix.satellites,
                                 fix.confidence_m);
                    }
                } else {
                    ESP_LOGW(TAG,
                             "GPS fix rejected (plausibility) lat=%.6f lon=%.6f alt=%.1f m speed=%.1f km/h sats=%u conf=%.1f m",
                             fix.latitude,
                             fix.longitude,
                             fix.altitude_m,
                             fix.speed_kmh,
                             fix.satellites,
                             fix.confidence_m);
                }
            } else {
                ESP_LOGW(TAG, "GNSS cycle failed: %s", esp_err_to_name(req_err));
            }
        } else {
            TickType_t since_log = now - last_wait_log;
            if (since_log >= wait_log_interval) {
                TickType_t remaining_ticks = wait - (now - last_request);
                uint32_t remaining_ms = (uint32_t)(remaining_ticks * portTICK_PERIOD_MS);
                ESP_LOGD(TAG, "GPS waiting: next cycle in %u ms (first=%s)", remaining_ms, first_cycle ? "yes" : "no");
                last_wait_log = now;
            }
        }

        vTaskDelay(poll_delay);
    }
#endif // WALTER_ENABLE_LTE
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
    int64_t ts_us;
    float net_a_kg;
    float net_b_kg;
    bool valid_a;
    bool valid_b;
} gas_hist_sample_t;

static constexpr size_t GAS_HISTORY_SAMPLES = (WALTER_GAS_HISTORY_MINUTES * 60) / 10; // 10s cadence
static gas_hist_sample_t s_gas_history[GAS_HISTORY_SAMPLES];
static size_t s_gas_hist_count = 0;
static size_t s_gas_hist_head = 0;

static float s_gas_tara_kg[2] = {WALTER_GAS_TARA_KG, WALTER_GAS_TARA_KG};

static constexpr size_t GAS_NVS_SLOTS = WALTER_GAS_HISTORY_MINUTES / WALTER_GAS_NVS_INTERVAL_MIN;
static gas_hist_sample_t s_gas_nvs_slots[GAS_NVS_SLOTS];
static size_t s_gas_nvs_count = 0;
static int64_t s_gas_last_nvs_save_us = 0;
static portMUX_TYPE s_gas_hist_mux = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    bool valid;
    int active_idx; // 0=A, 1=B, -1=keine
    float net_kg;
    float rate_kgph_1h;
    float rate_kgph_2h;
    float rest_hours;
    float net_a;
    float net_b;
} gas_consumption_state_t;
static gas_consumption_state_t s_gas_state = {};

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
        bool nc;
        float kg_a;
        float kg_b;
        int64_t timestamp_us;
    } hx711;
    struct {
        bool valid;
        int active_idx; // 0=A, 1=B, -1=none
        float net_kg;
        float rate_kgph_1h;
        float rate_kgph_2h;
        float rest_hours;
        float net_a;
        float net_b;
    } gas;
#endif
#if WALTER_ENABLE_ANALOG
    struct {
        bool valid;
        womo_analog_data_t data;
        bool batt_nc[2];
        bool tank_nc[2];
        int64_t timestamp_us;
    } analog;
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
        uint32_t time_to_fix_ms;
        int64_t timestamp;
        int64_t last_fix_us;
        int64_t last_request_us;
        esp_err_t last_error;
    } gps;
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
        uint8_t iaq_accuracy; // Added for IAQ accuracy
        float co2_eq_ppm;     // Added for CO2 equivalent concentration
        float bvoc_ppm;       // Added for VOC concentration
        bool gas_valid;
        bool heater_stable;
        uint8_t gas_range;
        uint8_t gas_index;
        float press_trend_slope_hpa_h;
        uint16_t press_trend_samples;
        uint16_t press_trend_window_min;
        uint8_t press_trend_state;
        float press_diff_hpa;
        uint16_t press_diff_samples;
        uint16_t press_diff_window_min;
        uint8_t press_diff_state;
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
                                       bool nc,
                                       int64_t timestamp_us)
{
    portENTER_CRITICAL(&s_sensor_state_mux);
    s_sensor_state.hx711.valid_a = have_a;
    s_sensor_state.hx711.valid_b = have_b;
    s_sensor_state.hx711.fallback_a = fallback_a;
    s_sensor_state.hx711.fallback_b = fallback_b;
    s_sensor_state.hx711.nc = nc;
    s_sensor_state.hx711.kg_a = kg_a;
    s_sensor_state.hx711.kg_b = kg_b;
    s_sensor_state.hx711.timestamp_us = timestamp_us;
    portEXIT_CRITICAL(&s_sensor_state_mux);
}
#endif

// ------------------------------------------------------------
// Gas-Verbrauch: Verlauf, Persistenz, Ableitungen
// ------------------------------------------------------------

static inline float gas_net_from_raw(int idx, float kg_raw)
{
    float tara = WALTER_GAS_TARA_KG;
    if (idx >= 0 && idx < 2) {
        tara = s_gas_tara_kg[idx];
    }
    float net = kg_raw - tara;
    return net < 0.0f ? 0.0f : net;
}

static esp_err_t gas_tara_save_to_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(GAS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    struct __attribute__((packed)) {
        uint32_t version;
        float tara[2];
    } blob = {};
    blob.version = GAS_TARA_BLOB_VERSION;
    blob.tara[0] = s_gas_tara_kg[0];
    blob.tara[1] = s_gas_tara_kg[1];

    err = nvs_set_blob(handle, GAS_NVS_TARA_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static void gas_tara_restore_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(GAS_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Gas tara NVS open failed: %s", esp_err_to_name(err));
        }
        return;
    }

    struct __attribute__((packed)) {
        uint32_t version;
        float tara[2];
    } blob = {};
    size_t sz = sizeof(blob);
    err = nvs_get_blob(handle, GAS_NVS_TARA_KEY, &blob, &sz);
    nvs_close(handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Gas tara NVS read failed: %s", esp_err_to_name(err));
        }
        return;
    }
    if (sz != sizeof(blob) || blob.version != GAS_TARA_BLOB_VERSION) {
        ESP_LOGW(TAG, "Gas tara NVS blob invalid (sz=%u, ver=%u)", (unsigned)sz, (unsigned)blob.version);
        return;
    }

    float max_tara = (WALTER_GAS_TARA_KG_MAX > 0.0f) ? WALTER_GAS_TARA_KG_MAX : WALTER_GAS_TARA_KG;
    for (int i = 0; i < 2; ++i) {
        float v = blob.tara[i];
        if (!isfinite(v) || v <= 0.0f) {
            v = WALTER_GAS_TARA_KG;
        }
        if (v > max_tara) {
            v = max_tara;
        }
        s_gas_tara_kg[i] = v;
    }
    ESP_LOGI(TAG, "Gas tara restored: A=%.2f kg, B=%.2f kg", s_gas_tara_kg[0], s_gas_tara_kg[1]);
}

static void gas_history_push(bool valid_a, float kg_a, bool valid_b, float kg_b, int64_t ts_us)
{
    float net_a = valid_a ? gas_net_from_raw(HX_IDX_PLATFORM_A, kg_a) : 0.0f;
    float net_b = valid_b ? gas_net_from_raw(HX_IDX_PLATFORM_B, kg_b) : 0.0f;

    portENTER_CRITICAL(&s_gas_hist_mux);
    gas_hist_sample_t *slot = &s_gas_history[s_gas_hist_head];
    slot->ts_us = ts_us;
    slot->net_a_kg = net_a;
    slot->net_b_kg = net_b;
    slot->valid_a = valid_a;
    slot->valid_b = valid_b;

    s_gas_hist_head = (s_gas_hist_head + 1) % GAS_HISTORY_SAMPLES;
    if (s_gas_hist_count < GAS_HISTORY_SAMPLES) {
        ++s_gas_hist_count;
    }
    portEXIT_CRITICAL(&s_gas_hist_mux);

    // NVS-Checkpoint nur selten, um Wear zu begrenzen
    const int64_t interval_us = (int64_t)WALTER_GAS_NVS_INTERVAL_MIN * 60 * 1000000LL;
    if (ts_us - s_gas_last_nvs_save_us >= interval_us) {
        size_t idx = s_gas_nvs_count < GAS_NVS_SLOTS ? s_gas_nvs_count : (s_gas_nvs_count % GAS_NVS_SLOTS);
        s_gas_nvs_slots[idx] = *slot;
        if (s_gas_nvs_count < GAS_NVS_SLOTS) {
            ++s_gas_nvs_count;
        }
        s_gas_last_nvs_save_us = ts_us;

        nvs_handle_t handle;
        esp_err_t err = nvs_open(GAS_NVS_NAMESPACE, NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            struct __attribute__((packed)) {
                uint32_t count;
                gas_hist_sample_t slots[GAS_NVS_SLOTS];
            } blob = {};
            blob.count = s_gas_nvs_count;
            memcpy(blob.slots, s_gas_nvs_slots, sizeof(s_gas_nvs_slots));
            err = nvs_set_blob(handle, GAS_NVS_KEY, &blob, sizeof(blob));
            if (err == ESP_OK) {
                err = nvs_commit(handle);
            }
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Gas history NVS save failed: %s", esp_err_to_name(err));
            }
            nvs_close(handle);
        } else {
            ESP_LOGW(TAG, "Gas history NVS open failed: %s", esp_err_to_name(err));
        }
    }
}

static void gas_history_restore_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(GAS_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Gas history NVS open (ro) failed: %s", esp_err_to_name(err));
        }
        return;
    }

    struct __attribute__((packed)) {
        uint32_t count;
        gas_hist_sample_t slots[GAS_NVS_SLOTS];
    } blob = {};
    size_t sz = sizeof(blob);
    err = nvs_get_blob(handle, GAS_NVS_KEY, &blob, &sz);
    nvs_close(handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Gas history NVS read failed: %s", esp_err_to_name(err));
        }
        return;
    }

    s_gas_nvs_count = blob.count > GAS_NVS_SLOTS ? GAS_NVS_SLOTS : blob.count;
    memcpy(s_gas_nvs_slots, blob.slots, sizeof(s_gas_nvs_slots));

    // Seed RAM-Historie mit NVS-Slots (chronologische Reihenfolge annehmen)
    for (size_t i = 0; i < s_gas_nvs_count; ++i) {
        gas_hist_sample_t *s = &s_gas_nvs_slots[i];
        float kg_a = s->net_a_kg + s_gas_tara_kg[HX_IDX_PLATFORM_A];
        float kg_b = s->net_b_kg + s_gas_tara_kg[HX_IDX_PLATFORM_B];
        gas_history_push(s->valid_a, kg_a, s->valid_b, kg_b, s->ts_us);
    }
    ESP_LOGI(TAG, "Gas history restored: %u checkpoints", (unsigned)s_gas_nvs_count);
}

static bool gas_history_find_sample(int64_t now_us, int64_t age_target_us, gas_hist_sample_t *out)
{
    bool found = false;
    portENTER_CRITICAL(&s_gas_hist_mux);
    size_t idx = s_gas_hist_head;
    for (size_t i = 0; i < s_gas_hist_count; ++i) {
        if (idx == 0) {
            idx = GAS_HISTORY_SAMPLES;
        }
        idx--;
        gas_hist_sample_t s = s_gas_history[idx];
        if (now_us - s.ts_us >= age_target_us) {
            *out = s;
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_gas_hist_mux);
    return found;
}

static void gas_compute_state(bool valid_a, float kg_a, bool valid_b, float kg_b, int64_t ts_us)
{
    float net_a = valid_a ? gas_net_from_raw(HX_IDX_PLATFORM_A, kg_a) : 0.0f;
    float net_b = valid_b ? gas_net_from_raw(HX_IDX_PLATFORM_B, kg_b) : 0.0f;

    gas_hist_sample_t s1h = {};
    gas_hist_sample_t s2h = {};
    bool have1h = gas_history_find_sample(ts_us, 3600LL * 1000000LL, &s1h);
    bool have2h = gas_history_find_sample(ts_us, 7200LL * 1000000LL, &s2h);

    auto rate_from = [](float now_net, float prev_net, int64_t dt_us) {
        if (dt_us <= 0) return 0.0f;
        float delta = prev_net - now_net;
        if (delta <= 0.0f) return 0.0f;
        float hours = (float)dt_us / 3600000000.0f;
        return delta / hours;
    };

    float rate1_a = have1h && s1h.valid_a ? rate_from(net_a, s1h.net_a_kg, ts_us - s1h.ts_us) : 0.0f;
    float rate1_b = have1h && s1h.valid_b ? rate_from(net_b, s1h.net_b_kg, ts_us - s1h.ts_us) : 0.0f;
    float rate2_a = have2h && s2h.valid_a ? rate_from(net_a, s2h.net_a_kg, ts_us - s2h.ts_us) : 0.0f;
    float rate2_b = have2h && s2h.valid_b ? rate_from(net_b, s2h.net_b_kg, ts_us - s2h.ts_us) : 0.0f;

    // Aktive Flasche wählen: die mit Netto > Min und geringerem Netto (typisch: aktuell in Benutzung)
    int active_idx = -1;
    if (net_a >= WALTER_GAS_MIN_NET_KG && net_b >= WALTER_GAS_MIN_NET_KG) {
        active_idx = (net_a <= net_b) ? HX_IDX_PLATFORM_A : HX_IDX_PLATFORM_B;
    } else if (net_a >= WALTER_GAS_MIN_NET_KG) {
        active_idx = HX_IDX_PLATFORM_A;
    } else if (net_b >= WALTER_GAS_MIN_NET_KG) {
        active_idx = HX_IDX_PLATFORM_B;
    }

    float net_active = 0.0f;
    float rate1 = 0.0f;
    float rate2 = 0.0f;
    if (active_idx == HX_IDX_PLATFORM_A) {
        net_active = net_a;
        rate1 = rate1_a;
        rate2 = rate2_a;
    } else if (active_idx == HX_IDX_PLATFORM_B) {
        net_active = net_b;
        rate1 = rate1_b;
        rate2 = rate2_b;
    }

    float rate_use = rate1 > 0.0f ? rate1 : rate2;
    float rest_h = (rate_use > 0.0f) ? (net_active / rate_use) : -1.0f;

    gas_consumption_state_t snapshot = {};
    snapshot.valid = (active_idx >= 0);
    snapshot.active_idx = active_idx;
    snapshot.net_kg = net_active;
    snapshot.rate_kgph_1h = rate1;
    snapshot.rate_kgph_2h = rate2;
    snapshot.rest_hours = rest_h;
    snapshot.net_a = net_a;
    snapshot.net_b = net_b;

    portENTER_CRITICAL(&s_sensor_state_mux);
    s_gas_state = snapshot;
    s_sensor_state.gas.valid = snapshot.valid;
    s_sensor_state.gas.active_idx = snapshot.active_idx;
    s_sensor_state.gas.net_kg = snapshot.net_kg;
    s_sensor_state.gas.rate_kgph_1h = snapshot.rate_kgph_1h;
    s_sensor_state.gas.rate_kgph_2h = snapshot.rate_kgph_2h;
    s_sensor_state.gas.rest_hours = snapshot.rest_hours;
    s_sensor_state.gas.net_a = snapshot.net_a;
    s_sensor_state.gas.net_b = snapshot.net_b;
    portEXIT_CRITICAL(&s_sensor_state_mux);
}

#if WALTER_ENABLE_ANALOG
static void sensor_state_publish_analog(const womo_analog_data_t *analog,
                                        const bool batt_nc[2],
                                        const bool tank_nc[2],
                                        int64_t timestamp_us)
{
    if (!analog || !batt_nc || !tank_nc) {
        return;
    }
    portENTER_CRITICAL(&s_sensor_state_mux);
    s_sensor_state.analog.valid = true;
    s_sensor_state.analog.data = *analog;
    memcpy(s_sensor_state.analog.batt_nc, batt_nc, sizeof(s_sensor_state.analog.batt_nc));
    memcpy(s_sensor_state.analog.tank_nc, tank_nc, sizeof(s_sensor_state.analog.tank_nc));
    s_sensor_state.analog.timestamp_us = timestamp_us;
    portEXIT_CRITICAL(&s_sensor_state_mux);
}
#endif

#if WALTER_ENABLE_GPS
static void sensor_state_note_gps_request_result(esp_err_t status)
{
    portENTER_CRITICAL(&s_sensor_state_mux);
    auto &slot = s_sensor_state.gps;
    slot.last_request_us = esp_timer_get_time();
    slot.last_error = status;
    if (status != ESP_OK) {
        slot.valid = false;
    }
    portEXIT_CRITICAL(&s_sensor_state_mux);
}

static void sensor_state_publish_gps_fix(const womo_gps_data_t &fix)
{
    portENTER_CRITICAL(&s_sensor_state_mux);
    auto &slot = s_sensor_state.gps;
    slot.valid = fix.valid;
    slot.latitude = fix.latitude;
    slot.longitude = fix.longitude;
    slot.altitude_m = fix.altitude_m;
    slot.speed_kmh = fix.speed_kmh;
    slot.heading_deg = fix.heading_deg;
    slot.satellites = fix.satellites;
    slot.confidence_m = fix.confidence_m;
    slot.time_to_fix_ms = fix.time_to_fix_ms;
    slot.timestamp = fix.timestamp;
    slot.last_fix_us = esp_timer_get_time();
    slot.last_error = ESP_OK;
    portEXIT_CRITICAL(&s_sensor_state_mux);
}

static bool gps_fix_plausible(const womo_gps_data_t &fix)
{
    if (!fix.valid) {
        return false;
    }
    const bool sats_ok = fix.satellites >= 4;
    const bool conf_ok = fix.confidence_m <= 1000.0f; // reject huge uncertainty
    const bool alt_ok = (fix.altitude_m > -500.0f) && (fix.altitude_m < 3000.0f);
    const bool speed_ok = fix.speed_kmh <= 80.0f; // assume Fahrzeug steht/rollt langsam
    return sats_ok && conf_ok && alt_ok && speed_ok;
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

    ESP_LOGI(TAG, "LTE reg state set: registered=%s info_valid=%s",
             registered ? "yes" : "no",
             s_sensor_state.lte.info_valid ? "yes" : "no");
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

    ESP_LOGI(TAG,
             "LTE cell info: op=%s reg=yes rsrp=%.1f dBm rsrq=%.1f dB band=%u",
             s_sensor_state.lte.operator_name,
             s_sensor_state.lte.rsrp_dbm,
             s_sensor_state.lte.rsrq_db,
             static_cast<unsigned>(s_sensor_state.lte.band));
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
    slot.press_trend_slope_hpa_h = 0.0f;
    slot.press_trend_samples = 0;
    slot.press_trend_window_min = 0;
    slot.press_trend_state = static_cast<uint8_t>(PressureTrendState::kUnknown);
    slot.press_diff_hpa = 0.0f;
    slot.press_diff_samples = 0;
    slot.press_diff_window_min = 0;
    slot.press_diff_state = static_cast<uint8_t>(PressureDiffState::kUnknown);
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

    // Trendberechnung vor dem Lock vorbereiten
    pressure_history_add(index, timestamp_us, measurement.pressure_hpa);

    float trend_slope_hpa_h = 0.0f;
    uint16_t trend_samples = 0;
    uint16_t trend_window_min = 0;
    PressureTrendState trend_state = s_bme_pressure_trend[index];
    bool trend_ok = pressure_history_trend(index,
                                           timestamp_us,
                                           &trend_slope_hpa_h,
                                           &trend_samples,
                                           &trend_window_min,
                                           &trend_state);
    if (!trend_ok) {
        trend_slope_hpa_h = 0.0f;
        trend_samples = 0;
        trend_window_min = 0;
        trend_state = PressureTrendState::kUnknown;
    } else {
        s_bme_pressure_trend[index] = trend_state;
    }

    float diff_hpa = 0.0f;
    uint16_t diff_samples = 0;
    uint16_t diff_window_min = 0;
    PressureDiffState diff_state = PressureDiffState::kUnknown;
    uint8_t sensor_addr = 0;
    {
        portENTER_CRITICAL(&s_sensor_state_mux);
        sensor_addr = s_sensor_state.bme680[index].address;
        portEXIT_CRITICAL(&s_sensor_state_mux);
    }
    if (sensor_addr == WALTER_BME680_ADDR_0) {
        // Outdoor ist Referenz; Indoor dient nur als Plausibilisierung (kein Mittelwert)
        float out_avg = 0.0f;
        uint16_t out_samples = 0;
        bool have_out_avg = pressure_history_avg(index,
                                                 timestamp_us,
                                                 kPressDiffWindowSec,
                                                 &out_avg,
                                                 &out_samples);

        float in_latest = 0.0f;
        bool have_in = false;
        int64_t in_age_us = 0;
        if (s_bme680_plausibility[1].have_value && s_bme680_plausibility[1].last_timestamp_us > 0) {
            in_latest = s_bme680_plausibility[1].last.pressure_hpa;
            in_age_us = timestamp_us - s_bme680_plausibility[1].last_timestamp_us;
            if (in_age_us <= static_cast<int64_t>(kPressDiffWindowSec) * 1000000LL) {
                have_in = true;
            }
        }

        if (have_out_avg && have_in) {
            diff_hpa = out_avg - in_latest;
            diff_samples = out_samples;
            diff_window_min = static_cast<uint16_t>(kPressDiffWindowSec / 60);
            float adiff = fabsf(diff_hpa);
            if (adiff < kPressDiffWarn) {
                diff_state = PressureDiffState::kOk;
            } else if (adiff < kPressDiffHigh) {
                diff_state = PressureDiffState::kWarn;
            } else {
                diff_state = PressureDiffState::kHigh;
            }
        }
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
    slot.iaq_accuracy = measurement.iaq_accuracy;
    slot.co2_eq_ppm = measurement.co2_eq_ppm;
    slot.bvoc_ppm = measurement.bvoc_ppm;
    slot.gas_valid = measurement.gas_valid;
    slot.heater_stable = measurement.heater_stable;
    slot.gas_range = measurement.gas_range;
    slot.gas_index = measurement.gas_index;
    slot.press_trend_slope_hpa_h = trend_slope_hpa_h;
    slot.press_trend_samples = trend_samples;
    slot.press_trend_window_min = trend_window_min;
    slot.press_trend_state = static_cast<uint8_t>(trend_state);
    slot.press_diff_hpa = diff_hpa;
    slot.press_diff_samples = diff_samples;
    slot.press_diff_window_min = diff_window_min;
    slot.press_diff_state = static_cast<uint8_t>(diff_state);
    slot.timestamp_us = timestamp_us;
    portEXIT_CRITICAL(&s_sensor_state_mux);
}
#endif

// Kompassrichtungen: 16-teilige Rose
static __attribute__((unused)) const char* heading_to_compass(float heading) {
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
#if WALTER_ENABLE_GPS
static void gps_task(void *arg);
static void sensor_state_publish_gps_fix(const womo_gps_data_t &fix);
static void sensor_state_note_gps_request_result(esp_err_t status);
#endif
#if WALTER_ENABLE_RS485
static void rs485_tx_task(void *arg);
static void rs485_rx_task(void *arg);
static void rs485_process_rx_line(const char *line);
static bool rs485_execute_command(const cJSON *root, const char *cmd_str, esp_err_t *out_err);
#endif
static esp_err_t sensor_subsystem_init(void);
static void configure_routing_after_connect(void);
static void configure_softap_dns(void);
static void enable_switched_3v3(void);
#if WALTER_ENABLE_RS485
static esp_err_t command_set_wifi_ap_enabled(bool enable);
static esp_err_t command_set_wifi_sta_enabled(bool enable);
static esp_err_t command_start_wifi_scan(void);
static esp_err_t command_set_wifi_credentials(const char *ssid, const char *password);
static void rs485_report_command_result(const char *cmd, esp_err_t err);
#if WALTER_ENABLE_HX711
static esp_err_t command_gas_bottle_replace(const cJSON *root);
#endif
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
    wifi_mode_t mode = WIFI_MODE_NULL;

    if (!ap_enable && !sta_enable) {
        if (s_wifi_driver_started) {
            err = esp_wifi_stop();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to stop WiFi driver: %s", esp_err_to_name(err));
                goto exit;
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
        goto exit;
    }

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
        goto exit;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &s_wifi_ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update SoftAP config: %s", esp_err_to_name(err));
        goto exit;
    }

    if (sta_enable) {
        err = esp_wifi_set_config(WIFI_IF_STA, &s_wifi_sta_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update STA config: %s", esp_err_to_name(err));
            goto exit;
        }
    } else if (prev_sta) {
        esp_wifi_disconnect();
        s_wifi_sta_ip_valid = false;
    }

    if (!s_wifi_driver_started) {
        err = esp_wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start WiFi driver: %s", esp_err_to_name(err));
            goto exit;
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
            goto exit;
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

exit:
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
        int32_t raw_a = 0;
        float kg_a = 0.0f;
        bool have_a = false;
        bool fallback_a = false;
        bool read_a_ok = false;
        int64_t fallback_a_ts = 0;

        if (womo_hx711_set_gain(&s_hx711, WOMO_HX711_GAIN_A_128) == ESP_OK &&
            womo_hx711_read_average(&s_hx711, WALTER_HX711_AVG_SAMPLES, &raw_a) == ESP_OK) {
            read_a_ok = true;
#if WALTER_HX711_INVERT_A
            float grams_a = ((int32_t)WALTER_HX711_OFFSET_A - raw_a) * WALTER_HX711_SCALE_A;
#else
            float grams_a = (raw_a - (int32_t)WALTER_HX711_OFFSET_A) * WALTER_HX711_SCALE_A;
#endif
            if (raw_a == 0) {
                read_a_ok = false;
                ESP_LOGW(TAG, "HX711: raw A is 0 -> likely not connected");
            } else {
                kg_a = grams_a / 1000.0f;
                have_a = hx_plausibility_check(HX_IDX_PLATFORM_A, kg_a, "HX711.A");
            }
        }
        if (!have_a && hx_get_last_valid(HX_IDX_PLATFORM_A, &kg_a, &fallback_a_ts)) {
            have_a = true;
            fallback_a = true;
            const char *reason = read_a_ok ? "new sample rejected" : "read failed";
            ESP_LOGW(TAG, "HX711.A using last valid value %.2f kg (%s)", kg_a, reason);
        }

#if WALTER_HX711_ENABLE_CHANNEL_B
        int32_t raw_b = 0;
        float kg_b = 0.0f;
        bool have_b = false;
        bool fallback_b = false;
        bool read_b_ok = false;
        int64_t fallback_b_ts = 0;

        if (womo_hx711_set_gain(&s_hx711, WOMO_HX711_GAIN_B_32) == ESP_OK &&
            womo_hx711_read_average(&s_hx711, WALTER_HX711_AVG_SAMPLES, &raw_b) == ESP_OK) {
            read_b_ok = true;
            float grams_b = (raw_b - (int32_t)WALTER_HX711_OFFSET_B) * WALTER_HX711_SCALE_B;
            if (raw_b == 0) {
                read_b_ok = false;
                ESP_LOGW(TAG, "HX711: raw B is 0 -> likely not connected");
            } else {
                kg_b = grams_b / 1000.0f;
                have_b = hx_plausibility_check(HX_IDX_PLATFORM_B, kg_b, "HX711.B");
            }
        }
        if (!have_b && hx_get_last_valid(HX_IDX_PLATFORM_B, &kg_b, &fallback_b_ts)) {
            have_b = true;
            fallback_b = true;
            const char *reason = read_b_ok ? "new sample rejected" : "read failed";
            ESP_LOGW(TAG, "HX711.B using last valid value %.2f kg (%s)", kg_b, reason);
        }

        if (have_a && have_b) {
            ESP_LOGI(TAG, "HX711: A=%.2fkg%s (raw=%ld) B=%.2fkg%s (raw=%ld)",
                     kg_a, fallback_a ? "*" : "", (long)raw_a,
                     kg_b, fallback_b ? "*" : "", (long)raw_b);
        } else if (have_a) {
            ESP_LOGI(TAG, "HX711: A=%.2fkg%s (raw=%ld)",
                     kg_a, fallback_a ? "*" : "", (long)raw_a);
        } else if (have_b) {
            ESP_LOGI(TAG, "HX711: B=%.2fkg%s (raw=%ld)",
                     kg_b, fallback_b ? "*" : "", (long)raw_b);
        }
#else
        const bool have_b = false;
        const bool fallback_b = false;
        const float kg_b = 0.0f;
        const int64_t fallback_b_ts = 0;
        if (have_a) {
            ESP_LOGI(TAG, "HX711: A=%.2fkg%s (raw=%ld)",
                 kg_a, fallback_a ? "*" : "", (long)raw_a);
        }
#endif

        bool hx_nc = false;
    #if WALTER_HX711_ENABLE_CHANNEL_B
        if (raw_a == 0 && raw_b == 0) {
            hx_nc = true;
        }
    #else
        if (raw_a == 0) {
            hx_nc = true;
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
        if (fallback_a) {
            timestamp_us = fallback_a_ts;
        }
#endif

        gas_history_push(have_a, kg_a, have_b, kg_b, timestamp_us);
        gas_compute_state(have_a, kg_a, have_b, kg_b, timestamp_us);

        sensor_state_publish_hx711(have_a, kg_a, fallback_a,
                                   have_b, kg_b, fallback_b,
                                   hx_nc,
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
            const int batt_nc_thresh_mv = 30; // ADC mV unterhalb dieses Werts => Batterie nicht angeschlossen
            bool batt_nc[2] = {false, false};
            bool tank_nc[2] = {false, false};

            for (int i = 0; i < 2; ++i) {
                if (analog.battery_valid[i] && analog.battery_mv[i] <= batt_nc_thresh_mv) {
                    batt_nc[i] = true;
                    analog.battery_valid[i] = false;
                    analog.battery_mv[i] = 0;
                    analog.battery_v[i] = 0.0f;
                    ESP_LOGI(TAG, "Battery%d marked NC (<=%d mV)", i + 1, batt_nc_thresh_mv);
                }
            }

            int64_t now_us = esp_timer_get_time();
            sensor_state_publish_analog(&analog, batt_nc, tank_nc, now_us);
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
static constexpr int WALTER_RS485_MAX_PENDING = 8;
static constexpr int WALTER_RS485_ACK_TIMEOUT_MS = 3000;

typedef struct {
    bool in_use;
    bool warned;
    uint32_t seq;
    int64_t sent_us;
    char label[32];
} rs485_pending_frame_t;

static SemaphoreHandle_t s_rs485_tx_mutex = nullptr;
static rs485_pending_frame_t s_rs485_pending[WALTER_RS485_MAX_PENDING];
static portMUX_TYPE s_rs485_pending_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_rs485_seq_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_rs485_tx_seq = 1;
static uint32_t s_rs485_last_rx_seq = 0;
static uint32_t s_rs485_last_ack_seq = 0;
static int64_t s_rs485_last_ack_us = 0;
static int64_t s_rs485_last_rx_heartbeat_us = 0;
static std::atomic<int64_t> s_rs485_last_rx_us{0};
static std::atomic<int64_t> s_rs485_last_rx_line_us{0};
static int64_t s_rs485_last_tx_heartbeat_us = 0;
static std::atomic<bool> s_rs485_display_ready(false);
static std::atomic<uint32_t> s_rs485_display_ready_seen(0);
static std::atomic<uint32_t> s_rs485_hello_sent(0);
static std::atomic<uint32_t> s_rs485_heartbeat_sent(0);
static std::atomic<int64_t> s_rs485_last_display_ready_us(0);

static uint32_t rs485_next_seq(void);
static void rs485_mark_rx_seq(uint32_t seq);
static void rs485_track_pending(uint32_t seq, const char *label);
static void rs485_resolve_pending(uint32_t seq, bool success, const char *label_from_packet, const char *err_text);
static void rs485_check_pending_timeouts(void);
static esp_err_t rs485_send_frame(const char *context, cJSON *root, bool default_need_ack);
static esp_err_t rs485_send_ack_packet(uint32_t rx_seq, bool success, const char *label, const char *err_text);
static void rs485_send_hello_packet(void);
static void rs485_send_heartbeat_packet(void);
static void rs485_handle_ack_packet(const cJSON *root);
static void rs485_handle_display_ready(void);
static void rs485_log_hex_bytes(const char *label, const uint8_t *data, size_t len);

// RS485 TX Task - sends sensor data as JSON
static void rs485_tx_task(void *arg)
{
    ESP_LOGI(TAG, "RS485 TX task started");
    
    s_rs485_display_ready.store(false);
    s_rs485_display_ready_seen.store(0);
    s_rs485_hello_sent.store(0);
    s_rs485_heartbeat_sent.store(0);
    s_rs485_last_display_ready_us.store(0);

    const TickType_t full_interval = pdMS_TO_TICKS(5000);  // full payload every 5 seconds
    TickType_t last_full_send = xTaskGetTickCount() - full_interval;
    const TickType_t loop_delay = pdMS_TO_TICKS(100);

    const TickType_t hello_pending_interval = ms_to_ticks(WALTER_RS485_HELLO_PENDING_INTERVAL_MS);
    const TickType_t heartbeat_interval = (WALTER_RS485_HEARTBEAT_INTERVAL_MS > 0)
                                              ? ms_to_ticks(WALTER_RS485_HEARTBEAT_INTERVAL_MS)
                                              : 0;
    TickType_t last_hello_send = xTaskGetTickCount() - hello_pending_interval;
    TickType_t last_heartbeat_send = xTaskGetTickCount();

    rs485_send_hello_packet();
    last_hello_send = xTaskGetTickCount();

    while (true) {
        TickType_t now = xTaskGetTickCount();
        bool send_full = ((now - last_full_send) >= full_interval);

        // Prefer to send full frames kurz nach dem letzten Heartbeat des Displays, um Kollisionen zu vermeiden.
        // Wenn der letzte HB frisch (<=500 ms) oder älter als 2 s ist, senden; sonst warten bis zum nächsten HB.
        int64_t now_us = esp_timer_get_time();
        int64_t last_hb_us = s_rs485_last_rx_heartbeat_us;
        if (send_full && s_rs485_display_ready.load() && last_hb_us > 0) {
            int64_t hb_age = now_us - last_hb_us;
            if (hb_age > 500000 && hb_age < 2000000) {
                send_full = false;  // außerhalb des bevorzugten Fensters: sende nach nächstem HB
            }
        }

        TickType_t hello_interval = s_rs485_display_ready.load() ? 0 : hello_pending_interval;
        if (hello_interval > 0 && (now - last_hello_send) >= hello_interval) {
            rs485_send_hello_packet();
            last_hello_send = now;
        }

        if (heartbeat_interval > 0 && s_rs485_display_ready.load() &&
            (now - last_heartbeat_send) >= heartbeat_interval) {
            rs485_send_heartbeat_packet();
            last_heartbeat_send = now;
        }

        if (send_full) {
            sensor_shared_state_t snapshot = sensor_state_snapshot();
                last_full_send = now;

            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "type", "full");
            cJSON_AddNumberToObject(root, "ts", (double)(esp_timer_get_time() / 1000));

#if WALTER_ENABLE_HX711
            if (snapshot.hx711.valid_a || snapshot.hx711.valid_b || snapshot.hx711.nc) {
                cJSON *hx = cJSON_CreateObject();
                const bool swap = (WALTER_GAS_SWAP_AB != 0);
                float hx_a = snapshot.hx711.kg_a;
                float hx_b = snapshot.hx711.kg_b;
                bool fb_a = snapshot.hx711.fallback_a;
                bool fb_b = snapshot.hx711.fallback_b;
                bool valid_a = snapshot.hx711.valid_a;
                bool valid_b = snapshot.hx711.valid_b;
                if (swap) {
                    std::swap(hx_a, hx_b);
                    std::swap(fb_a, fb_b);
                    std::swap(valid_a, valid_b);
                }

                if (valid_a) {
                    cJSON_AddNumberToObject(hx, "a", hx_a);
                    if (fb_a) {
                        cJSON_AddBoolToObject(hx, "a_fb", true);
                    }
                }
                if (valid_b) {
                    cJSON_AddNumberToObject(hx, "b", hx_b);
                    if (fb_b) {
                        cJSON_AddBoolToObject(hx, "b_fb", true);
                    }
                }
                if (valid_a && valid_b) {
                    cJSON_AddNumberToObject(hx, "sum", hx_a + hx_b);
                }
                cJSON_AddNumberToObject(hx, "ts_us", (double)snapshot.hx711.timestamp_us);
                if (snapshot.hx711.nc) {
                    cJSON_AddBoolToObject(hx, "nc", true);
                }
                cJSON_AddItemToObject(root, "hx", hx);
            }

            if (snapshot.gas.valid) {
                cJSON *gas = cJSON_CreateObject();
                const bool swap = (WALTER_GAS_SWAP_AB != 0);
                int active = snapshot.gas.active_idx;
                float net = snapshot.gas.net_kg;
                float net_a = snapshot.gas.net_a;
                float net_b = snapshot.gas.net_b;
                float net_active = -1.0f;
                if (swap) {
                    std::swap(net_a, net_b);
                    if (active == HX_IDX_PLATFORM_A) active = HX_IDX_PLATFORM_B;
                    else if (active == HX_IDX_PLATFORM_B) active = HX_IDX_PLATFORM_A;
                }
                if (active == HX_IDX_PLATFORM_A) {
                    net_active = net_a;
                } else if (active == HX_IDX_PLATFORM_B) {
                    net_active = net_b;
                }

                const float cap_kg = WALTER_GAS_FILL_KG;
                auto pct_from = [cap_kg](float net_kg) {
                    if (cap_kg <= 0.0f || !isfinite(net_kg)) return 0.0f;
                    float pct = (net_kg / cap_kg) * 100.0f;
                    if (pct < 0.0f) pct = 0.0f;
                    if (pct > 100.0f) pct = 100.0f;
                    return pct;
                };
                float pct_a = pct_from(net_a);
                float pct_b = pct_from(net_b);
                float pct_active = (net_active >= 0.0f) ? pct_from(net_active) : 0.0f;

                cJSON_AddNumberToObject(gas, "active", active);
                cJSON_AddNumberToObject(gas, "net", net);
                cJSON_AddNumberToObject(gas, "cap_kg", cap_kg);
                cJSON_AddNumberToObject(gas, "rate1h", snapshot.gas.rate_kgph_1h);
                cJSON_AddNumberToObject(gas, "rate2h", snapshot.gas.rate_kgph_2h);
                cJSON_AddNumberToObject(gas, "rest_h", snapshot.gas.rest_hours);
                cJSON_AddNumberToObject(gas, "net_a", net_a);
                cJSON_AddNumberToObject(gas, "net_b", net_b);
                cJSON_AddNumberToObject(gas, "pct_a", pct_a);
                cJSON_AddNumberToObject(gas, "pct_b", pct_b);
                cJSON_AddNumberToObject(gas, "pct", pct_active);
                cJSON_AddItemToObject(root, "gas", gas);
            }
#endif

#if WALTER_ENABLE_ANALOG
            if (snapshot.analog.valid) {
                const womo_analog_data_t &analog = snapshot.analog.data;
                cJSON *bat = cJSON_CreateObject();
                bool have_bat = false;
                if (analog.battery_valid[0]) cJSON_AddNumberToObject(bat, "b1", analog.battery_v[0]);
                if (analog.battery_valid[1]) cJSON_AddNumberToObject(bat, "b2", analog.battery_v[1]);
                if (snapshot.analog.batt_nc[0]) { cJSON_AddBoolToObject(bat, "nc1", true); have_bat = true; }
                if (snapshot.analog.batt_nc[1]) { cJSON_AddBoolToObject(bat, "nc2", true); have_bat = true; }
                if (analog.battery_valid[0] || analog.battery_valid[1]) {
                    have_bat = true;
                }
                if (have_bat) {
                    cJSON_AddItemToObject(root, "bat", bat);
                } else {
                    cJSON_Delete(bat);
                }

                cJSON *tank = cJSON_CreateObject();
                bool have_tank = false;
                if (analog.tank_valid[0]) cJSON_AddNumberToObject(tank, "t1", analog.tank_percent[0]);
                if (analog.tank_valid[1]) cJSON_AddNumberToObject(tank, "t2", analog.tank_percent[1]);
                if (snapshot.analog.tank_nc[0]) { cJSON_AddBoolToObject(tank, "nc1", true); have_tank = true; }
                if (snapshot.analog.tank_nc[1]) { cJSON_AddBoolToObject(tank, "nc2", true); have_tank = true; }
                if (analog.tank_valid[0] || analog.tank_valid[1]) {
                    have_tank = true;
                }
                if (have_tank) {
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
                    if (entry.iaq_accuracy > 0) {
                        cJSON_AddNumberToObject(node, "iaq_acc", entry.iaq_accuracy);
                    }
                    if (entry.co2_eq_ppm > 0.0f) {
                        cJSON_AddNumberToObject(node, "eco2_ppm", entry.co2_eq_ppm);
                    }
                    if (entry.bvoc_ppm > 0.0f) {
                        cJSON_AddNumberToObject(node, "bvoc_ppm", entry.bvoc_ppm);
                    }
                    if (entry.press_trend_samples > 0) {
                        cJSON *trend = cJSON_CreateObject();
                        cJSON_AddStringToObject(trend,
                                                 "state",
                                                 pressure_trend_state_str(static_cast<PressureTrendState>(entry.press_trend_state)));
                        cJSON_AddNumberToObject(trend, "slope_hpa_h", entry.press_trend_slope_hpa_h);
                        cJSON_AddNumberToObject(trend, "samples", entry.press_trend_samples);
                        cJSON_AddNumberToObject(trend, "window_min", entry.press_trend_window_min);
                        cJSON_AddItemToObject(node, "press_trend", trend);
                    }
                    if (entry.press_diff_samples > 0) {
                        cJSON *diff = cJSON_CreateObject();
                        cJSON_AddStringToObject(diff,
                                                "state",
                                                pressure_diff_state_str(static_cast<PressureDiffState>(entry.press_diff_state)));
                        cJSON_AddNumberToObject(diff, "hpa", entry.press_diff_hpa);
                        cJSON_AddNumberToObject(diff, "samples", entry.press_diff_samples);
                        cJSON_AddNumberToObject(diff, "window_min", entry.press_diff_window_min);
                        cJSON_AddItemToObject(node, "press_diff", diff);
                    }
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

                ESP_LOGI(TAG,
                         "RS485 full: lte registered=%s info_valid=%s rsrp=%.1f signal_pct=%u",
                         snapshot.lte.registered ? "yes" : "no",
                         snapshot.lte.info_valid ? "yes" : "no",
                         snapshot.lte.rsrp_dbm,
                         snapshot.lte.info_valid
                             ? lte_signal_strength_percent(snapshot.lte.rsrp_dbm)
                             : 0U);
            }
#endif

#if WALTER_ENABLE_GPS
            if (snapshot.gps.valid || snapshot.gps.last_request_us != 0 || snapshot.gps.last_error != ESP_OK) {
                cJSON *gps = cJSON_CreateObject();
                cJSON_AddBoolToObject(gps, "valid", snapshot.gps.valid);
                if (snapshot.gps.valid) {
                    // Mirror verbose fields with the short aliases expected by the display firmware.
                    cJSON_AddNumberToObject(gps, "lat", snapshot.gps.latitude);
                    cJSON_AddNumberToObject(gps, "lon", snapshot.gps.longitude);
                    cJSON_AddNumberToObject(gps, "alt_m", snapshot.gps.altitude_m);
                    cJSON_AddNumberToObject(gps, "alt", snapshot.gps.altitude_m);
                    cJSON_AddNumberToObject(gps, "speed_kmh", snapshot.gps.speed_kmh);
                    cJSON_AddNumberToObject(gps, "spd", snapshot.gps.speed_kmh);
                    cJSON_AddNumberToObject(gps, "heading_deg", snapshot.gps.heading_deg);
                    cJSON_AddNumberToObject(gps, "hdg", snapshot.gps.heading_deg);
                    cJSON_AddNumberToObject(gps, "sat", snapshot.gps.satellites);
                    cJSON_AddNumberToObject(gps, "conf_m", snapshot.gps.confidence_m);
                    cJSON_AddNumberToObject(gps, "conf", snapshot.gps.confidence_m);
                    cJSON_AddNumberToObject(gps, "ttf_ms", snapshot.gps.time_to_fix_ms);
                    cJSON_AddNumberToObject(gps, "ts", (double)snapshot.gps.timestamp);
                    if (snapshot.gps.last_fix_us != 0) {
                        cJSON_AddNumberToObject(gps, "last_fix_us", (double)snapshot.gps.last_fix_us);
                    }
                }
                if (snapshot.gps.last_request_us != 0) {
                    cJSON_AddNumberToObject(gps, "last_req_us", (double)snapshot.gps.last_request_us);
                }
                if (snapshot.gps.last_error != ESP_OK) {
                    cJSON_AddStringToObject(gps, "last_err", esp_err_to_name(snapshot.gps.last_error));
                }
                cJSON_AddItemToObject(root, "gps", gps);
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

            bool request_ack = s_rs485_display_ready.load();
            esp_err_t send_err = rs485_send_frame("full", root, request_ack);
            if (send_err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to send RS485 full snapshot: %s", esp_err_to_name(send_err));
            }
            cJSON_Delete(root);
        }

        rs485_check_pending_timeouts();
        vTaskDelay(loop_delay);  // Small delay to not hog CPU
    }
}

static esp_err_t rs485_send_frame(const char *context, cJSON *root, bool default_need_ack)
{
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }

    // Avoid collisions: wait for a short idle window after last RX before transmitting.
    static constexpr int64_t RS485_MIN_IDLE_US = 150000;   // 150 ms idle required
    static constexpr int64_t RS485_IDLE_WAIT_MAX_US = 400000; // cap wait at 400 ms
    int64_t wait_start = esp_timer_get_time();
    for (;;) {
        int64_t now = esp_timer_get_time();
        int64_t last_rx = s_rs485_last_rx_us.load();
        if (last_rx == 0 || (now - last_rx) >= RS485_MIN_IDLE_US) {
            break;  // idle gap satisfied
        }
        if ((now - wait_start) >= RS485_IDLE_WAIT_MAX_US) {
            ESP_LOGW(TAG,
                     "RS485 TX after idle wait timeout (last_rx=%lld us ago)",
                     (long long)(now - last_rx));
            break;  // give up waiting, try to send
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    bool need_ack = default_need_ack;
    cJSON *need_ack_item = cJSON_GetObjectItem(root, "need_ack");
    if (cJSON_IsBool(need_ack_item)) {
        need_ack = cJSON_IsTrue(need_ack_item);
    } else {
        cJSON_AddBoolToObject(root, "need_ack", need_ack);
    }

    uint32_t seq = rs485_next_seq();
    cJSON_AddNumberToObject(root, "seq", (double)seq);

    if (!cJSON_GetObjectItem(root, "ts")) {
        cJSON_AddNumberToObject(root, "ts", (double)(esp_timer_get_time() / 1000));
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to encode RS485 %s payload", context ? context : "payload");
        return ESP_ERR_NO_MEM;
    }

    size_t len = strlen(json_str);
    char *payload = static_cast<char *>(malloc(len + 2));
    if (!payload) {
        cJSON_free(json_str);
        return ESP_ERR_NO_MEM;
    }
    memcpy(payload, json_str, len);
    payload[len] = '\n';
    payload[len + 1] = '\0';

    const char *ctx = context ? context : "payload";
    bool log_info = true;
    if (context && strcmp(context, "heartbeat") == 0) {
        log_info = false;
    }
    if (log_info) {
        ESP_LOGI(TAG,
                 "Sending RS485 %s (seq=%lu need_ack=%s): %zu bytes",
                 ctx,
                 (unsigned long)seq,
                 need_ack ? "true" : "false",
                 len + 1);
    } else {
        ESP_LOGD(TAG, "Sending RS485 %s (seq=%lu)", ctx, (unsigned long)seq);
    }

    if (s_rs485_tx_mutex) {
        if (xSemaphoreTake(s_rs485_tx_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            free(payload);
            cJSON_free(json_str);
            return ESP_ERR_TIMEOUT;
        }
    }

    // Allow enough TX time for larger JSON frames (≈10 bits/byte @ 115200 baud).
    uint32_t tx_time_ms = ((len + 1) * 10 * 1000 + (WALTER_RS485_BAUDRATE - 1)) / WALTER_RS485_BAUDRATE;
    if (tx_time_ms < 200) {
        tx_time_ms = 200;  // minimum to cover small frames
    } else if (tx_time_ms > 1000) {
        tx_time_ms = 1000; // cap wait time to 1s
    }

    esp_err_t write_err = womo_rs485_write((const uint8_t *)payload, len + 1, pdMS_TO_TICKS(tx_time_ms));

    if (s_rs485_tx_mutex) {
        xSemaphoreGive(s_rs485_tx_mutex);
    }

    free(payload);
    cJSON_free(json_str);

    if (write_err != ESP_OK) {
        ESP_LOGW(TAG, "RS485 write (%s) failed: %s", ctx, esp_err_to_name(write_err));
        return write_err;
    }

    if (need_ack) {
        rs485_track_pending(seq, ctx);
    }

    return ESP_OK;
}

static void rs485_send_hello_packet(void)
{
    cJSON *hello = cJSON_CreateObject();
    if (!hello) {
        ESP_LOGE(TAG, "Failed to allocate RS485 hello packet");
        return;
    }

    int64_t now_us = esp_timer_get_time();
    double uptime_sec = (double)now_us / 1000000.0;
    cJSON_AddStringToObject(hello, "type", "hello");

    char fw_buf[64] = "Walter";
    const esp_app_desc_t *app = esp_app_get_description();
    if (app) {
        if (app->project_name[0] != '\0') {
            snprintf(fw_buf, sizeof(fw_buf), "%s %s", app->project_name, app->version);
        } else if (app->version[0] != '\0') {
            snprintf(fw_buf, sizeof(fw_buf), "%s", app->version);
        }
    }

    cJSON_AddStringToObject(hello, "fw", fw_buf);
    cJSON_AddNumberToObject(hello, "uptime", uptime_sec);
    cJSON_AddBoolToObject(hello, "display_ready", s_rs485_display_ready.load());
    cJSON_AddNumberToObject(hello, "ts", (double)(now_us / 1000));
    if (s_rs485_last_rx_seq != 0) {
        cJSON_AddNumberToObject(hello, "rx_seq", (double)s_rs485_last_rx_seq);
    }
    if (s_rs485_last_ack_seq != 0) {
        cJSON_AddNumberToObject(hello, "last_ack", (double)s_rs485_last_ack_seq);
    }

    esp_err_t send_err = rs485_send_frame("hello", hello, false);
    cJSON_Delete(hello);

    if (send_err != ESP_OK) {
        ESP_LOGW(TAG, "RS485 hello send failed: %s", esp_err_to_name(send_err));
        return;
    }

    uint32_t sent = ++s_rs485_hello_sent;
    if (sent == 1) {
        ESP_LOGI(TAG, "RS485 hello sent (fw=%s)", fw_buf);
    } else {
        ESP_LOGD(TAG,
                 "RS485 hello #%lu sent (ready=%s)",
                 (unsigned long)sent,
                 s_rs485_display_ready.load() ? "true" : "false");
    }
}

static void rs485_send_heartbeat_packet(void)
{
    cJSON *hb = cJSON_CreateObject();
    if (!hb) {
        ESP_LOGE(TAG, "Failed to allocate RS485 heartbeat packet");
        return;
    }

    int64_t now_us = esp_timer_get_time();
    cJSON_AddStringToObject(hb, "type", "hb");
    cJSON_AddNumberToObject(hb, "uptime", (double)now_us / 1000000.0);
    cJSON_AddNumberToObject(hb, "ts", (double)(now_us / 1000));
    if (s_rs485_last_rx_seq != 0) {
        cJSON_AddNumberToObject(hb, "rx_seq", (double)s_rs485_last_rx_seq);
    }
    if (s_rs485_last_ack_seq != 0) {
        cJSON_AddNumberToObject(hb, "last_ack", (double)s_rs485_last_ack_seq);
    }

    esp_err_t send_err = rs485_send_frame("heartbeat", hb, false);
    cJSON_Delete(hb);

    if (send_err == ESP_OK) {
        s_rs485_last_tx_heartbeat_us = now_us;
        uint32_t sent = ++s_rs485_heartbeat_sent;
        ESP_LOGD(TAG, "RS485 heartbeat #%lu sent", (unsigned long)sent);
    } else {
        ESP_LOGW(TAG, "Heartbeat send failed: %s", esp_err_to_name(send_err));
    }
}

static uint32_t rs485_next_seq(void)
{
    uint32_t seq = 0;
    portENTER_CRITICAL(&s_rs485_seq_lock);
    seq = s_rs485_tx_seq++;
    if (s_rs485_tx_seq == 0) {
        s_rs485_tx_seq = 1;
    }
    portEXIT_CRITICAL(&s_rs485_seq_lock);
    if (seq == 0) {
        return rs485_next_seq();
    }
    return seq;
}

static void rs485_mark_rx_seq(uint32_t seq)
{
    if (seq == 0) {
        return;
    }
    portENTER_CRITICAL(&s_rs485_seq_lock);
    s_rs485_last_rx_seq = seq;
    portEXIT_CRITICAL(&s_rs485_seq_lock);
}

static void rs485_track_pending(uint32_t seq, const char *label)
{
    if (seq == 0) {
        return;
    }
    rs485_pending_frame_t entry = {};
    entry.in_use = true;
    entry.warned = false;
    entry.seq = seq;
    entry.sent_us = esp_timer_get_time();
    if (label && label[0] != '\0') {
        strlcpy(entry.label, label, sizeof(entry.label));
    } else {
        strlcpy(entry.label, "frame", sizeof(entry.label));
    }

    bool stored = false;
    portENTER_CRITICAL(&s_rs485_pending_lock);
    for (int i = 0; i < WALTER_RS485_MAX_PENDING; ++i) {
        if (!s_rs485_pending[i].in_use) {
            s_rs485_pending[i] = entry;
            stored = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_rs485_pending_lock);

    if (!stored) {
        ESP_LOGW(TAG,
                 "Pending buffer full, dropping tracking for %s (seq=%lu)",
                 entry.label,
                 (unsigned long)seq);
    }
}

static void rs485_resolve_pending(uint32_t seq,
                                  bool success,
                                  const char *label_from_packet,
                                  const char *err_text)
{
    if (seq == 0) {
        return;
    }

    rs485_pending_frame_t resolved = {};
    bool found = false;

    portENTER_CRITICAL(&s_rs485_pending_lock);
    for (int i = 0; i < WALTER_RS485_MAX_PENDING; ++i) {
        if (s_rs485_pending[i].in_use && s_rs485_pending[i].seq == seq) {
            resolved = s_rs485_pending[i];
            s_rs485_pending[i].in_use = false;
            s_rs485_pending[i].warned = false;
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_rs485_pending_lock);

    const char *label = label_from_packet;
    if ((!label || label[0] == '\0') && resolved.label[0] != '\0') {
        label = resolved.label;
    }
    if (!label || label[0] == '\0') {
        label = "frame";
    }

    if (found) {
        ESP_LOGI(TAG,
                 "RS485 ACK for %s (seq=%lu) status=%s",
                 label,
                 (unsigned long)seq,
                 success ? "ok" : "err");
    } else {
        ESP_LOGW(TAG,
                 "Unexpected RS485 ACK for seq=%lu (label=%s)",
                 (unsigned long)seq,
                 label);
    }

    if (!success && err_text && err_text[0] != '\0') {
        ESP_LOGW(TAG, "ACK error detail: %s", err_text);
    }
}

static void rs485_check_pending_timeouts(void)
{
    const int64_t now_us = esp_timer_get_time();
    const int64_t timeout_us = (int64_t)WALTER_RS485_ACK_TIMEOUT_MS * 1000;

    struct warn_entry {
        bool valid;
        uint32_t seq;
        char label[sizeof(s_rs485_pending[0].label)];
        double age_ms;
    } warn_list[WALTER_RS485_MAX_PENDING] = {};

    portENTER_CRITICAL(&s_rs485_pending_lock);
    for (int i = 0; i < WALTER_RS485_MAX_PENDING; ++i) {
        if (!s_rs485_pending[i].in_use || s_rs485_pending[i].warned) {
            continue;
        }
        int64_t age_us = now_us - s_rs485_pending[i].sent_us;
        if (age_us >= timeout_us) {
            s_rs485_pending[i].warned = true;
            s_rs485_pending[i].in_use = false;  // give slot back immediately after timeout
            warn_list[i].valid = true;
            warn_list[i].seq = s_rs485_pending[i].seq;
            warn_list[i].age_ms = (double)age_us / 1000.0;
            strlcpy(warn_list[i].label, s_rs485_pending[i].label, sizeof(warn_list[i].label));
        }
    }
    portEXIT_CRITICAL(&s_rs485_pending_lock);

    for (int i = 0; i < WALTER_RS485_MAX_PENDING; ++i) {
        if (!warn_list[i].valid) {
            continue;
        }
        ESP_LOGW(TAG,
                 "Awaiting ACK for %s (seq=%lu) since %.0f ms",
                 warn_list[i].label,
                 (unsigned long)warn_list[i].seq,
                 warn_list[i].age_ms);
    }
}

static esp_err_t rs485_send_ack_packet(uint32_t rx_seq, bool success, const char *label, const char *err_text)
{
    if (rx_seq == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *ack = cJSON_CreateObject();
    if (!ack) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(ack, "type", "ack");
    cJSON_AddNumberToObject(ack, "ack", (double)rx_seq);
    cJSON_AddStringToObject(ack, "status", success ? "ok" : "err");
    cJSON_AddBoolToObject(ack, "need_ack", false);
    if (label && label[0] != '\0') {
        cJSON_AddStringToObject(ack, "cmd", label);
    }
    if (!success && err_text && err_text[0] != '\0') {
        cJSON_AddStringToObject(ack, "err", err_text);
    }

    esp_err_t err = rs485_send_frame("ack", ack, false);
    cJSON_Delete(ack);
    return err;
}

static void rs485_handle_ack_packet(const cJSON *root)
{
    if (!root) {
        return;
    }

    const cJSON *ack_val = cJSON_GetObjectItem(root, "ack");
    if (!cJSON_IsNumber(ack_val)) {
        ESP_LOGW(TAG, "ACK packet missing numeric ack field");
        return;
    }

    uint32_t ack_seq = (uint32_t)ack_val->valuedouble;
    const cJSON *status = cJSON_GetObjectItem(root, "status");
    const char *status_str = (cJSON_IsString(status) && status->valuestring) ? status->valuestring : "ok";
    bool success = (strcmp(status_str, "ok") == 0);
    const cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    const char *cmd_str = (cJSON_IsString(cmd) && cmd->valuestring) ? cmd->valuestring : nullptr;
    const cJSON *err = cJSON_GetObjectItem(root, "err");
    const char *err_str = (cJSON_IsString(err) && err->valuestring) ? err->valuestring : nullptr;

    rs485_resolve_pending(ack_seq, success, cmd_str, err_str);

    portENTER_CRITICAL(&s_rs485_seq_lock);
    s_rs485_last_ack_seq = ack_seq;
    s_rs485_last_ack_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_rs485_seq_lock);
}

static void rs485_handle_display_ready(void)
{
    bool was_ready = s_rs485_display_ready.exchange(true);
    uint32_t ack_count = ++s_rs485_display_ready_seen;
    int64_t now_us = esp_timer_get_time();
    int64_t previous = s_rs485_last_display_ready_us.exchange(now_us);
    double delta = (previous > 0) ? (double)(now_us - previous) / 1000000.0 : 0.0;

    if (!was_ready) {
        ESP_LOGI(TAG, "Display acknowledged handshake (ack #%lu)", (unsigned long)ack_count);
    } else {
        ESP_LOGI(TAG,
                 "Display re-acknowledged handshake (ack #%lu, dt=%.1fs)",
                 (unsigned long)ack_count,
                 delta);
    }
}

// RS485 RX Task - receives commands from AMOLED
static void rs485_rx_task(void *arg)
{
    ESP_LOGI(TAG, "RS485 RX task started");
    
    uint8_t buffer[512];
    char line_buffer[512];
    size_t line_pos = 0;
    int64_t last_dump_us = 0;
    
    while (true) {
        int len = womo_rs485_read(buffer, sizeof(buffer) - 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            s_rs485_last_rx_us.store(esp_timer_get_time());
            buffer[len] = '\0';
            
            // Process byte by byte to find complete JSON lines
            for (int i = 0; i < len; i++) {
                char c = (char)buffer[i];
                
                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        line_buffer[line_pos] = '\0';
                        s_rs485_last_rx_line_us.store(esp_timer_get_time());
                        rs485_process_rx_line(line_buffer);
                        line_pos = 0;
                    }
                } else if ((unsigned char)c >= 0x20 && (unsigned char)c <= 0x7E) {
                    if (line_pos < sizeof(line_buffer) - 1) {
                        line_buffer[line_pos++] = c;
                    }
                } else {
                    // Non-ASCII: often idle noise (0x00/0xFF). Ignore 0x00 quietly; log others throttled.
                    if ((unsigned char)c != 0x00) {
                        int64_t now_us = esp_timer_get_time();
                        if ((now_us - last_dump_us) > 500000) { // max 2 dumps/s
                            rs485_log_hex_bytes("RS485 raw", buffer, (size_t)len);
                            last_dump_us = now_us;
                        }
                        ESP_LOGW(TAG, "RS485 byte dropped (0x%02X) - resetting line", (unsigned char)c);
                    }
                    line_pos = 0;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static bool rs485_line_is_ascii(const char *line)
{
    if (!line) {
        return false;
    }
    const unsigned char *p = reinterpret_cast<const unsigned char *>(line);
    while (*p) {
        if (*p < 0x20 || *p > 0x7E) {
            ESP_LOGW(TAG, "RS485 line dropped (non-ASCII byte 0x%02X)", (unsigned)*p);
            return false;
        }
        ++p;
    }
    return true;
}

static void rs485_log_hexdump(const char *label, const char *line)
{
    if (!line) {
        return;
    }
    char buf[200];
    size_t len = strnlen(line, sizeof(buf));
    size_t n = 0;
    for (size_t i = 0; i < len && n + 3 < sizeof(buf); ++i) {
        n += snprintf(buf + n, sizeof(buf) - n, "%02X", (unsigned char)line[i]);
    }
    buf[sizeof(buf) - 1] = '\0';
    ESP_LOGW(TAG, "%s (hex %zu bytes): %s", label ? label : "RS485 dump", len, buf);
}

static void rs485_log_hex_bytes(const char *label, const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return;
    }
    char buf[200];
    size_t n = 0;
    for (size_t i = 0; i < len && n + 3 < sizeof(buf); ++i) {
        n += snprintf(buf + n, sizeof(buf) - n, "%02X", (unsigned char)data[i]);
    }
    buf[sizeof(buf) - 1] = '\0';
    ESP_LOGW(TAG, "%s (%zu bytes): %s", label ? label : "RS485 bytes", len, buf);
}

// Handle packets received from display
static void rs485_process_rx_line(const char *line)
{
    if (!line) {
        return;
    }

    while (*line && static_cast<unsigned char>(*line) < 0x20) {
        ++line;
    }
    if (*line == '\0') {
        return;
    }

    if (line[0] && line[1] && line[2] &&
        static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB &&
        static_cast<unsigned char>(line[2]) == 0xBF) {
        line += 3;
        while (*line && static_cast<unsigned char>(*line) < 0x20) {
            ++line;
        }
        if (*line == '\0') {
            return;
        }
    }

    if (!rs485_line_is_ascii(line)) {
        return;
    }

    while (*line && *line != '{' && *line != '[') {
        ++line;
    }
    if (*line == '\0') {
        ESP_LOGW(TAG, "RS485 line without JSON start ignored");
        rs485_log_hexdump("RS485 no JSON start", line);
        return;
    }

    // Trim trailing noise after the last closing brace/bracket
    const char *end_brace = strrchr(line, '}');
    const char *end_bracket = strrchr(line, ']');
    const char *end = end_brace;
    if (end_bracket && (!end || end_bracket > end)) {
        end = end_bracket;
    }
    if (!end) {
        ESP_LOGW(TAG, "RS485 line without JSON end ignored");
        rs485_log_hexdump("RS485 no JSON end", line);
        return;
    }

    size_t json_len = (size_t)(end - line + 1);
    char clean_line[512];
    if (json_len >= sizeof(clean_line)) {
        ESP_LOGW(TAG, "RS485 JSON too long (%zu)", json_len);
        return;
    }
    memcpy(clean_line, line, json_len);
    clean_line[json_len] = '\0';
    line = clean_line;

    ESP_LOGI(TAG, "RS485 RX: %s", line);

    cJSON *root = cJSON_Parse(line);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse JSON packet");
        rs485_log_hexdump("RS485 JSON parse fail", line);
        return;
    }

    const cJSON *seq_obj = cJSON_GetObjectItem(root, "seq");
    uint32_t rx_seq = 0;
    if (cJSON_IsNumber(seq_obj)) {
        rx_seq = (uint32_t)seq_obj->valuedouble;
        rs485_mark_rx_seq(rx_seq);
    }

    bool need_ack = false;
    const cJSON *need_ack_obj = cJSON_GetObjectItem(root, "need_ack");
    if (cJSON_IsBool(need_ack_obj)) {
        need_ack = cJSON_IsTrue(need_ack_obj);
    } else if (rx_seq != 0) {
        need_ack = true;
    }

    const cJSON *type_obj = cJSON_GetObjectItem(root, "type");
    const char *type = (cJSON_IsString(type_obj) && type_obj->valuestring)
                           ? type_obj->valuestring
                           : nullptr;
    const cJSON *cmd_obj = cJSON_GetObjectItem(root, "cmd");
    const char *cmd_str = (cJSON_IsString(cmd_obj) && cmd_obj->valuestring)
                              ? cmd_obj->valuestring
                              : nullptr;

    const char *ack_label = nullptr;
    if (type && type[0] != '\0') {
        ack_label = type;
    } else if (cmd_str && cmd_str[0] != '\0') {
        ack_label = cmd_str;
    } else {
        ack_label = "frame";
    }

    bool ack_success = false;
    char ack_error[96] = "";

    if (type && (strcmp(type, "ack") == 0 || strcmp(type, "cmd_ack") == 0)) {
        rs485_handle_ack_packet(root);
        need_ack = false;
        ack_success = true;
    } else if (type && strcmp(type, "hb") == 0) {
        s_rs485_last_rx_heartbeat_us = esp_timer_get_time();
        ack_success = true;
        need_ack = false;
    } else if (cmd_str) {
        esp_err_t cmd_err = ESP_OK;
        bool handled = rs485_execute_command(root, cmd_str, &cmd_err);
        if (handled) {
            rs485_report_command_result(cmd_str, cmd_err);
            ack_success = (cmd_err == ESP_OK);
            if (!ack_success) {
                strlcpy(ack_error, esp_err_to_name(cmd_err), sizeof(ack_error));
            }
        } else {
            ESP_LOGW(TAG, "Unknown command: %s", cmd_str);
            strlcpy(ack_error, "unknown command", sizeof(ack_error));
            ack_success = false;
        }
    } else {
        ESP_LOGW(TAG, "RS485 packet missing 'cmd' field");
        strlcpy(ack_error, "missing cmd", sizeof(ack_error));
        ack_success = false;
    }

    if (need_ack && rx_seq != 0) {
        esp_err_t ack_err = rs485_send_ack_packet(rx_seq, ack_success, ack_label, ack_success ? nullptr : ack_error);
        if (ack_err != ESP_OK) {
            ESP_LOGW(TAG,
                     "Failed to send ACK for seq=%lu (%s): %s",
                     (unsigned long)rx_seq,
                     ack_label,
                     esp_err_to_name(ack_err));
        }
    }

    cJSON_Delete(root);
}

static bool rs485_execute_command(const cJSON *root, const char *cmd_str, esp_err_t *out_err)
{
    if (!cmd_str) {
        if (out_err) {
            *out_err = ESP_ERR_INVALID_ARG;
        }
        return false;
    }

    esp_err_t cmd_err = ESP_OK;
    bool handled = true;

    if (strcmp(cmd_str, "display_ready") == 0) {
        rs485_handle_display_ready();
    } else if (strcmp(cmd_str, "lte_enable") == 0) {
        cmd_err = lte_runtime_set_enabled(true);
    } else if (strcmp(cmd_str, "lte_disable") == 0) {
        cmd_err = lte_runtime_set_enabled(false);
    } else if (strcmp(cmd_str, "wifi_enable_ap") == 0) {
        cmd_err = command_set_wifi_ap_enabled(true);
    } else if (strcmp(cmd_str, "wifi_disable_ap") == 0) {
        cmd_err = command_set_wifi_ap_enabled(false);
    } else if (strcmp(cmd_str, "wifi_enable_sta") == 0) {
        cmd_err = command_set_wifi_sta_enabled(true);
    } else if (strcmp(cmd_str, "wifi_disable_sta") == 0) {
        cmd_err = command_set_wifi_sta_enabled(false);
    } else if (strcmp(cmd_str, "wifi_scan_start") == 0) {
        cmd_err = command_start_wifi_scan();
    } else if (strcmp(cmd_str, "wifi_set_credentials") == 0) {
        const cJSON *ssid = root ? cJSON_GetObjectItem(root, "ssid") : nullptr;
        const cJSON *password = root ? cJSON_GetObjectItem(root, "password") : nullptr;
        if (!ssid || !password || !cJSON_IsString(ssid) || !cJSON_IsString(password)) {
            cmd_err = ESP_ERR_INVALID_ARG;
        } else {
            cmd_err = command_set_wifi_credentials(ssid->valuestring, password->valuestring);
        }
#if WALTER_ENABLE_HX711
    } else if (strcmp(cmd_str, "gas_bottle_replace") == 0) {
        cmd_err = command_gas_bottle_replace(root);
    } else if (strcmp(cmd_str, "tare_a") == 0) {
        ESP_LOGI(TAG, "Tare Platform A requested");
        cmd_err = ESP_ERR_NOT_SUPPORTED;
    } else if (strcmp(cmd_str, "tare_b") == 0) {
        ESP_LOGI(TAG, "Tare Platform B requested");
        cmd_err = ESP_ERR_NOT_SUPPORTED;
#endif
    } else {
        handled = false;
    }

    if (out_err) {
        *out_err = handled ? cmd_err : ESP_ERR_INVALID_ARG;
    }
    return handled;
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
}
#endif // WALTER_ENABLE_RS485

#if WALTER_ENABLE_RS485
#if WALTER_ENABLE_HX711
static int gas_display_slot_to_hx_idx(int display_slot)
{
    if (display_slot == 0) {
        return (WALTER_GAS_SWAP_AB != 0) ? HX_IDX_PLATFORM_B : HX_IDX_PLATFORM_A;
    }
    if (display_slot == 1) {
        return (WALTER_GAS_SWAP_AB != 0) ? HX_IDX_PLATFORM_A : HX_IDX_PLATFORM_B;
    }
    return -1;
}

static esp_err_t command_gas_bottle_replace(const cJSON *root)
{
    const cJSON *slot = root ? cJSON_GetObjectItem(root, "slot") : nullptr;
    if (!slot && root) {
        slot = cJSON_GetObjectItem(root, "channel");
    }

    int display_slot = -1;
    if (cJSON_IsNumber(slot)) {
        int v = static_cast<int>(slot->valuedouble);
        if (v == 0 || v == 1) {
            display_slot = v;
        }
    } else if (cJSON_IsString(slot) && slot->valuestring) {
        const char *s = slot->valuestring;
        if (s[0] == 'f' || s[0] == 'F' || s[0] == 'a' || s[0] == 'A') {
            display_slot = 0;
        } else if (s[0] == 'b' || s[0] == 'B' || s[0] == 'r' || s[0] == 'R') {
            display_slot = 1;
        }
    }

    if (display_slot < 0) {
        ESP_LOGW(TAG, "Gas bottle replace: missing/invalid slot");
        return ESP_ERR_INVALID_ARG;
    }

    int hx_idx = gas_display_slot_to_hx_idx(display_slot);
    if (hx_idx < 0) {
        ESP_LOGW(TAG, "Gas bottle replace: slot %d not mapped", display_slot);
        return ESP_ERR_INVALID_ARG;
    }

    sensor_shared_state_t snapshot = sensor_state_snapshot();
    bool valid = (hx_idx == HX_IDX_PLATFORM_A) ? snapshot.hx711.valid_a : snapshot.hx711.valid_b;
    float kg_raw = (hx_idx == HX_IDX_PLATFORM_A) ? snapshot.hx711.kg_a : snapshot.hx711.kg_b;
    if (!valid || !isfinite(kg_raw)) {
        ESP_LOGW(TAG, "Gas bottle replace: HX%c value invalid", (hx_idx == HX_IDX_PLATFORM_A) ? 'A' : 'B');
        return ESP_ERR_INVALID_STATE;
    }

    float new_tara = kg_raw - WALTER_GAS_FILL_KG;
    float max_tara = (WALTER_GAS_TARA_KG_MAX > 0.0f) ? WALTER_GAS_TARA_KG_MAX : WALTER_GAS_TARA_KG;
    if (new_tara < 0.0f) {
        new_tara = 0.0f;
    }
    if (new_tara > max_tara) {
        new_tara = max_tara;
    }

    float old_tara = s_gas_tara_kg[hx_idx];
    s_gas_tara_kg[hx_idx] = new_tara;
    esp_err_t save_err = gas_tara_save_to_nvs();
    if (save_err != ESP_OK) {
        ESP_LOGW(TAG, "Gas tara NVS save failed: %s", esp_err_to_name(save_err));
    }

    int64_t ts_us = snapshot.hx711.timestamp_us != 0 ? snapshot.hx711.timestamp_us : esp_timer_get_time();
    gas_history_push(snapshot.hx711.valid_a, snapshot.hx711.kg_a,
                     snapshot.hx711.valid_b, snapshot.hx711.kg_b,
                     ts_us);
    gas_compute_state(snapshot.hx711.valid_a, snapshot.hx711.kg_a,
                      snapshot.hx711.valid_b, snapshot.hx711.kg_b,
                      ts_us);

    const char *slot_label = (display_slot == 0) ? "front" : "back";
    ESP_LOGI(TAG,
             "Gas bottle replace (%s -> HX%c): tara %.2f -> %.2f kg (raw=%.2f kg, fill=%.1f kg)%s",
             slot_label,
             (hx_idx == HX_IDX_PLATFORM_A) ? 'A' : 'B',
             old_tara,
             new_tara,
             kg_raw,
             WALTER_GAS_FILL_KG,
             (save_err == ESP_OK) ? "" : " [NVS save failed]");

    return save_err;
}
#endif
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
#endif // WALTER_ENABLE_RS485

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
    int last_log = 0;
    while (!lte_is_registered()) {
        if (!s_lte_target_enabled.load()) {
            ESP_LOGW(LTE_TAG, "Registration aborted (LTE disabled)");
            sensor_state_publish_lte_registration(false);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        ++elapsed;
        if (elapsed - last_log >= 5) {
            WalterModemNetworkRegState reg = modem.getNetworkRegState();
            ESP_LOGI(LTE_TAG, "Registration pending... state=%d elapsed=%ds", (int)reg, elapsed);
            last_log = elapsed;
        }
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
    
    // Try to reset RF state (CFUN=4) multiple times as it might fail if modem is busy
    bool rf_reset_ok = false;
    for (int i = 0; i < 3; i++) {
        if (modem.setOpState(WALTER_MODEM_OPSTATE_NO_RF)) {
            rf_reset_ok = true;
            break;
        }
        ESP_LOGW(LTE_TAG, "setOpState(NO_RF) failed (attempt %d/3), retrying...", i + 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (!rf_reset_ok) {
        ESP_LOGE(LTE_TAG, "setOpState(NO_RF) failed after retries");
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

    // Warte darauf, dass das Modem den Suchzustand verlässt, damit GNSS exklusiv nutzen kann
    const TickType_t timeout = ms_to_ticks(15000);
    TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start < timeout) {
        WalterModemNetworkRegState reg = modem.getNetworkRegState();
        if (reg == WALTER_MODEM_NETWORK_REG_NOT_SEARCHING) {
            break;
        }
        vTaskDelay(ms_to_ticks(100));
    }

    s_lte_socket_id = 0;
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

    const int sock_id = static_cast<int>(s_lte_socket_id);
    if (!modem.socketSend(payload, sizeof(payload), nullptr, nullptr, nullptr, WALTER_MODEM_RAI_NO_INFO, sock_id)) {
        WalterModemSocketState st = WalterModem::socketGetState(sock_id);
        WalterModemNetworkRegState reg = modem.getNetworkRegState();
        ESP_LOGE(LTE_TAG, "socketSend failed (sock=%d state=%d reg=%d) -> forcing reconnect", sock_id, static_cast<int>(st), (int)reg);
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

#if WALTER_ENABLE_GPS
    if (s_modem_ready_sem) {
        xSemaphoreGive(s_modem_ready_sem);
    }
#endif

    const TickType_t interval_ticks = ms_to_ticks(WALTER_LTE_SEND_INTERVAL_MS);
    bool link_ready = false;
    bool runtime_enabled = s_lte_target_enabled.load();
    bool quiet = s_lte_quiet_active.load();
    bool quiet_applied = false;
    lte_runtime_notify(runtime_enabled, link_ready, ESP_OK);

    while (true) {
        // Ziehe den Quiet-Status zu Beginn, damit unmittelbar nach einem Quiet-On keine Heartbeats/SQNMONI mehr laufen.
        quiet = s_lte_quiet_active.load();
        QueueHandle_t queue = s_lte_command_queue;
        lte_runtime_cmd_t cmd;

        if (!runtime_enabled) {
            quiet = false;
            s_lte_quiet_active.store(false);
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
                } else if (cmd == LTE_CMD_QUIET_OFF) {
                    // no-op, quiet already cleared above
                } else if (cmd == LTE_CMD_QUIET_ON) {
                    quiet = true;
                    s_lte_quiet_active.store(true);
                }
            }
            continue;
        }

        if (queue && xQueueReceive(queue, &cmd, 0) == pdPASS) {
            if (cmd == LTE_CMD_DISABLE) {
                runtime_enabled = false;
                s_lte_target_enabled.store(false);
                quiet = false;
                s_lte_quiet_active.store(false);
                if (link_ready) {
                    lte_close_socket();
                }
                lte_set_low_power();
                link_ready = false;
                lte_runtime_notify(runtime_enabled, link_ready, ESP_OK);
                continue;
            } else if (cmd == LTE_CMD_RESTART) {
                if (link_ready) {
                    lte_close_socket();
                    lte_set_low_power();
                    link_ready = false;
                    lte_runtime_notify(runtime_enabled, link_ready, ESP_OK);
                }
            } else if (cmd == LTE_CMD_QUIET_ON) {
                quiet = true;
                s_lte_quiet_active.store(true);
            } else if (cmd == LTE_CMD_QUIET_OFF) {
                quiet = false;
                s_lte_quiet_active.store(false);
            }
        }

        if (quiet) {
            if (!quiet_applied) {
                if (link_ready) {
                    lte_close_socket();
                    link_ready = false;
                }
                if (!modem.setOpState(WALTER_MODEM_OPSTATE_NO_RF)) {
                    ESP_LOGW(LTE_TAG, "setOpState(NO_RF) during quiet failed");
                }
                s_lte_quiet_active.store(true);
                quiet_applied = true;
            }

            if (queue && xQueueReceive(queue, &cmd, ms_to_ticks(200)) == pdPASS) {
                if (cmd == LTE_CMD_DISABLE) {
                    runtime_enabled = false;
                    s_lte_target_enabled.store(false);
                    quiet = false;
                    s_lte_quiet_active.store(false);
                    quiet_applied = false;
                    if (link_ready) {
                        lte_close_socket();
                    }
                    lte_set_low_power();
                    link_ready = false;
                    lte_runtime_notify(runtime_enabled, link_ready, ESP_OK);
                    continue;
                } else if (cmd == LTE_CMD_RESTART) {
                    if (link_ready) {
                        lte_close_socket();
                        lte_set_low_power();
                        link_ready = false;
                        lte_runtime_notify(runtime_enabled, link_ready, ESP_OK);
                    }
                } else if (cmd == LTE_CMD_QUIET_OFF) {
                    quiet = false;
                    s_lte_quiet_active.store(false);
                    quiet_applied = false;
                }
            } else {
                vTaskDelay(ms_to_ticks(200));
            }
            continue;
        }

        // Reset quiet sentinel when we leave quiet state
        if (!quiet) {
            quiet_applied = false;
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
            s_lte_socket_id = 0;
            lte_runtime_notify(runtime_enabled, link_ready, ESP_FAIL);
            // Aggressiver Re-Dial: sofort versuchen, Registrierung + Socket neu aufzubauen
            if (lte_attach_network() && lte_open_socket()) {
                link_ready = true;
                lte_runtime_notify(runtime_enabled, link_ready, ESP_OK);
                continue;
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
    if (WALTER_HX711_STARTUP_DELAY_MS > 0) {
        ESP_LOGI(TAG,
                 "Waiting %u ms before HX711 init",
                 (unsigned)WALTER_HX711_STARTUP_DELAY_MS);
        vTaskDelay(ms_to_ticks(WALTER_HX711_STARTUP_DELAY_MS));
    }

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

#if WALTER_ENABLE_GPS
    {
        BaseType_t task_created = xTaskCreate(
            gps_task,
            "gps",
            WALTER_GPS_TASK_STACK,
            nullptr,
            WALTER_GPS_TASK_PRIORITY,
            nullptr);
        if (task_created == pdPASS) {
            any_sensor = true;
        } else {
            ESP_LOGE(TAG, "Failed to create GPS task");
            first_error = (first_error == ESP_OK) ? ESP_ERR_NO_MEM : first_error;
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
    gas_tara_restore_from_nvs();
    gas_history_restore_from_nvs();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    enable_switched_3v3();

#if WALTER_ENABLE_GPS && WALTER_ENABLE_LTE
    if (!s_modem_ready_sem) {
        s_modem_ready_sem = xSemaphoreCreateBinary();
        if (!s_modem_ready_sem) {
            ESP_LOGE(TAG, "Failed to create modem-ready semaphore");
        }
    }
#endif

    ESP_ERROR_CHECK(wifi_init_apsta());

#if WALTER_ENABLE_WEBUI
    esp_err_t web_err = womo_web_start();
    if (web_err != ESP_OK) {
        ESP_LOGW(TAG, "Web UI start failed: %s", esp_err_to_name(web_err));
    }
#endif

    esp_err_t sensor_err = sensor_subsystem_init();
    if (sensor_err != ESP_OK) {
        ESP_LOGW(TAG, "Sensor subsystem init reported: %s", esp_err_to_name(sensor_err));
    }

#if WALTER_ENABLE_RS485
    esp_err_t rs485_err = womo_rs485_init();
    ESP_LOGI(TAG, "womo_rs485_init() returned: %s (0x%x)", esp_err_to_name(rs485_err), rs485_err);
    if (rs485_err == ESP_OK) {
        ESP_LOGI(TAG, "RS485 initialized, starting communication tasks");
        if (!s_rs485_tx_mutex) {
            s_rs485_tx_mutex = xSemaphoreCreateMutex();
            if (!s_rs485_tx_mutex) {
                ESP_LOGE(TAG, "Failed to create RS485 TX mutex");
            }
        }
        
        // Start RS485 TX task (sends sensor data)
        BaseType_t tx_created = xTaskCreate(
            rs485_tx_task,
            "rs485_tx",
            5120,
            nullptr,
            5,
            nullptr);
        if (tx_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create RS485 TX task");
        }

        // Start RS485 RX task (receives commands)
        BaseType_t rx_created = xTaskCreate(
            rs485_rx_task,
            "rs485_rx",
            4096,
            nullptr,
            5,
            nullptr);
        if (rx_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create RS485 RX task");
        }
    }

    s_lte_command_queue = xQueueCreate(8, sizeof(lte_runtime_cmd_t));
    if (!s_lte_command_queue) {
        ESP_LOGE(TAG, "Failed to create LTE command queue");
    } else {
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
