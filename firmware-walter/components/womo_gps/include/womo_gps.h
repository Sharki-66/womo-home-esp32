/**
 * @file womo_gps.h
 * @brief GPS/GNSS wrapper for Walter Modem
 */

#ifndef WOMO_GPS_H
#define WOMO_GPS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GPS data structure
 */
typedef struct {
    bool valid;                 ///< True if GPS fix is valid
    double latitude;            ///< Latitude in degrees (-90.0 to +90.0)
    double longitude;           ///< Longitude in degrees (-180.0 to +180.0)
    double altitude_m;          ///< Altitude above sea level in meters
    float speed_kmh;            ///< Ground speed in km/h
    float heading_deg;          ///< Heading in degrees (0-360)
    uint8_t satellites;         ///< Number of satellites used
    float confidence_m;         ///< Horizontal confidence in meters
    int64_t timestamp;          ///< Unix timestamp of fix
    uint32_t time_to_fix_ms;    ///< Time to get fix in milliseconds
} womo_gps_data_t;

/**
 * @brief Initialize GPS subsystem
 * 
 * This initializes the Walter modem and configures the GNSS receiver.
 * Must be called before any other GPS functions.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t womo_gps_init(void);

/**
 * @brief Request a GPS fix (non-blocking)
 * 
 * This starts the GNSS fix process. The fix will be received asynchronously
 * via the internal event handler. Use womo_gps_get_last_fix() to retrieve it.
 * 
 * @note LTE must be disconnected before requesting a GNSS fix (shared radio)
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t womo_gps_request_fix(void);

/**
 * @brief Get the last received GPS fix
 * 
 * @param[out] data Pointer to GPS data structure to fill
 * @return ESP_OK if valid fix available, ESP_ERR_NOT_FOUND if no fix yet
 */
esp_err_t womo_gps_get_last_fix(womo_gps_data_t *data);

/**
 * @brief Check if a valid GPS fix is available
 * 
 * @return true if valid fix is available, false otherwise
 */
bool womo_gps_is_valid(void);

/**
 * @brief Get GNSS subsystem UTC time
 * 
 * @param[out] epoch_time Pointer to store Unix timestamp
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t womo_gps_get_utc_time(int64_t *epoch_time);

/**
 * @brief Set GNSS subsystem UTC time
 * 
 * @param epoch_time Unix timestamp to set
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t womo_gps_set_utc_time(int64_t epoch_time);

#ifdef __cplusplus
}
#endif

#endif // WOMO_GPS_H
