/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t raw_a;
    int32_t raw_b;
    float kg_a;
    float kg_b;
    bool valid_a;
    bool valid_b;
    int64_t timestamp_us;
} hx711_snapshot_t;

esp_err_t hx711_app_start(void);

esp_err_t hx711_app_get_snapshot(hx711_snapshot_t *out);

/** HX711 in Power-Down versetzen (vor Deep Sleep aufrufen). */
void hx711_app_sleep(void);

#ifdef __cplusplus
}
#endif
