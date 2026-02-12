#pragma once

#include <stdbool.h>
#include <stdint.h>

// Snapshot-Daten für IMU, bereitgestellt vom BNO055-Task.
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

// Speichert den aktuellen IMU-Status.
void web_wifi_imu_update(const web_wifi_imu_snapshot_t *snap);

// Liefert den letzten gespeicherten Snapshot; gibt true zurück, wenn out gesetzt wurde.
bool web_wifi_imu_get_snapshot(web_wifi_imu_snapshot_t *out);
