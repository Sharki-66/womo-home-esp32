/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * womo_display_cfg.h – Einstellungen der Hintergrundbeleuchtung
 * (Einstellungen > System > Display, NVS „womo_disp“)
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WOMO_DISP_AUTO_OFF_MAX_MIN  5   /* Display-Aus-Automatik: 0 = deaktiviert, 1–5 min */

/* Bei neuer Meldung im Fehler-Stapel einschalten */
typedef enum {
    WOMO_DISP_WAKE_NEVER = 0,   /* nie */
    WOMO_DISP_WAKE_ALARM,       /* nur Alarm */
    WOMO_DISP_WAKE_ALL,         /* Warnung + Alarm (in der Ruhezeit nur Alarm) */
    WOMO_DISP_WAKE_COUNT
} womo_disp_wake_t;

typedef struct {
    uint8_t auto_off_min;       /* 0 = deaktiviert */
    uint8_t wake;               /* womo_disp_wake_t */
} womo_disp_cfg_t;

typedef void (*womo_disp_cfg_change_cb_t)(void);

/* NVS lesen – nur aus Task mit internem Stack (NVS schaltet den Flash-Cache ab) */
void womo_disp_cfg_init(void);
void womo_disp_cfg_get(womo_disp_cfg_t *out);
/* Speichert ins NVS und ruft den Change-Callback – nur aus dem LVGL-Task */
void womo_disp_cfg_set(const womo_disp_cfg_t *in);
void womo_disp_cfg_register_change_cb(womo_disp_cfg_change_cb_t cb);

#ifdef __cplusplus
}
#endif
