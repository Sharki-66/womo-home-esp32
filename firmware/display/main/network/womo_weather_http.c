/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#include "womo_weather_http.h"

#include "sdkconfig.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "isrg_root_x1_pem.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "womo_http_mutex.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <math.h>

#include <string.h>
#include <stdio.h>

#define OM_DEFAULT_INTERVAL_MIN 5

/* NVS: letzte bekannte GPS-Position (überlebt Reboot) */
#define NVS_NAMESPACE       "weather_gps"
#define NVS_KEY_LAT         "lat"
#define NVS_KEY_LON         "lon"
#define NVS_SAVE_DELTA_DEG  0.005   /* ~550 m – NVS-Write nur bei signifikanter Bewegung */

/* Live-GPS-Koordinaten (Thread-safe via Spinlock) */
static portMUX_TYPE s_gps_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_gps_lat[16] = "";   // leer = kein Live-Fix
static char s_gps_lon[16] = "";

/* NVS-Cache: letzte gespeicherte Position (für Delta-Check) */
static double s_nvs_lat = 0.0;
static double s_nvs_lon = 0.0;
static bool   s_nvs_loaded = false;

#define TAG "weather_http"

#define WEATHER_HTTP_BUFFER_SIZE 8192
#define WEATHER_HTTP_TASK_STACK 4096
#define WEATHER_HTTP_TASK_PRIO   4
#define WEATHER_HTTP_INITIAL_DELAY_MS 3000

typedef struct {
    char buffer[WEATHER_HTTP_BUFFER_SIZE];
    size_t length;
    esp_err_t last_error;
} weather_http_response_t;

typedef struct {
    womo_weather_http_callback_t callback;
    void *user_data;
    womo_weather_forecast_callback_t forecast_callback;
    void *forecast_user_data;
    TaskHandle_t task_handle;
    bool stop_requested;
} weather_http_ctx_t;

static weather_http_ctx_t s_ctx = {0};
static int s_last_http_status = 0;  /* letzter HTTP-Statuscode aus perform_request */

static esp_err_t weather_http_fetch(womo_weather_http_data_t *out_data, womo_weather_forecast_t *out_forecast);
static esp_err_t weather_http_parse_json(const char *json, womo_weather_http_data_t *out_data, womo_weather_forecast_t *out_forecast);
static esp_err_t weather_http_build_url(char *out_url, size_t max_len);
static void weather_http_task(void *arg);
static esp_err_t weather_http_perform_request(weather_http_response_t *response);
static const char* weather_http_get_latitude(void);
static const char* weather_http_get_longitude(void);
static uint32_t weather_http_get_interval_minutes(void);
static void weather_nvs_load(void);
static void weather_nvs_save(double lat, double lon);
static void weather_http_log_heap(const char *phase);

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    weather_http_response_t *resp = (weather_http_response_t *)evt->user_data;
    if (!resp) {
        return ESP_FAIL;
    }

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (evt->data && evt->data_len > 0) {
            if (resp->length + evt->data_len >= WEATHER_HTTP_BUFFER_SIZE) {
                ESP_LOGE(TAG, "Response buffer too small (%zu + %d)", resp->length, evt->data_len);
                resp->last_error = ESP_ERR_NO_MEM;
                return ESP_FAIL;
            }
            memcpy(resp->buffer + resp->length, evt->data, evt->data_len);
            resp->length += evt->data_len;
        }
        break;
    case HTTP_EVENT_ERROR:
        resp->last_error = ESP_FAIL;
        break;
    default:
        break;
    }
    return ESP_OK;
}

esp_err_t womo_weather_http_start(womo_weather_http_callback_t callback, void *user_data)
{
    if (s_ctx.task_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    /* NVS-Position laden (falls vorhanden) */
    weather_nvs_load();

    s_ctx.callback = callback;
    s_ctx.user_data = user_data;
    s_ctx.stop_requested = false;

    BaseType_t created = xTaskCreateWithCaps(
        weather_http_task,
        "om_task",
        WEATHER_HTTP_TASK_STACK,
        NULL,
        WEATHER_HTTP_TASK_PRIO,
        &s_ctx.task_handle,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (created != pdPASS) {
        ESP_LOGW(TAG, "om_task create failed: internal_free=%u largest=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        s_ctx.task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Open-Meteo task started (interval %u min), GPS: %s",
             weather_http_get_interval_minutes(),
             weather_http_get_latitude() ? "OK" : "wartet auf Fix");
    return ESP_OK;
}

esp_err_t womo_weather_http_stop(void)
{
    if (!s_ctx.task_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    s_ctx.stop_requested = true;
    xTaskNotifyGive(s_ctx.task_handle);

    // Wait for task to exit
    while (s_ctx.task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    s_ctx.callback = NULL;
    s_ctx.user_data = NULL;
    ESP_LOGI(TAG, "Open-Meteo task stopped");
    return ESP_OK;
}

bool womo_weather_http_is_running(void)
{
    return s_ctx.task_handle != NULL;
}

static void weather_http_task(void *arg)
{
    const uint32_t interval_minutes = weather_http_get_interval_minutes();
    uint32_t interval_ms = interval_minutes * 60000U;
    if (interval_ms == 0) {
        interval_ms = 60000U;
    }
    TickType_t delay_ticks = pdMS_TO_TICKS(interval_ms);
    if (delay_ticks == 0) {
        delay_ticks = pdMS_TO_TICKS(60000U);
    }

    /* Nach WiFi-Reconnect kurz Luft holen lassen: DHCP/TLS/WiFi interner Heap
     * beruhigt sich meist innerhalb weniger Sekunden. */
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WEATHER_HTTP_INITIAL_DELAY_MS)) > 0) {
        s_ctx.task_handle = NULL;
        s_ctx.stop_requested = false;
        vTaskDelete(NULL);
    }

    while (!s_ctx.stop_requested) {
        /* Auf GPS-Position warten (Live oder NVS) */
        const char *lat = weather_http_get_latitude();
        const char *lon = weather_http_get_longitude();
        if (!lat || !lon) {
            ESP_LOGD(TAG, "Keine GPS-Position bekannt – Wetter-Abfrage übersprungen");
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(15000)) > 0) {
                continue;
            }
            continue;
        }

        womo_weather_http_data_t data = {0};
        womo_weather_forecast_t forecast = {0};
        esp_err_t err = weather_http_fetch(&data, &forecast);
        if (err == ESP_OK && data.valid) {
            if (s_ctx.callback) {
                s_ctx.callback(&data, s_ctx.user_data);
            }
            if (forecast.valid && s_ctx.forecast_callback) {
                s_ctx.forecast_callback(&forecast, s_ctx.forecast_user_data);
            }
        } else {
            /* HTTP 429: Rate-Limit – langes Backoff damit Open-Meteo sich erholt */
            if (s_last_http_status == 429) {
                ESP_LOGW(TAG, "Weather fetch failed (%s) – rate limited (429), retry in 15 min",
                         esp_err_to_name(err));
                if (!s_ctx.stop_requested) {
                    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(15UL * 60UL * 1000UL));
                }
            } else {
                ESP_LOGW(TAG, "Weather fetch failed (%s) – retry in 30 s",
                         esp_err_to_name(err));
                /* Bei Fehler (DNS noch nicht bereit, kein Internet, etc.)
                 * kurz warten und erneut versuchen statt die vollen 5 min. */
                if (!s_ctx.stop_requested) {
                    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000));
                }
            }
            s_last_http_status = 0;
            continue;
        }

        if (s_ctx.stop_requested) {
            break;
        }

        if (ulTaskNotifyTake(pdTRUE, delay_ticks) > 0) {
            continue; // awakened early (stop request)
        }
    }

    s_ctx.task_handle = NULL;
    s_ctx.stop_requested = false;
    vTaskDelete(NULL);
}

static esp_err_t weather_http_fetch(womo_weather_http_data_t *out_data, womo_weather_forecast_t *out_forecast)
{
    if (!out_data) {
        return ESP_ERR_INVALID_ARG;
    }

    weather_http_response_t *response = calloc(1, sizeof(weather_http_response_t));
    if (!response) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = weather_http_perform_request(response);
    if (err != ESP_OK) {
        free(response);
        return err;
    }

    response->buffer[response->length] = '\0';
    err = weather_http_parse_json(response->buffer, out_data, out_forecast);
    free(response);
    return err;
}

static esp_err_t weather_http_perform_request(weather_http_response_t *response)
{
    char url[512];
    esp_err_t err = weather_http_build_url(url, sizeof(url));
    if (err != ESP_OK) {
        return err;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 7000,
        .event_handler = http_event_handler,
        .user_data = response,
        .cert_pem = isrg_root_x1_pem,   // direkt PEM statt crt_bundle (umgeht 0x4290)
    };

    weather_http_log_heap("before_http_init");

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        weather_http_log_heap("http_init_failed");
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_ERR_NO_MEM;
    }

    response->length = 0;
    response->last_error = ESP_OK;

    /* TLS-Mutex: nur eine HTTPS-Session gleichzeitig (Heap-Limit).
     * cleanup() MUSS im Mutex-Scope liegen, damit TLS-RAM frei ist
     * bevor der nächste Client den Mutex bekommt. */
    if (womo_http_mutex_acquire() != ESP_OK) {
        esp_http_client_cleanup(client);
        return ESP_ERR_TIMEOUT;
    }
    weather_http_log_heap("before_http_perform");
    err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    womo_http_mutex_release();

    if (err != ESP_OK) {
        weather_http_log_heap("http_perform_failed");
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        return err;
    }

    if (response->last_error != ESP_OK) {
        return response->last_error;
    }

    if (status != 200) {
        ESP_LOGW(TAG, "Open-Meteo status %d", status);
        s_last_http_status = status;
        return ESP_FAIL;
    }
    s_last_http_status = status;

    if (response->length == 0) {
        ESP_LOGW(TAG, "Empty weather response");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void weather_http_log_heap(const char *phase)
{
    ESP_LOGI(TAG,
             "Heap %s: internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u total_free=%u",
             phase,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             (unsigned)esp_get_free_heap_size());
}

/* ── NVS-Helfer ─────────────────────────────────────────── */

static void weather_nvs_load(void)
{
    if (s_nvs_loaded) return;
    s_nvs_loaded = true;

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;

    char buf[16] = "";
    size_t len = sizeof(buf);
    if (nvs_get_str(h, NVS_KEY_LAT, buf, &len) == ESP_OK) {
        s_nvs_lat = strtod(buf, NULL);
        taskENTER_CRITICAL(&s_gps_lock);
        if (s_gps_lat[0] == '\0') {          /* Nur füllen wenn noch kein Live-GPS */
            memcpy(s_gps_lat, buf, sizeof(s_gps_lat));
        }
        taskEXIT_CRITICAL(&s_gps_lock);
    }
    len = sizeof(buf);
    if (nvs_get_str(h, NVS_KEY_LON, buf, &len) == ESP_OK) {
        s_nvs_lon = strtod(buf, NULL);
        taskENTER_CRITICAL(&s_gps_lock);
        if (s_gps_lon[0] == '\0') {
            memcpy(s_gps_lon, buf, sizeof(s_gps_lon));
        }
        taskEXIT_CRITICAL(&s_gps_lock);
    }
    nvs_close(h);

    if (s_nvs_lat != 0.0 || s_nvs_lon != 0.0) {
        ESP_LOGI(TAG, "Letzte GPS-Position aus NVS: %.6f / %.6f", s_nvs_lat, s_nvs_lon);
    } else {
        ESP_LOGW(TAG, "Keine gespeicherte GPS-Position – Wetter wartet auf ersten Fix");
    }
}

static void weather_nvs_save(double lat, double lon)
{
    /* Nur schreiben wenn signifikante Bewegung (Flash schonen) */
    if (s_nvs_loaded &&
        fabs(lat - s_nvs_lat) < NVS_SAVE_DELTA_DEG &&
        fabs(lon - s_nvs_lon) < NVS_SAVE_DELTA_DEG) {
        return;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%.6f", lat);
    nvs_set_str(h, NVS_KEY_LAT, buf);
    snprintf(buf, sizeof(buf), "%.6f", lon);
    nvs_set_str(h, NVS_KEY_LON, buf);
    nvs_commit(h);
    nvs_close(h);

    s_nvs_lat = lat;
    s_nvs_lon = lon;
    ESP_LOGI(TAG, "GPS-Position im NVS gespeichert: %.6f / %.6f", lat, lon);
}

/* ── Thread-safe Getter ─────────────────────────────────── */
static char s_lat_copy[16];
static char s_lon_copy[16];

static const char* weather_http_get_latitude(void)
{
    weather_nvs_load();   /* Lazy-Init beim ersten Aufruf */
    taskENTER_CRITICAL(&s_gps_lock);
    bool have = (s_gps_lat[0] != '\0');
    if (have) memcpy(s_lat_copy, s_gps_lat, sizeof(s_lat_copy));
    taskEXIT_CRITICAL(&s_gps_lock);
    return have ? s_lat_copy : NULL;
}

static const char* weather_http_get_longitude(void)
{
    weather_nvs_load();
    taskENTER_CRITICAL(&s_gps_lock);
    bool have = (s_gps_lon[0] != '\0');
    if (have) memcpy(s_lon_copy, s_gps_lon, sizeof(s_lon_copy));
    taskEXIT_CRITICAL(&s_gps_lock);
    return have ? s_lon_copy : NULL;
}

void womo_weather_http_set_forecast_callback(womo_weather_forecast_callback_t cb, void *user_data)
{
    s_ctx.forecast_callback = cb;
    s_ctx.forecast_user_data = user_data;
}

void womo_weather_http_set_location(double lat, double lon)
{
    char lat_buf[16], lon_buf[16];
    snprintf(lat_buf, sizeof(lat_buf), "%.6f", lat);
    snprintf(lon_buf, sizeof(lon_buf), "%.6f", lon);

    taskENTER_CRITICAL(&s_gps_lock);
    memcpy(s_gps_lat, lat_buf, sizeof(s_gps_lat));
    memcpy(s_gps_lon, lon_buf, sizeof(s_gps_lon));
    taskEXIT_CRITICAL(&s_gps_lock);

    /* Kein NVS-Schreiben hier:
     * Diese Funktion wird aus router_poll_task aufgerufen, dessen Stack in
     * PSRAM liegt. NVS/Flash-Zugriffe können Cache-Disable auslösen und sind
     * aus diesem Kontext nicht sicher. */
    s_nvs_lat = lat;
    s_nvs_lon = lon;
    s_nvs_loaded = true;
}

static uint32_t weather_http_get_interval_minutes(void)
{
#ifdef CONFIG_WOMO_OWM_UPDATE_MINUTES
    if (CONFIG_WOMO_OWM_UPDATE_MINUTES > 0) {
        return CONFIG_WOMO_OWM_UPDATE_MINUTES;
    }
#endif
    return OM_DEFAULT_INTERVAL_MIN;
}

static const char* weather_http_wmo_desc(int code)
{
    switch (code) {
        case 0: return "clear";
        case 1: case 2: return "partly cloudy";
        case 3: return "overcast";
        case 45: case 48: return "fog";
        case 51: case 53: case 55: return "drizzle";
        case 56: case 57: return "freezing drizzle";
        case 61: return "rain light";
        case 63: return "rain";
        case 65: return "rain heavy";
        case 66: case 67: return "freezing rain";
        case 71: case 73: case 75: return "snow";
        case 77: return "snow grains";
        case 80: return "rain shower";
        case 81: return "rain shower heavy";
        case 82: return "rain shower violent";
        case 85: case 86: return "snow shower";
        case 95: return "thunderstorm";
        case 96: case 99: return "thunderstorm hail";
        default: return "unknown";
    }
}

static esp_err_t weather_http_parse_json(const char *json, womo_weather_http_data_t *out_data, womo_weather_forecast_t *out_forecast)
{
    if (!json || !out_data) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_FAIL;
    }

    womo_weather_http_data_t data = {0};
    data.valid = true;
    bool has_weather_code = false;

    const cJSON *current = cJSON_GetObjectItem(root, "current");
    if (!cJSON_IsObject(current)) {
        current = cJSON_GetObjectItem(root, "current_weather"); // fallback
    }

    if (cJSON_IsObject(current)) {
        const cJSON *temp = cJSON_GetObjectItem(current, "temperature_2m");
        const cJSON *pressure = cJSON_GetObjectItem(current, "pressure_msl");
        const cJSON *humidity = cJSON_GetObjectItem(current, "relative_humidity_2m");
        const cJSON *wind = cJSON_GetObjectItem(current, "wind_speed_10m");
        const cJSON *wmo = cJSON_GetObjectItem(current, "weather_code");
        const cJSON *is_day = cJSON_GetObjectItem(current, "is_day");

        if (cJSON_IsNumber(temp)) data.temperature_c = (float)temp->valuedouble;
        if (cJSON_IsNumber(pressure)) data.pressure_hpa = (float)pressure->valuedouble;
        if (cJSON_IsNumber(humidity)) data.humidity_percent = (float)humidity->valuedouble;
        if (cJSON_IsNumber(wind)) data.wind_speed_ms = (float)wind->valuedouble;
        if (cJSON_IsNumber(wmo)) {
            data.weather_id = wmo->valueint;
            has_weather_code = true;
            strncpy(data.description, weather_http_wmo_desc(data.weather_id), sizeof(data.description) - 1);
        }
        if (cJSON_IsNumber(is_day)) {
            data.is_night = (is_day->valueint == 0);
        }
    }

    if (!has_weather_code) {
        ESP_LOGW(TAG, "No weather code in response");
        data.valid = false;
    }

    *out_data = data;

    /* ── Daily forecast ────────────────────────────────── */
    if (out_forecast) {
        memset(out_forecast, 0, sizeof(*out_forecast));
        const cJSON *daily = cJSON_GetObjectItem(root, "daily");
        if (cJSON_IsObject(daily)) {
            const cJSON *dates    = cJSON_GetObjectItem(daily, "time");
            const cJSON *wmo_arr  = cJSON_GetObjectItem(daily, "weather_code");
            const cJSON *tmax_arr = cJSON_GetObjectItem(daily, "temperature_2m_max");
            const cJSON *tmin_arr = cJSON_GetObjectItem(daily, "temperature_2m_min");
            const cJSON *prec_arr = cJSON_GetObjectItem(daily, "precipitation_sum");
            const cJSON *prob_arr = cJSON_GetObjectItem(daily, "precipitation_probability_max");
            const cJSON *wind_arr = cJSON_GetObjectItem(daily, "wind_speed_10m_max");
            const cJSON *sun_arr  = cJSON_GetObjectItem(daily, "sunshine_duration");

            int n = cJSON_GetArraySize(dates);
            if (n > WOMO_FORECAST_DAYS) n = WOMO_FORECAST_DAYS;

            for (int i = 0; i < n; i++) {
                womo_weather_forecast_day_t *d = &out_forecast->day[i];
                d->valid = true;

                const cJSON *date_item = cJSON_GetArrayItem(dates, i);
                if (cJSON_IsString(date_item)) {
                    strncpy(d->date, date_item->valuestring, sizeof(d->date) - 1);
                }
                const cJSON *wc = cJSON_GetArrayItem(wmo_arr, i);
                if (cJSON_IsNumber(wc)) d->weather_code = wc->valueint;

                const cJSON *tmax = cJSON_GetArrayItem(tmax_arr, i);
                if (cJSON_IsNumber(tmax)) d->temp_max_c = (float)tmax->valuedouble;

                const cJSON *tmin = cJSON_GetArrayItem(tmin_arr, i);
                if (cJSON_IsNumber(tmin)) d->temp_min_c = (float)tmin->valuedouble;

                const cJSON *prec = cJSON_GetArrayItem(prec_arr, i);
                if (cJSON_IsNumber(prec)) d->precip_mm = (float)prec->valuedouble;

                const cJSON *prob = cJSON_GetArrayItem(prob_arr, i);
                if (cJSON_IsNumber(prob)) d->rain_prob_pct = prob->valueint;

                const cJSON *wind = cJSON_GetArrayItem(wind_arr, i);
                if (cJSON_IsNumber(wind)) d->wind_max_ms = (float)wind->valuedouble;

                const cJSON *sun = cJSON_GetArrayItem(sun_arr, i);
                if (cJSON_IsNumber(sun)) d->sunshine_h = (float)(sun->valuedouble / 3600.0);
            }
            if (n > 0) out_forecast->valid = true;
        }
    }

    cJSON_Delete(root);
    return data.valid ? ESP_OK : ESP_FAIL;
}

static esp_err_t weather_http_build_url(char *out_url, size_t max_len)
{
    if (!out_url || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *latitude = weather_http_get_latitude();
    const char *longitude = weather_http_get_longitude();

    int written = snprintf(out_url, max_len,
                           "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
                           "&current=temperature_2m,weather_code,is_day,pressure_msl,relative_humidity_2m,wind_speed_10m"
                           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,precipitation_probability_max,wind_speed_10m_max,sunshine_duration"
                           "&timezone=auto&forecast_days=5",
                           latitude, longitude);
    if (written < 0 || (size_t)written >= max_len) {
        ESP_LOGE(TAG, "URL buffer too small");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
