/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/**
 * ruuvi_ble_scanner.h
 *
 * Eigenständiger Ruuvi BLE Scanner — zwei Slots:
 *
 *   Indoor  (has_pressure = true)  — Ruuvi Standard mit BME280/BME688
 *   Outdoor (has_pressure = false) — Ruuvi IP68 ohne Drucksensor
 *
 * Erkennung automatisch anhand Ruuvi DF5: raw_press 0xFFFF = kein Sensor.
 * MAC-Filter pro Slot über SENSOR_RUUVI_INDOOR_MAC / SENSOR_RUUVI_OUTDOOR_MAC
 * in sensor_config.h (leer = erster passender Tag wird akzeptiert).
 *
 * Kabel-Sensor hat Vorrang: Ruuvi wird nur in espnow_modem als Fallback
 * genutzt, wenn der jeweilige BME-Sensor nicht verfügbar ist.
 */

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>
#include <host/ble_gap.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float   temp_c;
    float   humidity_pct;
    float   pressure_hpa;   // nur gültig wenn has_pressure = true
    bool    has_pressure;   // false = IP68-Außentag ohne Drucksensor
    int8_t  rssi;
    uint8_t mac[6];         // BLE-Adresse (LSB-first, wie vom Stack geliefert)
    bool    valid;          // true sobald mind. ein gültiges Paket empfangen
    int64_t timestamp_us;
} ruuvi_snapshot_t;

/**
 * @brief Mutex anlegen + beim BLE-Manager als Advertisement-Handler registrieren.
 *        Muss nach ble_manager_init() aufgerufen werden.
 */
esp_err_t ruuvi_ble_scanner_init(void);

/** @brief Innen-Snapshot (Tag mit Luftdruck) thread-safe auslesen. */
void ruuvi_ble_scanner_get_indoor(ruuvi_snapshot_t *out);

/** @brief Außen-Snapshot (Tag ohne Luftdruck) thread-safe auslesen. */
void ruuvi_ble_scanner_get_outdoor(ruuvi_snapshot_t *out);

#ifdef __cplusplus
}
#endif
