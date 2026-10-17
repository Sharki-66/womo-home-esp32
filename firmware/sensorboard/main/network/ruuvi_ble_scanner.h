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
 * MAC-Filter pro Slot wird per RS485 vom Display konfiguriert (cmd "ruuvi_config")
 * und in NVS (sensor_config.h: SENSOR_RUUVI_NVS_*) persistiert – leer/unkonfiguriert
 * = Slot deaktiviert, kein Tag wird akzeptiert.
 *
 * Kabel-Sensor hat Vorrang: Ruuvi wird nur in espnow_modem als Fallback
 * genutzt, wenn der jeweilige BME-Sensor nicht verfügbar ist.
 */

#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <host/ble_gap.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float    temp_c;
    float    humidity_pct;
    float    pressure_hpa;   // nur gültig wenn has_pressure = true
    bool     has_pressure;   // false = IP68-Außentag ohne Drucksensor
    uint16_t battery_mv;     // nur gültig wenn battery_valid = true
    bool     battery_valid;  // false = Power-Info fehlt oder 0x7FF (invalid)
    int8_t   rssi;
    uint8_t  mac[6];         // BLE-Adresse (LSB-first, wie vom Stack geliefert)
    bool     valid;          // true sobald mind. ein gültiges Paket empfangen
    int64_t  timestamp_us;
} ruuvi_snapshot_t;

// Ein zuletzt gesehener Ruuvi-Tag, unabhängig vom konfigurierten Filter –
// für die Auswahlliste im Display (Einstellungen > RTC / Ruuvi).
typedef struct {
    uint8_t mac[6];        // BLE-Adresse (LSB-first, wie vom Stack geliefert)
    int8_t  rssi;
    float   temp_c;
    bool    has_pressure;
    int64_t last_seen_us;
} ruuvi_scan_entry_t;

/**
 * @brief Mutex anlegen, MAC-Filter aus NVS laden + beim BLE-Manager als
 *        Advertisement-Handler registrieren. Muss nach ble_manager_init() aufgerufen werden.
 */
esp_err_t ruuvi_ble_scanner_init(void);

/** @brief Innen-Snapshot (Tag mit Luftdruck) thread-safe auslesen. */
void ruuvi_ble_scanner_get_indoor(ruuvi_snapshot_t *out);

/** @brief Außen-Snapshot (Tag ohne Luftdruck) thread-safe auslesen. */
void ruuvi_ble_scanner_get_outdoor(ruuvi_snapshot_t *out);

/**
 * @brief MAC-Filter zur Laufzeit setzen, in NVS persistieren (vom Display per
 *        RS485-Kommando "ruuvi_config" gesetzt). Leerer String = Slot deaktiviert.
 */
esp_err_t ruuvi_ble_scanner_set_macs(const char *indoor_mac, const char *outdoor_mac);

/**
 * @brief Zuletzt gesehene Ruuvi-Tags (unabhängig vom Filter) für die Auswahl im
 *        Display kopieren. Gibt die Anzahl kopierter Einträge zurück.
 */
size_t ruuvi_ble_scanner_get_scan_list(ruuvi_scan_entry_t *out, size_t max_entries);

#ifdef __cplusplus
}
#endif
