/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialisiert ADC1 mit 12 Bit und 12 dB Attenuation.
esp_err_t analog_init(void);

// Liest einen ADC1-Kanal (0-9) als Rohwert.
esp_err_t analog_read_raw(int channel, int *raw_out);

// Liest und konvertiert einen ADC1-Kanal in Millivolt.
// Batterie-Kanaele: Spannungsteiler 100k/22k wird intern beruecksichtigt.
// Tank-Kanaele: direkte 0-1V Messung, optionaler Kalibrierfaktor aus sensor_config.h.
esp_err_t analog_read_mv(int channel, int *mv_out);

// Wie analog_read_mv(), aber mittelt über 'samples' Einzelmessungen
// (Median-of-3 Variante: bei 3 Samples wird der Median genommen).
esp_err_t analog_read_mv_avg(int channel, int *mv_out, int samples);

#ifdef __cplusplus
}
#endif
