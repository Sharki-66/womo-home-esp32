#pragma once

#include "esp_err.h"
#include <time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Zeit-Synchronisations-Modul für Modem-Board
 * 
 * Hierarchie der Zeitquellen:
 * 1. GPS (primär, ~1s Genauigkeit)
 * 2. LTE Netzwerkzeit via AT+CCLK (sekundär, ~1s Genauigkeit)
 * 3. PCF8523 RTC (Backup offline, Drift ~2ppm = ~5min/Monat)
 * 
 * Modem ist Zeitzentrale: Zeit wird via RS485 an Display verteilt.
 */

/**
 * @brief Zeitquelle für Synchronisation
 */
typedef enum {
    TIME_SOURCE_NONE = 0,    ///< Keine gültige Zeitquelle
    TIME_SOURCE_RTC,         ///< PCF8523 RTC (Backup)
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
    bool rtc_battery_low;            ///< RTC Batterie schwach
    bool system_time_valid;          ///< System-Zeit ist gültig (nicht 1970-01-01)
    uint32_t gps_sync_count;         ///< Anzahl erfolgreicher GPS-Synchronisationen
    uint32_t lte_sync_count;         ///< Anzahl erfolgreicher LTE-Synchronisationen
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
esp_err_t time_sync_check_rtc_battery(bool *battery_low);

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
