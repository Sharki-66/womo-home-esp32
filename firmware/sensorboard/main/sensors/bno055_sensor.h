#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bno055_app_start(void);

// Aktuellen Pitch/Roll als Nullpunkt setzen (NVS-persistent).
esp_err_t bno055_app_zero_pitch_roll(void);

// Temporär schnelleren Messzyklus anfordern (z. B. für Web-Horizont)
void bno055_app_request_fast(uint32_t duration_ms, uint32_t interval_ms);

#ifdef __cplusplus
}
#endif
