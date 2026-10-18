/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/**
 * gasbee_ble_scanner.h
 *
 * Passiver Scanner für GasBee-Gaswaagen (firmware/gasbee_nrf), Advertising-Format v1.
 * Kein Verbindungsaufbau – Gewicht, Akku und Flags kommen aus der Manufacturer Data.
 *
 * Zwei Slots (Flasche A / B), Zuordnung ausschließlich per MAC. Die MACs werden
 * per RS485/ESP-NOW vom Display konfiguriert (cmd "gasbee_config") und in NVS
 * (sensor_config.h: SENSOR_GASBEE_NVS_*) persistiert – leer = Slot deaktiviert.
 *
 * Kabel-HX711 hat Vorrang: GasBee wird nur über gas_source genutzt, wenn das
 * HX711-Modul nicht vorhanden ist.
 */

#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float    weight_kg;      // Bruttogewicht auf der Wägezelle (nur gültig wenn weight_valid)
    bool     weight_valid;   // false = HX711 der GasBee meldet Fehler (Flag Bit 0 / 0x8000)
    uint16_t battery_mv;     // nur gültig wenn battery_valid
    bool     battery_valid;  // false = 0xFFFF
    uint8_t  battery_pct;    // aus battery_mv per LiPo-Kennlinie (SENSOR_GASBEE_LIPO_CURVE)
    bool     charging;       // Flag Bit 1: Akku lädt (nur mit USB)
    bool     calibrated;     // Flag Bit 2: Offset/Skalierung aus NVS der GasBee
    bool     config_mode;    // Flag Bit 3: Konfigurationsmodus (USB angesteckt, Kalibrierung möglich)
    uint8_t  seq;            // Sequenzzähler des letzten Messzyklus
    int8_t   rssi;
    uint8_t  mac[6];         // BLE-Adresse (LSB-first, wie vom Stack geliefert)
    bool     valid;          // true sobald mind. ein gültiges Paket empfangen
    int64_t  timestamp_us;   // Empfangszeit der letzten NEUEN seq (Basis für Veraltung)
} gasbee_snapshot_t;

// Eine zuletzt gesehene GasBee, unabhängig vom konfigurierten Filter –
// für die Auswahlliste im Display (Einstellungen > RTC / Temp / Gas).
typedef struct {
    uint8_t  mac[6];         // BLE-Adresse (LSB-first, wie vom Stack geliefert)
    uint8_t  addr_type;      // BLE-Adresstyp (für den Verbindungsaufbau zur Kalibrierung)
    int8_t   rssi;
    float    weight_kg;
    bool     weight_valid;
    uint16_t battery_mv;
    bool     battery_valid;
    bool     charging;
    bool     calibrated;
    bool     config_mode;    // Flag Bit 3: USB angesteckt
    uint8_t  seq;
    int64_t  last_seen_us;   // letztes Paket (im Konfigurationsmodus jede Sekunde)
} gasbee_scan_entry_t;

/**
 * @brief Mutex anlegen, MAC-Filter aus NVS laden + beim BLE-Manager als
 *        Advertisement-Handler registrieren. Muss nach ble_manager_init() aufgerufen werden.
 */
esp_err_t gasbee_ble_scanner_init(void);

/**
 * @brief Snapshot Flasche A (slot 0) bzw. B (slot 1) thread-safe auslesen.
 *        valid wird hier NICHT auf Veraltung geprüft – das macht gas_source.
 */
void gasbee_ble_scanner_get(int slot, gasbee_snapshot_t *out);

/**
 * @brief MAC-Filter zur Laufzeit setzen, in NVS persistieren (vom Display per
 *        Kommando "gasbee_config" gesetzt). Leerer String = Slot deaktiviert.
 */
esp_err_t gasbee_ble_scanner_set_macs(const char *bottle_a_mac, const char *bottle_b_mac);

/**
 * @brief Zuletzt gesehene GasBees (unabhängig vom Filter) für die Auswahl im
 *        Display kopieren. Gibt die Anzahl kopierter Einträge zurück.
 */
size_t gasbee_ble_scanner_get_scan_list(gasbee_scan_entry_t *out, size_t max_entries);

/**
 * @brief Eintrag der Scan-Liste per MAC suchen ("AA:BB:CC:DD:EE:FF", MSB-first).
 *        Gibt false zurück, wenn die GasBee nicht (mehr) in der Liste steht.
 */
bool gasbee_ble_scanner_find(const char *mac_str, gasbee_scan_entry_t *out);

/** @brief Akkuspannung (mV) per LiPo-Kennlinie in Prozent (0–100) umrechnen. */
uint8_t gasbee_battery_pct(uint16_t mv);

#ifdef __cplusplus
}
#endif
