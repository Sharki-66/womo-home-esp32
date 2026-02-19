#ifndef WOMO_WEATHER_HTTP_H
#define WOMO_WEATHER_HTTP_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parsed weather data from Open-Meteo current API.
 */
typedef struct {
    bool valid;                 ///< True when the HTTP request and JSON parse succeeded
    int weather_id;             ///< WMO weather code from Open-Meteo (e.g. 0, 3, 61, 95)
    bool is_night;              ///< True when API reports is_day == 0
    float temperature_c;        ///< Current temperature in °C
    float feels_like_c;         ///< Not provided by Open-Meteo (kept for compatibility, set 0)
    float pressure_hpa;         ///< Atmospheric pressure in hPa (pressure_msl)
    float humidity_percent;     ///< Relative humidity in % (relative_humidity_2m)
    float wind_speed_ms;        ///< Wind speed in m/s (wind_speed_10m)
    char description[48];       ///< Short text derived from WMO code (ASCII)
} womo_weather_http_data_t;

typedef void (*womo_weather_http_callback_t)(const womo_weather_http_data_t *data, void *user_data);

/**
 * @brief Start periodic weather updates from Open-Meteo.
 *
 * Requires Wi-Fi to be connected. Configuration values (coordinates,
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

/**
 * @brief Update the GPS coordinates used for weather queries.
 *
 * Call this whenever a valid GPS fix is obtained. The next weather
 * fetch will use these coordinates instead of the default (Rodgau).
 * Thread-safe (called from router_poll_task, consumed by weather task).
 *
 * @param lat  Latitude  in decimal degrees
 * @param lon  Longitude in decimal degrees
 */
void womo_weather_http_set_location(double lat, double lon);

#ifdef __cplusplus
}
#endif

#endif // WOMO_WEATHER_HTTP_H
