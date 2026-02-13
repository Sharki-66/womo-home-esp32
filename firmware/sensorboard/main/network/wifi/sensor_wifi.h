#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * WiFi STA-Modul für das Sensorboard.
 *
 * Verbindet sich als Station mit dem RUTX11 Router.
 * SSID/Passwort werden aus NVS gelesen (Fallback: sensor_config.h Defaults).
 * mDNS registriert den Hostnamen "womo-sensor.local".
 */

/// Initialisiert WiFi-STA (netif, event-loop, NVS-Credentials, mDNS).
/// Startet die Verbindung automatisch.
esp_err_t sensor_wifi_init(void);

/// Stoppt WiFi und gibt Ressourcen frei.
esp_err_t sensor_wifi_deinit(void);

/// Gibt true zurück wenn WiFi verbunden und IP vorhanden.
bool sensor_wifi_is_connected(void);

/// WiFi-Credentials im NVS aktualisieren und neu verbinden.
/// ssid/pass dürfen NULL sein (dann wird der jeweilige Wert nicht geändert).
esp_err_t sensor_wifi_set_credentials(const char *ssid, const char *pass);

/// Liefert die aktuelle IP-Adresse als String (z.B. "192.168.1.120").
/// Gibt leeren String zurück wenn nicht verbunden.
const char *sensor_wifi_get_ip_str(void);

#ifdef __cplusplus
}
#endif
