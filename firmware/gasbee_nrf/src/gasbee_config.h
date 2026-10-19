/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <zephyr/sys/util.h>

/* GasBee nRF – Seeed XIAO nRF52840 Konfiguration
 * Einzelkanal HX711 + BLE GATT (protokollgleich mit firmware/gasbee, ESP32-C3)
 * Pinout: DOUT=D2 (P0.28), PD_SCK=D3 (P0.29) → siehe app.overlay
 */

// ── HX711 ────────────────────────────────────────────────────────────────────
#define GASBEE_HX711_STARTUP_MS        1000U
#define GASBEE_HX711_READY_TIMEOUT_MS  200U
#define GASBEE_HX711_AVG_SAMPLES       5U          // Mehr Samples = stabiler
#define GASBEE_HX711_WAKE_TIMEOUT_MS   1000U       // erste Wandlung nach Power-Up
#define GASBEE_HX711_DISCARD_SAMPLES   4U          // Einschwingen nach Power-Up (Datenblatt: 400 ms bei 10 SPS)
// Messintervall: CONFIG_GASBEE_MEASURE_INTERVAL_S (Kconfig, Standard 300 s, debug.conf 60 s)

// Kalibrierung
// Grob-Kalibrierung XIAO + HX711 an 3,3 V, gemessen 2026-10-18 über das Advertising
// (Werte mit alter Kalibrierung OFFSET −278300 / SCALE 0.00003999):
//   leer:                −0,64 kg  (2 Zyklen)
//   14,285 kg aufgelegt: 13,805 kg (Mittel aus 4 Zyklen)
//   SCALE_neu  = 0.00003999 × 14,285 / (13,805 + 0,64)
//   OFFSET_neu = −0,64 / 0.00003999 − 278300  (Rohwert bei leerer Waage)
// Der Nullpunkt enthält alles, was fest auf der Wägezelle sitzt. Tara und
// Feinkalibrierung (Faktor pro GasBee-MAC) übernimmt das Sensorboard.
// Neu ermitteln: Waage leer → Rohwert = OFFSET; Gewicht m auflegen → raw_m;
//   SCALE = m / (raw_m − OFFSET)
#define GASBEE_HX711_OFFSET            -294304
#define GASBEE_HX711_SCALE             0.0000395471f  // kg pro raw-LSB (≈ 25 290 LSB/kg)
// Die Werte oben sind nur Startwerte (gemessen an GasBee 1, F7:8A:BE:C9:83:CB).
// Jeder GasBee wird per GATT kalibriert (0x2B11/0x2B12/0x2B13), das Ergebnis
// liegt pro Gerät in NVS. Erst dann ist Advertising-Flag Bit 2 "kalibriert" gesetzt.
#define GASBEE_CAL_MIN_DELTA_RAW       1000        // min. |raw − OFFSET| beim Referenzgewicht (≈ 40 g)
#define GASBEE_CAL_REF_MAX_G           200000U     // max. Referenzgewicht 200 kg

// Settings (NVS) – Kalibrierung persistent speichern
#define GASBEE_SETTINGS_SUBTREE        "gasbee"
#define GASBEE_SETTINGS_KEY_OFFSET     "offset"
#define GASBEE_SETTINGS_KEY_SCALE      "scale"
#define GASBEE_SETTINGS_KEY_TARE       "tare"

// ── Gas-Berechnung ───────────────────────────────────────────────────────────
#define GASBEE_GAS_FILL_KG             11.0f       // Netto einer vollen 11-kg-Flasche
#define GASBEE_GAS_TARE_KG             10.5f       // Tara-Gewicht (leer)

// ── BLE ──────────────────────────────────────────────────────────────────────
// Name muss exakt "GasBee" sein: Sensorboard filtert darauf (gasbee_ble_scanner.c)
#define GASBEE_BLE_DEVICE_NAME         "GasBee"

// Service UUID: 4a5b6c7d-8e9f-0a1b-2c3d-4e5f60718293 (identisch zu firmware/gasbee)
#define GASBEE_BLE_SERVICE_UUID_VAL \
	BT_UUID_128_ENCODE(0x4a5b6c7d, 0x8e9f, 0x0a1b, 0x2c3d, 0x4e5f60718293)

// Characteristics (16-bit short UUIDs, identisch zu firmware/gasbee)
#define GASBEE_CHR_WEIGHT_KG           0x2B01  // Gewicht in kg (float, Notify)
#define GASBEE_CHR_GAS_PCT             0x2B02  // Füllstand in % (uint8, Notify)
#define GASBEE_CHR_GAS_NET_KG          0x2B03  // Nettogewicht Gas in kg (float, Notify)
#define GASBEE_CHR_TARE                0x2B10  // Tara-Befehl (Write, 1 Byte: 0x01 = Tare)
#define GASBEE_CHR_CAL_ZERO            0x2B11  // Kalibrierung Nullpunkt (Write 0x01, Waage leer)
#define GASBEE_CHR_CAL_REF_G           0x2B12  // Kalibrierung Referenzgewicht (Write uint32 Gramm)
#define GASBEE_CHR_CAL_VALUES          0x2B13  // Kalibrierwerte int32 Offset + float32 Scale (Read/Write)

#define GASBEE_CHR_BAT_MV              0x2B04  // Akkuspannung in mV (uint16, Notify)

// ── Advertising-Format v1 (Manufacturer Specific Data) ───────────────────────
// Nach jeder Messung ein Burst aus GASBEE_ADV_BURST_COUNT Paketen im Abstand
// GASBEE_BLE_ADV_INTERVAL_MS, danach Funkstille bis zur nächsten Messung.
#define GASBEE_BLE_ADV_INTERVAL_MS     1000
#define GASBEE_ADV_BURST_COUNT         10
#define GASBEE_ADV_COMPANY_ID          0xFFFF  // SIG: Test/interne Nutzung
#define GASBEE_ADV_FORMAT_VERSION      1
#define GASBEE_ADV_WEIGHT_INVALID      INT16_MIN   // 0x8000
#define GASBEE_ADV_BAT_INVALID         UINT16_MAX  // 0xFFFF
#define GASBEE_ADV_FLAG_HX711_OK       BIT(0)
#define GASBEE_ADV_FLAG_CHARGING       BIT(1)  // BQ25101 ~CHG an P0.17
#define GASBEE_ADV_FLAG_CALIBRATED     BIT(2)  // Offset + Scale aus NVS (per GATT kalibriert)
#define GASBEE_ADV_FLAG_CONFIG_MODE    BIT(3)  // Konfigurationsmodus (USB): Dauersenden, Kalibrierung möglich

// ── Konfigurationsmodus (USB/VBUS vorhanden) ─────────────────────────────────
#define GASBEE_USB_POLL_MS             2000U   // VBUS-Abfrage, Moduswechsel spätestens nach 2 s
#define GASBEE_CONFIG_MEASURE_MS       5000U   // Messtakt im Konfigurationsmodus (live während der Kalibrierung)

// ── Status-LED (nur mit CONFIG_GASBEE_STATUS_LED) ────────────────────────────
#define GASBEE_LED_FLASH_MS            80     // Dauer eines Blitzes / einer Pause
#define GASBEE_LED_ADV_BLINK_MS        1000   // Blink-Periode Blau beim Advertising
