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
    STR_RS485_WAITING,
    STR_RS485_PACKETS,
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
    // Weekdays
    STR_WEEKDAY_SUN,
    STR_WEEKDAY_MON,
    STR_WEEKDAY_TUE,
    STR_WEEKDAY_WED,
    STR_WEEKDAY_THU,
    STR_WEEKDAY_FRI,
    STR_WEEKDAY_SAT,
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

#endif // WOMO_LOCALE_H
