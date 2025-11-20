#include "womo_weather_http.h"

#include "sdkconfig.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

#include <string.h>
#include <stdio.h>

#define OWM_DEFAULT_API_KEY      "0984ac7736baa59683e8b261d764b290"
#define OWM_DEFAULT_LATITUDE     "50.0260"   // Rodgau (will be GPS-driven later)
#define OWM_DEFAULT_LONGITUDE    "8.8850"
#define OWM_DEFAULT_INTERVAL_MIN 15

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
static const char* weather_http_get_api_key(void);
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

    const char *api_key = weather_http_get_api_key();
    const char *latitude = weather_http_get_latitude();
    const char *longitude = weather_http_get_longitude();

    if (!api_key || api_key[0] == '\0') {
        ESP_LOGW(TAG, "OWM API key empty, skipping online weather updates");
        return ESP_ERR_INVALID_ARG;
    }

    if (!latitude || !longitude || latitude[0] == '\0' || longitude[0] == '\0') {
        ESP_LOGW(TAG, "Latitude/Longitude not configured");
        return ESP_ERR_INVALID_ARG;
    }

    s_ctx.callback = callback;
    s_ctx.user_data = user_data;
    s_ctx.stop_requested = false;

    BaseType_t created = xTaskCreate(
        weather_http_task,
        "owm_task",
        WEATHER_HTTP_TASK_STACK,
        NULL,
        WEATHER_HTTP_TASK_PRIO,
        &s_ctx.task_handle
    );

    if (created != pdPASS) {
        s_ctx.task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "OpenWeatherMap task started (interval %u min) lat=%s lon=%s",
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
    ESP_LOGI(TAG, "OpenWeatherMap task stopped");
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
        ESP_LOGW(TAG, "OpenWeatherMap status %d", status);
        return ESP_FAIL;
    }

    if (response->length == 0) {
        ESP_LOGW(TAG, "Empty weather response");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static const char* weather_http_get_api_key(void)
{
#ifdef CONFIG_WOMO_OWM_API_KEY
    if (CONFIG_WOMO_OWM_API_KEY[0] != '\0') {
        return CONFIG_WOMO_OWM_API_KEY;
    }
#endif
    return OWM_DEFAULT_API_KEY;
}

static const char* weather_http_get_latitude(void)
{
#ifdef CONFIG_WOMO_OWM_LATITUDE
    if (CONFIG_WOMO_OWM_LATITUDE[0] != '\0') {
        return CONFIG_WOMO_OWM_LATITUDE;
    }
#endif
    return OWM_DEFAULT_LATITUDE;
}

static const char* weather_http_get_longitude(void)
{
#ifdef CONFIG_WOMO_OWM_LONGITUDE
    if (CONFIG_WOMO_OWM_LONGITUDE[0] != '\0') {
        return CONFIG_WOMO_OWM_LONGITUDE;
    }
#endif
    return OWM_DEFAULT_LONGITUDE;
}

static uint32_t weather_http_get_interval_minutes(void)
{
#ifdef CONFIG_WOMO_OWM_UPDATE_MINUTES
    if (CONFIG_WOMO_OWM_UPDATE_MINUTES > 0) {
        return CONFIG_WOMO_OWM_UPDATE_MINUTES;
    }
#endif
    return OWM_DEFAULT_INTERVAL_MIN;
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

    const cJSON *main_obj = cJSON_GetObjectItem(root, "main");
    if (cJSON_IsObject(main_obj)) {
        const cJSON *temp = cJSON_GetObjectItem(main_obj, "temp");
        const cJSON *feels_like = cJSON_GetObjectItem(main_obj, "feels_like");
        const cJSON *pressure = cJSON_GetObjectItem(main_obj, "pressure");
        const cJSON *humidity = cJSON_GetObjectItem(main_obj, "humidity");
        if (cJSON_IsNumber(temp)) data.temperature_c = (float)temp->valuedouble;
        if (cJSON_IsNumber(feels_like)) data.feels_like_c = (float)feels_like->valuedouble;
        if (cJSON_IsNumber(pressure)) data.pressure_hpa = (float)pressure->valuedouble;
        if (cJSON_IsNumber(humidity)) data.humidity_percent = (float)humidity->valuedouble;
    }

    const cJSON *wind_obj = cJSON_GetObjectItem(root, "wind");
    if (cJSON_IsObject(wind_obj)) {
        const cJSON *speed = cJSON_GetObjectItem(wind_obj, "speed");
        if (cJSON_IsNumber(speed)) data.wind_speed_ms = (float)speed->valuedouble;
    }

    const cJSON *weather_arr = cJSON_GetObjectItem(root, "weather");
    if (cJSON_IsArray(weather_arr) && cJSON_GetArraySize(weather_arr) > 0) {
        const cJSON *w = cJSON_GetArrayItem(weather_arr, 0);
        const cJSON *id = cJSON_GetObjectItem(w, "id");
        const cJSON *desc = cJSON_GetObjectItem(w, "description");
        const cJSON *icon = cJSON_GetObjectItem(w, "icon");
        if (cJSON_IsNumber(id)) data.weather_id = id->valueint;
        if (cJSON_IsString(desc) && desc->valuestring) {
            strncpy(data.description, desc->valuestring, sizeof(data.description) - 1);
        }
        if (cJSON_IsString(icon) && icon->valuestring && strlen(icon->valuestring) >= 3) {
            data.is_night = (icon->valuestring[2] == 'n');
        }
    }

    if (!data.weather_id) {
        ESP_LOGW(TAG, "No weather id in response");
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

    const char *api_key = weather_http_get_api_key();
    const char *latitude = weather_http_get_latitude();
    const char *longitude = weather_http_get_longitude();

    int written = snprintf(out_url, max_len,
                           "http://api.openweathermap.org/data/2.5/weather?lat=%s&lon=%s&units=metric&appid=%s",
                           latitude,
                           longitude,
                           api_key);
    if (written < 0 || (size_t)written >= max_len) {
        ESP_LOGE(TAG, "URL buffer too small");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
