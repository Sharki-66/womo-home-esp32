/**
 * @file womo_gps.c
 * @brief GPS/GNSS wrapper for Walter Modem
 */

#include "womo_gps.h"
#include "WalterModem.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "womo_gps";

// Global modem instance (extern, defined in main.c)
extern WalterModem modem;

// Last received GPS fix
static womo_gps_data_t s_last_fix = {0};
static bool s_fix_received = false;
static SemaphoreHandle_t s_gps_mutex = NULL;
static const char *gnss_status_to_string(WalterModemGNSSFixStatus status)
{
    switch (status) {
        case WALTER_MODEM_GNSS_FIX_STATUS_READY:
            return "READY";
        case WALTER_MODEM_GNSS_FIX_STATUS_STOPPED_BY_USER:
            return "STOPPED_BY_USER";
        case WALTER_MODEM_GNSS_FIX_STATUS_NO_RTC:
            return "NO_RTC";
        case WALTER_MODEM_GNSS_FIX_STATUS_LTE_CONCURRENCY:
            return "LTE_CONCURRENCY";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Convert north/east speed to heading and ground speed
 */
static void calculate_speed_heading(double north_speed, double east_speed, 
                                    float *speed_kmh, float *heading_deg)
{
    // Calculate ground speed from north/east components
    double ground_speed_ms = sqrt(north_speed * north_speed + east_speed * east_speed);
    *speed_kmh = (float)(ground_speed_ms * 3.6); // m/s to km/h
    
    // Calculate heading (0 = North, 90 = East, 180 = South, 270 = West)
    if (ground_speed_ms < 0.1) {
        *heading_deg = 0.0f; // No meaningful heading at very low speeds
    } else {
        double heading_rad = atan2(east_speed, north_speed);
        *heading_deg = (float)(heading_rad * 180.0 / M_PI);
        if (*heading_deg < 0) {
            *heading_deg += 360.0f;
        }
    }
}

/**
 * @brief GNSS event handler callback
 * 
 * Called by Walter modem when a GNSS fix is received.
 * This runs in the modem's event context - must not block!
 */
static void gnss_event_handler(const WalterModemGNSSFix *fix, void *args)
{
    if (!fix) {
        return;
    }

    const WalterModemGNSSFixStatus status = fix->status;
    const bool fix_ready = (status == WALTER_MODEM_GNSS_FIX_STATUS_READY);
    const char *status_str = gnss_status_to_string(status);
    const uint8_t sat_count = fix->satCount;
    const uint32_t time_to_fix = fix->timeToFix;
    const float confidence = (float)fix->estimatedConfidence;
    double latitude = fix->latitude;
    double longitude = fix->longitude;
    float speed_kmh = 0.0f;
    float heading_deg = 0.0f;
    calculate_speed_heading(fix->northSpeed, fix->eastSpeed, &speed_kmh, &heading_deg);
    
    // Take mutex
    if (xSemaphoreTake(s_gps_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Copy fix data
        s_last_fix.valid = fix_ready;
        s_last_fix.latitude = latitude;
        s_last_fix.longitude = longitude;
        s_last_fix.altitude_m = fix->height;
        s_last_fix.satellites = fix->satCount;
        s_last_fix.confidence_m = confidence;
        s_last_fix.timestamp = fix->timestamp;
        s_last_fix.time_to_fix_ms = fix->timeToFix;
        
        // Calculate speed and heading from north/east components
        s_last_fix.speed_kmh = speed_kmh;
        s_last_fix.heading_deg = heading_deg;
        
        s_fix_received = true;
        
        xSemaphoreGive(s_gps_mutex);
    }

    if (fix_ready) {
        ESP_LOGI(TAG,
                 "GNSS fix ready: status=%s (%d) sats=%u ttff=%ums conf=%.1fm lat=%.6f lon=%.6f alt=%.1fm speed=%.1fkm/h heading=%.1f°",
                 status_str,
                 (int)status,
                 sat_count,
                 time_to_fix,
                 confidence,
                 latitude,
                 longitude,
                 fix->height,
                 speed_kmh,
                 heading_deg);
    } else {
        ESP_LOGW(TAG,
                 "GNSS update without fix: status=%s (%d) sats=%u ttff=%ums conf=%.1fm north=%.2fm/s east=%.2fm/s",
                 status_str,
                 (int)status,
                 sat_count,
                 time_to_fix,
                 confidence,
                 fix->northSpeed,
                 fix->eastSpeed);
    }
}

esp_err_t womo_gps_init(void)
{
    ESP_LOGI(TAG, "Initializing GPS subsystem");
    
    // Create mutex
    if (s_gps_mutex == NULL) {
        s_gps_mutex = xSemaphoreCreateMutex();
        if (s_gps_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create GPS mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    
    // Register GNSS event handler
    modem.gnssSetEventHandler(gnss_event_handler, NULL);
    
    // Configure GNSS subsystem (use default configuration like in official example)
    if (!modem.gnssConfig()) {
        ESP_LOGE(TAG, "Failed to configure GNSS subsystem");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "GPS subsystem initialized successfully");
    return ESP_OK;
}

esp_err_t womo_gps_request_fix(void)
{
    ESP_LOGI(TAG, "Requesting GNSS fix");
    
    // Request a single GNSS fix
    if (!modem.gnssPerformAction(WALTER_MODEM_GNSS_ACTION_GET_SINGLE_FIX)) {
        ESP_LOGE(TAG, "Failed to request GNSS fix");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t womo_gps_get_last_fix(womo_gps_data_t *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(s_gps_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!s_fix_received) {
            xSemaphoreGive(s_gps_mutex);
            return ESP_ERR_NOT_FOUND;
        }
        
        memcpy(data, &s_last_fix, sizeof(womo_gps_data_t));
        xSemaphoreGive(s_gps_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

bool womo_gps_is_valid(void)
{
    bool valid = false;
    
    if (xSemaphoreTake(s_gps_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        valid = s_fix_received && s_last_fix.valid;
        xSemaphoreGive(s_gps_mutex);
    }
    
    return valid;
}

esp_err_t womo_gps_get_utc_time(int64_t *epoch_time)
{
    if (!epoch_time) {
        return ESP_ERR_INVALID_ARG;
    }
    
    WalterModemRsp rsp = {};
    if (!modem.gnssGetUTCTime(&rsp)) {
        ESP_LOGE(TAG, "Failed to get GNSS UTC time");
        return ESP_FAIL;
    }
    
    *epoch_time = rsp.data.clock.epochTime;
    return ESP_OK;
}

esp_err_t womo_gps_set_utc_time(int64_t epoch_time)
{
    if (!modem.gnssSetUTCTime(epoch_time)) {
        ESP_LOGE(TAG, "Failed to set GNSS UTC time");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Set GNSS UTC time to %lld", epoch_time);
    return ESP_OK;
}
