#include "womo_geocode.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "geocode"

#define GEOCODE_HTTP_BUFFER_SIZE 4096
// Stack etwas größer, da cJSON + HTTP-Pipeline sonst Stack-Overflow riskieren
#define GEOCODE_TASK_STACK       7168
#define GEOCODE_TASK_PRIO        4

typedef struct {
    char buffer[GEOCODE_HTTP_BUFFER_SIZE];
    size_t length;
    esp_err_t last_error;
} geocode_response_t;

typedef struct {
    TaskHandle_t task_handle;
    double lat;
    double lon;
    char language[8];
    womo_geocode_callback_t callback;
    void *user_data;
} geocode_ctx_t;

static geocode_ctx_t s_ctx = {0};

static void geocode_task(void *arg);
static esp_err_t geocode_perform_request(geocode_response_t *response);
static esp_err_t geocode_parse_json(const char *json, womo_geocode_result_t *out_result);
static esp_err_t geocode_build_url(char *out_url, size_t max_len);
static esp_err_t geocode_http_fetch(womo_geocode_result_t *out_result);
static esp_err_t geocode_http_event_handler(esp_http_client_event_t *evt);

bool womo_geocode_is_running(void)
{
    return s_ctx.task_handle != NULL;
}

esp_err_t womo_geocode_reverse_request(double latitude,
                                       double longitude,
                                       const char *accept_language,
                                       womo_geocode_callback_t callback,
                                       void *user_data)
{
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!isfinite(latitude) || !isfinite(longitude)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (womo_geocode_is_running()) {
        return ESP_ERR_INVALID_STATE;
    }

    s_ctx.lat = latitude;
    s_ctx.lon = longitude;
    s_ctx.callback = callback;
    s_ctx.user_data = user_data;
    memset(s_ctx.language, 0, sizeof(s_ctx.language));
    if (accept_language && accept_language[0] != '\0') {
        strlcpy(s_ctx.language, accept_language, sizeof(s_ctx.language));
    } else {
        strlcpy(s_ctx.language, "en", sizeof(s_ctx.language));
    }

    BaseType_t created = xTaskCreate(
        geocode_task,
        "geocode_task",
        GEOCODE_TASK_STACK,
        NULL,
        GEOCODE_TASK_PRIO,
        &s_ctx.task_handle);

    if (created != pdPASS) {
        s_ctx.task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static void geocode_task(void *arg)
{
    womo_geocode_result_t result = {0};
    esp_err_t err = geocode_http_fetch(&result);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Reverse geocode fehlgeschlagen (%s)", esp_err_to_name(err));
    }

    if (s_ctx.callback) {
        s_ctx.callback(&result, s_ctx.user_data);
    }

    s_ctx.task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t geocode_http_fetch(womo_geocode_result_t *out_result)
{
    if (!out_result) {
        return ESP_ERR_INVALID_ARG;
    }

    geocode_response_t *response = calloc(1, sizeof(geocode_response_t));
    if (!response) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = geocode_perform_request(response);
    if (err == ESP_OK) {
        response->buffer[response->length] = '\0';
        err = geocode_parse_json(response->buffer, out_result);
    }

    free(response);
    return err;
}

static esp_err_t geocode_perform_request(geocode_response_t *response)
{
    char url[256];
    esp_err_t err = geocode_build_url(url, sizeof(url));
    if (err != ESP_OK) {
        return err;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 8000,
        .event_handler = geocode_http_event_handler,
        .user_data = response,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_header(client, "Accept-Language", s_ctx.language);
    esp_http_client_set_header(client, "User-Agent", "womo-home-esp32/1.0 (reverse geocode)");

    response->length = 0;
    response->last_error = ESP_OK;

    err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (response->last_error != ESP_OK) {
        return response->last_error;
    }

    if (status != 200) {
        ESP_LOGW(TAG, "Nominatim Status %d", status);
        return ESP_FAIL;
    }

    if (response->length == 0) {
        ESP_LOGW(TAG, "Leere Geocode-Antwort");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t geocode_build_url(char *out_url, size_t max_len)
{
    if (!out_url || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int written = snprintf(out_url,
                           max_len,
                           "https://nominatim.openstreetmap.org/reverse?format=jsonv2&lat=%.6f&lon=%.6f&zoom=14",
                           s_ctx.lat,
                           s_ctx.lon);
    if (written < 0 || (size_t)written >= max_len) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t geocode_http_event_handler(esp_http_client_event_t *evt)
{
    geocode_response_t *resp = (geocode_response_t *)evt->user_data;
    if (!resp) {
        return ESP_FAIL;
    }

    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (evt->data && evt->data_len > 0) {
            if (resp->length + evt->data_len >= GEOCODE_HTTP_BUFFER_SIZE) {
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

static const char* geocode_select_primary(const cJSON *address)
{
    if (!address) {
        return NULL;
    }

    const char *keys[] = {"city", "town", "village", "hamlet", "municipality", "suburb"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        const cJSON *val = cJSON_GetObjectItem(address, keys[i]);
        if (cJSON_IsString(val) && val->valuestring && val->valuestring[0] != '\0') {
            return val->valuestring;
        }
    }
    return NULL;
}

static const char* geocode_select_secondary(const cJSON *address)
{
    if (!address) {
        return NULL;
    }

    const char *keys[] = {"state", "county", "region", "country"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        const cJSON *val = cJSON_GetObjectItem(address, keys[i]);
        if (cJSON_IsString(val) && val->valuestring && val->valuestring[0] != '\0') {
            return val->valuestring;
        }
    }
    return NULL;
}

static esp_err_t geocode_parse_json(const char *json, womo_geocode_result_t *out_result)
{
    if (!json || !out_result) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return ESP_FAIL;
    }

    womo_geocode_result_t result = {0};

    const cJSON *display = cJSON_GetObjectItem(root, "display_name");
    if (cJSON_IsString(display) && display->valuestring) {
        strlcpy(result.display_name, display->valuestring, sizeof(result.display_name));
    }

    const cJSON *address = cJSON_GetObjectItem(root, "address");
    const char *primary = geocode_select_primary(address);
    const char *secondary = geocode_select_secondary(address);

    if (primary && secondary) {
        snprintf(result.short_name, sizeof(result.short_name), "%s, %s", primary, secondary);
    } else if (primary) {
        strlcpy(result.short_name, primary, sizeof(result.short_name));
    } else if (result.display_name[0]) {
        strlcpy(result.short_name, result.display_name, sizeof(result.short_name));
    }

    if (result.short_name[0] != '\0') {
        result.valid = true;
    }

    *out_result = result;
    cJSON_Delete(root);
    return result.valid ? ESP_OK : ESP_FAIL;
}
