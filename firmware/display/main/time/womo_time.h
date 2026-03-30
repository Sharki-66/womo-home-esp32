/*
 * WoMo Time Management Module
 * 
 * Manages system time with multiple sources:
 * - Internal ESP32 RTC (always running)
 * - NTP sync via WiFi (when available)
 * - GPS sync via RS485 Sensorboard
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
    TIME_SOURCE_RS485
} womo_time_source_t;

// Time sync configuration
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
 * @brief Sync time from GPS/RS485 (Sensorboard)
 * 
 * Uses timestamp from Sensorboard RS485 packets
 * 
 * @param gps_time UTC time from GPS
 * @return ESP_OK on successful sync
 */
esp_err_t womo_time_sync_gps(time_t gps_time);

/**
 * @brief Mark time as synced from RS485 sensor
 * 
 * Call this after successfully receiving timestamp from RS485 sensor
 * and updating system time via settimeofday()
 * 
 * @return ESP_OK on success
 */
esp_err_t womo_time_mark_synced_rs485(void);

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

#endif // WOMO_TIME_H
