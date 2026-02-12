/*
 * WoMo WiFi Manager - Implementation
 */

#include "womo_wifi.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "womo_wifi";

// Event group for WiFi connection
static EventGroupHandle_t s_wifi_event_group = NULL;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// WiFi state
static womo_wifi_status_t current_status = WOMO_WIFI_DISCONNECTED;
static uint8_t retry_count = 0;
static uint8_t max_retry_count = 5;
static bool auto_reconnect_enabled = true;
static char last_error[64] = "No error";
static esp_netif_t *sta_netif = NULL;
static TaskHandle_t s_wifi_scan_task = NULL;
static womo_wifi_scan_callback_t s_wifi_scan_callback = NULL;
static void *s_wifi_scan_user_data = NULL;
// Persistenz für bis zu 50 bekannte SSIDs mit Passwort
#define WIFI_KNOWN_MAX 50
typedef struct {
    char ssid[33];
    char pwd[64];
} wifi_known_entry_t;

static wifi_known_entry_t s_known_list[WIFI_KNOWN_MAX] = {0};
static size_t s_known_count = 0;
static bool s_known_loaded = false;

static esp_err_t wifi_nvs_load_known(wifi_known_entry_t *list, size_t *count)
{
    if (!list || !count) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open("wifi_cfg", NVS_READONLY, &h);
    if (err != ESP_OK) {
        *count = 0;
        return err;
    }

    uint8_t cnt = 0;
    err = nvs_get_u8(h, "known_count", &cnt);
    if (err != ESP_OK) {
        *count = 0;
        nvs_close(h);
        return ESP_OK;
    }
    if (cnt > WIFI_KNOWN_MAX) cnt = WIFI_KNOWN_MAX;

    size_t blob_size = cnt * sizeof(wifi_known_entry_t);
    if (cnt > 0) {
        err = nvs_get_blob(h, "known_list", list, &blob_size);
        if (err != ESP_OK) {
            cnt = 0;
        }
    }
    *count = cnt;
    nvs_close(h);
    return ESP_OK;
}

static void wifi_nvs_save_known(const wifi_known_entry_t *list, size_t count)
{
    if (!list || count == 0) return;
    if (count > WIFI_KNOWN_MAX) count = WIFI_KNOWN_MAX;

    nvs_handle_t h;
    if (nvs_open("wifi_cfg", NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_u8(h, "known_count", (uint8_t)count);
    nvs_set_blob(h, "known_list", list, count * sizeof(wifi_known_entry_t));
    nvs_commit(h);
    nvs_close(h);
}

static int wifi_known_find(const wifi_known_entry_t *list, size_t count, const char *ssid)
{
    if (!list || !ssid) return -1;
    for (size_t i = 0; i < count; i++) {
        if (strncmp(list[i].ssid, ssid, sizeof(list[i].ssid)) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void wifi_known_promote(wifi_known_entry_t *list, size_t *count, const char *ssid, const char *pwd)
{
    if (!list || !count || !ssid || !pwd || ssid[0] == '\0' || pwd[0] == '\0') return;

    size_t cnt = *count;
    int idx = wifi_known_find(list, cnt, ssid);

    wifi_known_entry_t new_entry = {0};
    strncpy(new_entry.ssid, ssid, sizeof(new_entry.ssid) - 1);
    strncpy(new_entry.pwd, pwd, sizeof(new_entry.pwd) - 1);

    // Platz schaffen: Eintrag an die Spitze, Rest eins nach hinten
    if (idx >= 0) {
        for (int i = idx; i > 0; i--) {
            list[i] = list[i - 1];
        }
        list[0] = new_entry;
    } else {
        if (cnt < WIFI_KNOWN_MAX) {
            cnt++;
        }
        for (size_t i = cnt - 1; i > 0; i--) {
            list[i] = list[i - 1];
        }
        list[0] = new_entry;
    }
    *count = cnt;
}

static void wifi_known_ensure_loaded(void)
{
    if (!s_known_loaded) {
        (void)wifi_nvs_load_known(s_known_list, &s_known_count);
        s_known_loaded = true;
    }
}

static void wifi_scan_task(void *arg);

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started, connecting...");
        esp_wifi_connect();
        current_status = WOMO_WIFI_CONNECTING;
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGW(TAG, "Disconnected from WiFi (reason: %d)", disconnected->reason);
        
        current_status = WOMO_WIFI_DISCONNECTED;
        
        if (auto_reconnect_enabled && (max_retry_count == 0 || retry_count < max_retry_count)) {
            esp_wifi_connect();
            retry_count++;
            current_status = WOMO_WIFI_CONNECTING;
            ESP_LOGI(TAG, "Retry %d/%d...", retry_count, max_retry_count);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            current_status = WOMO_WIFI_ERROR;
            snprintf(last_error, sizeof(last_error), "Connection failed after %d retries", retry_count);
            ESP_LOGE(TAG, "%s", last_error);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        retry_count = 0;
        current_status = WOMO_WIFI_CONNECTED;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        snprintf(last_error, sizeof(last_error), "Connected successfully");
    }
}

esp_err_t womo_wifi_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi module");
    
    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");
    
    // Create event group
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    
    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop if not exists
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create default WiFi station
    sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create network interface");
        return ESP_FAIL;
    }

    // Sprechender Hostname für DHCP-Lease-Erkennung
    esp_netif_set_hostname(sta_netif, "WoMo-Display");
    
    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    
    // Set WiFi mode to station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    ESP_LOGI(TAG, "WiFi initialized successfully");
    return ESP_OK;
}

esp_err_t womo_wifi_connect(const char *ssid, const char *password, uint8_t max_retry)
{
    if (ssid == NULL) {
        ESP_LOGE(TAG, "SSID cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    max_retry_count = max_retry;
    retry_count = 0;
    auto_reconnect_enabled = true;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);

    char pwd_buf[64] = "";
    bool password_provided = (password != NULL && password[0] != '\0');
    wifi_known_ensure_loaded();

    if (!password_provided) {
        int idx = wifi_known_find(s_known_list, s_known_count, ssid);
        if (idx >= 0) {
            strncpy(pwd_buf, s_known_list[idx].pwd, sizeof(pwd_buf) - 1);
            pwd_buf[sizeof(pwd_buf) - 1] = '\0';
            if (pwd_buf[0] != '\0') {
                password = pwd_buf;
                password_provided = true;
            }
        }
    }
    
    // Configure WiFi
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password_provided && password != NULL) {
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Wait for connection result
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY);
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi: %s", ssid);
        if (password_provided && password != NULL) {
            // Promote/insert SSID in known list and persist
            wifi_known_ensure_loaded();
            wifi_known_promote(s_known_list, &s_known_count, ssid, password);
            wifi_nvs_save_known(s_known_list, s_known_count);
        }
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s", ssid);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Unexpected WiFi event");
        return ESP_FAIL;
    }
}

esp_err_t womo_wifi_connect_best_known(uint8_t max_retry)
{
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    wifi_known_ensure_loaded();
    if (s_known_count == 0) {
        ESP_LOGI(TAG, "No known WiFi networks stored");
        return ESP_ERR_NOT_FOUND;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {
            .min = 0,
            .max = 120,
        },
        .scan_time.passive = 0,
    };

    esp_err_t err = esp_wifi_start();
    if (err == ESP_ERR_WIFI_CONN || err == ESP_ERR_WIFI_STATE) {
        err = ESP_OK; // already started/connected
    }
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK || ap_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    wifi_ap_record_t *records = calloc(ap_count, sizeof(*records));
    if (!records) {
        return ESP_ERR_NO_MEM;
    }

    uint16_t copy_count = ap_count;
    err = esp_wifi_scan_get_ap_records(&copy_count, records);
    if (err != ESP_OK || copy_count == 0) {
        free(records);
        return ESP_ERR_NOT_FOUND;
    }

    const char *target_ssid = NULL;
    for (size_t k = 0; k < s_known_count && !target_ssid; k++) {
        for (size_t i = 0; i < copy_count; i++) {
            if (strncmp(s_known_list[k].ssid,
                        (const char *)records[i].ssid,
                        sizeof(s_known_list[k].ssid)) == 0) {
                target_ssid = s_known_list[k].ssid;
                break;
            }
        }
    }

    free(records);

    if (!target_ssid) {
        ESP_LOGI(TAG, "No known WiFi found in scan");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Connecting to known WiFi: %s", target_ssid);
    return womo_wifi_connect(target_ssid, NULL, max_retry);
}

esp_err_t womo_wifi_disconnect(void)
{
    ESP_LOGI(TAG, "Disconnecting from WiFi");
    
    auto_reconnect_enabled = false;
    esp_err_t err = esp_wifi_disconnect();
    
    if (err == ESP_OK) {
        current_status = WOMO_WIFI_DISCONNECTED;
        esp_wifi_stop();
    }
    
    return err;
}

womo_wifi_status_t womo_wifi_get_status(void)
{
    return current_status;
}

bool womo_wifi_is_connected(void)
{
    return (current_status == WOMO_WIFI_CONNECTED);
}

int8_t womo_wifi_get_rssi(void)
{
    if (!womo_wifi_is_connected()) {
        return -127;
    }
    
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    
    return -127;
}

esp_err_t womo_wifi_get_ssid(char *ssid_str, size_t max_len)
{
    if (ssid_str == NULL || max_len < 33) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!womo_wifi_is_connected()) {
        strncpy(ssid_str, "", max_len);
        return ESP_FAIL;
    }
    
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        strncpy(ssid_str, (char*)ap_info.ssid, max_len - 1);
        ssid_str[max_len - 1] = '\0';
        return ESP_OK;
    }
    
    strncpy(ssid_str, "", max_len);
    return ESP_FAIL;
}

esp_err_t womo_wifi_get_ip_string(char *ip_str, size_t max_len)
{
    if (ip_str == NULL || max_len < 16) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!womo_wifi_is_connected() || sta_netif == NULL) {
        strncpy(ip_str, "0.0.0.0", max_len);
        return ESP_FAIL;
    }
    
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
        snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
        return ESP_OK;
    }
    
    strncpy(ip_str, "0.0.0.0", max_len);
    return ESP_FAIL;
}

void womo_wifi_set_auto_reconnect(bool enable)
{
    auto_reconnect_enabled = enable;
    ESP_LOGI(TAG, "Auto-reconnect %s", enable ? "enabled" : "disabled");
}

const char* womo_wifi_get_last_error(void)
{
    return last_error;
}

static void wifi_scan_task(void *arg)
{
    (void)arg;
    esp_err_t err = ESP_OK;
    womo_wifi_scan_result_t *results = NULL;
    size_t result_count = 0;

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {
            .min = 0,
            .max = 120,
        },
        .scan_time.passive = 0,
    };

    err = esp_wifi_start();
    if (err == ESP_ERR_WIFI_CONN || err == ESP_ERR_WIFI_STATE) {
        err = ESP_OK;
    }

    if (err == ESP_OK) {
        err = esp_wifi_scan_start(&scan_config, true);
    }

    if (err == ESP_OK) {
        uint16_t ap_count = 0;
        err = esp_wifi_scan_get_ap_num(&ap_count);
        if (err == ESP_OK && ap_count > 0) {
            wifi_ap_record_t *ap_records = calloc(ap_count, sizeof(*ap_records));
            if (ap_records) {
                uint16_t copy_count = ap_count;
                err = esp_wifi_scan_get_ap_records(&copy_count, ap_records);
                if (err == ESP_OK && copy_count > 0) {
                    result_count = copy_count;
                    results = calloc(result_count, sizeof(womo_wifi_scan_result_t));
                    if (results) {
                        for (size_t i = 0; i < result_count; i++) {
                            const wifi_ap_record_t *rec = &ap_records[i];
                            strncpy(results[i].ssid, (const char *)rec->ssid, sizeof(results[i].ssid) - 1);
                            results[i].ssid[sizeof(results[i].ssid) - 1] = '\0';
                            results[i].auth_mode = rec->authmode;
                            results[i].rssi = rec->rssi;
                            results[i].pairwise_cipher = rec->pairwise_cipher;
                        }
                    } else {
                        err = ESP_ERR_NO_MEM;
                    }
                }
                free(ap_records);
            } else {
                err = ESP_ERR_NO_MEM;
            }
        }
    }

    if (s_wifi_scan_callback) {
        s_wifi_scan_callback(results, result_count, err, s_wifi_scan_user_data);
    }

    if (results) {
        free(results);
    }

    s_wifi_scan_task = NULL;
    s_wifi_scan_callback = NULL;
    s_wifi_scan_user_data = NULL;
    vTaskDelete(NULL);
}

esp_err_t womo_wifi_scan_async(womo_wifi_scan_callback_t callback, void *user_data)
{
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_wifi_scan_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_wifi_scan_callback = callback;
    s_wifi_scan_user_data = user_data;

    BaseType_t created = xTaskCreate(wifi_scan_task, "wifi_scan", 4096, NULL, 4, &s_wifi_scan_task);
    if (created != pdPASS) {
        s_wifi_scan_task = NULL;
        s_wifi_scan_callback = NULL;
        s_wifi_scan_user_data = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

bool womo_wifi_is_scan_in_progress(void)
{
    return (s_wifi_scan_task != NULL);
}
