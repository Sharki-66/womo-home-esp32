/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/**
 * power_mode.h
 *
 * Energiesparen des Sensorboards.
 *  - Immer: dynamische Taktanpassung (SENSOR_PM_DFS_ENABLE, ohne Light Sleep)
 *  - Eco-Modus: vom Display per hb-Feld "eco" gewünscht (Hintergrundlicht aus),
 *    aktiv nur ohne Landstrom. Stufen (sensor_config.h, je einzeln abschaltbar):
 *    BLE-Scan in Fenstern, WLAN getrennt, BNO055 Suspend, Topics seltener.
 */

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Dynamische Taktanpassung konfigurieren (einmal beim Boot). */
esp_err_t power_mode_init(void);

/** @brief Wunsch des Displays (aus jedem hb). Ohne hb verfällt er nach SENSOR_ECO_REQUEST_TIMEOUT_MS. */
void power_mode_set_requested(bool eco);

/**
 * @brief Modus auswerten und Stufen anwenden (zyklisch aus dem ESP-NOW-TX-Task).
 * @param ac_present Landstrom vorhanden → nie Eco
 * @return true, wenn sich der Modus geändert hat
 */
bool power_mode_tick(bool ac_present);

/** @brief true, solange der Eco-Modus aktiv ist. */
bool power_mode_is_eco(void);

#ifdef __cplusplus
}
#endif
