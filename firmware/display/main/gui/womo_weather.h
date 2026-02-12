#ifndef WOMO_WEATHER_H
#define WOMO_WEATHER_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Weather condition codes
 */
typedef enum {
    WEATHER_UNKNOWN = 0,
    WEATHER_CLEAR,
    WEATHER_SUNNY,
    WEATHER_PARTLYSUNNY,
    WEATHER_PARTLYCLOUDY,
    WEATHER_MOSTLYSUNNY,
    WEATHER_MOSTLYCLOUDY,
    WEATHER_CLOUDY,
    WEATHER_CHANCERAIN,
    WEATHER_RAIN,
    WEATHER_CHANCESNOW,
    WEATHER_SNOW,
    WEATHER_CHANCESLEET,
    WEATHER_SLEET,
    WEATHER_CHANCETSTORMS,
    WEATHER_TSTORMS,
    WEATHER_FLURRIES,
    WEATHER_FOG,
    WEATHER_HAZY,
    // Night versions
    WEATHER_NT_CLEAR,
    WEATHER_NT_SUNNY,
    WEATHER_NT_PARTLYSUNNY,
    WEATHER_NT_PARTLYCLOUDY,
    WEATHER_NT_MOSTLYSUNNY,
    WEATHER_NT_MOSTLYCLOUDY,
    WEATHER_NT_CLOUDY,
    WEATHER_NT_CHANCERAIN,
    WEATHER_NT_RAIN,
    WEATHER_NT_CHANCESNOW,
    WEATHER_NT_SNOW,
    WEATHER_NT_CHANCESLEET,
    WEATHER_NT_SLEET,
    WEATHER_NT_CHANCETSTORMS,
    WEATHER_NT_TSTORMS,
    WEATHER_NT_CHANCEFLURRIES,
    WEATHER_NT_FLURRIES,
    WEATHER_NT_FOG,
    WEATHER_NT_HAZY,
    WEATHER_NT_UNKNOWN
} womo_weather_condition_t;

/**
 * Weather widget structure
 */
typedef struct {
    lv_obj_t *container;          // Main container
    lv_obj_t *weather_icon;       // Weather icon image
    lv_obj_t *temp_label;         // Temperature label
    womo_weather_condition_t condition;  // Current weather condition
    int16_t temperature_c;        // Temperature in Celsius
    bool is_night;               // Day/night mode
    char icon_path[128];         // Current icon file path
} womo_weather_t;

/**
 * @brief Create weather widget in top-right corner
 * @param parent Parent object (screen)
 * @return Weather widget instance or NULL on error
 */
womo_weather_t* womo_weather_create(lv_obj_t *parent);

/**
 * @brief Update weather condition and icon
 * @param weather Weather widget instance
 * @param condition Weather condition code
 * @param is_night Day/night mode
 */
void womo_weather_set_condition(womo_weather_t *weather, womo_weather_condition_t condition, bool is_night);

/**
 * @brief Update temperature display
 * @param weather Weather widget instance
 * @param temperature_c Temperature in Celsius
 */
void womo_weather_set_temperature(womo_weather_t *weather, int16_t temperature_c);

/**
 * @brief Set weather widget position
 * @param weather Weather widget instance
 * @param x X coordinate
 * @param y Y coordinate
 */
void womo_weather_set_pos(womo_weather_t *weather, lv_coord_t x, lv_coord_t y);

/**
 * @brief Set weather widget visibility
 * @param weather Weather widget instance
 * @param visible Visibility state
 */
void womo_weather_set_visible(womo_weather_t *weather, bool visible);

/**
 * @brief Delete weather widget
 * @param weather Weather widget instance
 */
void womo_weather_delete(womo_weather_t *weather);

/**
 * @brief Get weather condition name for debugging
 * @param condition Weather condition code
 * @return Weather condition name string
 */
const char* womo_weather_get_condition_name(womo_weather_condition_t condition);

#endif // WOMO_WEATHER_H