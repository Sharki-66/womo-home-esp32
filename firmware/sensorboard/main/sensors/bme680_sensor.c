#include "sensors/bme680_sensor.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "bme680.h"
#include "hal/sensor_i2c_bus.h"
#include "bsec_interface.h"
#include "bsec_datatypes.h"

#define BME680_LOG_INTERVAL_MS 5000
#define BME680_USE_BSEC 1
#define BSEC_STATE_SAVE_INTERVAL_US (30 * 60 * 1000000LL)  // 30 Minuten
#define BSEC_NVS_NAMESPACE "bsec"
#define BSEC_NVS_KEY_INDOOR "state_in"

// Luftdruck-Trend
#define PRESS_HISTORY_SIZE 96            // 24h @ 15min Intervall
#define PRESS_SAMPLE_INTERVAL_US (15 * 60 * 1000000LL)  // 15 Minuten
#define PRESS_TREND_WINDOW_1H_SAMPLES 4  // 1h = 4× 15min
#define PRESS_TREND_WINDOW_3H_SAMPLES 12 // 3h = 12× 15min
#define PRESS_NVS_NAMESPACE "bme680"
#define PRESS_NVS_KEY "press_hist"

// Trend-Schwellwerte (hPa pro 3 Stunden)
#define PRESS_TREND_FALLING_FAST_THRESHOLD -2.5f
#define PRESS_TREND_FALLING_THRESHOLD -0.5f
#define PRESS_TREND_STEADY_THRESHOLD 0.5f
#define PRESS_TREND_RISING_THRESHOLD 2.5f

// Plausibilitätsgrenzen (aus Bosch BME680 Datasheet)
#define BME680_TEMP_MIN_C -40.0f
#define BME680_TEMP_MAX_C 85.0f
#define BME680_HUM_MIN_PCT 0.0f
#define BME680_HUM_MAX_PCT 100.0f
#define BME680_PRESS_MIN_HPA 300.0f
#define BME680_PRESS_MAX_HPA 1100.0f

static const char *TAG = "bme680_app";

typedef struct {
    float temperature_c;
    float humidity_pct;
    float pressure_hpa;
    float gas_res_kohm;
    bool gas_valid;
    bool heater_stable;
    int64_t timestamp_us;
    bool have_value;
} bme680_plausibility_t;

static bme680_handle_t s_bme_lo = NULL;
static bme680_handle_t s_bme_hi = NULL;
static TaskHandle_t s_task = NULL;
static bool s_bsec_ready = false;
static uint8_t s_bsec_work_buffer[BSEC_MAX_WORKBUFFER_SIZE] = {0};
static int64_t s_last_bsec_state_save_us = 0;
static bme680_plausibility_t s_plausibility_indoor = {0};
static bme680_plausibility_t s_plausibility_outdoor = {0};

typedef struct {
    const char *label;
    bme680_handle_t handle;
    uint8_t state[BSEC_MAX_STATE_BLOB_SIZE];
    uint32_t state_len;
    bme680_plausibility_t *plausibility;
} bsec_ctx_t;

static bsec_ctx_t s_bsec_lo = { 
    .label = "BME680 Innen (0x76)", 
    .handle = NULL, 
    .state_len = 0,
    .plausibility = &s_plausibility_indoor
};
static bsec_ctx_t s_bsec_hi = { 
    .label = "BME680 Außen (0x77)", 
    .handle = NULL, 
    .state_len = 0,
    .plausibility = &s_plausibility_outdoor
};

typedef struct {
    bool valid;
    float iaq;
    int iaq_accuracy;
    float eco2_ppm;
    float bvoc_ppm;
    float run_in;
    float stab;
    int64_t timestamp_us;
} bsec_output_snapshot_t;

static bsec_output_snapshot_t s_bsec_out_indoor = {0};

// Luftdruck-Historie für Trend-Berechnung (nur outdoor)
typedef struct {
    float pressure_hpa;
    int64_t timestamp_us;
} press_history_entry_t;

typedef struct {
    press_history_entry_t samples[PRESS_HISTORY_SIZE];
    uint8_t write_index;
    uint8_t count;
    int64_t last_sample_us;
} press_history_t;

static press_history_t s_press_history = {0};
static bool s_press_history_loaded = false;

// ── Luftdruck-Trend Funktionen ──────────────────────────────────────────

static void press_history_load_from_nvs(void)
{
    if (s_press_history_loaded) {
        return;
    }
    s_press_history_loaded = true;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(PRESS_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Druck-Historie NVS nicht gefunden, starte mit leerer Historie");
        return;
    }

    size_t size = sizeof(s_press_history);
    err = nvs_get_blob(handle, PRESS_NVS_KEY, &s_press_history, &size);
    nvs_close(handle);

    if (err == ESP_OK && size == sizeof(s_press_history)) {
        ESP_LOGI(TAG, "Druck-Historie aus NVS geladen (%u Werte)", s_press_history.count);
        // last_sample_us ist ein Uptime-Timestamp des vorherigen Boots.
        // Nach Reboot startet esp_timer bei 0 → alten Wert löschen,
        // damit kein blockierter Sample-Skip entsteht.
        s_press_history.last_sample_us = 0;
    } else {
        ESP_LOGW(TAG, "Druck-Historie NVS-Fehler: %s", esp_err_to_name(err));
        memset(&s_press_history, 0, sizeof(s_press_history));
    }
}

static void press_history_save_to_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PRESS_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Druck-Historie NVS open failed: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(handle, PRESS_NVS_KEY, &s_press_history, sizeof(s_press_history));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Druck-Historie NVS save failed: %s", esp_err_to_name(err));
    }
}

static void press_history_add_sample(float pressure_hpa, int64_t timestamp_us)
{
    if (!s_press_history_loaded) {
        press_history_load_from_nvs();
    }

    // Zeitprüfung: nur alle 15 min samplen
    if (s_press_history.last_sample_us > 0) {
        int64_t delta_us = timestamp_us - s_press_history.last_sample_us;
        if (delta_us < PRESS_SAMPLE_INTERVAL_US) {
            return;  // Zu früh
        }
    }

    s_press_history.samples[s_press_history.write_index].pressure_hpa = pressure_hpa;
    s_press_history.samples[s_press_history.write_index].timestamp_us = timestamp_us;
    s_press_history.write_index = (s_press_history.write_index + 1) % PRESS_HISTORY_SIZE;
    if (s_press_history.count < PRESS_HISTORY_SIZE) {
        s_press_history.count++;
    }
    s_press_history.last_sample_us = timestamp_us;

    // Alle 10 Samples (2,5h) speichern
    if (s_press_history.count % 10 == 0) {
        press_history_save_to_nvs();
    }
}

// window_samples: Anzahl 15-min-Schritte zurück (4 = 1h, 12 = 3h)
// threshold_scale: Faktor für Schwellwerte (1.0 für 3h-Deltas, 1/3 für 1h-Deltas)
static bool press_history_calculate_trend_window(uint8_t window_samples, float threshold_scale,
                                                  float *trend_hpa_h, bme680_pressure_trend_t *state)
{
    if (!s_press_history_loaded) {
        press_history_load_from_nvs();
    }

    if (s_press_history.count < window_samples) {
        return false;
    }

    uint8_t newest_idx = (s_press_history.write_index + PRESS_HISTORY_SIZE - 1) % PRESS_HISTORY_SIZE;
    float press_now = s_press_history.samples[newest_idx].pressure_hpa;
    int64_t time_now = s_press_history.samples[newest_idx].timestamp_us;

    uint8_t old_idx = (newest_idx + PRESS_HISTORY_SIZE - window_samples) % PRESS_HISTORY_SIZE;
    float press_old = s_press_history.samples[old_idx].pressure_hpa;
    int64_t time_old = s_press_history.samples[old_idx].timestamp_us;

    float delta_hpa = press_now - press_old;
    float delta_h = (float)(time_now - time_old) / 3600000000.0f;
    if (delta_h < 0.1f) {
        return false;
    }
    *trend_hpa_h = delta_hpa / delta_h;

    // Schwellwerte skaliert auf das gewählte Zeitfenster
    float d = delta_hpa;
    float ff = PRESS_TREND_FALLING_FAST_THRESHOLD * threshold_scale;
    float f  = PRESS_TREND_FALLING_THRESHOLD      * threshold_scale;
    float s  = PRESS_TREND_STEADY_THRESHOLD       * threshold_scale;
    float r  = PRESS_TREND_RISING_THRESHOLD       * threshold_scale;
    if      (d < ff) *state = BME680_TREND_FALLING_FAST;
    else if (d < f)  *state = BME680_TREND_FALLING;
    else if (d <= s) *state = BME680_TREND_STEADY;
    else if (d <= r) *state = BME680_TREND_RISING;
    else             *state = BME680_TREND_RISING_FAST;

    return true;
}

// ────────────────────────────────────────────────────────────────────────

static bool bme680_check_plausibility(float temp_c, float hum_pct, float press_hpa, const char *label)
{
    bool ok = true;
    if (temp_c < BME680_TEMP_MIN_C || temp_c > BME680_TEMP_MAX_C) {
        ESP_LOGW(TAG, "%s: Temperatur außerhalb Grenzen: %.1f°C", label, temp_c);
        ok = false;
    }
    if (hum_pct < BME680_HUM_MIN_PCT || hum_pct > BME680_HUM_MAX_PCT) {
        ESP_LOGW(TAG, "%s: Luftfeuchte außerhalb Grenzen: %.1f%%", label, hum_pct);
        ok = false;
    }
    if (press_hpa < BME680_PRESS_MIN_HPA || press_hpa > BME680_PRESS_MAX_HPA) {
        ESP_LOGW(TAG, "%s: Druck außerhalb Grenzen: %.1f hPa", label, press_hpa);
        ok = false;
    }
    return ok;
}

static bool bme680_apply_plausibility(bsec_ctx_t *ctx, const bme680_data_t *data, int64_t now_us)
{
    if (!ctx || !ctx->plausibility || !data) {
        return false;
    }

    bool plausible = bme680_check_plausibility(data->air_temperature, 
                                                 data->relative_humidity,
                                                 data->barometric_pressure / 100.0f,
                                                 ctx->label);
    
    if (plausible) {
        ctx->plausibility->temperature_c = data->air_temperature;
        ctx->plausibility->humidity_pct = data->relative_humidity;
        ctx->plausibility->pressure_hpa = data->barometric_pressure / 100.0f;
        ctx->plausibility->gas_res_kohm = data->gas_resistance / 1000.0f;
        ctx->plausibility->gas_valid = data->gas_valid;
        ctx->plausibility->heater_stable = data->heater_stable;
        ctx->plausibility->timestamp_us = now_us;
        ctx->plausibility->have_value = true;
    }
    
    return plausible;
}

static bool bsec_load_state(bsec_ctx_t *ctx)
{
    if (!ctx) {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(BSEC_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t blob_size = BSEC_MAX_STATE_BLOB_SIZE;
    err = nvs_get_blob(handle, BSEC_NVS_KEY_INDOOR, ctx->state, &blob_size);
    nvs_close(handle);

    if (err == ESP_OK && blob_size > 0 && blob_size <= BSEC_MAX_STATE_BLOB_SIZE) {
        ctx->state_len = blob_size;
        bsec_library_return_t bsec_status = bsec_set_state(ctx->state, ctx->state_len,
                                                            s_bsec_work_buffer,
                                                            sizeof(s_bsec_work_buffer));
        if (bsec_status == BSEC_OK) {
            ESP_LOGI(TAG, "%s: BSEC state restored from NVS (%u bytes)", ctx->label, ctx->state_len);
            return true;
        } else {
            ESP_LOGW(TAG, "%s: BSEC set_state failed: %d", ctx->label, bsec_status);
        }
    }
    return false;
}

static void bsec_save_state(bsec_ctx_t *ctx)
{
    if (!ctx || !s_bsec_ready) {
        return;
    }

    uint32_t state_len = BSEC_MAX_STATE_BLOB_SIZE;
    bsec_library_return_t bsec_status = bsec_get_state(0, ctx->state, BSEC_MAX_STATE_BLOB_SIZE,
                                                        s_bsec_work_buffer,
                                                        sizeof(s_bsec_work_buffer),
                                                        &state_len);
    if (bsec_status != BSEC_OK || state_len == 0) {
        return;
    }

    ctx->state_len = state_len;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(BSEC_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: BSEC NVS open for write failed: %s", ctx->label, esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(handle, BSEC_NVS_KEY_INDOOR, ctx->state, ctx->state_len);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%s: BSEC state saved to NVS (%u bytes)", ctx->label, ctx->state_len);
    } else {
        ESP_LOGW(TAG, "%s: BSEC state save failed: %s", ctx->label, esp_err_to_name(err));
    }
}

static void log_bsec_outputs(const char *label, const bsec_output_t *outputs, uint8_t n_outputs, const bme680_data_t *data)
{
    float iaq = 0.0f;
    float co2 = 0.0f;
    float voc = 0.0f;
    int iaq_accuracy = -1;
    float run_in = -1.0f;
    float stab = -1.0f;
    bool got_iaq = false;
    bool got_co2 = false;
    bool got_voc = false;
    bool got_run_in = false;
    bool got_stab = false;

    for (uint8_t i = 0; i < n_outputs; i++) {
        switch (outputs[i].sensor_id) {
            case BSEC_OUTPUT_IAQ:
                iaq = outputs[i].signal;
                got_iaq = true;
                iaq_accuracy = outputs[i].accuracy;
                break;
            case BSEC_OUTPUT_CO2_EQUIVALENT:
                co2 = outputs[i].signal;
                got_co2 = true;
                break;
            case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
                voc = outputs[i].signal;
                got_voc = true;
                break;
            case BSEC_OUTPUT_STABILIZATION_STATUS:
                stab = outputs[i].signal;
                got_stab = true;
                break;
            case BSEC_OUTPUT_RUN_IN_STATUS:
                run_in = outputs[i].signal;
                got_run_in = true;
                break;
            default:
                break;
        }
    }

    s_bsec_out_indoor.valid = (got_iaq || got_co2 || got_voc);
    s_bsec_out_indoor.iaq = iaq;
    s_bsec_out_indoor.iaq_accuracy = iaq_accuracy;
    s_bsec_out_indoor.eco2_ppm = co2;
    s_bsec_out_indoor.bvoc_ppm = voc;
    s_bsec_out_indoor.run_in = run_in;
    s_bsec_out_indoor.stab = stab;
    s_bsec_out_indoor.timestamp_us = esp_timer_get_time();

    if (got_iaq || got_co2 || got_voc) {
        ESP_LOGI(TAG, "%s: Luft IAQ=%.1f, CO2eq=%.1f ppm, VOCeq=%.3f ppm | acc=%d run_in=%.0f stab=%.0f",
                 label,
                 got_iaq ? iaq : -1.0f,
                 got_co2 ? co2 : -1.0f,
                 got_voc ? voc : -1.0f,
                 got_iaq ? iaq_accuracy : -1,
                 got_run_in ? run_in : -1.0f,
                 got_stab ? stab : -1.0f);
    }
}

static int64_t s_bsec_next_call_ns = 0;
static int64_t s_outdoor_next_us = 0;

static esp_err_t bsec_configure_sensor(bme680_handle_t handle, const bsec_bme_settings_t *settings)
{
    if (!settings || !handle) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(bme680_set_temperature_oversampling(handle, (bme680_temperature_oversampling_t)settings->temperature_oversampling),
                        TAG, "set temp os failed");
    ESP_RETURN_ON_ERROR(bme680_set_humidity_oversampling(handle, (bme680_humidity_oversampling_t)settings->humidity_oversampling),
                        TAG, "set hum os failed");
    ESP_RETURN_ON_ERROR(bme680_set_pressure_oversampling(handle, (bme680_pressure_oversampling_t)settings->pressure_oversampling),
                        TAG, "set press os failed");
    ESP_RETURN_ON_ERROR(bme680_set_gas_heater(handle, settings->heater_temperature, settings->heating_duration, settings->run_gas),
                        TAG, "set heater failed");
    ESP_RETURN_ON_ERROR(bme680_set_power_mode(handle, BME680_POWER_MODE_FORCED), TAG, "set power mode failed");

    return ESP_OK;
}

static void process_bme680(bsec_ctx_t *ctx)
{
    if (!ctx || !ctx->handle) {
        return;
    }

    int64_t now_ns = esp_timer_get_time() * 1000LL;
    bool is_outdoor = (ctx->label != NULL && strstr(ctx->label, "Außen") != NULL);

    if (BME680_USE_BSEC && !is_outdoor && s_bsec_ready) {
        // Warte bis exakt next_call erreicht ist
        if (s_bsec_next_call_ns > 0 && now_ns < s_bsec_next_call_ns) {
            return;
        }

        // Verwende den gespeicherten next_call Timestamp für sensor_control
        int64_t call_time_ns = (s_bsec_next_call_ns > 0) ? s_bsec_next_call_ns : now_ns;
        
        bsec_bme_settings_t settings = {0};
        bsec_library_return_t sc = bsec_sensor_control(call_time_ns, &settings);
        if (sc != BSEC_OK) {
            ESP_LOGW(TAG, "%s: BSEC sensor_control Fehler (%d) at call_time=%lld now=%lld", 
                     ctx->label, sc, call_time_ns, now_ns);
            s_bsec_next_call_ns = now_ns + 3000000000LL;  // 3s retry
            return;
        }
        s_bsec_next_call_ns = settings.next_call;

        if (!settings.trigger_measurement) {
            return;
        }

        esp_err_t cfg_err = bsec_configure_sensor(ctx->handle, &settings);
        if (cfg_err != ESP_OK) {
            ESP_LOGW(TAG, "%s: BME680 Konfiguration fehlgeschlagen (%s)", ctx->label, esp_err_to_name(cfg_err));
            return;
        }
        
        // Warte auf Messung: TPH (~100ms) + Gas Heater (settings.heating_duration)
        uint32_t meas_dur_ms = 100 + settings.heating_duration;
        vTaskDelay(pdMS_TO_TICKS(meas_dur_ms));
    }

    bme680_data_t data = {0};
    i2c_bus_lock();
    esp_err_t err = bme680_get_data(ctx->handle, &data);
    i2c_bus_unlock();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s: Lesen fehlgeschlagen (%s)", ctx->label, esp_err_to_name(err));
        return;
    }

    // Plausibilitätsprüfung
    bool plausible = bme680_apply_plausibility(ctx, &data, now_ns / 1000LL);
    if (!plausible && ctx->plausibility->have_value) {
        // Fallback auf letzte gültige Werte
        ESP_LOGW(TAG, "%s: Verwende Fallback-Werte", ctx->label);
        data.air_temperature = ctx->plausibility->temperature_c;
        data.relative_humidity = ctx->plausibility->humidity_pct;
        data.barometric_pressure = ctx->plausibility->pressure_hpa * 100.0f;
        data.gas_resistance = ctx->plausibility->gas_res_kohm * 1000.0f;
        data.gas_valid = ctx->plausibility->gas_valid;
        data.heater_stable = ctx->plausibility->heater_stable;
    }

    if (is_outdoor) {
        ESP_LOGI(TAG, "%s: T=%.2f °C, rH=%.2f %%, p=%.2f hPa%s",
                 ctx->label,
                 data.air_temperature,
                 data.relative_humidity,
                 data.barometric_pressure / 100.0f,
                 plausible ? "" : " (fallback)");
    } else {
        ESP_LOGI(TAG, "%s: T=%.2f °C, rH=%.2f %%, Gas=%.2f kOhm (valid=%d stable=%d)%s",
                 ctx->label,
                 data.air_temperature,
                 data.relative_humidity,
                 data.gas_resistance / 1000.0f,
                 data.gas_valid,
                 data.heater_stable,
                 plausible ? "" : " (fallback)");
    }

    if (!BME680_USE_BSEC || !s_bsec_ready || is_outdoor) {
        return;
    }

    if (!data.gas_valid || !data.heater_stable) {
        ESP_LOGW(TAG, "%s: BSEC wartet auf gueltige Gasdaten (valid=%d stable=%d)",
                 ctx->label, data.gas_valid, data.heater_stable);
    }

    if (ctx->state_len > 0) {
        bsec_set_state(ctx->state, ctx->state_len, s_bsec_work_buffer, sizeof(s_bsec_work_buffer));
    }

    int64_t ts_ns = now_ns;
    bsec_input_t inputs[4] = {0};
    uint8_t n_inputs = 0;

    inputs[n_inputs++] = (bsec_input_t){
        .sensor_id = BSEC_INPUT_TEMPERATURE,
        .signal = data.air_temperature,
        .time_stamp = ts_ns,
    };
    inputs[n_inputs++] = (bsec_input_t){
        .sensor_id = BSEC_INPUT_HUMIDITY,
        .signal = data.relative_humidity,
        .time_stamp = ts_ns,
    };
    inputs[n_inputs++] = (bsec_input_t){
        .sensor_id = BSEC_INPUT_PRESSURE,
        .signal = data.barometric_pressure,
        .time_stamp = ts_ns,
    };
    if (data.gas_resistance > 0.0f && data.gas_valid) {
        inputs[n_inputs++] = (bsec_input_t){
            .sensor_id = BSEC_INPUT_GASRESISTOR,
            .signal = data.gas_resistance,
            .time_stamp = ts_ns,
        };
    }

    bsec_output_t outputs[BSEC_NUMBER_OUTPUTS] = {0};
    uint8_t n_outputs = BSEC_NUMBER_OUTPUTS;
    bsec_library_return_t status = bsec_do_steps(inputs, n_inputs, outputs, &n_outputs);
    if (status == BSEC_OK) {
        log_bsec_outputs(ctx->label, outputs, n_outputs, &data);
    } else {
        ESP_LOGW(TAG, "%s: BSEC Fehler (%d)", ctx->label, status);
    }

    uint32_t out_len = BSEC_MAX_STATE_BLOB_SIZE;
    bsec_get_state(0, ctx->state, BSEC_MAX_STATE_BLOB_SIZE, s_bsec_work_buffer, sizeof(s_bsec_work_buffer), &out_len);
    ctx->state_len = out_len;
    
    // Periodisches Speichern des BSEC State (alle 30 min)
    int64_t now_us = now_ns / 1000LL;
    if (now_us - s_last_bsec_state_save_us >= BSEC_STATE_SAVE_INTERVAL_US) {
        s_last_bsec_state_save_us = now_us;
        bsec_save_state(ctx);
    }
}

static void bme680_task(void *arg)
{
    while (1) {
        int64_t now_us = esp_timer_get_time();
        int64_t now_ns = now_us * 1000LL;

        bool indoor_due = false;
        if (s_bme_lo) {
            if (!BME680_USE_BSEC || s_bsec_next_call_ns == 0 || now_ns >= s_bsec_next_call_ns) {
                indoor_due = true;
            }
        }

        bool outdoor_due = false;
        if (s_bme_hi) {
            if (s_outdoor_next_us == 0 || now_us >= s_outdoor_next_us) {
                outdoor_due = true;
            }
        }

        if (indoor_due) {
            process_bme680(&s_bsec_lo);
            now_us = esp_timer_get_time();
            now_ns = now_us * 1000LL;
        }

        if (outdoor_due) {
            process_bme680(&s_bsec_hi);
            s_outdoor_next_us = now_us + (BME680_LOG_INTERVAL_MS * 1000LL);
        }

        int64_t delay_ms = BME680_LOG_INTERVAL_MS;
        if (BME680_USE_BSEC && s_bsec_next_call_ns > now_ns) {
            int64_t delta_ms = (s_bsec_next_call_ns - now_ns) / 1000000LL;
            if (delta_ms > 0 && delta_ms < delay_ms) {
                delay_ms = delta_ms;
            }
        }
        if (s_bme_hi && s_outdoor_next_us > now_us) {
            int64_t delta_ms = (s_outdoor_next_us - now_us) / 1000LL;
            if (delta_ms > 0 && delta_ms < delay_ms) {
                delay_ms = delta_ms;
            }
        }

        if (delay_ms < 50) {
            delay_ms = 50;
        }
        vTaskDelay(pdMS_TO_TICKS((uint32_t)delay_ms));
    }
}

static esp_err_t init_one(uint8_t addr, bme680_handle_t *out_handle)
{
    bme680_config_t cfg = BME680_CONFIG_DEFAULT;
    cfg.i2c_address = addr;

    i2c_master_bus_handle_t bus = i2c_bus_get_internal();
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_FAIL, TAG, "I2C bus fehlt");

    bme680_handle_t handle = NULL;
    i2c_bus_lock();
    esp_err_t err = bme680_init(bus, &cfg, &handle);
    i2c_bus_unlock();
    if (err != ESP_OK || handle == NULL) {
        ESP_LOGW(TAG, "BME680 0x%02X init fehlgeschlagen (%s)", addr, esp_err_to_name(err));
        return err != ESP_OK ? err : ESP_FAIL;
    }

    *out_handle = handle;
    ESP_LOGI(TAG, "BME680 0x%02X bereit", addr);
    return ESP_OK;
}

esp_err_t bme680_app_get_snapshot(bme680_snapshot_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    if (s_plausibility_indoor.have_value) {
        out->indoor.valid = true;
        out->indoor.temperature_c = s_plausibility_indoor.temperature_c;
        out->indoor.humidity_pct = s_plausibility_indoor.humidity_pct;
        out->indoor.pressure_hpa = s_plausibility_indoor.pressure_hpa;
        out->indoor.gas_kohm = s_plausibility_indoor.gas_res_kohm;
        out->indoor.gas_valid = s_plausibility_indoor.gas_valid;
        out->indoor.heater_stable = s_plausibility_indoor.heater_stable;
        out->indoor.timestamp_us = s_plausibility_indoor.timestamp_us;

        if (s_bsec_out_indoor.valid) {
            out->indoor.iaq_valid = true;
            out->indoor.iaq = s_bsec_out_indoor.iaq;
            out->indoor.iaq_accuracy = s_bsec_out_indoor.iaq_accuracy;
            out->indoor.eco2_ppm = s_bsec_out_indoor.eco2_ppm;
            out->indoor.bvoc_ppm = s_bsec_out_indoor.bvoc_ppm;
            out->indoor.run_in = s_bsec_out_indoor.run_in;
            out->indoor.stab = s_bsec_out_indoor.stab;
        }
    }

    if (s_plausibility_outdoor.have_value) {
        out->outdoor.valid = true;
        out->outdoor.temperature_c = s_plausibility_outdoor.temperature_c;
        out->outdoor.humidity_pct = s_plausibility_outdoor.humidity_pct;
        out->outdoor.pressure_hpa = s_plausibility_outdoor.pressure_hpa;
        out->outdoor.gas_kohm = s_plausibility_outdoor.gas_res_kohm;
        out->outdoor.gas_valid = s_plausibility_outdoor.gas_valid;
        out->outdoor.heater_stable = s_plausibility_outdoor.heater_stable;
        out->outdoor.timestamp_us = s_plausibility_outdoor.timestamp_us;

        // Luftdruck-Trend 1h (ab 4 Samples)
        float trend1h_hpa_h = 0.0f;
        bme680_pressure_trend_t trend1h_state = BME680_TREND_STEADY;
        out->outdoor.press_trend_1h_valid = press_history_calculate_trend_window(
            PRESS_TREND_WINDOW_1H_SAMPLES, 1.0f / 3.0f, &trend1h_hpa_h, &trend1h_state);
        if (out->outdoor.press_trend_1h_valid) {
            out->outdoor.press_trend_1h_hpa_h = trend1h_hpa_h;
            out->outdoor.press_trend_1h_state = trend1h_state;
        }

        // Luftdruck-Trend 3h (ab 12 Samples)
        float trend3h_hpa_h = 0.0f;
        bme680_pressure_trend_t trend3h_state = BME680_TREND_STEADY;
        out->outdoor.press_trend_3h_valid = press_history_calculate_trend_window(
            PRESS_TREND_WINDOW_3H_SAMPLES, 1.0f, &trend3h_hpa_h, &trend3h_state);
        if (out->outdoor.press_trend_3h_valid) {
            out->outdoor.press_trend_3h_hpa_h = trend3h_hpa_h;
            out->outdoor.press_trend_3h_state = trend3h_state;
        }

        // Sample für Historie hinzufügen
        press_history_add_sample(s_plausibility_outdoor.pressure_hpa, s_plausibility_outdoor.timestamp_us);
    }

    return (out->indoor.valid || out->outdoor.valid) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t bme680_app_start(void)
{
    if (s_task) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "I2C init fehlgeschlagen");

    i2c_bus_handle_t bus = i2c_bus_get();
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_FAIL, TAG, "I2C bus fehlt");

    uint8_t found[8] = {0};
    i2c_bus_lock();
    uint8_t count = i2c_bus_scan(bus, found, sizeof(found));
    i2c_bus_unlock();
    bool seen_lo = false;
    bool seen_hi = false;
    for (int i = 0; i < count; i++) {
        if (found[i] == I2C_BME680_DEV_ADDR_LO) {
            seen_lo = true;
        } else if (found[i] == I2C_BME680_DEV_ADDR_HI) {
            seen_hi = true;
        }
    }

    if (count > 0) {
        char list[64] = {0};
        size_t off = 0;
        for (int i = 0; i < count; i++) {
            int written = snprintf(list + off, sizeof(list) - off, "%s0x%02X", (i == 0) ? "" : ",", found[i]);
            if (written < 0 || (size_t)written >= (sizeof(list) - off)) {
                break;
            }
            off += (size_t)written;
        }
        ESP_LOGI(TAG, "Scan: %u Device(s): %s", count, list);
    }

    if (seen_lo) {
        init_one(I2C_BME680_DEV_ADDR_LO, &s_bme_lo);
        s_bsec_lo.handle = s_bme_lo;
    } else {
        ESP_LOGW(TAG, "BME680 0x76 nicht gesehen");
    }

    if (seen_hi) {
        init_one(I2C_BME680_DEV_ADDR_HI, &s_bme_hi);
        s_bsec_hi.handle = s_bme_hi;
    } else {
        ESP_LOGW(TAG, "BME680 0x77 nicht gesehen");
    }

    if (!s_bme_lo && !s_bme_hi) {
        ESP_LOGW(TAG, "Kein BME680 aktiv");
        return ESP_ERR_NOT_FOUND;
    }

    if (BME680_USE_BSEC) {
        bsec_library_return_t bsec_status = bsec_init();
        if (bsec_status == BSEC_OK) {
            // Versuche gespeicherten State zu laden
            bool state_restored = bsec_load_state(&s_bsec_lo);
            if (state_restored) {
                ESP_LOGI(TAG, "BSEC State aus NVS wiederhergestellt");
            }
            
            bsec_sensor_configuration_t requested[7] = {
                { .sensor_id = BSEC_OUTPUT_IAQ, .sample_rate = BSEC_SAMPLE_RATE_LP },
                { .sensor_id = BSEC_OUTPUT_CO2_EQUIVALENT, .sample_rate = BSEC_SAMPLE_RATE_LP },
                { .sensor_id = BSEC_OUTPUT_BREATH_VOC_EQUIVALENT, .sample_rate = BSEC_SAMPLE_RATE_LP },
                { .sensor_id = BSEC_OUTPUT_RUN_IN_STATUS, .sample_rate = BSEC_SAMPLE_RATE_LP },
                { .sensor_id = BSEC_OUTPUT_STABILIZATION_STATUS, .sample_rate = BSEC_SAMPLE_RATE_LP },
                { .sensor_id = BSEC_OUTPUT_RAW_PRESSURE, .sample_rate = BSEC_SAMPLE_RATE_LP },
                { .sensor_id = BSEC_OUTPUT_RAW_GAS, .sample_rate = BSEC_SAMPLE_RATE_LP },
            };
            bsec_sensor_configuration_t required[BSEC_MAX_PHYSICAL_SENSOR] = {0};
            uint8_t n_required = BSEC_MAX_PHYSICAL_SENSOR;
            bsec_status = bsec_update_subscription(requested, 7, required, &n_required);
            if (bsec_status == BSEC_OK) {
                s_bsec_ready = true;
                ESP_LOGI(TAG, "BSEC aktiviert (LP 3s, IAQ+CO2+VOC+RAW)");
            } else {
                ESP_LOGW(TAG, "BSEC subscription fehlgeschlagen (%d)", bsec_status);
            }
        } else {
            ESP_LOGW(TAG, "BSEC init fehlgeschlagen (%d)", bsec_status);
        }
    } else {
        ESP_LOGI(TAG, "BSEC deaktiviert (Bosch-Umrechnung aus)");
    }

    BaseType_t r = xTaskCreatePinnedToCore(bme680_task, "bme680_task", 8192, NULL, 4, &s_task, 0);
    if (r != pdPASS) {
        s_task = NULL;
        ESP_RETURN_ON_ERROR(ESP_FAIL, TAG, "task start failed");
    }

    return ESP_OK;
}
