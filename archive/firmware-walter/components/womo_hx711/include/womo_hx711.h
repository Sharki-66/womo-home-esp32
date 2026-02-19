#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "walter_config.h"
#include "hx711.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WOMO_HX711_GAIN_A_128 = 1,
    WOMO_HX711_GAIN_B_32 = 2,
    WOMO_HX711_GAIN_A_64 = 3
} womo_hx711_gain_t;

typedef struct {
    hx711_t hx;
    gpio_num_t dout_gpio;
    gpio_num_t sck_gpio;
    womo_hx711_gain_t current_gain;
    womo_hx711_gain_t target_gain;
    int32_t offset;
    float scale;
    bool initialized;
} womo_hx711_t;

esp_err_t womo_hx711_init(womo_hx711_t *dev, gpio_num_t dout_gpio, gpio_num_t sck_gpio, womo_hx711_gain_t gain);
bool womo_hx711_is_ready(const womo_hx711_t *dev);
esp_err_t womo_hx711_read_raw(womo_hx711_t *dev, int32_t *out_value);
esp_err_t womo_hx711_read_average(womo_hx711_t *dev, size_t samples, int32_t *out_value);
esp_err_t womo_hx711_set_gain(womo_hx711_t *dev, womo_hx711_gain_t gain);
esp_err_t womo_hx711_power_down(womo_hx711_t *dev);
esp_err_t womo_hx711_power_up(womo_hx711_t *dev);
void womo_hx711_set_offset(womo_hx711_t *dev, int32_t offset);
void womo_hx711_set_scale(womo_hx711_t *dev, float scale);
int32_t womo_hx711_get_offset(const womo_hx711_t *dev);
float womo_hx711_get_scale(const womo_hx711_t *dev);
float womo_hx711_convert_to_units(const womo_hx711_t *dev, int32_t raw);

#ifdef __cplusplus
}
#endif
