/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file womo_weather_warn.h
 * @brief Eigene Unwetter-Auswertung aus Open-Meteo-Stundenwerten
 *
 * Keine amtliche Warnung: Die stündliche Punktvorhersage von Open-Meteo
 * (nächste 24 h) wird gegen die Warnkriterien des Deutschen Wetterdienstes
 * geprüft. Quelle der Schwellwerte:
 *   https://www.dwd.de/DE/wetter/warnungen_aktuell/kriterien/warnkriterien.html
 *   (abgerufen 2026-10-20)
 *
 * Stufen wie beim DWD: 1 Wetterwarnung, 2 markantes Wetter, 3 Unwetter,
 * 4 extremes Unwetter. Badge/Auto-Popup ab Stufe 2, Stufe 1 nur im
 * Forecast-Modal.
 *
 * Nicht ableitbar: Hochwasser, Lawinen, Sturmflut, Waldbrand, Glätte durch
 * überfrierende Nässe.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Eingangsdaten: Stundenwerte aus Open-Meteo ──────────────── */
#define WOMO_WEATHER_HOURLY_MAX  24   // forecast_hours=24

typedef struct {
    bool    valid;
    uint8_t count;                               // Anzahl gültiger Stunden
    float   elevation_m;                         // Modellhöhe des Gitterpunkts
    char    time[WOMO_WEATHER_HOURLY_MAX][17];   // "YYYY-MM-DDTHH:MM", Ortszeit (timezone=auto)
    float   gust_kmh[WOMO_WEATHER_HOURLY_MAX];   // wind_gusts_10m
    float   precip_mm[WOMO_WEATHER_HOURLY_MAX];  // precipitation (Summe der Stunde)
    float   snow_cm[WOMO_WEATHER_HOURLY_MAX];    // snowfall (Summe der Stunde)
    float   temp_c[WOMO_WEATHER_HOURLY_MAX];     // temperature_2m
    float   feels_c[WOMO_WEATHER_HOURLY_MAX];    // apparent_temperature
    int16_t code[WOMO_WEATHER_HOURLY_MAX];       // weather_code (WMO), -1 = fehlt
} womo_weather_hourly_t;

/* ── Schwellwerte (DWD-Warnkriterien) ────────────────────────── */
/* Böen in km/h: Stufe 1 > 50, Stufe 2 65–104, Stufe 3 105–139, Stufe 4 > 140 */
#define WW_GUST_L1_GT        50.0f
#define WW_GUST_L2_GE        65.0f
#define WW_GUST_SEVERE_GE    90.0f    // Bezeichnung „schwere Sturmböen“ (Stufe 2)
#define WW_GUST_L3_GE       105.0f
#define WW_GUST_HURRICANE_GE 120.0f   // Bezeichnung „Orkanböen“ (Stufe 3)
#define WW_GUST_L4_GT       140.0f

/* Starkregen in mm: 1 h / 6 h (keine Stufe 1) */
#define WW_RAIN1H_L2_GE  15.0f
#define WW_RAIN1H_L3_GE  25.0f
#define WW_RAIN1H_L4_GT  40.0f
#define WW_RAIN6H_L2_GE  20.0f
#define WW_RAIN6H_L3_GE  35.0f
#define WW_RAIN6H_L4_GT  60.0f

/* Dauerregen in mm: 12 h / 24 h (keine Stufe 1) */
#define WW_RAIN12H_L2_GE  25.0f
#define WW_RAIN12H_L3_GE  40.0f
#define WW_RAIN12H_L4_GT  70.0f
#define WW_RAIN24H_L2_GE  30.0f
#define WW_RAIN24H_L3_GE  50.0f
#define WW_RAIN24H_L4_GT  80.0f

/* Schnee in cm: 6 h / 12 h / 24 h, Tabelle hängt von der Höhe ab */
#define WW_SNOW_ALT_SPLIT_M  800.0f
/* DWD nennt für Stufe 1 keine Untergrenze („leichter Schneefall“).
 * Eigene Festlegung: ab 1 cm in 6 h, damit Spuren nicht warnen. */
#define WW_SNOW_L1_6H_GE     1.0f
#define WW_SNOW_L2_6H_GT     5.0f
#define WW_SNOW_L2_12H_GT   10.0f
#define WW_SNOW_L2_24H_GT   15.0f
#define WW_SNOW_LO_L3_6H_GT  10.0f   // bis 800 m
#define WW_SNOW_LO_L3_12H_GT 15.0f
#define WW_SNOW_LO_L3_24H_GT 30.0f
#define WW_SNOW_LO_L4_6H_GT  20.0f
#define WW_SNOW_LO_L4_12H_GT 25.0f
#define WW_SNOW_LO_L4_24H_GT 40.0f
#define WW_SNOW_HI_L3_6H_GT  20.0f   // über 800 m
#define WW_SNOW_HI_L3_12H_GT 30.0f
#define WW_SNOW_HI_L3_24H_GT 40.0f
#define WW_SNOW_HI_L4_6H_GT  30.0f
#define WW_SNOW_HI_L4_12H_GT 50.0f
#define WW_SNOW_HI_L4_24H_GT 60.0f

/* Frost in °C (Lufttemperatur) */
#define WW_FROST_L1_LT    0.0f
#define WW_FROST_L2_LT  -10.0f

/* Hitze: gefühlte Temperatur in °C, DWD nutzt nur Stufe 1 und 3.
 * Das DWD-Kriterium „geringe nächtliche Abkühlung“ wird nicht geprüft. */
#define WW_HEAT_L1_GT  32.0f
#define WW_HEAT_L3_GT  38.0f

/* ── Ergebnis ────────────────────────────────────────────────── */
#define WOMO_WEATHER_WARN_MAX  8   // max. eine Warnung pro Gefahrentyp

typedef enum {
    WOMO_WARN_SEV_NONE     = 0,
    WOMO_WARN_SEV_MINOR    = 1,   // Stufe 1 – nur im Forecast-Modal
    WOMO_WARN_SEV_MODERATE = 2,   // Stufe 2 – markantes Wetter
    WOMO_WARN_SEV_SEVERE   = 3,   // Stufe 3 – Unwetter
    WOMO_WARN_SEV_EXTREME  = 4,   // Stufe 4 – extremes Unwetter
} womo_warn_severity_t;

typedef enum {
    WOMO_WARN_TYPE_WIND = 0,
    WOMO_WARN_TYPE_RAIN_HEAVY,    // Starkregen (1 h / 6 h)
    WOMO_WARN_TYPE_RAIN_LONG,     // Dauerregen (12 h / 24 h)
    WOMO_WARN_TYPE_SNOW,
    WOMO_WARN_TYPE_THUNDER,
    WOMO_WARN_TYPE_ICE,           // gefrierender (Sprüh-)Regen
    WOMO_WARN_TYPE_FROST,
    WOMO_WARN_TYPE_HEAT,
} womo_warn_type_t;

/* Uhrzeit an der Position (Ortszeit laut Open-Meteo) */
typedef struct {
    uint8_t wday;    // 0 = Sonntag
    uint8_t hour;
    uint8_t minute;
    bool    today;   // gleicher Tag wie die erste Vorhersagestunde
} womo_warn_time_t;

typedef struct {
    womo_warn_severity_t severity;
    womo_warn_type_t     type;
    float                value;    // Böe km/h, Summe mm/cm oder Temperatur °C
    uint8_t              hours;    // Summenfenster in h (0 = Einzelwert)
    womo_warn_time_t     from;
    womo_warn_time_t     until;
} womo_weather_warning_t;

typedef struct {
    bool    valid;          // true = Auswertung lief mit gültigen Daten
    uint8_t count;          // alle Warnungen inkl. Stufe 1
    uint8_t count_major;    // Warnungen ab Stufe 2 (Badge/Popup)
    uint8_t max_severity;   // höchste Stufe über alle Warnungen
    womo_weather_warning_t warnings[WOMO_WEATHER_WARN_MAX];  // absteigend nach Stufe
} womo_weather_warn_result_t;

typedef void (*womo_weather_warn_cb_t)(const womo_weather_warn_result_t *result, void *user_data);

/**
 * @brief Stundenwerte gegen die DWD-Kriterien prüfen.
 */
void womo_weather_warn_evaluate(const womo_weather_hourly_t *in, womo_weather_warn_result_t *out);

/**
 * @brief Warnung als lesbaren Text (aktuelle Sprache über womo_locale).
 *
 * @param title  z. B. „Sturmböen bis 78 km/h“
 * @param when   z. B. „14:00 - 20:00“ bzw. „Mi 02:00 - Mi 08:00“
 */
void womo_weather_warn_format(const womo_weather_warning_t *w,
                              char *title, size_t title_len,
                              char *when, size_t when_len);

#ifdef __cplusplus
}
#endif
