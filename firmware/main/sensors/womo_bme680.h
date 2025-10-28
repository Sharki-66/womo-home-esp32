/*
 * WoMo BME680 Sensor Manager - Header
 * 
 * Manages BME680 environmental sensor (Temperature, Humidity, Pressure, Gas/VOC)
 */

#ifndef WOMO_BME680_H
#define WOMO_BME680_H

#include "esp_err.h"
#include <stdbool.h>

// BME680 sensor data structure
typedef struct {
    float temperature;      // Temperature in °C
    float humidity;         // Relative humidity in %
    float pressure;         // Atmospheric pressure in hPa
    uint32_t gas_resistance; // Gas resistance in Ohms (VOC indicator)
    bool valid;             // True if data is valid
} womo_bme680_data_t;

/**
 * @brief Initialize BME680 sensor
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t womo_bme680_init(void);

/**
 * @brief Deinitialize BME680 sensor
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t womo_bme680_deinit(void);

/**
 * @brief Read sensor data from BME680
 * 
 * @param data Pointer to structure to store sensor data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t womo_bme680_read(womo_bme680_data_t *data);

/**
 * @brief Check if BME680 is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool womo_bme680_is_initialized(void);

/**
 * @brief Get air quality index from gas resistance (0-500, lower is better)
 * Simple estimation: high resistance = good air, low = bad
 * 
 * @param gas_resistance Gas resistance in Ohms
 * @return Air quality index (0=excellent, 500=hazardous)
 */
uint16_t womo_bme680_get_air_quality_index(uint32_t gas_resistance);

#endif // WOMO_BME680_H
