/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * womo_display_http – HTTP-Dashboard-Server (Port 8080)
 *
 * Liefert:
 *   GET /          → womo_display.html (800×480 Web-Dashboard)
 *   GET /display   → identisch mit /
 *   GET /api/status → JSON mit allen Sensor- und Router-Daten
 */

#include "womo_display_http.h"

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "time/womo_time.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define TAG "womo_disp_http"

/* ── Eingebettetes HTML (EMBED_TXTFILES in CMakeLists.txt) ─────────────── */
extern const char womo_display_html_start[] asm("_binary_womo_display_html_start");

/* ── Interner Snapshot ─────────────────────────────────────────────────── */
typedef struct {
    /* Sensor-Daten (RS485) */
    womo_sensor_data_t sensor;
    bool               sensor_valid;

    /* Router-Daten */
    womo_router_wifi_status_t wifi;
    womo_router_lte_status_t  lte;
    womo_router_ap_status_t   ap;
    bool                      router_valid;

    /* System-Status */
    womo_dash_status_t status_level;
    char               status_text[32];
    bool               rs485_ok;
    char               location[128];
} display_snapshot_t;

static display_snapshot_t s_snap = {0};
static SemaphoreHandle_t  s_mutex = NULL;
static httpd_handle_t     s_server = NULL;

/* ── Hilfsfunktion: gemeinsame HTTP-Header setzen ───────────────────────── */
static void set_cors_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
}

/* ── Handler: GET / und GET /display ────────────────────────────────────── */
static esp_err_t handle_index(httpd_req_t *req)
{
    set_cors_headers(req);
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    const char *html = womo_display_html_start;
    if (!html || html[0] == '\0') {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "womo_display.html nicht eingebettet");
    }
    return httpd_resp_send(req, html, (ssize_t)strlen(html));
}

/* ── Handler: GET /api/status ───────────────────────────────────────────── */
static esp_err_t handle_status(httpd_req_t *req)
{
    set_cors_headers(req);
    httpd_resp_set_type(req, "application/json");

    /* Snapshot thread-sicher kopieren */
    display_snapshot_t snap;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return httpd_resp_sendstr(req, "{\"error\":\"busy\"}");
    }
    memcpy(&snap, &s_snap, sizeof(snap));
    xSemaphoreGive(s_mutex);

    /* ── Uhrzeit aus System-RTC ─────────────────────────────────────── */
    char time_str[12] = "--:--:--";
    char date_str[14] = "--.--.----";
    struct tm tinfo;
    if (womo_time_get(&tinfo) == ESP_OK && tinfo.tm_year >= (2024 - 1900)) {
        strftime(time_str, sizeof(time_str), "%H:%M:%S", &tinfo);
        strftime(date_str, sizeof(date_str), "%d.%m.%Y",  &tinfo);
    }

    /* ── Tag/Nacht-Theme bestimmen ──────────────────────────────────── */
    const char *theme = "day";
    if (tinfo.tm_year >= (2024 - 1900)) {
        int h = tinfo.tm_hour;
        theme = (h >= 7 && h < 20) ? "day" : "night";
    }

    /* ── Status-String ──────────────────────────────────────────────── */
    const char *status_key = "ok";
    if (snap.status_level == WOMO_DASH_STATUS_WARN)  status_key = "warn";
    if (snap.status_level == WOMO_DASH_STATUS_CRIT)  status_key = "crit";

    /* ── Drucktendenz-Symbol ─────────────────────────────────────────── */
    const char *trend_sym = "\xe2\x86\x92"; /* → */
    if (snap.sensor_valid && snap.sensor.bme680.valid) {
        const char *ts = snap.sensor.bme680.press_trend_state;
        if (strncmp(ts, "rise", 4) == 0)      trend_sym = "\xe2\x86\x97"; /* ↗ */
        else if (strncmp(ts, "fall", 4) == 0) trend_sym = "\xe2\x86\x98"; /* ↘ */
        else if (strncmp(ts, "fast_rise", 9) == 0) trend_sym = "\xe2\x86\x91"; /* ↑ */
        else if (strncmp(ts, "fast_fall", 9) == 0) trend_sym = "\xe2\x86\x93"; /* ↓ */
    }

    /* ── JSON aufbauen ──────────────────────────────────────────────── */
    /* Puffer: 2 KB reicht für alle Felder */
    static char buf[2048];
    int pos = 0;

#define JCAT(fmt, ...) do { \
    int _n = snprintf(buf + pos, sizeof(buf) - (size_t)pos, fmt, ##__VA_ARGS__); \
    if (_n > 0) pos += _n; \
} while(0)

    JCAT("{");
    JCAT("\"time\":\"%s\",\"date\":\"%s\",\"theme\":\"%s\",", time_str, date_str, theme);
    JCAT("\"status\":\"%s\",\"status_text\":\"%s\",", status_key, snap.status_text);
    JCAT("\"rs485_ok\":%s,", snap.rs485_ok ? "true" : "false");
    JCAT("\"location\":\"%s\",", snap.location);
    JCAT("\"press_trend\":\"%s\",", trend_sym);

    /* ── Power / Steuerung ──────────────────────────────────────────── */
    const womo_sensor_data_t *s = &snap.sensor;
    JCAT("\"power\":{\"pwr_12v_on\":%s,\"radio_on\":%s,\"ac_present\":%s},",
         (s->power.valid && s->power.pwr_12v_on) ? "true" : "false",
         (s->power.valid && s->power.radio_on)   ? "true" : "false",
         (s->power.valid && s->power.ac_present) ? "true" : "false");

    /* ── WiFi ───────────────────────────────────────────────────────── */
    if (snap.router_valid) {
        JCAT("\"wifi\":{\"connected\":%s,\"ssid\":\"%s\",\"signal_pct\":%u},",
             snap.wifi.connected ? "true" : "false",
             snap.wifi.ssid,
             snap.wifi.signal_percent);
        JCAT("\"lte\":{\"registered\":%s,\"operator\":\"%s\",\"signal_pct\":%u},",
             snap.lte.registered ? "true" : "false",
             snap.lte.operator_name,
             snap.lte.signal_percent);
    } else {
        JCAT("\"wifi\":{\"connected\":null,\"ssid\":\"\",\"signal_pct\":0},");
        JCAT("\"lte\":{\"registered\":null,\"operator\":\"\",\"signal_pct\":0},");
    }

    /* ── GPS ────────────────────────────────────────────────────────── */
    if (s->gps.valid) {
        JCAT("\"gps\":{\"valid\":true,\"lat\":%.6f,\"lon\":%.6f,"
             "\"alt_m\":%.1f,\"spd_kmh\":%.1f,\"sats\":%u},",
             s->gps.latitude, s->gps.longitude,
             s->gps.altitude_m, s->gps.speed_kmh,
             s->gps.satellites);
    } else {
        JCAT("\"gps\":{\"valid\":false},");
    }

    /* ── Außen-Sensor (BME680 outdoor) ──────────────────────────────── */
    if (s->bme680.valid) {
        JCAT("\"bme_out\":{\"valid\":true,\"temp_c\":%.2f,\"rh_pct\":%.2f,"
             "\"press_hpa\":%.1f},",
             s->bme680.temperature_c,
             s->bme680.humidity_percent,
             s->bme680.pressure_hpa);
    } else {
        JCAT("\"bme_out\":{\"valid\":false},");
    }

    /* ── Innen-Sensor (BME680 indoor) ───────────────────────────────── */
    if (s->bme680_indoor.valid) {
        JCAT("\"bme_in\":{\"valid\":true,\"temp_c\":%.2f,\"rh_pct\":%.2f,"
             "\"press_hpa\":%.1f,\"iaq\":%u,\"iaq_acc\":%u,"
             "\"eco2_ppm\":%.1f,\"bvoc_ppm\":%.2f},",
             s->bme680_indoor.temperature_c,
             s->bme680_indoor.humidity_percent,
             s->bme680_indoor.pressure_hpa,
             s->bme680_indoor.iaq,
             s->bme680_indoor.iaq_accuracy,
             s->bme680_indoor.eco2_ppm,
             s->bme680_indoor.bvoc_ppm);
    } else {
        JCAT("\"bme_in\":{\"valid\":false},");
    }

    /* ── IMU (BNO055) ───────────────────────────────────────────────── */
    if (s->bno.valid) {
        JCAT("\"imu\":{\"valid\":true,\"pitch_deg\":%.2f,\"roll_deg\":%.2f,"
             "\"heading_deg\":%.1f,\"direction\":\"%s\","
             "\"cal\":{\"sys\":%u,\"gyro\":%u,\"acc\":%u,\"mag\":%u}},",
             s->bno.pitch_deg, s->bno.roll_deg,
             s->bno.heading_deg, s->bno.direction,
             s->bno.cal_sys, s->bno.cal_gyro,
             s->bno.cal_accel, s->bno.cal_mag);
    } else {
        JCAT("\"imu\":{\"valid\":false},");
    }

    /* ── Tanks ──────────────────────────────────────────────────────── */
    if (s->tank.valid) {
        JCAT("\"tank\":{\"valid\":true,\"fresh_pct\":%u,\"grey_pct\":%u,"
             "\"fresh_rate_lh\":%.2f,\"grey_rate_lh\":%.2f,"
             "\"fresh_rest_h\":%.1f,\"grey_rest_h\":%.1f},",
             s->tank.tank1_percent,
             s->tank.tank2_percent,
             s->tank.tank1_rate1h,
             s->tank.tank2_rate1h,
             s->tank.tank1_rest_h,
             s->tank.tank2_rest_h);
    } else {
        JCAT("\"tank\":{\"valid\":false},");
    }

    /* ── Gas-Flaschen ───────────────────────────────────────────────── */
    if (s->gas.valid) {
        JCAT("\"gas\":{\"valid\":true,\"active_idx\":%d,\"net_kg\":%.3f,"
             "\"rate_kgph_1h\":%.3f,\"rest_h\":%.1f,"
             "\"net_a_kg\":%.3f,\"net_b_kg\":%.3f,"
             "\"pct\":%.1f,\"pct_a\":%.1f,\"pct_b\":%.1f},",
             s->gas.active_idx,
             s->gas.net_kg,
             s->gas.rate_kgph_1h,
             s->gas.rest_hours,
             s->gas.net_a_kg,
             s->gas.net_b_kg,
             s->gas.pct,
             s->gas.pct_a,
             s->gas.pct_b);
    } else {
        JCAT("\"gas\":{\"valid\":false},");
    }

    /* ── Batterien ──────────────────────────────────────────────────── */
    if (s->battery.valid) {
        JCAT("\"battery\":{\"valid\":true,\"bat1_v\":%.3f,\"bat2_v\":%.3f,"
             "\"nc1\":%s,\"nc2\":%s}",
             s->battery.battery1_v,
             s->battery.battery2_v,
             s->battery.nc1 ? "true" : "false",
             s->battery.nc2 ? "true" : "false");
    } else {
        JCAT("\"battery\":{\"valid\":false}");
    }

    JCAT("}");

    /* Sicherheitsabschluss */
    if (pos >= (int)sizeof(buf) - 1) {
        buf[sizeof(buf) - 1] = '\0';
        ESP_LOGW(TAG, "/api/status Puffer zu klein (%d >= %d)", pos, (int)sizeof(buf));
    }

#undef JCAT

    return httpd_resp_sendstr(req, buf);
}

/* ── Handler: OPTIONS (CORS-Preflight) ─────────────────────────────────── */
static esp_err_t handle_options(httpd_req_t *req)
{
    set_cors_headers(req);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    return httpd_resp_send(req, NULL, 0);
}

/* ── Handler registrieren ───────────────────────────────────────────────── */
static esp_err_t register_handlers(httpd_handle_t server)
{
    static const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = handle_index
    };
    static const httpd_uri_t display = {
        .uri = "/display", .method = HTTP_GET, .handler = handle_index
    };
    static const httpd_uri_t api_status = {
        .uri = "/api/status", .method = HTTP_GET, .handler = handle_status
    };
    static const httpd_uri_t opts = {
        .uri = "/*", .method = HTTP_OPTIONS, .handler = handle_options
    };

    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &root),
                        TAG, "register / failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &display),
                        TAG, "register /display failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &api_status),
                        TAG, "register /api/status failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &opts),
                        TAG, "register OPTIONS failed");
    return ESP_OK;
}

/* ── Öffentliche API ─────────────────────────────────────────────────────── */

esp_err_t womo_display_http_init(void)
{
    if (s_mutex) {
        return ESP_OK; /* bereits initialisiert */
    }
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Mutex konnte nicht erstellt werden");
        return ESP_ERR_NO_MEM;
    }
    memset(&s_snap, 0, sizeof(s_snap));
    strncpy(s_snap.status_text, "OK", sizeof(s_snap.status_text) - 1);
    strncpy(s_snap.location, "Standort...", sizeof(s_snap.location) - 1);
    s_snap.rs485_ok = false;
    ESP_LOGI(TAG, "Initialisiert");
    return ESP_OK;
}

esp_err_t womo_display_http_start(void)
{
    if (!s_mutex) {
        ESP_LOGE(TAG, "Nicht initialisiert – womo_display_http_init() zuerst aufrufen");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_server) {
        return ESP_OK; /* bereits gestartet */
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port        = WOMO_DISPLAY_HTTP_PORT;
    cfg.uri_match_fn       = httpd_uri_match_wildcard;
    cfg.max_uri_handlers   = 8;
    cfg.stack_size         = 8192;
    cfg.task_priority      = 4;
    cfg.lru_purge_enable   = true;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &cfg),
                        TAG, "httpd_start fehlgeschlagen");

    esp_err_t err = register_handlers(s_server);
    if (err != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Dashboard-Server gestartet auf Port %d", WOMO_DISPLAY_HTTP_PORT);
    return ESP_OK;
}

esp_err_t womo_display_http_stop(void)
{
    if (!s_server) {
        return ESP_OK;
    }
    esp_err_t err = httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "Dashboard-Server gestoppt");
    return err;
}

void womo_display_http_update_sensor(const womo_sensor_data_t *data)
{
    if (!data || !s_mutex) {
        return;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    memcpy(&s_snap.sensor, data, sizeof(s_snap.sensor));
    s_snap.sensor_valid = true;
    xSemaphoreGive(s_mutex);
}

void womo_display_http_update_router(const womo_router_wifi_status_t *wifi,
                                     const womo_router_lte_status_t  *lte,
                                     const womo_router_ap_status_t   *ap)
{
    if (!s_mutex) {
        return;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    if (wifi) memcpy(&s_snap.wifi, wifi, sizeof(s_snap.wifi));
    if (lte)  memcpy(&s_snap.lte,  lte,  sizeof(s_snap.lte));
    if (ap)   memcpy(&s_snap.ap,   ap,   sizeof(s_snap.ap));
    s_snap.router_valid = true;
    xSemaphoreGive(s_mutex);
}

void womo_display_http_update_status(womo_dash_status_t level,
                                     const char        *text,
                                     bool               rs485_ok,
                                     const char        *location)
{
    if (!s_mutex) {
        return;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    s_snap.status_level = level;
    s_snap.rs485_ok     = rs485_ok;
    if (text) {
        strncpy(s_snap.status_text, text, sizeof(s_snap.status_text) - 1);
        s_snap.status_text[sizeof(s_snap.status_text) - 1] = '\0';
    }
    if (location) {
        strncpy(s_snap.location, location, sizeof(s_snap.location) - 1);
        s_snap.location[sizeof(s_snap.location) - 1] = '\0';
    }
    xSemaphoreGive(s_mutex);
}
