#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "walter_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    bool fallback;
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
} womo_web_imu_sample_t;

#if WALTER_ENABLE_WEBUI
esp_err_t womo_web_start(void);
void womo_web_publish_imu(const womo_web_imu_sample_t *sample);
#else
static inline esp_err_t womo_web_start(void)
{
    return ESP_OK;
}

static inline void womo_web_publish_imu(const womo_web_imu_sample_t *sample)
{
    (void)sample;
}
#endif

#ifdef __cplusplus
}
#endif
