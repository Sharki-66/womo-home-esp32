#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t womo_rs485_init(void);
void womo_rs485_deinit(void);
esp_err_t womo_rs485_write(const uint8_t *data, size_t length, TickType_t ticks_to_wait);
int womo_rs485_read(uint8_t *buffer, size_t max_length, TickType_t ticks_to_wait);
void womo_rs485_flush_input(void);

#ifdef __cplusplus
}
#endif
