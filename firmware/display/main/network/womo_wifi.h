/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

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

typedef struct {
    char ssid[33];
    wifi_auth_mode_t auth_mode;
    int8_t rssi;
    wifi_cipher_type_t pairwise_cipher;
} womo_wifi_scan_result_t;

typedef void (*womo_wifi_scan_callback_t)(const womo_wifi_scan_result_t *results,
                                          size_t count,
                                          esp_err_t status,
                                          void *user_data);

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
 * @brief Periodically check WiFi connection and attempt reconnect if needed
 * 
 * Call this function periodically (e.g. every 15s) to ensure persistent WiFi connection.
 * Resets retry counter and attempts reconnect if in ERROR state.
 */
void womo_wifi_watchdog(void);

/**
 * @brief Get last connection error
 * 
 * @return Error description string
 */
const char* womo_wifi_get_last_error(void);

esp_err_t womo_wifi_scan_async(womo_wifi_scan_callback_t callback, void *user_data);
bool womo_wifi_is_scan_in_progress(void);

/**
 * @brief Scan nach bekannten Netzwerken und verbinde zum ersten Treffer (MRU-Reihenfolge)
 *
 * Lädt die gespeicherten Zugangsdaten aus NVS und wählt die zuerst gespeicherte SSID,
 * die im Scan gefunden wird. Passwort wird aus dem NVS-Eintrag verwendet.
 *
 * @param max_retry Maximale Verbindungsversuche (0 = unendlich)
 * @return ESP_OK bei erfolgreicher Verbindung, ESP_ERR_NOT_FOUND wenn kein bekanntes Netz in Reichweite
 */
/**
 * @brief Gibt das gespeicherte Passwort für eine bekannte SSID zurück.
 *
 * Sucht in der NVS-Known-List nach der SSID und kopiert das Passwort.
 *
 * @param ssid        SSID für die das Passwort gesucht wird
 * @param pwd_out     Puffer für das Passwort
 * @param pwd_out_sz  Größe des Puffers
 * @return ESP_OK bei Erfolg, ESP_ERR_NOT_FOUND wenn SSID unbekannt
 */
esp_err_t womo_wifi_get_known_credentials(const char *ssid,
                                           char *pwd_out, size_t pwd_out_sz);

esp_err_t womo_wifi_connect_best_known(uint8_t max_retry);

/**
 * @brief WiFi-Verbindung sofort im Hintergrund starten (nicht-blockierend).
 *        Ermöglicht parallelen Ablauf während Display-HW + LVGL-UI-Konstruktion.
 *        Ergebnis per womo_wifi_wait_connected() abfragen.
 * @param ssid      SSID des Zielnetzwerks
 * @param password  Passwort (NULL = aus NVS-Bekannteliste laden)
 * @param max_retry Max. Wiederholungen bei Fehler (0 = unbegrenzt)
 */
esp_err_t womo_wifi_connect_async(const char *ssid, const char *password, uint8_t max_retry);

/**
 * @brief Auf das Ergebnis einer via womo_wifi_connect_async() gestarteten
 *        Verbindung warten. Speichert Passwort bei Erfolg in NVS.
 * @param timeout_ms Maximale Wartezeit in ms. 0 = unbegrenzt.
 * @return ESP_OK=verbunden, ESP_FAIL=fehlgeschlagen, ESP_ERR_TIMEOUT=Timeout
 */
esp_err_t womo_wifi_wait_connected(uint32_t timeout_ms);

#endif // WOMO_WIFI_H
