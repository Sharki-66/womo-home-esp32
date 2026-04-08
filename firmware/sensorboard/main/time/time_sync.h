/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "esp_err.h"
#include <time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Zeit-Synchronisations-Modul für Sensorboard
 *
 * Hierarchie der Zeitquellen:
 * 1. NTP via WiFi (primär, nach Router-Verbindung)
 * 2. PCF8523 RTC (Backup offline, Drift ~2ppm = ~5min/Monat)
 *
 * GPS/LTE-Funktionen sind für zukünftige Erweiterung vorgesehen,
 * werden aktuell auf dem Sensorboard nicht aufgerufen.
 */

/**
 * @brief Zeitquelle für Synchronisation
 */
typedef enum {
    TIME_SOURCE_NONE = 0,    ///< Keine gültige Zeitquelle
    TIME_SOURCE_RTC,         ///< PCF8523 RTC (Backup)
    TIME_SOURCE_NTP,         ///< NTP via WiFi (primär nach Boot)
    TIME_SOURCE_LTE,         ///< LTE Netzwerkzeit (AT+CCLK)
    TIME_SOURCE_GPS,         ///< GPS-Zeit (primär)
} time_source_t;

/**
 * @brief Zeit-Synchronisations-Status
 */
typedef struct {
    time_source_t active_source;     ///< Aktive Zeitquelle
    time_t last_sync_time;           ///< Zeitpunkt der letzten Synchronisation (system time)
    time_t last_sync_value;          ///< Wert der letzten Synchronisation (UTC)
    bool rtc_battery_low;            ///< BLF: Batteriespannung unter ~1.2V
    bool rtc_bat_switched;           ///< BSF: VDD-Ausfall erkannt, Chip lief auf Batterie
    bool system_time_valid;          ///< System-Zeit ist gültig (nicht 1970-01-01)
    uint32_t gps_sync_count;         ///< Anzahl erfolgreicher GPS-Synchronisationen
    uint32_t lte_sync_count;         ///< Anzahl erfolgreicher LTE-Synchronisationen
    uint32_t ntp_sync_count;         ///< Anzahl erfolgreicher NTP-Synchronisationen
    uint32_t rtc_sync_count;         ///< Anzahl erfolgreicher RTC-Synchronisationen
} time_sync_status_t;

/**
 * @brief Initialisiert das Zeit-Synchronisations-Modul
 * 
 * Startet keine eigenen Tasks, sondern bietet API für andere Module.
 * RTC wird beim Start ausgelesen und System-Zeit initialisiert falls verfügbar.
 * 
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t time_sync_init(void);

/**
 * @brief Aktualisiert die System-Zeit aus GPS-Daten
 * 
 * Primäre Zeitquelle. Schreibt bei Erfolg auch in RTC.
 * 
 * @param gps_utc_time GPS-Zeit als Unix-Timestamp (UTC)
 * @return ESP_OK bei Erfolg
 */
esp_err_t time_sync_update_from_gps(time_t gps_utc_time);

/**
 * @brief Aktualisiert die System-Zeit aus LTE-Netzwerkzeit
 * 
 * Sekundäre Zeitquelle (Fallback wenn GPS nicht verfügbar).
 * Schreibt bei Erfolg auch in RTC.
 * 
 * @param lte_utc_time LTE-Zeit als Unix-Timestamp (UTC)
 * @return ESP_OK bei Erfolg
 */
esp_err_t time_sync_update_from_lte(time_t lte_utc_time);

/**
 * @brief Prüft RTC-Batteriestatus
 * 
 * Liest PCF8523 Control_3 Register und prüft Battery-Low-Flag (Bit 2).
 * 
 * @param[out] battery_low true wenn Batterie schwach
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
/**
 * @brief Startet NTP-Client für automatische Zeitsynchronisation via WiFi
 *
 * SNTP verbindet sich nach WiFi-Connect automatisch. Bei erfolgreichem Sync
 * wird die RTC aktualisiert und als Backup für Netzausfälle gespeichert.
 *
 * @param ntp_server NTP-Server-Adresse (z.B. "pool.ntp.org")
 * @return ESP_OK bei Erfolg
 */
esp_err_t time_sync_start_ntp(const char *ntp_server);

/**
 * @brief Prüft RTC-Batteriestatus
 *
 * Liest PCF8523 Control_3 Register:
 * - BLF (Bit 3): Batteriespannung unter ~1.2V
 * - BSF (Bit 4): VDD-Ausfall erkannt, Chip lief auf Batterie
 *
 * @param[out] battery_low   true wenn BLF gesetzt
 * @param[out] bat_switched  true wenn BSF gesetzt (darf NULL sein)
 * @return ESP_OK bei Erfolg, sonst Fehlercode
 */
esp_err_t time_sync_check_rtc_battery(bool *battery_low, bool *bat_switched);

/**
 * @brief Gibt aktuellen Zeit-Synchronisations-Status zurück
 * 
 * @param[out] status Status-Struktur (darf nicht NULL sein)
 * @return ESP_OK bei Erfolg
 */
esp_err_t time_sync_get_status(time_sync_status_t *status);

/**
 * @brief Gibt String-Repräsentation der aktiven Zeitquelle zurück
 * 
 * @param source Zeitquelle
 * @return String-Repräsentation
 */
const char* time_sync_source_to_string(time_source_t source);

#ifdef __cplusplus
}
#endif
