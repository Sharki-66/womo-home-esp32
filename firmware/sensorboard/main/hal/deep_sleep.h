/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Initialisiert den kapazitiven Touch (GPIO6 = TOUCH_PAD_NUM6).
 *        Liest Baseline, setzt Schwellwert, konfiguriert Sleep-Channel.
 */
esp_err_t deep_sleep_init(void);

/**
 * @brief Startet Debug-Task: gibt alle 500ms Rohwert/Baseline/Schwellwert aus.
 */
void deep_sleep_start_monitor(void);

/**
 * @brief Gibt zurück ob der letzte Boot durch Touch-Wakeup ausgelöst wurde.
 */
bool deep_sleep_wakeup_by_touch(void);

/**
 * @brief Fährt das System in Deep Sleep (GPIO7 LOW, Touch-HW-Wakeup).
 * @note  Kehrt NICHT zurück.
 */
void deep_sleep_enter(void);
