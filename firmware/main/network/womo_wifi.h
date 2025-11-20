/*
 * WoMo WiFi Manager
 * 
 * Manages WiFi connection for:
 * - NTP time synchronization
 * - OTA updates
 * - Remote monitoring
 */

#ifndef WOMO_WIFI_H
#define WOMO_WIFI_H

#include "esp_err.h"
#include "esp_wifi.h"
#include <stdbool.h>

// WiFi connection status
typedef enum {
    WOMO_WIFI_DISCONNECTED,
    WOMO_WIFI_CONNECTING,
    WOMO_WIFI_CONNECTED,
    WOMO_WIFI_ERROR
} womo_wifi_status_t;

// WiFi configuration
typedef struct {
    char ssid[32];
    char password[64];
    uint8_t max_retry;
    bool auto_reconnect;
} womo_wifi_config_t;

/**
 * @brief Initialize WiFi module
 * 
 * Sets up WiFi in station mode
 * 
 * @return ESP_OK on success
 */
esp_err_t womo_wifi_init(void);

/**
 * @brief Connect to WiFi network
 * 
 * @param ssid WiFi SSID (network name)
 * @param password WiFi password
 * @param max_retry Maximum connection retry attempts (0 = infinite)
 * @return ESP_OK on success
 */
esp_err_t womo_wifi_connect(const char *ssid, const char *password, uint8_t max_retry);

/**
 * @brief Disconnect from WiFi
 * 
 * @return ESP_OK on success
 */
esp_err_t womo_wifi_disconnect(void);

/**
 * @brief Get current WiFi connection status
 * 
 * @return Current WiFi status
 */
womo_wifi_status_t womo_wifi_get_status(void);

/**
 * @brief Check if connected to WiFi
 * 
 * @return true if connected, false otherwise
 */
bool womo_wifi_is_connected(void);

/**
 * @brief Get WiFi RSSI (signal strength)
 * 
 * @return RSSI in dBm (typically -30 to -90)
 */
int8_t womo_wifi_get_rssi(void);

/**
 * @brief Get connected WiFi SSID (network name)
 * 
 * @param ssid_str Buffer to store SSID string (min 33 bytes)
 * @param max_len Maximum buffer length
 * @return ESP_OK if connected and SSID available
 */
esp_err_t womo_wifi_get_ssid(char *ssid_str, size_t max_len);

/**
 * @brief Get local IP address
 * 
 * @param ip_str Buffer to store IP string (min 16 bytes)
 * @param max_len Maximum buffer length
 * @return ESP_OK if connected and IP available
 */
esp_err_t womo_wifi_get_ip_string(char *ip_str, size_t max_len);

/**
 * @brief Enable/disable auto-reconnect
 * 
 * @param enable true = auto reconnect on disconnect
 */
void womo_wifi_set_auto_reconnect(bool enable);

/**
 * @brief Get last connection error
 * 
 * @return Error description string
 */
const char* womo_wifi_get_last_error(void);

#endif // WOMO_WIFI_H
