#include "web_wifi_imu.h"

#include "esp_timer.h"
#include <string.h>

static web_wifi_imu_snapshot_t s_last = {
    .valid = false,
    .calibrated = false,
    .yaw_deg = 0.0f,
    .pitch_deg = 0.0f,
    .roll_deg = 0.0f,
    .temperature_c = 0.0f,
    .cal_sys = 0,
    .cal_gyro = 0,
    .cal_accel = 0,
    .cal_mag = 0,
    .timestamp_us = 0,
};

void web_wifi_imu_update(const web_wifi_imu_snapshot_t *snap)
{
    if (!snap) {
        return;
    }
    s_last = *snap;
    if (s_last.timestamp_us == 0) {
        s_last.timestamp_us = esp_timer_get_time();
    }
}

bool web_wifi_imu_get_snapshot(web_wifi_imu_snapshot_t *out)
{
    if (!out) {
        return false;
    }
    *out = s_last;
    return true;
}
