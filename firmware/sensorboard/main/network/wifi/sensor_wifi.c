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

// ── Event Handler ───────────────────────────────────────────────────────

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA gestartet, verbinde...");
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_ip_str[0] = '\0';
        if (s_retry_count < SENSOR_WIFI_RETRY_MAX) {
            s_retry_count++;
            ESP_LOGW(TAG, "Verbindung verloren, Retry %d/%d",
                     s_retry_count, SENSOR_WIFI_RETRY_MAX);
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "Max Retries erreicht, Pause %d ms",
                     SENSOR_WIFI_RETRY_PAUSE_MS);
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            // Reconnect-Task wird nach Pause neu versuchen
            vTaskDelay(pdMS_TO_TICKS(SENSOR_WIFI_RETRY_PAUSE_MS));
            s_retry_count = 0;
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "✓ Verbunden! IP: %s", s_ip_str);
        s_retry_count = 0;
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

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = (strlen(pass) > 0)
                                             ? WIFI_AUTH_WPA2_PSK
                                             : WIFI_AUTH_OPEN;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
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
    esp_err_t err = wifi_save_credentials(ssid, pass);
    if (err != ESP_OK) {
        return err;
    }

    if (s_initialized) {
        ESP_LOGI(TAG, "Credentials geändert, reconnect...");
        esp_wifi_disconnect();

        char new_ssid[33] = {0};
        char new_pass[65] = {0};
        wifi_load_credentials(new_ssid, sizeof(new_ssid),
                               new_pass, sizeof(new_pass));

        wifi_config_t wifi_config = {0};
        strlcpy((char *)wifi_config.sta.ssid, new_ssid, sizeof(wifi_config.sta.ssid));
        strlcpy((char *)wifi_config.sta.password, new_pass, sizeof(wifi_config.sta.password));
        wifi_config.sta.threshold.authmode = (strlen(new_pass) > 0)
                                                 ? WIFI_AUTH_WPA2_PSK
                                                 : WIFI_AUTH_OPEN;
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        s_retry_count = 0;
        esp_wifi_connect();
    }
    return ESP_OK;
}

const char *sensor_wifi_get_ip_str(void)
{
    return s_ip_str;
}
