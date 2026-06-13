/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * WoMo Time Management Module
 * 
 * Manages system time with multiple sources:
 * - Internal ESP32 RTC (always running)
 * - NTP sync via WiFi (when available)
 * - GPS sync via Sensorboard ESP-NOW
 */

#ifndef WOMO_TIME_H
#define WOMO_TIME_H

#include <time.h>
#include <stdbool.h>
#include "esp_err.h"

// Time sync sources
typedef enum {
    TIME_SOURCE_NONE = 0,
    TIME_SOURCE_INTERNAL_RTC,
    TIME_SOURCE_NTP,
    TIME_SOURCE_GPS,
    TIME_SOURCE_SENSOR
} womo_time_source_t;

// Time sync configuration
#define WOMO_TIME_SYNC_INTERVAL_SEC (3600)  // Sync every hour
#define WOMO_TIME_NTP_SERVER "pool.ntp.org"
#define WOMO_TIME_TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"  // Central European Time

/**
 * @brief Initialize time management system
 * 
 * Sets up timezone and internal RTC
 * 
 * @return ESP_OK on success
 */
esp_err_t womo_time_init(void);

/**
 * @brief Sync time from NTP server via WiFi
 * 
 * Requires active WiFi connection
 * 
 * @param wait_for_sync Wait for sync to complete (blocking)
 * @return ESP_OK on successful sync
 */
esp_err_t womo_time_sync_ntp(bool wait_for_sync);

/**
 * @brief Sync time from GPS/Sensorboard (Sensorboard)
 * 
 * Uses timestamp from Sensorboard RS485 packets
 * 
 * @param gps_time UTC time from GPS
 * @return ESP_OK on successful sync
 */
esp_err_t womo_time_sync_gps(time_t gps_time);

/**
 * @brief Mark time as synced from Sensorboard
 * 
 * Call this after successfully receiving timestamp from Sensorboard
 * and updating system time via settimeofday()
 * 
 * @return ESP_OK on success
 */
esp_err_t womo_time_mark_synced_sensor(void);

/**
 * @brief Get current time
 * 
 * @param timeinfo Pointer to tm structure to fill
 * @return ESP_OK if time is valid
 */
esp_err_t womo_time_get(struct tm *timeinfo);

/**
 * @brief Get current time as string
 * 
 * @param buffer Buffer to write formatted time
 * @param buffer_size Size of buffer
 * @param format strftime format string (e.g., "%H:%M:%S")
 * @return ESP_OK on success
 */
esp_err_t womo_time_get_string(char *buffer, size_t buffer_size, const char *format);

/**
 * @brief Get last sync source
 * 
 * @return Time source of last successful sync
 */
womo_time_source_t womo_time_get_source(void);

/**
 * @brief Check if time is synced and valid
 * 
 * @return true if time is synced from external source
 */
bool womo_time_is_synced(void);

/**
 * @brief Get seconds since last sync
 * 
 * @return Seconds since last successful time sync
 */
uint32_t womo_time_get_seconds_since_sync(void);

/**
 * @brief Auto-sync task (call periodically or as task)
 * 
 * Attempts to sync time based on available sources
 * Should be called every hour or run as FreeRTOS task
 */
void womo_time_auto_sync(void);

/* ── Sonnenzeiten ─────────────────────────────────────────────────────── */

/**
 * @brief Koordinaten aktualisieren und Sonnenzeiten neu berechnen.
 *
 * Berechnet Sonnenauf-/-untergang für den aktuellen Tag und übergibt das
 * Ergebnis intern an womo_theme_set_sun_times().  Kann jederzeit aufgerufen
 * werden, wenn sich der Standort ändert (GPS-Fix, Boot-Fallback, …).
 *
 * @param latitude   Breitengrad (-90 … +90)
 * @param longitude  Längengrad (-180 … +180)
 * @return true  – Berechnung erfolgreich, sun_times aktualisiert
 * @return false – Zeit noch nicht gültig oder Koordinaten ungültig
 */
bool womo_time_update_location(double latitude, double longitude);

/**
 * @brief Zuletzt berechnete Sonnenzeiten zurückgeben.
 *
 * Gibt den gecachten Wert des letzten womo_time_update_location()-Aufrufes
 * zurück, ohne eine Neuberechnung auszulösen.
 *
 * @param sr_h  Sonnenaufgang Stunde (Ausgabe)
 * @param sr_m  Sonnenaufgang Minute (Ausgabe)
 * @param ss_h  Sonnenuntergang Stunde (Ausgabe)
 * @param ss_m  Sonnenuntergang Minute (Ausgabe)
 * @return true wenn Wert gültig (mindestens einmal berechnet)
 */
bool womo_time_get_sun_times(uint8_t *sr_h, uint8_t *sr_m,
                              uint8_t *ss_h, uint8_t *ss_m);

#endif // WOMO_TIME_H
