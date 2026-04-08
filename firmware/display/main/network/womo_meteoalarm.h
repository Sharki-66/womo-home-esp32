/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file womo_meteoalarm.h
 * @brief Meteoalarm Unwetterwarnungen (Stufen 2+3) – GPS-basiert
 *
 * Nutzt die öffentliche Meteoalarm REST-API:
 *   GET https://feeds.meteoalarm.org/api/v1/warnings/for-coordinates/{lat}/{lon}
 *
 * Gefiltert auf Severity "Severe" (Stufe 2 / Orange) und "Extreme"
 * (Stufe 3 / Rot).  Stufen 1 (Minor) und unbekannte Einträge werden
 * ignoriert.
 *
 * Thread-Safety: Callback wird aus FreeRTOS-Task heraus aufgerufen.
 * Alle UI-Zugriffe müssen unter lvgl_port_lock() erfolgen.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Limits ──────────────────────────────────────────────────── */
#define WOMO_METEOALARM_MAX_WARNINGS  8    // max. gecachte Warnungen
#define WOMO_METEOALARM_HEADLINE_LEN  80   // inkl. '\0'
#define WOMO_METEOALARM_EVENT_LEN     40
#define WOMO_METEOALARM_EXPIRES_LEN   24   // "HH:MM TT.MM.JJJJ"
#define WOMO_METEOALARM_REGION_LEN    64   // Regionsname (areaDesc)

/* ── Severity ────────────────────────────────────────────────── */
typedef enum {
    WOMO_WARN_SEV_UNKNOWN  = 0,
    WOMO_WARN_SEV_MINOR    = 1,   // Stufe 1 – wird ignoriert
    WOMO_WARN_SEV_MODERATE = 2,   // Stufe 2 – Orange
    WOMO_WARN_SEV_SEVERE   = 3,   // Stufe 2 (Meteoalarm "Severe")
    WOMO_WARN_SEV_EXTREME  = 4,   // Stufe 3 – Rot
} womo_warn_severity_t;

/* ── Einzelne Warnung ────────────────────────────────────────── */
typedef struct {
    womo_warn_severity_t severity;
    char event[WOMO_METEOALARM_EVENT_LEN];       // z. B. "Wind", "Rain"
    char headline[WOMO_METEOALARM_HEADLINE_LEN];  // Kurzbeschreibung
    char region[WOMO_METEOALARM_REGION_LEN];     // Regionsname (areaDesc)
    char expires[WOMO_METEOALARM_EXPIRES_LEN];   // Ablaufzeit, formatiert
    time_t expires_ts;                           // Unix-Timestamp (0 = unbekannt)
} womo_meteoalarm_warning_t;

/* ── Ergebnis-Struct (an Callback übergeben) ─────────────────── */
typedef struct {
    bool valid;                          // true = Fetch erfolgreich
    uint8_t count;                       // Anzahl gefilterter Warnungen
    uint8_t max_severity;                // höchste Severity (0 = keine Warnungen)
    char region[WOMO_METEOALARM_REGION_LEN]; // Meteoalarm-Region des Standorts
    womo_meteoalarm_warning_t warnings[WOMO_METEOALARM_MAX_WARNINGS];
} womo_meteoalarm_result_t;

/* ── Callback-Typ ────────────────────────────────────────────── */
typedef void (*womo_meteoalarm_cb_t)(const womo_meteoalarm_result_t *result, void *user_data);

/* ── Öffentliche API ─────────────────────────────────────────── */

/**
 * @brief  Meteoalarm-Task starten.
 *
 * Startet einen Hintergrundtask, der periodisch (default 30 min) die
 * Meteoalarm-API für die gesetzte GPS-Position abfragt.  Nach einem
 * Fehler wird ein kürzerer Retry-Intervall verwendet.
 *
 * @param callback  Funktion, die bei jedem Update aufgerufen wird.
 * @param user_data Opaker Zeiger, der an callback weitergegeben wird.
 * @return ESP_OK oder Fehlercode (z. B. ESP_ERR_NO_MEM).
 */
esp_err_t womo_meteoalarm_start(womo_meteoalarm_cb_t callback, void *user_data);

/**
 * @brief  Meteoalarm-Task stoppen (blockiert bis Task beendet).
 */
esp_err_t womo_meteoalarm_stop(void);

/**
 * @brief  GPS-Position setzen (thread-safe, löst sofortigen Fetch aus).
 *
 * Wird bei jeder GPS-Aktualisierung aufgerufen.  Wenn die neue Position
 * mehr als ~5 km von der letzten Fetch-Position abweicht, wird ein
 * sofortiger Fetch angestoßen.
 */
void womo_meteoalarm_set_location(double lat, double lon);

/**
 * @brief  Sofortigen Fetch anstoßen (z. B. nach Netzwerkverbindung).
 */
void womo_meteoalarm_trigger_now(void);

/**
 * @brief  Gibt an, ob der Task läuft.
 */
bool womo_meteoalarm_is_running(void);

/**
 * @brief  Lesbare Bezeichnung für eine Severity-Stufe.
 */
const char *womo_meteoalarm_severity_label(womo_warn_severity_t sev);

#ifdef __cplusplus
}
#endif
