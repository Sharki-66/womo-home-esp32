/**
 * HTTP-Server für Parkhilfe (Künstlicher Horizont).
 *
 * - SPIFFS mounten (Partition "storage" → /spiffs)
 * - /api/imu  → JSON mit IMU-Snapshot
 * - /         → horizon.html
 * - /...       → statische Dateien aus SPIFFS
 */

#include "network/wifi/sensor_http.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "sensor_config.h"
#include "sensors/bno055_sensor.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

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

// ── Statische Datei Handler ─────────────────────────────────────────────

static esp_err_t static_file_handler(httpd_req_t *req)
{
    const char *uri = req->uri;

    // "/" → "/horizon.html"
    if (strcmp(uri, "/") == 0) {
        uri = "/horizon.html";
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
    config.server_port      = SENSOR_HTTP_PORT;
    config.uri_match_fn     = httpd_uri_match_wildcard;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;

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
