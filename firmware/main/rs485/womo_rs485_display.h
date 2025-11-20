#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Sensor data structure matching Walter's JSON output
typedef struct {
    // Timestamp
    uint64_t timestamp_ms;
    
    // BNO055 IMU
    struct {
        bool valid;
        float heading_deg;
        float roll_deg;
        float pitch_deg;
        char direction[4];  // "N", "NNO", "NO", etc.
        uint8_t cal_sys;
        uint8_t cal_gyro;
        uint8_t cal_accel;
        uint8_t cal_mag;
    } bno;
    
    // HX711 Load Cells
    struct {
        bool valid;
        float weight_a_kg;
        float weight_b_kg;
        float weight_sum_kg;
    } hx711;
    
    // BME680 Environmental
    struct {
        bool valid;
        float temperature_c;
        float humidity_percent;
        float pressure_hpa;
        float gas_kohm;
        uint16_t iaq;
    } bme680;
    
    // Battery voltages
    struct {
        bool valid;
        float battery1_v;
        float battery2_v;
    } battery;
    
    // Tank levels
    struct {
        bool valid;
        uint8_t tank1_percent;
        uint8_t tank2_percent;
    } tank;
    
    // GPS/GNSS
    struct {
        bool valid;
        double latitude;
        double longitude;
        double altitude_m;
        float speed_kmh;
        float heading_deg;
        uint8_t satellites;
        float confidence_m;
    } gps;
} womo_sensor_data_t;

// Callback for received sensor data
typedef void (*womo_rs485_data_cb_t)(const womo_sensor_data_t *data, void *user_data);

// Initialize RS485 communication
esp_err_t womo_rs485_display_init(void);

// Set callback for full sensor data
void womo_rs485_set_data_callback(womo_rs485_data_cb_t callback, void *user_data);

// Send commands to Walter
esp_err_t womo_rs485_send_level_start(void);
esp_err_t womo_rs485_send_level_stop(void);
esp_err_t womo_rs485_send_tare_a(void);
esp_err_t womo_rs485_send_tare_b(void);

// Get latest sensor data (non-blocking)
bool womo_rs485_get_latest_data(womo_sensor_data_t *data);

#ifdef __cplusplus
}
#endif
