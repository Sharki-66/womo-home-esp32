/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/**
 * gas_source.h
 *
 * Einheitliche Gewichtsquelle für die Gasflaschen A/B:
 *
 *   1. HX711-Modul am Kabel vorhanden (valid_a || valid_b) → HX711 liefert A+B (Vorrang)
 *   2. sonst GasBee (BLE, per MAC Flasche A/B zugeordnet), wenn nicht veraltet
 *      (SENSOR_GASBEE_STALE_MS seit der letzten neuen seq)
 *   3. sonst keine Quelle (alle Kanäle ungültig → nc)
 *
 * Das HX711-Modul wird über DOUT erkannt (Pull-up, Timeout ohne Modul). Eine
 * fehlende Wägezelle bei vorhandenem Modul ist NICHT erkennbar – dieser Fall
 * kommt im Betrieb nicht vor (entweder Kabelwaage komplett oder GasBees).
 *
 * Die Gewichte liegen im selben Format wie beim HX711 vor (Bruttogewicht),
 * daher bleibt die Gaslogik (Tara, Voll-Referenz, Rate) unverändert.
 */

#include "hx711_sensor.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GAS_SRC_NONE = 0,
    GAS_SRC_HX711,
    GAS_SRC_GASBEE,
} gas_src_t;

// Zusatzinfos je Flasche, nur bei GAS_SRC_GASBEE belegt
typedef struct {
    bool     present;        // GasBee zugeordnet und nicht veraltet
    bool     battery_valid;
    uint16_t battery_mv;
    uint8_t  battery_pct;
    bool     charging;
    bool     calibrated;
} gas_bee_info_t;

typedef struct {
    gas_src_t        src;
    hx711_snapshot_t w;      // Gewichte A/B (valid_a/b, kg_a/b, timestamp_us); raw_* nur bei HX711
    gas_bee_info_t   bee_a;
    gas_bee_info_t   bee_b;
} gas_source_snapshot_t;

/** @brief Aktuelle Gewichtsquelle ermitteln und Snapshot füllen (thread-safe). */
void gas_source_get_snapshot(gas_source_snapshot_t *out);

/** @brief Kurzname der Quelle für Topics ("hx", "ble", "none"). */
const char *gas_source_name(gas_src_t src);

#ifdef __cplusplus
}
#endif
