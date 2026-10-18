/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/**
 * gasbee_gatt_cal.h
 *
 * Kalibrierung einer GasBee (firmware/gasbee_nrf) per GATT, ausgelöst vom
 * Display-Assistenten. Nur möglich, solange die GasBee im Konfigurationsmodus
 * ist (USB angesteckt, Advertising-Flag Bit 3).
 *
 * Ablauf je Schritt: gezielt per MAC verbinden → Service per UUID suchen →
 * Characteristic schreiben → sofort trennen. Solange die Verbindung steht,
 * sendet die GasBee kein Advertising (max. 1 Verbindung).
 *
 * Der Write bestätigt nur das Format. Ob die GasBee die Kalibrierung gespeichert
 * hat, zeigt erst das nächste Advertising (Flag Bit 2 + Gewicht).
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GASBEE_CAL_STEP_ZERO = 0,   // 0x2B11 = 01: Nullpunkt (Waage leer)
    GASBEE_CAL_STEP_REF,        // 0x2B12 = uint32 LE: Referenzgewicht in Gramm
} gasbee_cal_step_t;

// Ergebnis: 0 = Write bestätigt, > 0 = ATT-Fehlercode der GasBee
// (0x03 nicht im Konfigurationsmodus, 0x0D falsche Länge, 0x13 Wert ungültig), < 0 = lokal:
#define GASBEE_CAL_OK              0
#define GASBEE_CAL_ERR_BUSY       -1   // Kalibrierschritt läuft bereits
#define GASBEE_CAL_ERR_NOT_FOUND  -2   // GasBee nicht gesehen bzw. nicht im Konfigurationsmodus
#define GASBEE_CAL_ERR_CONNECT    -3   // Verbindungsaufbau fehlgeschlagen
#define GASBEE_CAL_ERR_NO_SERVICE -4   // Service/Characteristic nicht gefunden
#define GASBEE_CAL_ERR_TIMEOUT    -5   // SENSOR_GASBEE_CAL_TIMEOUT_MS überschritten
#define GASBEE_CAL_ERR_DISCONNECT -6   // Verbindung vor dem Ergebnis getrennt
#define GASBEE_CAL_ERR_ARG        -7   // ungültige MAC / ungültiges Gewicht
#define GASBEE_CAL_ERR_BLE        -8   // sonstiger BLE-Fehler

// Wird genau einmal je gestartetem Schritt aufgerufen (NimBLE-Host- oder esp_timer-Task)
typedef void (*gasbee_cal_done_cb_t)(gasbee_cal_step_t step, int result, void *arg);

/** @brief Beim BLE-Manager als Connection-Handler registrieren. Nach ble_manager_init(). */
void gasbee_gatt_cal_init(void);

/**
 * @brief Kalibrierschritt starten.
 * @param mac   "AA:BB:CC:DD:EE:FF" (MSB-first, wie gasbee_config)
 * @param grams nur für GASBEE_CAL_STEP_REF (1 … 200000)
 * @return GASBEE_CAL_OK = gestartet (cb folgt), sonst Fehlercode (cb wird nicht aufgerufen)
 */
int gasbee_gatt_cal_start(const char *mac, gasbee_cal_step_t step, uint32_t grams,
                          gasbee_cal_done_cb_t cb, void *arg);

/** @brief Kurzbeschreibung eines Ergebniscodes für Log und Display. */
const char *gasbee_gatt_cal_result_str(int result);

#ifdef __cplusplus
}
#endif
