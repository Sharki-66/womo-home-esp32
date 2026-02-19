#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// IMU-Snapshot (Shared-State zwischen BNO055-Task und RS485-Modem)
typedef struct {
    bool valid;
    bool calibrated;
    float yaw_deg;
    float pitch_deg;
    float roll_deg;
    float temperature_c;
    uint8_t cal_sys;
    uint8_t cal_gyro;
    uint8_t cal_accel;
    uint8_t cal_mag;
    int64_t timestamp_us;
} web_wifi_imu_snapshot_t;

void web_wifi_imu_update(const web_wifi_imu_snapshot_t *snap);
bool web_wifi_imu_get_snapshot(web_wifi_imu_snapshot_t *out);

esp_err_t bno055_app_start(void);

// Aktuellen Pitch/Roll als Nullpunkt setzen (NVS-persistent).
esp_err_t bno055_app_zero_pitch_roll(void);

// Temporär schnelleren Messzyklus anfordern (z. B. für Web-Horizont)
void bno055_app_request_fast(uint32_t duration_ms, uint32_t interval_ms);

#ifdef __cplusplus
}
#endif
