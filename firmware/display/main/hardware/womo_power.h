/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * womo_power.h – Strom sparen bei dunklem Hintergrundlicht
 * (Pixeltakt senken, CPU-Takt per DFS zwischen WOMO_PM_CPU_MIN_MHZ und
 * WOMO_PM_CPU_MAX_MHZ). Bei Licht läuft alles wie ohne Eco.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Nach waveshare_esp32_s3_rgb_lcd_init() aufrufen. Startet im Normalbetrieb (240 MHz fest). */
esp_err_t womo_power_init(void);

/* true: Hintergrundlicht aus → Eco (niedriger Pixeltakt, CPU darf heruntertakten).
 * false: vor dem Einschalten des Hintergrundlichts aufrufen. */
void womo_power_set_eco(bool eco);

bool womo_power_is_eco(void);

#ifdef __cplusplus
}
#endif
