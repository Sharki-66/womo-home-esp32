/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert ESP-NOW-Verbindung und startet TX/RX-Tasks
esp_err_t espnow_modem_init(void);

// Sendet WiFi-Passwort-Anfrage ans Display (wird bei Auth-Failure aufgerufen)
esp_err_t espnow_modem_request_wifi_pass(void);

/**
 * Erstellt einen vollständigen JSON-Snapshot aller Sensordaten (ohne RS485/ESP-NOW Protokoll).
 * Liest direkt von Sensor-Snapshots und internem EMA-Zustand.
 * Rückgabe-String muss vom Aufrufer mit free() freigegeben werden.
 *
 * @param[out] json_out  Allozierter JSON-String (Aufrufer muss free() aufrufen)
 * @return ESP_OK bei Erfolg, ESP_ERR_NO_MEM bei Speichermangel
 */
esp_err_t espnow_modem_get_snapshot(char **json_out);

/**
 * Führt ein Steuerkommando aus (z. B. "pwr_12v_on", "radio_off").
 * Wird vom Web-API-Handler aufgerufen; bei "pwr_12v_off" wird nach
 * 500 ms automatisch Deep Sleep eingeleitet.
 *
 * @param cmd  Kommando-String (z. B. "pwr_12v_on")
 * @return ESP_OK, ESP_ERR_NOT_FOUND (unbekanntes Kommando), oder Fehlercode
 */
esp_err_t espnow_modem_execute_cmd(const char *cmd);

/**
 * Wie espnow_modem_execute_cmd, aber übergibt das vollständige JSON-Objekt.
 * Ermöglicht parametrisierte Befehle wie gas_bottle_replace (slot/channel).
 *
 * @param root  Geparstes cJSON-Objekt mit "cmd" und optionalen Feldern (slot, channel, ssid, pass)
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NOT_FOUND oder Fehlercode
 */
esp_err_t espnow_modem_execute_cmd_json(const cJSON *root);

#ifdef __cplusplus
}
#endif
