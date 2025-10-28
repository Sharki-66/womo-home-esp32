/*
 * WoMo BME680 Sensor Manager - Implementation
 * Uses esp-idf-lib BME680 driver with proper calibration
 */

#include "womo_bme680.h"
#include "bme680.h"  // esp-idf-lib
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "womo_bme680";

// I2C configuration (shared with display/touch)
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_SDA_IO       8
#define I2C_MASTER_SCL_IO       9
#define BME680_I2C_ADDR         BME680_I2C_ADDR_1  // 0x77

static bme680_t bme680_dev = { 0 };
static bool initialized = false;

esp_err_t womo_bme680_init(void)
{
    if (initialized) {
        ESP_LOGW(TAG, "BME680 already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing BME680 sensor using esp-idf-lib...");
    ESP_LOGI(TAG, "I2C address 0x%02X on port %d", BME680_I2C_ADDR, I2C_MASTER_NUM);
    
    // Initialize device descriptor (doesn't install I2C bus, just registers device)
    esp_err_t ret = bme680_init_desc(&bme680_dev, BME680_I2C_ADDR, I2C_MASTER_NUM, 
                                      I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init BME680 descriptor: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize sensor (reads calibration from sensor, resets, configures defaults)
    ret = bme680_init_sensor(&bme680_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init BME680 sensor: %s", esp_err_to_name(ret));
        bme680_free_desc(&bme680_dev);
        return ret;
    }
    
    // Configure sensor settings for weather station application
    // Oversampling rates: Temperature x8, Pressure x4, Humidity x2
    bme680_set_oversampling_rates(&bme680_dev, BME680_OSR_8X, BME680_OSR_4X, BME680_OSR_2X);
    
    // IIR filter coefficient: 3 (smooth out noise)
    bme680_set_filter_size(&bme680_dev, BME680_IIR_SIZE_3);
    
    // Configure gas heater profile: 320°C for 150ms (standard for air quality)
    bme680_set_heater_profile(&bme680_dev, 0, 320, 150);
    bme680_use_heater_profile(&bme680_dev, 0);
    
    initialized = true;
    ESP_LOGI(TAG, "BME680 initialized successfully with calibration");
    
    return ESP_OK;
}

esp_err_t womo_bme680_deinit(void)
{
    if (!initialized) {
        return ESP_OK;
    }
    
    bme680_free_desc(&bme680_dev);
    initialized = false;
    ESP_LOGI(TAG, "BME680 deinitialized");
    
    return ESP_OK;
}

esp_err_t womo_bme680_read(womo_bme680_data_t *data)
{
    if (!initialized) {
        ESP_LOGE(TAG, "BME680 not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (data == NULL) {
        ESP_LOGE(TAG, "NULL data pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Force a single measurement
    esp_err_t ret = bme680_force_measurement(&bme680_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start measurement: %s", esp_err_to_name(ret));
        data->valid = false;
        return ret;
    }
    
    // Wait for measurement to complete (duration depends on oversampling settings)
    uint32_t duration;
    ret = bme680_get_measurement_duration(&bme680_dev, &duration);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get measurement duration: %s", esp_err_to_name(ret));
        duration = 150;  // Fallback: typical duration ~150ms
    }
    vTaskDelay(pdMS_TO_TICKS(duration));
    
    // Read calibrated float results from sensor
    bme680_values_float_t values;
    ret = bme680_get_results_float(&bme680_dev, &values);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read results: %s", esp_err_to_name(ret));
        data->valid = false;
        return ret;
    }
    
    // Copy calibrated values to output structure
    data->temperature = values.temperature;
    data->pressure = values.pressure;
    data->humidity = values.humidity;
    data->gas_resistance = values.gas_resistance;
    data->valid = true;
    
    ESP_LOGI(TAG, "T: %.1f°C, H: %.1f%%, P: %.1f hPa, Gas: %.0f Ohms",
             data->temperature, data->humidity, data->pressure, data->gas_resistance);
    
    return ESP_OK;
}

bool womo_bme680_is_initialized(void)
{
    return initialized;
}

uint16_t womo_bme680_get_air_quality_index(uint32_t gas_resistance)
{
    // Simple air quality estimation based on gas resistance
    // BME680 gas resistance typically ranges from 10kΩ (bad) to 200kΩ+ (good)
    // Map to IAQ-like scale: 0-50=Excellent, 51-100=Good, 101-150=Moderate, 
    // 151-200=Poor, 201-300=Unhealthy, 301+=Hazardous
    
    if (gas_resistance > 200000) {
        return 0 + (250000 - gas_resistance) / 1000;  // Excellent (0-50)
    } else if (gas_resistance > 100000) {
        return 50 + (200000 - gas_resistance) / 2000; // Good (50-100)
    } else if (gas_resistance > 50000) {
        return 100 + (100000 - gas_resistance) / 1000; // Moderate (100-150)
    } else if (gas_resistance > 20000) {
        return 150 + (50000 - gas_resistance) / 600;  // Poor (150-200)
    } else if (gas_resistance > 10000) {
        return 200 + (20000 - gas_resistance) / 100;  // Unhealthy (200-300)
    } else {
        return 300 + (10000 - gas_resistance) / 50;   // Hazardous (300-500)
    }
}
