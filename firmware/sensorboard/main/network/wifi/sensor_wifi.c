/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * WiFi STA-Modul – verbindet sich mit dem RUTX11 Router.
 *
 * - Credentials aus NVS (Fallback: SENSOR_WIFI_DEFAULT_SSID/PASS)
 * - Auto-Reconnect mit Retry-Pause
 * - mDNS: womo-sensor.local
 */

#include "network/wifi/sensor_wifi.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sensor_config.h"

#include <string.h>

#define TAG "sensor_wifi"

// Event-Group Bits
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static esp_netif_t       *s_sta_netif        = NULL;
static bool               s_initialized      = false;
static int                s_retry_count       = 0;
static char               s_ip_str[16]        = "";
// Eco-Modus: Verbindung bewusst getrennt, keine Reconnects. Kanal bleibt für ESP-NOW stehen.
static volatile bool      s_paused            = false;
static uint8_t            s_pause_channel     = 0;
static sensor_wifi_auth_fail_cb_t s_auth_fail_cb = NULL;

// Probeverbindung mit neuen Zugangsdaten (sensor_wifi_set_credentials):
// erst nach GOT_IP ins NVS, nach SENSOR_WIFI_RETRY_MAX Fehlversuchen zurück
// auf die gespeicherten Daten.
static portMUX_TYPE       s_trial_lock        = portMUX_INITIALIZER_UNLOCKED;
static volatile bool      s_trial_active      = false;
static char               s_trial_ssid[33]    = "";
static char               s_trial_pass[65]    = "";

// ── NVS Credentials ─────────────────────────────────────────────────────

static esp_err_t wifi_load_credentials(char *ssid, size_t ssid_sz,
                                        char *pass, size_t pass_sz)
{
    // Defaults
    strlcpy(ssid, SENSOR_WIFI_DEFAULT_SSID, ssid_sz);
    strlcpy(pass, SENSOR_WIFI_DEFAULT_PASS, pass_sz);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(SENSOR_WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS: keine WiFi-Config gespeichert, nutze Defaults");
        return ESP_OK;
    }

    size_t len = ssid_sz;
    if (nvs_get_str(nvs, SENSOR_WIFI_NVS_KEY_SSID, ssid, &len) == ESP_OK) {
        ESP_LOGI(TAG, "NVS: SSID geladen: %s", ssid);
    }
    len = pass_sz;
    if (nvs_get_str(nvs, SENSOR_WIFI_NVS_KEY_PASS, pass, &len) == ESP_OK) {
        ESP_LOGI(TAG, "NVS: Passwort geladen (len=%d)", (int)strlen(pass));
    }
    nvs_close(nvs);
    return ESP_OK;
}

static esp_err_t wifi_save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(SENSOR_WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }
    if (ssid) {
        nvs_set_str(nvs, SENSOR_WIFI_NVS_KEY_SSID, ssid);
    }
    if (pass) {
        nvs_set_str(nvs, SENSOR_WIFI_NVS_KEY_PASS, pass);
    }
    err = nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "NVS: Credentials gespeichert");
    return err;
}

static void wifi_apply_config(const char *ssid, const char *pass)
{
    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = (strlen(pass) > 0)
                                             ? WIFI_AUTH_WPA2_PSK
                                             : WIFI_AUTH_OPEN;
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
}

// ── Event Handler ───────────────────────────────────────────────────────

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA gestartet, verbinde...");
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_ip_str[0] = '\0';
        if (s_paused) {
            // Kein Reconnect (Kanalsuche würde ESP-NOW unterbrechen), Kanal festhalten
            if (s_pause_channel != 0) {
                esp_wifi_set_channel(s_pause_channel, WIFI_SECOND_CHAN_NONE);
            }
            ESP_LOGI(TAG, "WLAN pausiert (Eco), ESP-NOW bleibt auf Kanal %u", s_pause_channel);
        } else if (s_trial_active && s_retry_count >= SENSOR_WIFI_RETRY_MAX) {
            // Neue Zugangsdaten funktionieren nicht → gespeicherte wieder aktivieren
            char ssid[33] = {0};
            char pass[65] = {0};
            wifi_load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
            ESP_LOGW(TAG, "Neue SSID \"%s\" nicht erreichbar – zurück auf \"%s\"",
                     s_trial_ssid, ssid);
            s_trial_active = false;
            wifi_apply_config(ssid, pass);
            s_retry_count = 0;
            esp_wifi_connect();
        } else if (s_retry_count < SENSOR_WIFI_RETRY_MAX) {
            s_retry_count++;
            ESP_LOGW(TAG, "Verbindung verloren, Retry %d/%d",
                     s_retry_count, SENSOR_WIFI_RETRY_MAX);
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "Max Retries erreicht – Passwort falsch?");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            // Callback aufrufen (z.B. ESP-NOW Passwort-Anfrage ans Display)
            if (s_auth_fail_cb) {
                ESP_LOGI(TAG, "→ Auth-Failure-Callback wird aufgerufen");
                s_auth_fail_cb();
            }
            // Nach Pause erneut versuchen (evtl. mit neuem Passwort)
            ESP_LOGI(TAG, "Pause %d ms, dann neuer Versuch...", SENSOR_WIFI_RETRY_PAUSE_MS);
            vTaskDelay(pdMS_TO_TICKS(SENSOR_WIFI_RETRY_PAUSE_MS));
            s_retry_count = 0;
            if (!s_paused) esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "✓ Verbunden! IP: %s", s_ip_str);
        s_retry_count = 0;
        wifi_second_chan_t second;
        esp_wifi_get_channel(&s_pause_channel, &second);   // Kanal für eine spätere Pause merken
        if (s_trial_active) {
            char ssid[33], pass[65];
            taskENTER_CRITICAL(&s_trial_lock);
            strlcpy(ssid, s_trial_ssid, sizeof(ssid));
            strlcpy(pass, s_trial_pass, sizeof(pass));
            s_trial_active = false;
            taskEXIT_CRITICAL(&s_trial_lock);
            ESP_LOGI(TAG, "Neue Zugangsdaten bestätigt: SSID=\"%s\"", ssid);
            wifi_save_credentials(ssid, pass);
        }
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ── mDNS ────────────────────────────────────────────────────────────────

static esp_err_t wifi_mdns_init(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS init fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }
    mdns_hostname_set(SENSOR_WIFI_HOSTNAME);
    mdns_instance_name_set(SENSOR_WIFI_MDNS_INSTANCE);

    // HTTP-Service bekanntgeben
    mdns_service_add(NULL, "_http", "_tcp", SENSOR_HTTP_PORT, NULL, 0);

    ESP_LOGI(TAG, "mDNS: %s.local → :%d", SENSOR_WIFI_HOSTNAME, SENSOR_HTTP_PORT);
    return ESP_OK;
}

// ── Public API ──────────────────────────────────────────────────────────

esp_err_t sensor_wifi_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        return ESP_ERR_NO_MEM;
    }

    // Netif + Event-Loop (nur einmal, idempotent)
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) {
        return ESP_FAIL;
    }

    // Hostname setzen (vor DHCP)
    esp_netif_set_hostname(s_sta_netif, SENSOR_WIFI_HOSTNAME);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    // Event-Handler registrieren
    esp_event_handler_instance_t inst_wifi, inst_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                         &wifi_event_handler, NULL, &inst_wifi);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                         &wifi_event_handler, NULL, &inst_ip);

    // Credentials laden
    char ssid[33] = {0};
    char pass[65] = {0};
    wifi_load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));

    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_apply_config(ssid, pass);
    esp_wifi_start();

    ESP_LOGI(TAG, "WiFi STA gestartet: SSID=\"%s\"", ssid);

    // mDNS starten
    wifi_mdns_init();

    s_initialized = true;
    return ESP_OK;
}

esp_err_t sensor_wifi_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }
    mdns_free();
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_sta_netif) {
        esp_netif_destroy_default_wifi(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    s_ip_str[0] = '\0';
    s_initialized = false;
    ESP_LOGI(TAG, "WiFi deinitialisiert");
    return ESP_OK;
}

bool sensor_wifi_is_connected(void)
{
    return (s_ip_str[0] != '\0');
}

esp_err_t sensor_wifi_set_credentials(const char *ssid, const char *pass)
{
    if (!s_initialized) {
        // Ohne laufendes WLAN nichts zu testen → direkt speichern
        return wifi_save_credentials(ssid, pass);
    }

    char new_ssid[33] = {0};
    char new_pass[65] = {0};
    wifi_load_credentials(new_ssid, sizeof(new_ssid), new_pass, sizeof(new_pass));
    bool same_as_saved = (!ssid || strcmp(ssid, new_ssid) == 0) &&
                         (!pass || strcmp(pass, new_pass) == 0);
    if (ssid) strlcpy(new_ssid, ssid, sizeof(new_ssid));
    if (pass) strlcpy(new_pass, pass, sizeof(new_pass));

    taskENTER_CRITICAL(&s_trial_lock);
    bool same_as_trial = s_trial_active &&
                         strcmp(new_ssid, s_trial_ssid) == 0 &&
                         strcmp(new_pass, s_trial_pass) == 0;
    taskEXIT_CRITICAL(&s_trial_lock);

    // Wiederholtes Kommando (Display sendet bis zum ACK erneut): nicht neu starten
    if (same_as_trial || (!s_trial_active && same_as_saved && sensor_wifi_is_connected())) {
        ESP_LOGI(TAG, "Credentials unverändert (SSID=\"%s\")", new_ssid);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Teste neue Credentials: SSID=\"%s\"", new_ssid);
    taskENTER_CRITICAL(&s_trial_lock);
    strlcpy(s_trial_ssid, new_ssid, sizeof(s_trial_ssid));
    strlcpy(s_trial_pass, new_pass, sizeof(s_trial_pass));
    s_trial_active = true;
    taskEXIT_CRITICAL(&s_trial_lock);

    esp_wifi_disconnect();
    wifi_apply_config(new_ssid, new_pass);
    s_retry_count = 0;
    esp_wifi_connect();
    return ESP_OK;
}

bool sensor_wifi_is_trial_active(void)
{
    return s_trial_active;
}

void sensor_wifi_set_paused(bool paused)
{
    if (!s_initialized || s_paused == paused) return;
    s_paused = paused;
    if (paused) {
        ESP_LOGI(TAG, "Eco: WLAN-Verbindung wird getrennt");
        esp_wifi_disconnect();
    } else {
        ESP_LOGI(TAG, "Eco beendet: verbinde WLAN neu");
        s_retry_count = 0;
        esp_wifi_connect();
    }
}

bool sensor_wifi_is_paused(void)
{
    return s_paused;
}

const char *sensor_wifi_get_ip_str(void)
{
    return s_ip_str;
}

bool sensor_wifi_get_ap_info(char *ssid_out, size_t ssid_len, int8_t *rssi_out)
{
    if (ssid_out && ssid_len > 0) ssid_out[0] = '\0';
    if (rssi_out) *rssi_out = 0;
    if (!sensor_wifi_is_connected()) return false;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return false;
    if (ssid_out && ssid_len > 0) {
        strlcpy(ssid_out, (const char *)ap.ssid, ssid_len);
    }
    if (rssi_out) *rssi_out = ap.rssi;
    return true;
}

void sensor_wifi_set_auth_fail_cb(sensor_wifi_auth_fail_cb_t cb)
{
    s_auth_fail_cb = cb;
}
