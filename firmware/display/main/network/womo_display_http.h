/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * womo_display_http – HTTP-Webserver für das 800×480 Web-Dashboard
 *
 * Startet einen eigenen HTTP-Server auf Port 8080.
 * Endpunkte:
 *   GET /          → womo_display.html (eingebettetes SPIFFS-HTML)
 *   GET /display   → identisch mit /
 *   GET /api/status → JSON-Snapshot aller Sensor- und Router-Daten
 *
 * Nutzung in main.c:
 *   1. womo_display_http_init()            – einmalig beim Boot (vor HTTP-Start)
 *   2. womo_display_http_start()           – startet den Server
 *   3. womo_display_http_update_sensor()   – bei jedem RS485-Datenempfang aufrufen
 *   4. womo_display_http_update_router()   – bei jedem Router-Poll aufrufen
 *   5. womo_display_http_update_status()   – bei Status-Änderungen aufrufen
 */

#include "rs485/womo_rs485_display.h"
#include "network/womo_router_uci.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WOMO_DISPLAY_HTTP_PORT  8080

/** Status-Level für die Statusanzeige im Dashboard */
typedef enum {
    WOMO_DASH_STATUS_OK = 0,
    WOMO_DASH_STATUS_WARN,
    WOMO_DASH_STATUS_CRIT,
} womo_dash_status_t;

/**
 * @brief Modul initialisieren (Mutex + Snapshot-Speicher).
 *        Muss vor womo_display_http_start() aufgerufen werden.
 */
esp_err_t womo_display_http_init(void);

/**
 * @brief HTTP-Server auf Port WOMO_DISPLAY_HTTP_PORT starten.
 * @return ESP_OK bei Erfolg
 */
esp_err_t womo_display_http_start(void);

/**
 * @brief HTTP-Server stoppen.
 */
esp_err_t womo_display_http_stop(void);

/**
 * @brief Sensor-Snapshot aktualisieren (aus RS485-Callback aufrufen).
 *        Thread-sicher (Mutex).
 */
void womo_display_http_update_sensor(const womo_sensor_data_t *data);

/**
 * @brief Router-Snapshot aktualisieren (aus router_poll_task aufrufen).
 *        Thread-sicher (Mutex).
 *
 * @param wifi  WLAN-Status (kann NULL sein)
 * @param lte   LTE-Status  (kann NULL sein)
 * @param ap    AP-Status   (kann NULL sein)
 */
void womo_display_http_update_router(const womo_router_wifi_status_t *wifi,
                                     const womo_router_lte_status_t  *lte,
                                     const womo_router_ap_status_t   *ap);

/**
 * @brief System-Status für das Dashboard setzen.
 *        Thread-sicher (Mutex).
 *
 * @param level    Aktueller Status-Level
 * @param text     Kurztext (max. 31 Zeichen), oder NULL für Default
 * @param rs485_ok true = RS485-Verbindung aktiv
 * @param location Ortsname (max. 127 Zeichen), oder NULL
 */
void womo_display_http_update_status(womo_dash_status_t level,
                                     const char        *text,
                                     bool               rs485_ok,
                                     const char        *location);

#ifdef __cplusplus
}
#endif
