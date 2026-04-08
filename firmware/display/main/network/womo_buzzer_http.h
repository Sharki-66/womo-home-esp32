/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include "display_config.h"

#if WOMO_ENABLE_BUZZER_STUDIO_HTTP
esp_err_t womo_buzzer_http_start(void);
esp_err_t womo_buzzer_http_stop(void);
bool womo_buzzer_http_is_running(void);
#else
static inline esp_err_t womo_buzzer_http_start(void) { return ESP_ERR_NOT_SUPPORTED; }
static inline esp_err_t womo_buzzer_http_stop(void) { return ESP_ERR_NOT_SUPPORTED; }
static inline bool womo_buzzer_http_is_running(void) { return false; }
#endif
