#include "womo_buzzer_http.h"

#if WOMO_ENABLE_BUZZER_STUDIO_HTTP

#include "hardware/buzzer.h"

#include "cJSON.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WOMO_BUZZER_HTTP_MAX_BODY 4096

static const char *TAG = "womo_buzz_http";
static httpd_handle_t s_server = NULL;

extern const char buzzer_studio_html_start[] asm("_binary_buzzer_studio_html_start");

static void set_common_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

static esp_err_t send_json_ok(httpd_req_t *req, const char *json)
{
    set_common_headers(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t handle_options(httpd_req_t *req)
{
    set_common_headers(req);
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t handle_ping(httpd_req_t *req)
{
    return send_json_ok(req, "{\"ok\":true,\"service\":\"buzzer_http\"}");
}

static esp_err_t handle_stop(httpd_req_t *req)
{
    display_buzzer_stop();
    return send_json_ok(req, "{\"ok\":true}");
}

static esp_err_t read_body(httpd_req_t *req, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int total = req->content_len;
    if (total <= 0 || (size_t)total >= out_sz) {
        return ESP_ERR_INVALID_SIZE;
    }

    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, out + received, total - received);
        if (r <= 0) {
            return ESP_FAIL;
        }
        received += r;
    }

    out[received] = '\0';
    return ESP_OK;
}

static esp_err_t read_body_alloc(httpd_req_t *req, char **out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }

    *out = NULL;

    int total = req->content_len;
    if (total <= 0 || total > WOMO_BUZZER_HTTP_MAX_BODY) {
        return ESP_ERR_INVALID_SIZE;
    }

    char *body = malloc((size_t)total + 1U);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = read_body(req, body, (size_t)total + 1U);
    if (err != ESP_OK) {
        free(body);
        return err;
    }

    *out = body;
    return ESP_OK;
}

static esp_err_t play_preset(const char *preset)
{
    if (!preset || preset[0] == '\0') {
        return ESP_ERR_NOT_FOUND;
    }

    if (strcmp(preset, "click") == 0) {
        display_buzzer_click();
    } else if (strcmp(preset, "warn") == 0) {
        display_buzzer_warn();
    } else if (strcmp(preset, "alarm") == 0) {
        display_buzzer_alarm();
    } else if (strcmp(preset, "startup") == 0) {
        display_buzzer_startup();
    } else {
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

static esp_err_t parse_and_play_notes(cJSON *notes)
{
    if (!cJSON_IsArray(notes)) {
        return ESP_ERR_INVALID_ARG;
    }

    int count = cJSON_GetArraySize(notes);
    if (count <= 0 || count > DISPLAY_BUZZER_MAX_PREVIEW_NOTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    display_buzzer_note_t seq[DISPLAY_BUZZER_MAX_PREVIEW_NOTES] = {0};

    for (int i = 0; i < count; ++i) {
        cJSON *item = cJSON_GetArrayItem(notes, i);
        if (!cJSON_IsObject(item)) {
            return ESP_ERR_INVALID_ARG;
        }

        cJSON *freq = cJSON_GetObjectItem(item, "freq_hz");
        cJSON *dur = cJSON_GetObjectItem(item, "dur_ms");
        cJSON *pause = cJSON_GetObjectItem(item, "pause_ms");

        if (!cJSON_IsNumber(freq) || !cJSON_IsNumber(dur)) {
            return ESP_ERR_INVALID_ARG;
        }

        int freq_v = freq->valueint;
        int dur_v = dur->valueint;
        int pause_v = cJSON_IsNumber(pause) ? pause->valueint : 0;

        if (freq_v < 0 || freq_v > 20000 || dur_v <= 0 || dur_v > 5000 || pause_v < 0 || pause_v > 5000) {
            return ESP_ERR_INVALID_ARG;
        }

        seq[i].freq_hz = (uint16_t)freq_v;
        seq[i].dur_ms = (uint16_t)dur_v;
        seq[i].pause_ms = (uint16_t)pause_v;
    }

    return display_buzzer_play_preview(seq, (size_t)count);
}

static esp_err_t handle_play(httpd_req_t *req)
{
    char *body = NULL;
    esp_err_t body_err = read_body_alloc(req, &body);
    if (body_err != ESP_OK) {
        set_common_headers(req);
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"invalid_body\"}");
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        set_common_headers(req);
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"invalid_json\"}");
    }

    esp_err_t err = ESP_ERR_NOT_FOUND;

    /* Optionale Lautstärke 0–100 % – wirkt ab dem nächsten Ton */
    cJSON *vol = cJSON_GetObjectItem(root, "volume");
    if (cJSON_IsNumber(vol)) {
        int v = vol->valueint;
        if (v < 0)   v = 0;
        if (v > 100) v = 100;
        display_buzzer_set_volume((uint8_t)v);
    }

    cJSON *preset = cJSON_GetObjectItem(root, "preset");
    if (cJSON_IsString(preset) && preset->valuestring) {
        err = play_preset(preset->valuestring);
    }

    if (err != ESP_OK) {
        cJSON *notes = cJSON_GetObjectItem(root, "notes");
        if (notes) {
            err = parse_and_play_notes(notes);
        }
    }

    cJSON_Delete(root);

    if (err != ESP_OK) {
        set_common_headers(req);
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"err\":\"invalid_payload\"}");
    }

    return send_json_ok(req, "{\"ok\":true}");
}

static esp_err_t handle_index(httpd_req_t *req)
{
    set_common_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    const char *html = buzzer_studio_html_start;
    size_t html_len = strlen(html);

    if (!html || html_len == 0) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "embedded buzzer studio missing");
    }

    return httpd_resp_send(req, html, (ssize_t)html_len);
}

static esp_err_t register_handlers(httpd_handle_t server)
{
    httpd_uri_t root_get = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_index,
        .user_ctx = NULL,
    };
    httpd_uri_t studio_get = {
        .uri = "/buzzer_studio.html",
        .method = HTTP_GET,
        .handler = handle_index,
        .user_ctx = NULL,
    };
    httpd_uri_t ping_get = {
        .uri = "/api/buzzer/ping",
        .method = HTTP_GET,
        .handler = handle_ping,
        .user_ctx = NULL,
    };
    httpd_uri_t play_post = {
        .uri = "/api/buzzer/play",
        .method = HTTP_POST,
        .handler = handle_play,
        .user_ctx = NULL,
    };
    httpd_uri_t stop_post = {
        .uri = "/api/buzzer/stop",
        .method = HTTP_POST,
        .handler = handle_stop,
        .user_ctx = NULL,
    };
    httpd_uri_t opts = {
        .uri = "/*",
        .method = HTTP_OPTIONS,
        .handler = handle_options,
        .user_ctx = NULL,
    };

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &root_get), TAG, "register / failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &studio_get), TAG, "register studio failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &ping_get), TAG, "register ping failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &play_post), TAG, "register play failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &stop_post), TAG, "register stop failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &opts), TAG, "register options failed");

    return ESP_OK;
}

esp_err_t womo_buzzer_http_start(void)
{
    if (s_server) {
        return ESP_OK;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_uri_handlers = 6;
    cfg.stack_size = 6144;
    cfg.uri_match_fn = httpd_uri_match_wildcard;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &cfg), TAG, "httpd start failed");

    esp_err_t reg_err = register_handlers(s_server);
    if (reg_err != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        return reg_err;
    }

    ESP_LOGI(TAG, "Buzzer HTTP server started on port %u", cfg.server_port);
    return ESP_OK;
}

esp_err_t womo_buzzer_http_stop(void)
{
    if (!s_server) {
        return ESP_OK;
    }

    esp_err_t err = httpd_stop(s_server);
    s_server = NULL;

    return err;
}

bool womo_buzzer_http_is_running(void)
{
    return s_server != NULL;
}

#else

esp_err_t womo_buzzer_http_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t womo_buzzer_http_stop(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool womo_buzzer_http_is_running(void)
{
    return false;
}

#endif
