/*
 * WoMo Theme Manager
 * 
 * Manages background colors and themes based on:
 * - Time of day (day/night)
 * - System status (normal/warning/critical)
 * - Resource levels (water, gas, battery)
 */

#ifndef WOMO_THEME_H
#define WOMO_THEME_H

#include "lvgl.h"
#include "esp_err.h"
#include <stdbool.h>

// Theme modes
typedef enum {
    WOMO_THEME_DAY,           // Daytime (after sunrise)
    WOMO_THEME_NIGHT,         // Nighttime (after sunset)
    WOMO_THEME_SUNRISE,       // Sunrise transition
    WOMO_THEME_SUNSET         // Sunset transition
} womo_theme_mode_t;

// System status levels
typedef enum {
    WOMO_STATUS_OK,           // All systems normal (blue/green)
    WOMO_STATUS_WARNING,      // Low resources (orange/yellow)
    WOMO_STATUS_CRITICAL,     // Critical level (red)
    WOMO_STATUS_ERROR         // System error (dark red)
} womo_status_level_t;

// Color definitions for different states
#define WOMO_COLOR_DAY_NORMAL       lv_color_hex(0x87CEEB)  // Sky Blue (Hellblau)
#define WOMO_COLOR_NIGHT_NORMAL     lv_color_hex(0x191970)  // Midnight Blue
#define WOMO_COLOR_SUNRISE          lv_color_hex(0xFF6347)  // Tomato (orange-red)
#define WOMO_COLOR_SUNSET           lv_color_hex(0xFF8C00)  // Dark Orange

#define WOMO_COLOR_WARNING          lv_color_hex(0xFFA500)  // Orange
#define WOMO_COLOR_ERROR            lv_color_hex(0xFF0000)  // Red (helles Rot)
#define WOMO_COLOR_CRITICAL         lv_color_hex(0x8B0000)  // Dark Red (dunkles Rot)

// Sunrise/Sunset times (will be calculated based on location/date)
typedef struct {
    uint8_t sunrise_hour;
    uint8_t sunrise_minute;
    uint8_t sunset_hour;
    uint8_t sunset_minute;
} womo_sun_times_t;

/**
 * @brief Initialize theme manager
 * 
 * @param latitude Location latitude for sun calculations
 * @param longitude Location longitude for sun calculations
 * @return ESP_OK on success
 */
esp_err_t womo_theme_init(float latitude, float longitude);

/**
 * @brief Update theme based on current time and system status
 * 
 * Automatically determines theme mode based on time of day
 * 
 * @param status Current system status level
 * @return Current theme mode
 */
womo_theme_mode_t womo_theme_update(womo_status_level_t status);

/**
 * @brief Get current background color
 * 
 * Returns color based on theme mode and system status
 * 
 * @return LVGL color for background
 */
lv_color_t womo_theme_get_background_color(void);

/**
 * @brief Get current theme mode
 * 
 * @return Current theme mode (day/night/sunrise/sunset)
 */
womo_theme_mode_t womo_theme_get_mode(void);

/**
 * @brief Set system status level
 * 
 * Changes background color to indicate warnings/errors
 * 
 * @param status New status level
 */
void womo_theme_set_status(womo_status_level_t status);

/**
 * @brief Get current system status
 * 
 * @return Current status level
 */
womo_status_level_t womo_theme_get_status(void);

/**
 * @brief Manually set sunrise/sunset times
 * 
 * Use if GPS location not available
 * 
 * @param sunrise_hour Hour of sunrise (0-23)
 * @param sunrise_min Minute of sunrise (0-59)
 * @param sunset_hour Hour of sunset (0-23)
 * @param sunset_min Minute of sunset (0-59)
 */
void womo_theme_set_sun_times(uint8_t sunrise_hour, uint8_t sunrise_min,
                               uint8_t sunset_hour, uint8_t sunset_min);

/**
 * @brief Get current sunrise/sunset times
 * 
 * @return Pointer to sun times structure
 */
const womo_sun_times_t* womo_theme_get_sun_times(void);

/**
 * @brief Check if currently daytime
 * 
 * @return true if between sunrise and sunset
 */
bool womo_theme_is_daytime(void);

/**
 * @brief Apply theme to LVGL screen
 * 
 * Sets background color of main screen
 * 
 * @param screen LVGL screen object (or NULL for default screen)
 */
void womo_theme_apply_to_screen(lv_obj_t *screen);

/**
 * @brief Cycle to next theme mode (for touch control)
 * 
 * Cycles through: DAY -> SUNSET -> NIGHT -> SUNRISE -> DAY
 */
void womo_theme_cycle_mode(void);

/**
 * @brief Cycle to next status level (for touch control)
 * 
 * Cycles through: OK -> WARNING -> ERROR -> CRITICAL -> OK
 */
void womo_theme_cycle_status(void);

/**
 * @brief Set theme mode manually (disables automatic time-based switching)
 * 
 * @param mode Desired theme mode
 */
void womo_theme_set_mode(womo_theme_mode_t mode);

/**
 * @brief Enable/disable automatic time-based theme switching
 * 
 * @param enable true = automatic, false = manual control
 */
void womo_theme_set_auto_mode(bool enable);

/**
 * @brief Check if automatic mode is enabled
 * 
 * @return true if automatic, false if manual
 */
bool womo_theme_is_auto_mode(void);

#endif // WOMO_THEME_H
