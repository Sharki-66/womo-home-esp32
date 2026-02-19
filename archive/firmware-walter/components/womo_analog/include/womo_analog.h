#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t battery_mv[2];
    float battery_v[2];
    int32_t tank_mv[2];
    float tank_v[2];
    uint8_t tank_percent[2];
    bool battery_valid[2];
    bool tank_valid[2];
} womo_analog_data_t;

esp_err_t womo_analog_init(void);
void womo_analog_deinit(void);
esp_err_t womo_analog_read(womo_analog_data_t *out);

#ifdef __cplusplus
}
#endif
