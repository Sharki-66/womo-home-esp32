/*
 * WoMo WiFi Manager - Implementation
 */

#include "womo_wifi.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

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
    
    max_retry_count = max_retry;
    retry_count = 0;
    
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);
    
    // Configure WiFi
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password != NULL) {
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
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s", ssid);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Unexpected WiFi event");
        return ESP_FAIL;
    }
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
