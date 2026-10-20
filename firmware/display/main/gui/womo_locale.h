/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef WOMO_LOCALE_H
#define WOMO_LOCALE_H

#include <stdint.h>

// Supported locales
typedef enum {
    WOMO_LOCALE_DE = 0,  // German
    WOMO_LOCALE_EN = 1,  // English
    WOMO_LOCALE_MAX
} womo_locale_t;

// String IDs for all translatable strings
typedef enum {
    STR_TITLE,
    STR_MODE,
    STR_STATUS,
    STR_WIFI,
    STR_WIFI_DISCONNECTED,
    STR_TEMP,
    STR_HUMID,
    STR_PRESS,
    STR_GAS,
    STR_IMU,
    STR_WEIGHT,
    STR_BATTERY,
    STR_TANKS,
    STR_SENSOR_WAITING,
    STR_SENSOR_PACKETS,
    STR_SENSOR_TIMEOUT_LABEL,
    STR_SENSOR_TIMEOUT_SOURCE,
    STR_SENSOR_WAITING_HELLO,
    STR_SENSOR_INVALID_DATA,
    // Status values
    STR_STATUS_OK,
    STR_STATUS_WARNING,
    STR_STATUS_CRITICAL,
    STR_STATUS_ERROR,
    // Mode values
    STR_MODE_DAY,
    STR_MODE_NIGHT,
    STR_MODE_SUNRISE,
    STR_MODE_SUNSET,
    // Connectivity dialog
    STR_CONNECTIVITY_TITLE,
    STR_AP_SECTION_TITLE,
    STR_AP_STATUS_ACTIVE,
    STR_AP_STATUS_INACTIVE,
    STR_WIFI_SECTION_TITLE,
    STR_WIFI_ENABLE_SWITCH,
    STR_WIFI_SCAN_BUTTON,
    STR_WIFI_SCANNING_STATUS,
    STR_WIFI_SELECT_PLACEHOLDER,
    STR_WIFI_PASSWORD_LABEL,
    STR_WIFI_PASSWORD_PLACEHOLDER,
    STR_WIFI_NO_RESULTS,
    STR_WIFI_STATUS_CONNECTING,
    STR_WIFI_STATUS_DISCONNECTED,
    STR_WIFI_STATUS_ERROR,
    STR_WIFI_ACTION_NOT_AVAILABLE,
    STR_WIFI_CONNECT_BUTTON,
    STR_WIFI_CANCEL_BUTTON,
    STR_LTE_SECTION_TITLE,
    STR_LTE_ENABLE_SWITCH,
    STR_LTE_STATUS_WAITING,
    STR_LTE_STATUS_OFFLINE,
    STR_LTE_STATUS_DISABLING,
    STR_LTE_STATUS_ENABLING,
    STR_MODAL_CLOSE_BUTTON,
    // Gas bottle dialog
    STR_GAS_MODAL_TITLE_FRONT,
    STR_GAS_MODAL_TITLE_REAR,
    STR_GAS_MODAL_QUESTION,
    STR_GAS_MODAL_BTN_YES,
    STR_GAS_MODAL_BTN_NO,
    // Air value headings
    STR_AIR_OUTDOOR,
    STR_AIR_INDOOR,
    // Weekdays
    STR_WEEKDAY_SUN,
    STR_WEEKDAY_MON,
    STR_WEEKDAY_TUE,
    STR_WEEKDAY_WED,
    STR_WEEKDAY_THU,
    STR_WEEKDAY_FRI,
    STR_WEEKDAY_SAT,
    // Settings modal – threshold section
    STR_THRESH_TITLE,
    STR_THRESH_WARNING,
    STR_THRESH_GAS,
    STR_THRESH_FRESH,
    STR_THRESH_GREY,
    // Router LEDs modal
    STR_ROUTER_LEDS_TITLE,
    STR_ROUTER_LEDS_POWER_ON,
    STR_ROUTER_LEDS_POWER_OFF,
    STR_ROUTER_LEDS_WIFI_DISCONNECTED,
    STR_ROUTER_LEDS_LTE_OFFLINE,
    // Tank captions
    STR_TANK_FRESH,
    STR_TANK_GREY,
    STR_TANK_FRESH_PLACEHOLDER,
    // Gas bottle position labels
    STR_GAS_FRONT,
    STR_GAS_REAR,
    // Settings modal title
    STR_SETTINGS_TITLE,
    // Sound section in settings
    STR_SOUND_SECTION,
    STR_SOUND_TONES,
    STR_SOUND_TOUCH_CLICK,
    // Battery label
    STR_BATTERY_KFZ,
    // Forecast modal
    STR_FORECAST_TITLE,
    // Settings modal – Tabs
    STR_SET_TAB_AP,
    STR_SET_TAB_SENSORS,
    STR_SET_TAB_THRESH,
    STR_SET_TAB_SYSTEM,
    // Settings modal – System-Tab
    STR_ON,
    STR_OFF,
    STR_LANG_SECTION,
    STR_TIME_SECTION,
    STR_TIME_SOURCE,
    STR_TIME_LAST_SYNC,
    STR_TIME_RTC_BATT,
    STR_TIME_SRC_RTC,
    STR_TIME_SYNC_NEVER,
    STR_TIME_AGO_S,       // Format mit %lu
    STR_TIME_AGO_MIN,     // Format mit %lu
    STR_TIME_AGO_H,       // Format mit %lu
    STR_TIME_BATT_UNKNOWN,
    STR_TIME_BATT_LOW,
    STR_TIME_RTC_POWER_LOST,
    // Settings modal – Sensoren-Tab
    STR_SENS_RUUVI_SECTION,
    STR_SENS_GASBEE_SECTION,
    STR_SENS_INDOOR,
    STR_SENS_OUTDOOR,
    STR_SENS_BOTTLE_A,
    STR_SENS_BOTTLE_B,
    STR_SENS_SCAN_BTN,
    STR_SENS_SETUP_BTN,
    // Unwetter-Auswertung (womo_weather_warn) – Bezeichnungen nach DWD
    STR_WARN_WIND_GUSTS,
    STR_WARN_WIND_STORM,
    STR_WARN_WIND_STORM_SEVERE,
    STR_WARN_WIND_HURRICANE_LIKE,
    STR_WARN_WIND_HURRICANE,
    STR_WARN_WIND_EXTREME,
    STR_WARN_RAIN_HEAVY,
    STR_WARN_RAIN_HEAVY_SEVERE,
    STR_WARN_RAIN_HEAVY_EXTREME,
    STR_WARN_RAIN_LONG,
    STR_WARN_RAIN_LONG_SEVERE,
    STR_WARN_RAIN_LONG_EXTREME,
    STR_WARN_SNOW_LIGHT,
    STR_WARN_SNOW,
    STR_WARN_SNOW_SEVERE,
    STR_WARN_SNOW_EXTREME,
    STR_WARN_THUNDER,
    STR_WARN_THUNDER_HAIL,
    STR_WARN_ICE,
    STR_WARN_ICE_GLAZE,
    STR_WARN_FROST,
    STR_WARN_FROST_SEVERE,
    STR_WARN_HEAT,
    STR_WARN_HEAT_EXTREME,
    STR_WARN_FMT_UPTO,       // Format: Name, Wert (%.0f), Einheit
    STR_WARN_FMT_SUM,        // Format: Name, Wert (%.0f), Einheit, Stunden (%u)
    STR_WARN_FMT_DOWNTO,     // Format: Name, Wert (%.0f)
    STR_WARN_FMT_FEELS,      // Format: Name, Wert (%.0f)
    // Unwetter – Anzeige (Forecast-Modal, Popup)
    STR_WARN_NONE,
    STR_WARN_HDR_ONE,        // Format: Anzahl (%u)
    STR_WARN_HDR_MANY,       // Format: Anzahl (%u)
    STR_WARN_MORE,           // Format: Anzahl (%u)
    STR_WARN_SOURCE,
    STR_WARN_POPUP_TITLE,
    STR_WARN_LOCATION,       // Format: Ortsname (%s)
    // Max value
    STR_MAX
} womo_string_id_t;

/**
 * @brief Initialize locale system
 */
void womo_locale_init(void);

/**
 * @brief Set current locale
 * @param locale Locale to set (DE or EN)
 */
void womo_locale_set(womo_locale_t locale);

/**
 * @brief Get current locale
 * @return Current locale
 */
womo_locale_t womo_locale_get(void);

/**
 * @brief Get string for current locale
 * @param str_id String ID
 * @return Pointer to string (never NULL)
 */
const char* womo_locale_get_string(womo_string_id_t str_id);

/**
 * @brief Get weekday string for current locale
 * @param day Day of week (0=Sunday, 1=Monday, ...)
 * @return Pointer to string (never NULL)
 */
const char* womo_locale_get_weekday(uint8_t day);

/**
 * @brief Register a callback that is called whenever the locale changes.
 *        Maximum 4 callbacks can be registered.
 * @param cb Callback function (must not be NULL)
 */
void womo_locale_register_change_cb(void (*cb)(void));

#endif // WOMO_LOCALE_H
