/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * womo_thresholds.h – Grenzwerte für Warn-/Alarm-Schwellen
 *
 * Gas, Frisch- und Grauwassertank.
 * Werte in Prozent (0–100).
 * Persistenz via NVS ist vorbereitet (aktuell Defaults).
 */
#ifndef WOMO_THRESHOLDS_H
#define WOMO_THRESHOLDS_H

#include <stdint.h>

/* ── Defaults ───────────────────────────────────────────── */
#define THRESH_GAS_WARN_DEFAULT    30  /* % – low is bad */
#define THRESH_GAS_CRIT_DEFAULT    10
#define THRESH_FRESH_WARN_DEFAULT  30  /* % – low is bad */
#define THRESH_FRESH_CRIT_DEFAULT  10
#define THRESH_GREY_WARN_DEFAULT   70  /* % – high is bad */
#define THRESH_GREY_CRIT_DEFAULT   90
#define THRESH_IAQ_WARN_DEFAULT   150  /* IAQ-Wert – high is bad (BSEC: 0=gut, 500=schlecht) */
#define THRESH_IAQ_CRIT_DEFAULT   250

/* ── Struct ─────────────────────────────────────────────── */
typedef struct {
    uint8_t gas_warn;    /**< Gasflaschen Warnung (low is bad) */
    uint8_t gas_crit;    /**< Gasflaschen Alarm   (low is bad) */
    uint8_t fresh_warn;  /**< Frischwasser Warnung (low is bad) */
    uint8_t fresh_crit;  /**< Frischwasser Alarm   (low is bad) */
    uint8_t grey_warn;   /**< Grauwasser Warnung   (high is bad) */
    uint8_t grey_crit;   /**< Grauwasser Alarm     (high is bad) */
} womo_thresholds_t;

/* ── API ────────────────────────────────────────────────── */

/** Initialisierung (ggf. NVS-Laden, sonst Defaults). */
void womo_thresholds_init(void);

/** Aktuelle Werte auslesen. */
void womo_thresholds_get(womo_thresholds_t *out);

/** Neue Werte setzen und registrierte Callbacks auslösen. */
void womo_thresholds_set(const womo_thresholds_t *in);

/**
 * Callback registrieren, der bei jeder Änderung aufgerufen wird.
 * Max. 4 Callbacks möglich.
 */
void womo_thresholds_register_change_cb(void (*cb)(void));

#endif /* WOMO_THRESHOLDS_H */
