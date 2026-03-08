/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"     /* liefert lvgl_port_lock(uint32_t), lvgl_port_unlock() */

#ifdef __cplusplus
extern "C" {
#endif

/* ── Auflösung ──────────────────────────────────────────────────────── */
#define LVGL_PORT_H_RES             (800)
#define LVGL_PORT_V_RES             (480)

/* ── Anzahl LCD-Framebuffer (2 = Double‑Buffer / Avoid‑Tearing Mode 3) */
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS  (2)

/* ── Task / Timing – werden an esp_lvgl_port_init() übergeben ──────── */
#define LVGL_PORT_TICK_PERIOD_MS       (CONFIG_EXAMPLE_LVGL_PORT_TICK)
#define LVGL_PORT_TASK_MAX_DELAY_MS    (CONFIG_EXAMPLE_LVGL_PORT_TASK_MAX_DELAY_MS)
#define LVGL_PORT_TASK_MIN_DELAY_MS    (CONFIG_EXAMPLE_LVGL_PORT_TASK_MIN_DELAY_MS)
#define LVGL_PORT_TASK_STACK_SIZE      (CONFIG_EXAMPLE_LVGL_PORT_TASK_STACK_SIZE_KB * 1024)
#define LVGL_PORT_TASK_PRIORITY        (CONFIG_EXAMPLE_LVGL_PORT_TASK_PRIORITY)
#define LVGL_PORT_TASK_CORE            (CONFIG_EXAMPLE_LVGL_PORT_TASK_CORE)

/* ── Öffentliche API ────────────────────────────────────────────────── */

/**
 * @brief Initialisiert LVGL, Display und Touch via esp_lvgl_port.
 *
 * Registriert das RGB-Panel mit Avoid-Tearing (Bounce-Buffer + Direct-Mode)
 * und legt den LVGL-Task an.  Touch wird mit eigenem Read-Callback
 * registriert, damit Wake-Callback und Fast-Mode weiterhin funktionieren.
 *
 * @note  lvgl_port_lock() / lvgl_port_unlock() kommen aus esp_lvgl_port.h.
 *        0 als Timeout bedeutet "warte unbegrenzt".
 */
esp_err_t womo_lvgl_port_init(esp_lcd_panel_handle_t lcd_handle,
                              esp_lcd_touch_handle_t  tp_handle);

/**
 * @brief Touch-Wake-Callback-Typ.
 *
 * Wird bei jeder erkannten Touch-Berührung aufgerufen (LVGL-Task-Kontext,
 * Mutex gehalten).
 *
 * @return true  → Touch unterdrücken (Display war aus, wurde aufgeweckt)
 * @return false → Touch normal weiterleiten
 */
typedef bool (*lvgl_touch_wake_cb_t)(void);

/**
 * @brief Registriert einen Touch-Wake-Callback.
 *
 * Gibt er true zurück, wird der Touch als RELEASED gemeldet –
 * kein Widget-Event wird ausgelöst.
 *
 * @param cb  Callback oder NULL zum Deregistrieren.
 */
void lvgl_touch_set_wake_cb(lvgl_touch_wake_cb_t cb);

/**
 * @brief Schaltet schnelles Touch-Sampling manuell ein/aus.
 *
 * @param enable  true → TOUCH_FAST_PERIOD_MS, false → adaptiv
 */
void lvgl_touch_set_fast_mode(bool enable);

#ifdef __cplusplus
}
#endif



