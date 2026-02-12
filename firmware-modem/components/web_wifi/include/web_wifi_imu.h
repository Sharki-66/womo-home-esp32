#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

void web_wifi_imu_update(const web_wifi_imu_snapshot_t *sample);

bool web_wifi_imu_get_snapshot(web_wifi_imu_snapshot_t *out);

#ifdef __cplusplus
}
#endif
