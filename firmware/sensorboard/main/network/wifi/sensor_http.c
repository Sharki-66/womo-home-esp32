/**
 * HTTP-Server für WoMoHome Sensorboard.
 *
 * - SPIFFS mounten (Partition "storage" → /spiffs)
 * - /             → womo_dashboard.html (800×480 Gesamt-Dashboard)
 * - /horizon.html → Parkhilfe (Künstlicher Horizont)
 * - /api/imu      → JSON mit IMU-Snapshot (BNO055)
 * - /api/status   → JSON mit allen Sensordaten
 * - /...          → statische Dateien aus SPIFFS
 */

#include "network/wifi/sensor_http.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "sensor_config.h"
#include "sensors/bno055_sensor.h"
#include "sensors/bme680_sensor.h"
#include "sensors/hx711_sensor.h"
#include "sensors/analog_sensor.h"
#include "cJSON.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define TAG "sensor_http"

static httpd_handle_t s_server   = NULL;
static bool           s_spiffs_mounted = false;

// ── SPIFFS ──────────────────────────────────────────────────────────────

static esp_err_t spiffs_mount(void)
{
    if (s_spiffs_mounted) {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path              = SENSOR_SPIFFS_BASE_PATH,
        .partition_label        = SENSOR_SPIFFS_PARTITION,
        .max_files              = SENSOR_SPIFFS_MAX_FILES,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(SENSOR_SPIFFS_PARTITION, &total, &used);
    ESP_LOGI(TAG, "SPIFFS gemountet: %u/%u Bytes belegt", (unsigned)used, (unsigned)total);
    s_spiffs_mounted = true;
    return ESP_OK;
}

// ── Content-Type Erkennung ──────────────────────────────────────────────

static const char *content_type_for(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css")  == 0) return "text/css";
    if (strcmp(ext, ".js")   == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".ico")  == 0) return "image/x-icon";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".svg")  == 0) return "image/svg+xml";
    return "application/octet-stream";
}

// ── Hilfsfunktionen ─────────────────────────────────────────────────────

static void json_add_float_safe(cJSON *obj, const char *key, float val)
{
    if (isnan(val) || isinf(val)) {
        cJSON_AddNullToObject(obj, key);
    } else {
        cJSON_AddNumberToObject(obj, key, (double)val);
    }
}

// ── /api/imu Handler ────────────────────────────────────────────────────

static esp_err_t imu_get_handler(httpd_req_t *req)
{
    // Auto-Fast-Mode: Jeder /api/imu-Aufruf verlängert das 2s-Fenster.
    // Solange horizon.html pollt (alle 500ms), bleibt der BNO055 schnell.
    // Wird die Seite geschlossen → kein Poll mehr → Fast-Mode läuft nach 2s aus.
    bno055_app_request_fast(2000, 200);

    bno055_imu_snapshot_t snap;
    bool ok = bno055_imu_get_snapshot(&snap);

    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "{\"ok\":%s,"
         "\"yaw_deg\":%.1f,"
         "\"roll_deg\":%.2f,"
         "\"pitch_deg\":%.2f,"
         "\"cal\":[%u,%u,%u,%u],"
         "\"calibrated\":%s}",
        (ok && snap.valid) ? "true" : "false",
        snap.yaw_deg, snap.roll_deg, snap.pitch_deg,
        snap.cal_sys, snap.cal_gyro, snap.cal_accel, snap.cal_mag,
        snap.calibrated ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, buf, len);
}

// ── /api/status Handler ─────────────────────────────────────────────────

static esp_err_t status_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    /* Zeitstempel */
    time_t now_s = 0;
    time(&now_s);
    cJSON_AddNumberToObject(root, "ts", (double)now_s);

    /* ── BNO055 IMU ──────────────────────────────────────────────────── */
    bno055_imu_snapshot_t imu = {0};
    bool imu_ok = bno055_imu_get_snapshot(&imu) && imu.valid;
    cJSON *j_imu = cJSON_AddObjectToObject(root, "imu");
    cJSON_AddBoolToObject(j_imu, "valid", imu_ok);
    if (imu_ok) {
        cJSON_AddNumberToObject(j_imu, "yaw_deg",   (double)imu.yaw_deg);
        cJSON_AddNumberToObject(j_imu, "pitch_deg", (double)imu.pitch_deg);
        cJSON_AddNumberToObject(j_imu, "roll_deg",  (double)imu.roll_deg);
        cJSON_AddBoolToObject(j_imu, "calibrated",  imu.calibrated);
        cJSON *cal = cJSON_AddObjectToObject(j_imu, "cal");
        cJSON_AddNumberToObject(cal, "sys",  imu.cal_sys);
        cJSON_AddNumberToObject(cal, "gyro", imu.cal_gyro);
        cJSON_AddNumberToObject(cal, "acc",  imu.cal_accel);
        cJSON_AddNumberToObject(cal, "mag",  imu.cal_mag);
    }

    /* ── BME680 ──────────────────────────────────────────────────────── */
    bme680_snapshot_t bme = {0};
    bool bme_ok = (bme680_app_get_snapshot(&bme) == ESP_OK);

    /* indoor (0x76) */
    cJSON *j_in = cJSON_AddObjectToObject(root, "bme_in");
    cJSON_AddBoolToObject(j_in, "valid", bme_ok && bme.indoor.valid);
    if (bme_ok && bme.indoor.valid) {
        cJSON_AddNumberToObject(j_in, "temp_c",    (double)bme.indoor.temperature_c);
        cJSON_AddNumberToObject(j_in, "rh_pct",    (double)bme.indoor.humidity_pct);
        cJSON_AddNumberToObject(j_in, "press_hpa", (double)bme.indoor.pressure_hpa);
        if (bme.indoor.iaq_valid) {
            cJSON_AddNumberToObject(j_in, "iaq",      (double)bme.indoor.iaq);
            cJSON_AddNumberToObject(j_in, "iaq_acc",  bme.indoor.iaq_accuracy);
            json_add_float_safe(j_in, "eco2_ppm",     bme.indoor.eco2_ppm);
            json_add_float_safe(j_in, "bvoc_ppm",     bme.indoor.bvoc_ppm);
        }
    }

    /* outdoor (0x77) */
    cJSON *j_out = cJSON_AddObjectToObject(root, "bme_out");
    cJSON_AddBoolToObject(j_out, "valid", bme_ok && bme.outdoor.valid);
    if (bme_ok && bme.outdoor.valid) {
        cJSON_AddNumberToObject(j_out, "temp_c",    (double)bme.outdoor.temperature_c);
        cJSON_AddNumberToObject(j_out, "rh_pct",    (double)bme.outdoor.humidity_pct);
        cJSON_AddNumberToObject(j_out, "press_hpa", (double)bme.outdoor.pressure_hpa);
        if (bme.outdoor.iaq_valid) {
            cJSON_AddNumberToObject(j_out, "iaq",     (double)bme.outdoor.iaq);
            cJSON_AddNumberToObject(j_out, "iaq_acc", bme.outdoor.iaq_accuracy);
            json_add_float_safe(j_out, "eco2_ppm",    bme.outdoor.eco2_ppm);
            json_add_float_safe(j_out, "bvoc_ppm",    bme.outdoor.bvoc_ppm);
        }
        /* Luftdrucktrend */
        if (bme.outdoor.press_trend_1h_valid) {
            cJSON_AddNumberToObject(j_out, "trend_1h_hpa_h", (double)bme.outdoor.press_trend_1h_hpa_h);
            cJSON_AddNumberToObject(j_out, "trend_1h_state", (int)bme.outdoor.press_trend_1h_state);
        }
    }

    /* ── HX711 Gaswaage ──────────────────────────────────────────────── */
    hx711_snapshot_t hx = {0};
    bool hx_ok = (hx711_app_get_snapshot(&hx) == ESP_OK);
    cJSON *j_hx = cJSON_AddObjectToObject(root, "hx");
    bool hx_nc = !hx_ok || (!hx.valid_a && !hx.valid_b);
    cJSON_AddBoolToObject(j_hx, "valid", hx_ok && !hx_nc);
    cJSON_AddBoolToObject(j_hx, "nc",    hx_nc);
    if (hx_ok) {
        if (hx.valid_a) cJSON_AddNumberToObject(j_hx, "a_kg", (double)hx.kg_a);
        if (hx.valid_b) cJSON_AddNumberToObject(j_hx, "b_kg", (double)hx.kg_b);
        if (hx.valid_a && hx.valid_b)
            cJSON_AddNumberToObject(j_hx, "sum_kg", (double)(hx.kg_a + hx.kg_b));
    }

    /* ── Batterie (ADC) ──────────────────────────────────────────────── */
    int mv = 0;
    cJSON *j_bat = cJSON_AddObjectToObject(root, "bat");
    bool b1_ok  = (analog_read_mv(SENSOR_BATT1_ADC_CHANNEL, &mv) == ESP_OK) && (mv > 1000);
    cJSON_AddNumberToObject(j_bat, "b1_v", b1_ok ? mv / 1000.0 : 0.0);
    cJSON_AddBoolToObject(j_bat, "nc1", !b1_ok);
    bool b2_ok  = (analog_read_mv(SENSOR_BATT2_ADC_CHANNEL, &mv) == ESP_OK) && (mv > 1000);
    cJSON_AddNumberToObject(j_bat, "b2_v", b2_ok ? mv / 1000.0 : 0.0);
    cJSON_AddBoolToObject(j_bat, "nc2", !b2_ok);

    /* ── Tanks (ADC) ─────────────────────────────────────────────────── */
    cJSON *j_tank = cJSON_AddObjectToObject(root, "tank");
    bool t1_ok = (analog_read_mv(SENSOR_TANK1_ADC_CHANNEL, &mv) == ESP_OK);
    int t1_pct  = t1_ok ? ((mv < 0 ? 0 : mv > 1000 ? 1000 : mv) * 100 / 1000) : 0;
    bool t2_ok  = (analog_read_mv(SENSOR_TANK2_ADC_CHANNEL, &mv) == ESP_OK);
    int t2_pct  = t2_ok ? ((mv < 0 ? 0 : mv > 1000 ? 1000 : mv) * 100 / 1000) : 0;
    cJSON_AddNumberToObject(j_tank, "t1_pct", t1_pct);
    cJSON_AddNumberToObject(j_tank, "t2_pct", t2_pct);
    cJSON_AddBoolToObject(j_tank, "nc1", !t1_ok);
    cJSON_AddBoolToObject(j_tank, "nc2", !t2_ok);
    cJSON_AddNumberToObject(j_tank, "t1_l", t1_ok ? (t1_pct / 100.0) * 100.0 : 0.0);
    cJSON_AddNumberToObject(j_tank, "t2_l", t2_ok ? (t2_pct / 100.0) *  92.0 : 0.0);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t ret = httpd_resp_sendstr(req, json_str);
    free(json_str);
    return ret;
}

// ── Statische Datei Handler ─────────────────────────────────────────────

static esp_err_t static_file_handler(httpd_req_t *req)
{
    const char *uri = req->uri;

    // "/" → "/womo_dashboard.html"
    if (strcmp(uri, "/") == 0) {
        uri = "/womo_dashboard.html";
    }

    // Query-String abschneiden
    char clean_uri[128];
    strlcpy(clean_uri, uri, sizeof(clean_uri));
    char *q = strchr(clean_uri, '?');
    if (q) *q = '\0';

    // SPIFFS-Pfad zusammenbauen
    char filepath[160];
    snprintf(filepath, sizeof(filepath), "%s%s", SENSOR_SPIFFS_BASE_PATH, clean_uri);

    struct stat st;
    if (stat(filepath, &st) != 0) {
        ESP_LOGW(TAG, "404: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    FILE *f = fopen(filepath, "r");
    if (!f) {
        ESP_LOGE(TAG, "Kann Datei nicht öffnen: %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Read error");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type_for(clean_uri));

    char chunk[512];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);   // Chunked-Ende
    return ESP_OK;
}

// ── Public API ──────────────────────────────────────────────────────────

esp_err_t sensor_http_start(void)
{
    if (s_server) {
        ESP_LOGW(TAG, "HTTP-Server läuft bereits");
        return ESP_OK;
    }

    // SPIFFS mounten
    esp_err_t err = spiffs_mount();
    if (err != ESP_OK) {
        return err;
    }

    // HTTP-Server konfigurieren
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port        = SENSOR_HTTP_PORT;
    config.uri_match_fn       = httpd_uri_match_wildcard;
    config.max_open_sockets   = 4;
    config.lru_purge_enable   = true;
    config.max_uri_handlers   = 4;  /* /api/imu, /api/status, /*, OPTIONS */

    err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    // Routen registrieren (spezifisch zuerst)
    static const httpd_uri_t uri_imu = {
        .uri      = "/api/imu",
        .method   = HTTP_GET,
        .handler  = imu_get_handler,
    };
    httpd_register_uri_handler(s_server, &uri_imu);

    static const httpd_uri_t uri_status = {
        .uri      = "/api/status",
        .method   = HTTP_GET,
        .handler  = status_get_handler,
    };
    httpd_register_uri_handler(s_server, &uri_status);

    static const httpd_uri_t uri_static = {
        .uri      = "/*",
        .method   = HTTP_GET,
        .handler  = static_file_handler,
    };
    httpd_register_uri_handler(s_server, &uri_static);

    ESP_LOGI(TAG, "✓ HTTP-Server gestartet auf Port %d", SENSOR_HTTP_PORT);
    return ESP_OK;
}

esp_err_t sensor_http_stop(void)
{
    if (!s_server) {
        return ESP_OK;
    }
    httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "HTTP-Server gestoppt");
    return ESP_OK;
}

bool sensor_http_is_running(void)
{
    return (s_server != NULL);
}
