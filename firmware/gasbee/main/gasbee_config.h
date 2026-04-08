/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* GasBee – ESP32-C3 Mini Configuration
 * Einzelkanal HX711 + BLE GATT
 * Pinout: DOUT=GPIO2, SCK=GPIO3 (frei wählbar, kein PSRAM-Konflikt am C3)
 */

// ── HX711 ────────────────────────────────────────────────────────────────────
#define GASBEE_HX711_DOUT_GPIO      4   // GPIO2 wäre Strapping-Pin → GPIO4
#define GASBEE_HX711_SCK_GPIO       5
#define GASBEE_HX711_STARTUP_MS     1000U
#define GASBEE_HX711_READY_TIMEOUT  200U
#define GASBEE_HX711_AVG_SAMPLES    5U          // Mehr Samples = stabiler
#define GASBEE_HX711_POLL_INTERVAL_MS 5000U     // Messung alle 5 s

// Kalibrierung gemessen 21.02.2026:
//   Leer:          raw = -278300
//   5 x 3,6 kg = 18 kg: raw = 171790
//   scale = 18.0 / (171790 - (-278300)) = 18.0 / 450090 = 0.00003999
#define GASBEE_HX711_OFFSET         -278300
#define GASBEE_HX711_SCALE          0.00003999f    // kg pro raw-LSB

// NVS – Kalibrierung persistent speichern
#define GASBEE_NVS_NAMESPACE        "gasbee"
#define GASBEE_NVS_KEY_OFFSET       "offset"
#define GASBEE_NVS_KEY_SCALE        "scale"
#define GASBEE_NVS_KEY_TARE         "tare"

// ── Gas-Berechnung ───────────────────────────────────────────────────────────
#define GASBEE_GAS_FILL_KG          11.0f       // Netto einer vollen 11-kg-Flasche
#define GASBEE_GAS_TARE_KG          10.5f       // Tara-Gewicht (leer)

// ── BLE ──────────────────────────────────────────────────────────────────────
#define GASBEE_BLE_DEVICE_NAME      "GasBee"

// Service UUID: Custom 128-bit (zufällig generiert, fest)
// 4a5b6c7d-8e9f-0a1b-2c3d-4e5f60718293
#define GASBEE_BLE_SERVICE_UUID     0x4a5b6c7d, 0x8e9f, 0x0a1b, \
                                    0x2c, 0x3d, 0x4e, 0x5f, 0x60, 0x71, 0x82, 0x93

// Characteristics (16-bit short UUIDs für Einfachheit)
#define GASBEE_CHR_WEIGHT_KG        0x2B01  // Rohgewicht in kg (float, Notify)
#define GASBEE_CHR_GAS_PCT          0x2B02  // Füllstand in % (uint8, Notify)
#define GASBEE_CHR_GAS_NET_KG       0x2B03  // Nettogewicht Gas in kg (float, Notify)
#define GASBEE_CHR_TARE             0x2B10  // Tara-Befehl (Write, 1 Byte: 0x01 = Tare)

// BLE Advertising Interval (ms)
#define GASBEE_BLE_ADV_INTERVAL_MS  500
