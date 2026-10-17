/*
 * SPDX-FileCopyrightText: 2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * status_led.h – RGB-Status-LED des XIAO (nur mit CONFIG_GASBEE_STATUS_LED)
 *
 * Jede Farbe zeigt einen eigenen Zustand, Mischfarben sind möglich:
 *   Blau  blinkt   Advertising, wartet auf Verbindung
 *   Blau  an       verbunden
 *   Rot   an       HX711 nicht bereit
 *   Grün  1 Blitz  Messung fertig, Notify gesendet
 *   Grün  3 Blitze Tare übernommen
 *
 * Ohne CONFIG_GASBEE_STATUS_LED sind alle Funktionen leer.
 */

#pragma once

#include <stdbool.h>

#if defined(CONFIG_GASBEE_STATUS_LED)

int  status_led_init(void);
void status_led_ble_adv(void);
void status_led_ble_connected(void);
void status_led_ble_off(void);
void status_led_hx711_ok(bool ok);
void status_led_flash_measure(void);
void status_led_flash_tare(void);

#else

static inline int  status_led_init(void) { return 0; }
static inline void status_led_ble_adv(void) {}
static inline void status_led_ble_connected(void) {}
static inline void status_led_ble_off(void) {}
static inline void status_led_hx711_ok(bool ok) { (void)ok; }
static inline void status_led_flash_measure(void) {}
static inline void status_led_flash_tare(void) {}

#endif
