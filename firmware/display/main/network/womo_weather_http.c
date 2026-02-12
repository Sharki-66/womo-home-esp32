#include "womo_weather_http.h"

#include "sdkconfig.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

#include <string.h>
#include <stdio.h>

#define OM_DEFAULT_LATITUDE     "50.0260"   // Rodgau (will be GPS-driven later)
#define OM_DEFAULT_LONGITUDE    "8.8850"
#define OM_DEFAULT_INTERVAL_MIN 5

#define TAG "weather_http"

#define WEATHER_HTTP_BUFFER_SIZE 4096
#define WEATHER_HTTP_TASK_STACK 4096
#define WEATHER_HTTP_TASK_PRIO   4

typedef struct {
    char buffer[WEATHER_HTTP_BUFFER_SIZE];
    size_t length;
    esp_err_t last_error;
} weather_http_response_t;

typedef struct {
    womo_weather_http_callback_t callback;
    void *user_data;
    TaskHandle_t task_handle;
    bool stop_requested;
} weather_http_ctx_t;

static weather_http_ctx_t s_ctx = {0};

static esp_err_t weather_http_fetch(womo_weather_http_data_t *out_data);
static esp_err_t weather_http_parse_json(const char *json, womo_weather_http_data_t *out_data);
static esp_err_t weather_http_build_url(char *out_url, size_t max_len);
static void weather_http_task(void *arg);
static esp_err_t weather_http_perform_request(weather_http_response_t *response);
static const char* weather_http_get_latitude(void);
static const char* weather_http_get_longitude(void);
static uint32_t weather_http_get_interval_minutes(void);

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

    const char *latitude = weather_http_get_latitude();
    const char *longitude = weather_http_get_longitude();

    if (!latitude || !longitude || latitude[0] == '\0' || longitude[0] == '\0') {
        ESP_LOGW(TAG, "Latitude/Longitude not configured");
        return ESP_ERR_INVALID_ARG;
    }

    s_ctx.callback = callback;
    s_ctx.user_data = user_data;
    s_ctx.stop_requested = false;

    BaseType_t created = xTaskCreate(
        weather_http_task,
        "om_task",
        WEATHER_HTTP_TASK_STACK,
        NULL,
        WEATHER_HTTP_TASK_PRIO,
        &s_ctx.task_handle
    );

    if (created != pdPASS) {
        s_ctx.task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Open-Meteo task started (interval %u min) lat=%s lon=%s",
             weather_http_get_interval_minutes(), latitude, longitude);
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

    while (!s_ctx.stop_requested) {
        womo_weather_http_data_t data = {0};
        esp_err_t err = weather_http_fetch(&data);
        if (err == ESP_OK && data.valid) {
            if (s_ctx.callback) {
                s_ctx.callback(&data, s_ctx.user_data);
            }
        } else {
            ESP_LOGW(TAG, "Weather fetch failed (%s)", esp_err_to_name(err));
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

static esp_err_t weather_http_fetch(womo_weather_http_data_t *out_data)
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
    err = weather_http_parse_json(response->buffer, out_data);
    free(response);
    return err;
}

static esp_err_t weather_http_perform_request(weather_http_response_t *response)
{
    char url[192];
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
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_ERR_NO_MEM;
    }

    response->length = 0;
    response->last_error = ESP_OK;

    err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (response->last_error != ESP_OK) {
        return response->last_error;
    }

    if (status != 200) {
        ESP_LOGW(TAG, "Open-Meteo status %d", status);
        return ESP_FAIL;
    }

    if (response->length == 0) {
        ESP_LOGW(TAG, "Empty weather response");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static const char* weather_http_get_latitude(void)
{
#ifdef CONFIG_WOMO_OWM_LATITUDE
    if (CONFIG_WOMO_OWM_LATITUDE[0] != '\0') {
        return CONFIG_WOMO_OWM_LATITUDE;
    }
#endif
    return OM_DEFAULT_LATITUDE;
}

static const char* weather_http_get_longitude(void)
{
#ifdef CONFIG_WOMO_OWM_LONGITUDE
    if (CONFIG_WOMO_OWM_LONGITUDE[0] != '\0') {
        return CONFIG_WOMO_OWM_LONGITUDE;
    }
#endif
    return OM_DEFAULT_LONGITUDE;
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

static esp_err_t weather_http_parse_json(const char *json, womo_weather_http_data_t *out_data)
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
                           "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s&current=temperature_2m,weather_code,is_day,pressure_msl,relative_humidity_2m,wind_speed_10m&timezone=auto",
                           latitude,
                           longitude);
    if (written < 0 || (size_t)written >= max_len) {
        ESP_LOGE(TAG, "URL buffer too small");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
