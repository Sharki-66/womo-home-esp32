/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * WoMo Time Management Module - Implementation
 */

#include "womo_time.h"
#include "womo_sun_calc.h"
#include "gui/womo_theme.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "womo_time";

// Time sync state
static womo_time_source_t last_sync_source = TIME_SOURCE_NONE;
static time_t last_sync_time = 0;
static bool time_synced = false;

/* Gecachte Sonnenzeiten (gesetzt von womo_time_update_location) */
static uint8_t s_sr_h = 0, s_sr_m = 0, s_ss_h = 0, s_ss_m = 0;
static bool    s_sun_valid = false;

/**
 * @brief SNTP sync notification callback
 */
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronized via NTP");
    last_sync_source = TIME_SOURCE_NTP;
    last_sync_time = tv->tv_sec;
    time_synced = true;
}

esp_err_t womo_time_init(void)
{
    ESP_LOGI(TAG, "Initializing time management");
    
    // Set timezone
    setenv("TZ", WOMO_TIME_TIMEZONE, 1);
    tzset();
    
    ESP_LOGI(TAG, "Timezone set to: %s", WOMO_TIME_TIMEZONE);
    
    return ESP_OK;
}

esp_err_t womo_time_sync_ntp(bool wait_for_sync)
{
    ESP_LOGI(TAG, "Starting NTP time sync...");
    
    // Stop SNTP if already running
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    
    // Configure SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, WOMO_TIME_NTP_SERVER);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
    
    if (!wait_for_sync) {
        ESP_LOGI(TAG, "NTP sync started (non-blocking)");
        return ESP_OK;
    }
    
    // Wait for time sync (max 10 seconds)
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int max_retry = 10;
    
    while (timeinfo.tm_year < (2024 - 1900) && ++retry < max_retry) {
        ESP_LOGI(TAG, "Waiting for NTP sync... (%d/%d)", retry, max_retry);
        vTaskDelay(pdMS_TO_TICKS(1000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    
    if (timeinfo.tm_year < (2024 - 1900)) {
        ESP_LOGE(TAG, "NTP sync failed - timeout");
        return ESP_ERR_TIMEOUT;
    }
    
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "NTP sync successful: %s", strftime_buf);
    
    time(&last_sync_time);
    last_sync_source = TIME_SOURCE_NTP;
    time_synced = true;
    
    return ESP_OK;
}

esp_err_t womo_time_sync_gps(time_t gps_time)
{
    // Plausibilitätsprüfung: GPS-Zeit muss >= 2024 sein
    struct tm check;
    gmtime_r(&gps_time, &check);
    if (check.tm_year < (2024 - 1900)) {
        ESP_LOGW(TAG, "GPS time not plausible (year=%d), ignoring", check.tm_year + 1900);
        return ESP_FAIL;
    }

    struct timeval tv = {
        .tv_sec = gps_time,
        .tv_usec = 0
    };

    settimeofday(&tv, NULL);

    last_sync_time = gps_time;
    last_sync_source = TIME_SOURCE_GPS;
    time_synced = true;

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &check);
    ESP_LOGI(TAG, "Time synced from GPS: %s UTC", buf);

    return ESP_OK;
}

esp_err_t womo_time_mark_synced_sensor(void)
{
    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    // Nur als synced markieren wenn Zeit plausibel ist (>= 2024)
    if (timeinfo.tm_year < (2024 - 1900)) {
        ESP_LOGW(TAG, "Sensorboard time not plausible (year < 2024), not marking as synced");
        return ESP_FAIL;
    }
    
    time(&last_sync_time);
    last_sync_source = TIME_SOURCE_SENSOR;
    time_synced = true;
    
    ESP_LOGI(TAG, "Time marked as synced from Sensorboard");
    return ESP_OK;
}

esp_err_t womo_time_get(struct tm *timeinfo)
{
    if (timeinfo == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    time_t now;
    time(&now);
    localtime_r(&now, timeinfo);
    
    return ESP_OK;
}

esp_err_t womo_time_get_string(char *buffer, size_t buffer_size, const char *format)
{
    if (buffer == NULL || format == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    struct tm timeinfo;
    esp_err_t ret = womo_time_get(&timeinfo);
    if (ret != ESP_OK) {
        return ret;
    }
    
    strftime(buffer, buffer_size, format, &timeinfo);
    
    return ESP_OK;
}

womo_time_source_t womo_time_get_source(void)
{
    return last_sync_source;
}

bool womo_time_is_synced(void)
{
    return time_synced;
}

uint32_t womo_time_get_seconds_since_sync(void)
{
    if (!time_synced) {
        return 0;
    }
    
    time_t now;
    time(&now);
    
    return (uint32_t)(now - last_sync_time);
}

void womo_time_auto_sync(void)
{
    uint32_t seconds_since_sync = womo_time_get_seconds_since_sync();
    
    ESP_LOGI(TAG, "Auto-sync check: %lu seconds since last sync", seconds_since_sync);
    
    // If never synced or sync interval exceeded
    if (!time_synced || seconds_since_sync >= WOMO_TIME_SYNC_INTERVAL_SEC) {
        
        // Try NTP first (when WiFi available)
        // Note: Actual WiFi check should be implemented
        ESP_LOGI(TAG, "Attempting NTP sync...");
        esp_err_t ret = womo_time_sync_ntp(false);  // Non-blocking
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Auto-sync successful via NTP");
            return;
        }
        
        // Try Sensorboard ESP-NOW time sync
        // (handled in main.c time_update_timer_cb)
        
        ESP_LOGW(TAG, "Auto-sync failed - no sync source available");
    } else {
        ESP_LOGI(TAG, "Time sync not needed yet");
    }
}

bool womo_time_update_location(double latitude, double longitude)
{
    struct tm tm_now;
    if (womo_time_get(&tm_now) != ESP_OK || tm_now.tm_year < (2024 - 1900)) {
        ESP_LOGW(TAG, "update_location: Zeit noch nicht g\u00fcltig");
        return false;
    }

    uint8_t sr_h, sr_m, ss_h, ss_m;
    if (!womo_sun_calc_times(latitude, longitude, &tm_now,
                             &sr_h, &sr_m, &ss_h, &ss_m)) {
        return false;
    }

    s_sr_h = sr_h; s_sr_m = sr_m;
    s_ss_h = ss_h; s_ss_m = ss_m;
    s_sun_valid = true;

    womo_theme_set_sun_times(sr_h, sr_m, ss_h, ss_m);
    return true;
}

bool womo_time_get_sun_times(uint8_t *sr_h, uint8_t *sr_m,
                              uint8_t *ss_h, uint8_t *ss_m)
{
    if (!s_sun_valid || !sr_h || !sr_m || !ss_h || !ss_m) return false;
    *sr_h = s_sr_h; *sr_m = s_sr_m;
    *ss_h = s_ss_h; *ss_m = s_ss_m;
    return true;
}
