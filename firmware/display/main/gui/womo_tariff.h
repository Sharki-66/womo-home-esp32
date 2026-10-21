/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * womo_tariff.h – Mobilfunk-Tarif: Ablauf-Hinweis und Restvolumen-Warnung
 *
 * Ablaufdatum aus zwei Quellen:
 *   1. automatisch: valid_until_iso aus datapass.json (pass.telekom.de,
 *      siehe tools/router-webui/deploy.sh) – hat Vorrang, letzter Wert
 *      bleibt im NVS, damit "abgelaufen" auch ohne Datenverbindung
 *      erkannt wird
 *   2. manuell: Termin + Rhythmus (für Tarife ohne automatische Quelle),
 *      rollt nach Ablauf selbst weiter
 *
 * Daten als JJJJMMTT (uint32), Rechnung in ganzen Tagen ohne Zeitzone.
 * Thread-sicher: Router-Task schreibt Auto-Werte, LVGL-Task liest/konfiguriert.
 * NVS-Schreiben nur aus Tasks mit internem Stack (Flash-Cache wird abgeschaltet):
 * set_cfg/flush aus dem LVGL-Kontext, set_auto_due/set_volume schreiben nie.
 */
#ifndef WOMO_TARIFF_H
#define WOMO_TARIFF_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define TARIFF_LEAD_DAYS_DEFAULT   3   /* Hinweis 3 Tage vor Ablauf */
#define TARIFF_LEAD_DAYS_MAX      14
#define TARIFF_VOLUME_PCT_DEFAULT 10   /* Warnung unter 10 % Restvolumen */
#define TARIFF_VOLUME_PCT_MAX     50

typedef enum {
    WOMO_TARIFF_PERIOD_4W = 0,   /* 28 Tage (Prepaid-Optionen) */
    WOMO_TARIFF_PERIOD_30D,      /* 30 Tage */
    WOMO_TARIFF_PERIOD_MONTH,    /* Kalendermonat */
    WOMO_TARIFF_PERIOD_COUNT
} womo_tariff_period_t;

typedef struct {
    uint8_t  lead_days;    /**< Vorlauf Ablauf-Hinweis in Tagen, 0 = aus */
    uint8_t  volume_pct;   /**< Warnung unter x % Restvolumen, 0 = aus   */
    uint8_t  period;       /**< womo_tariff_period_t (nur manueller Termin) */
    uint32_t manual_due;   /**< Manueller Termin JJJJMMTT, 0 = keiner    */
} womo_tariff_cfg_t;

typedef enum {
    WOMO_TARIFF_OK = 0,
    WOMO_TARIFF_SOON,      /**< Ablauf innerhalb des Vorlaufs */
    WOMO_TARIFF_EXPIRED,   /**< Automatisches Datum überschritten */
} womo_tariff_phase_t;

typedef struct {
    womo_tariff_phase_t phase;
    uint32_t due;          /**< Maßgeblicher Termin JJJJMMTT, 0 = keiner */
    bool     due_auto;     /**< Termin stammt aus pass.telekom.de */
    int      days_left;    /**< Tage bis Ablauf (negativ = abgelaufen) */
    bool     volume_low;   /**< Restvolumen unter Schwelle */
    uint8_t  volume_pct;   /**< Restvolumen in % (nur wenn volume_valid) */
    bool     volume_valid;
} womo_tariff_state_t;

/** NVS laden (Konfiguration + letzter automatischer Termin). */
void womo_tariff_init(void);

void womo_tariff_get_cfg(womo_tariff_cfg_t *out);
/** Setzen und im NVS sichern. */
void womo_tariff_set_cfg(const womo_tariff_cfg_t *in);

/** Automatischer Termin aus datapass.json; 0 löscht ihn. Nur RAM – persistiert
 *  wird bei Änderung durch womo_tariff_flush() (darf aus PSRAM-Stack-Tasks kommen). */
void womo_tariff_set_auto_due(uint32_t yyyymmdd);
/** Ausstehende Änderung des Auto-Termins ins NVS schreiben (LVGL-Kontext). */
void womo_tariff_flush(void);
uint32_t womo_tariff_get_auto_due(void);

/** Restvolumen aus datapass.json ("29,99" / "30"), ungültige Werte verwerfen. */
void womo_tariff_set_volume(const char *remaining, const char *total);

/** Auswertung für das Datum in now; false wenn now ungültig. */
bool womo_tariff_evaluate(const struct tm *now, womo_tariff_state_t *out);

/** Parst "2026-11-16" → 20261116 (deutschen Text wandelt der Router um), 0 bei Fehler. */
uint32_t womo_tariff_parse_iso(const char *iso);

uint32_t womo_tariff_date_from_tm(const struct tm *tm);
uint32_t womo_tariff_add_days(uint32_t yyyymmdd, int days);
/** Termin um einen Rhythmus vor (dir > 0) oder zurück (dir < 0) verschieben. */
uint32_t womo_tariff_add_period(uint32_t yyyymmdd, uint8_t period, int dir);

#endif /* WOMO_TARIFF_H */
