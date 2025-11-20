#ifndef WOMO_WEATHER_HTTP_H
#define WOMO_WEATHER_HTTP_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parsed weather data from OpenWeatherMap current weather API.
 */
typedef struct {
    bool valid;                 ///< True when the HTTP request and JSON parse succeeded
    int weather_id;             ///< OpenWeatherMap weather condition ID (e.g. 200, 800, ...)
    bool is_night;              ///< True when API reports a night icon ("**n")
    float temperature_c;        ///< Current temperature in °C
    float feels_like_c;         ///< Feels-like temperature in °C
    float pressure_hpa;         ///< Atmospheric pressure in hPa
    float humidity_percent;     ///< Relative humidity in %
    float wind_speed_ms;        ///< Wind speed in m/s
    char description[48];       ///< Lowercase description text from the API (UTF-8)
} womo_weather_http_data_t;

typedef void (*womo_weather_http_callback_t)(const womo_weather_http_data_t *data, void *user_data);

/**
 * @brief Start periodic weather updates from OpenWeatherMap.
 *
 * Requires Wi-Fi to be connected. Configuration values (API key, coordinates,
 * refresh interval) are read from sdkconfig (menuconfig).
 *
 * @param callback Function invoked on every successful update (may be NULL).
 * @param user_data Opaque pointer forwarded to the callback.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already running,
 *         or ESP_ERR_INVALID_ARG when mandatory configuration is missing.
 */
esp_err_t womo_weather_http_start(womo_weather_http_callback_t callback, void *user_data);

/**
 * @brief Stop the periodic weather update task.
 */
esp_err_t womo_weather_http_stop(void);

/**
 * @brief Returns true when the weather update task is active.
 */
bool womo_weather_http_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // WOMO_WEATHER_HTTP_H
