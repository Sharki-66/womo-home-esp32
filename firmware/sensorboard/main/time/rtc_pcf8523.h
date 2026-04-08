/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <time.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pcf8523_app_start(void);
esp_err_t pcf8523_app_set_time(time_t utc_time);
esp_err_t pcf8523_app_get_time(time_t *utc_time);

/**
 * @brief Liest Batteriestatus aus PCF8523 Control_3 Register.
 *
 * BLF (Bit 3): Batteriespannung unter ~1.2V → Batterie fast leer.
 * BSF (Bit 4): VDD-Ausfall hat stattgefunden → Chip hat auf Batterie umgeschaltet.
 *              (BSF bleibt gesetzt bis manuell gelöscht wird)
 *
 * @param[out] bat_low     true = Batterie schwach (BLF gesetzt)
 * @param[out] bat_switched true = seit letztem Reset mind. 1× auf Batterie gelaufen (BSF gesetzt)
 * @return ESP_OK bei Erfolg, ESP_ERR_INVALID_STATE wenn noch nicht initialisiert
 */
esp_err_t pcf8523_app_get_battery_status(bool *bat_low, bool *bat_switched);

#ifdef __cplusplus
}
#endif
