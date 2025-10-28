/*
 * WoMo Theme Manager - Implementation
 */

#include "womo_theme.h"
#include "time/womo_time.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "womo_theme";

// Current theme state
static womo_theme_mode_t current_mode = WOMO_THEME_DAY;
static womo_status_level_t current_status = WOMO_STATUS_OK;
static bool auto_mode_enabled = false;  // Start in manual mode for testing
static womo_sun_times_t sun_times = {
    .sunrise_hour = 6,
    .sunrise_minute = 30,
    .sunset_hour = 18,
    .sunset_minute = 30
};

// Location for sun calculations (optional)
static float location_lat = 0.0;
static float location_lon = 0.0;

esp_err_t womo_theme_init(float latitude, float longitude)
{
    ESP_LOGI(TAG, "Initializing theme manager");
    ESP_LOGI(TAG, "Location: %.4f, %.4f", latitude, longitude);
    
    location_lat = latitude;
    location_lon = longitude;
    
    // TODO: Calculate actual sunrise/sunset based on location and date
    // For now using default times
    ESP_LOGI(TAG, "Sunrise: %02d:%02d, Sunset: %02d:%02d",
             sun_times.sunrise_hour, sun_times.sunrise_minute,
             sun_times.sunset_hour, sun_times.sunset_minute);
    
    return ESP_OK;
}

static bool is_between_times(uint8_t current_hour, uint8_t current_min,
                             uint8_t start_hour, uint8_t start_min,
                             uint8_t end_hour, uint8_t end_min)
{
    uint16_t current_minutes = current_hour * 60 + current_min;
    uint16_t start_minutes = start_hour * 60 + start_min;
    uint16_t end_minutes = end_hour * 60 + end_min;
    
    return (current_minutes >= start_minutes && current_minutes < end_minutes);
}

womo_theme_mode_t womo_theme_update(womo_status_level_t status)
{
    struct tm timeinfo;
    
    current_status = status;
    
    // Skip automatic mode if disabled (manual control via touch)
    if (!auto_mode_enabled) {
        return current_mode;
    }
    
    // Get current time
    if (womo_time_get(&timeinfo) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get time, using day mode");
        current_mode = WOMO_THEME_DAY;
        return current_mode;
    }
    
    uint8_t hour = timeinfo.tm_hour;
    uint8_t minute = timeinfo.tm_min;
    
    // Sunrise transition: 30 minutes before sunrise
    uint8_t sunrise_start_hour = sun_times.sunrise_hour;
    uint8_t sunrise_start_min = (sun_times.sunrise_minute >= 30) ? 
                                  sun_times.sunrise_minute - 30 : 
                                  sun_times.sunrise_minute + 30;
    if (sun_times.sunrise_minute < 30) {
        sunrise_start_hour = (sunrise_start_hour > 0) ? sunrise_start_hour - 1 : 23;
    }
    
    // Sunset transition: 30 minutes before sunset
    uint8_t sunset_start_hour = sun_times.sunset_hour;
    uint8_t sunset_start_min = (sun_times.sunset_minute >= 30) ? 
                                 sun_times.sunset_minute - 30 : 
                                 sun_times.sunset_minute + 30;
    if (sun_times.sunset_minute < 30) {
        sunset_start_hour = (sunset_start_hour > 0) ? sunset_start_hour - 1 : 23;
    }
    
    // Determine theme mode
    if (is_between_times(hour, minute, 
                        sunrise_start_hour, sunrise_start_min,
                        sun_times.sunrise_hour, sun_times.sunrise_minute)) {
        current_mode = WOMO_THEME_SUNRISE;
        ESP_LOGI(TAG, "Theme: SUNRISE");
    }
    else if (is_between_times(hour, minute,
                             sun_times.sunrise_hour, sun_times.sunrise_minute,
                             sunset_start_hour, sunset_start_min)) {
        current_mode = WOMO_THEME_DAY;
        ESP_LOGD(TAG, "Theme: DAY");
    }
    else if (is_between_times(hour, minute,
                             sunset_start_hour, sunset_start_min,
                             sun_times.sunset_hour, sun_times.sunset_minute)) {
        current_mode = WOMO_THEME_SUNSET;
        ESP_LOGI(TAG, "Theme: SUNSET");
    }
    else {
        current_mode = WOMO_THEME_NIGHT;
        ESP_LOGD(TAG, "Theme: NIGHT");
    }
    
    return current_mode;
}

lv_color_t womo_theme_get_background_color(void)
{
    // Error/Critical status overrides time-based themes (getauscht)
    if (current_status == WOMO_STATUS_ERROR) {
        return WOMO_COLOR_ERROR;
    }
    else if (current_status == WOMO_STATUS_CRITICAL) {
        return WOMO_COLOR_CRITICAL;
    }
    else if (current_status == WOMO_STATUS_WARNING) {
        return WOMO_COLOR_WARNING;
    }
    
    // Normal status - use time-based theme
    switch (current_mode) {
        case WOMO_THEME_SUNRISE:
            return WOMO_COLOR_SUNRISE;
        case WOMO_THEME_DAY:
            return WOMO_COLOR_DAY_NORMAL;
        case WOMO_THEME_SUNSET:
            return WOMO_COLOR_SUNSET;
        case WOMO_THEME_NIGHT:
            return WOMO_COLOR_NIGHT_NORMAL;
        default:
            return WOMO_COLOR_DAY_NORMAL;
    }
}

womo_theme_mode_t womo_theme_get_mode(void)
{
    return current_mode;
}

void womo_theme_set_status(womo_status_level_t status)
{
    if (current_status != status) {
        ESP_LOGI(TAG, "Status changed: %d -> %d", current_status, status);
        current_status = status;
    }
}

womo_status_level_t womo_theme_get_status(void)
{
    return current_status;
}

void womo_theme_set_sun_times(uint8_t sunrise_hour, uint8_t sunrise_min,
                               uint8_t sunset_hour, uint8_t sunset_min)
{
    sun_times.sunrise_hour = sunrise_hour;
    sun_times.sunrise_minute = sunrise_min;
    sun_times.sunset_hour = sunset_hour;
    sun_times.sunset_minute = sunset_min;
    
    ESP_LOGI(TAG, "Sun times updated - Sunrise: %02d:%02d, Sunset: %02d:%02d",
             sunrise_hour, sunrise_min, sunset_hour, sunset_min);
}

const womo_sun_times_t* womo_theme_get_sun_times(void)
{
    return &sun_times;
}

bool womo_theme_is_daytime(void)
{
    struct tm timeinfo;
    
    if (womo_time_get(&timeinfo) != ESP_OK) {
        return true; // Default to day if time not available
    }
    
    uint8_t hour = timeinfo.tm_hour;
    uint8_t minute = timeinfo.tm_min;
    
    return is_between_times(hour, minute,
                           sun_times.sunrise_hour, sun_times.sunrise_minute,
                           sun_times.sunset_hour, sun_times.sunset_minute);
}

void womo_theme_apply_to_screen(lv_obj_t *screen)
{
    if (screen == NULL) {
        screen = lv_scr_act();
    }
    
    lv_color_t bg_color = womo_theme_get_background_color();
    lv_obj_set_style_bg_color(screen, bg_color, 0);
    
    ESP_LOGI(TAG, "Applied theme to screen - Mode: %d, Status: %d", 
             current_mode, current_status);
}

void womo_theme_cycle_mode(void)
{
    // Cycle through theme modes
    switch (current_mode) {
        case WOMO_THEME_DAY:
            current_mode = WOMO_THEME_SUNSET;
            ESP_LOGI(TAG, "Cycled to SUNSET mode");
            break;
        case WOMO_THEME_SUNSET:
            current_mode = WOMO_THEME_NIGHT;
            ESP_LOGI(TAG, "Cycled to NIGHT mode");
            break;
        case WOMO_THEME_NIGHT:
            current_mode = WOMO_THEME_SUNRISE;
            ESP_LOGI(TAG, "Cycled to SUNRISE mode");
            break;
        case WOMO_THEME_SUNRISE:
            current_mode = WOMO_THEME_DAY;
            ESP_LOGI(TAG, "Cycled to DAY mode");
            break;
        default:
            current_mode = WOMO_THEME_DAY;
            break;
    }
}

void womo_theme_cycle_status(void)
{
    // Cycle through status levels
    switch (current_status) {
        case WOMO_STATUS_OK:
            current_status = WOMO_STATUS_WARNING;
            ESP_LOGI(TAG, "Cycled to WARNING status");
            break;
        case WOMO_STATUS_WARNING:
            current_status = WOMO_STATUS_ERROR;
            ESP_LOGI(TAG, "Cycled to ERROR status");
            break;
        case WOMO_STATUS_ERROR:
            current_status = WOMO_STATUS_CRITICAL;
            ESP_LOGI(TAG, "Cycled to CRITICAL status");
            break;
        case WOMO_STATUS_CRITICAL:
            current_status = WOMO_STATUS_OK;
            ESP_LOGI(TAG, "Cycled to OK status");
            break;
        default:
            current_status = WOMO_STATUS_OK;
            break;
    }
}

void womo_theme_set_mode(womo_theme_mode_t mode)
{
    current_mode = mode;
    ESP_LOGI(TAG, "Manual mode set to: %d", mode);
}

void womo_theme_set_auto_mode(bool enable)
{
    auto_mode_enabled = enable;
    ESP_LOGI(TAG, "Auto mode %s", enable ? "ENABLED" : "DISABLED");
}

bool womo_theme_is_auto_mode(void)
{
    return auto_mode_enabled;
}
