/*
 * SPDX-FileCopyrightText: 2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * battery.h – Akku (LiPo am XIAO): Spannung über voltage-divider, Ladestatus über BQ25101 ~CHG
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Treiber und Ladestatus-Pin prüfen. */
int battery_init(void);

/** Akkuspannung in mV messen (blockiert kurz für die ADC-Wandlung). */
int battery_read_mv(uint16_t *mv_out);

/** true, wenn USB-Spannung (VBUS) anliegt – Netzteil oder PC. */
bool battery_usb_present(void);

/** true, solange der Lader lädt (nur mit USB-Versorgung möglich). */
bool battery_is_charging(void);
