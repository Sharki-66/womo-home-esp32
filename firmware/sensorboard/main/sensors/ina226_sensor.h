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
    bool valid;
    float v_bus_v;       // Busspannung (V)
    float i_a;           // Strom (A, positiv = Verbrauch)
    float p_w;           // Leistung (W)
    float v_shunt_mv;    // Shuntspannung (mV, Diagnose)
    int64_t timestamp_us;
} ina226_snapshot_t;

/**
 * INA226 initialisieren und Messtask starten.
 * Gibt ESP_ERR_NOT_FOUND zurück wenn der Sensor nicht antwortet.
 */
esp_err_t ina226_app_start(void);

/** Aktuellen Snapshot abholen (thread-safe). */
esp_err_t ina226_app_get_snapshot(ina226_snapshot_t *out);

#ifdef __cplusplus
}
#endif
