/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file womo_weather_warn.c
 * @brief Eigene Unwetter-Auswertung aus Open-Meteo-Stundenwerten (DWD-Kriterien)
 *
 * Reine Rechenlogik ohne I/O: wird aus dem Wetter-Task nach jedem
 * erfolgreichen Open-Meteo-Abruf aufgerufen. Pro Gefahrentyp entsteht
 * höchstens eine Warnung mit der höchsten erreichten Stufe und dem
 * Zeitfenster, in dem diese Stufe überschritten wird.
 */

#include "womo_weather_warn.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "gui/womo_locale.h"

static const char *TAG = "weather_warn";

/* ── Zeit-Hilfen ─────────────────────────────────────────────── */

/* Wochentag nach Sakamoto (0 = Sonntag), zeitzonenunabhängig */
static uint8_t weekday_of(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (uint8_t)((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7);
}

/* "YYYY-MM-DDTHH:MM" → Zeitpunkt; date_key dient nur dem Tagesvergleich */
static bool parse_hour(const char *s, womo_warn_time_t *t, int *date_key)
{
    int y, mo, d, h, mi;
    if (!s || sscanf(s, "%4d-%2d-%2dT%2d:%2d", &y, &mo, &d, &h, &mi) != 5) {
        return false;
    }
    if (mo < 1 || mo > 12) return false;
    t->wday   = weekday_of(y, mo, d);
    t->hour   = (uint8_t)h;
    t->minute = (uint8_t)mi;
    t->today  = false;
    if (date_key) *date_key = y * 372 + mo * 31 + d;
    return true;
}

static womo_warn_time_t time_at(const womo_weather_hourly_t *in, int idx, int today_key)
{
    womo_warn_time_t t = {0};
    int key = 0;
    if (parse_hour(in->time[idx], &t, &key)) {
        t.today = (key == today_key);
    }
    return t;
}

/* Ende eines Fensters = Beginn der Stunde nach idx */
static womo_warn_time_t time_after(const womo_weather_hourly_t *in, int idx, int today_key)
{
    if (idx + 1 < in->count) {
        return time_at(in, idx + 1, today_key);
    }
    womo_warn_time_t t = time_at(in, idx, today_key);
    if (++t.hour >= 24) {
        t.hour  = 0;
        t.wday  = (uint8_t)((t.wday + 1) % 7);
        t.today = false;
    }
    return t;
}

/* ── Rechen-Hilfen ───────────────────────────────────────────── */

/* Größte Summe über ein gleitendes Fenster von w Stunden; NAN zählt als 0.
 * Liefert NAN, wenn weniger als w Stunden vorliegen. */
static float max_window_sum(const float *v, int n, int w, int *start_out)
{
    if (n < w) return NAN;
    float best = -1.0f;
    int best_start = 0;
    for (int s = 0; s + w <= n; s++) {
        float sum = 0.0f;
        for (int i = s; i < s + w; i++) {
            if (!isnan(v[i])) sum += v[i];
        }
        if (sum > best) {
            best = sum;
            best_start = s;
        }
    }
    if (start_out) *start_out = best_start;
    return best;
}

static void add_warning(womo_weather_warn_result_t *out, const womo_weather_hourly_t *in,
                        int today_key, womo_warn_type_t type, womo_warn_severity_t sev,
                        float value, uint8_t hours, int from_idx, int to_idx)
{
    if (sev == WOMO_WARN_SEV_NONE || out->count >= WOMO_WEATHER_WARN_MAX) return;
    womo_weather_warning_t *w = &out->warnings[out->count++];
    w->severity = sev;
    w->type     = type;
    w->value    = value;
    w->hours    = hours;
    w->from     = time_at(in, from_idx, today_key);
    w->until    = time_after(in, to_idx, today_key);
}

/* ── Regeln ──────────────────────────────────────────────────── */

static bool gust_meets(float g, womo_warn_severity_t sev)
{
    switch (sev) {
    case WOMO_WARN_SEV_EXTREME:  return g > WW_GUST_L4_GT;
    case WOMO_WARN_SEV_SEVERE:   return g >= WW_GUST_L3_GE;
    case WOMO_WARN_SEV_MODERATE: return g >= WW_GUST_L2_GE;
    default:                     return g > WW_GUST_L1_GT;
    }
}

static void rule_wind(const womo_weather_hourly_t *in, int today_key, womo_weather_warn_result_t *out)
{
    float gmax = NAN;
    for (int i = 0; i < in->count; i++) {
        if (!isnan(in->gust_kmh[i]) && (isnan(gmax) || in->gust_kmh[i] > gmax)) {
            gmax = in->gust_kmh[i];
        }
    }
    if (isnan(gmax)) return;

    womo_warn_severity_t sev = WOMO_WARN_SEV_NONE;
    if      (gust_meets(gmax, WOMO_WARN_SEV_EXTREME))  sev = WOMO_WARN_SEV_EXTREME;
    else if (gust_meets(gmax, WOMO_WARN_SEV_SEVERE))   sev = WOMO_WARN_SEV_SEVERE;
    else if (gust_meets(gmax, WOMO_WARN_SEV_MODERATE)) sev = WOMO_WARN_SEV_MODERATE;
    else if (gust_meets(gmax, WOMO_WARN_SEV_MINOR))    sev = WOMO_WARN_SEV_MINOR;
    if (sev == WOMO_WARN_SEV_NONE) return;

    int first = -1, last = -1;
    for (int i = 0; i < in->count; i++) {
        if (!isnan(in->gust_kmh[i]) && gust_meets(in->gust_kmh[i], sev)) {
            if (first < 0) first = i;
            last = i;
        }
    }
    add_warning(out, in, today_key, WOMO_WARN_TYPE_WIND, sev, gmax, 0, first, last);
}

static womo_warn_severity_t level_sum(float v, float l2_ge, float l3_ge, float l4_gt)
{
    if (isnan(v))    return WOMO_WARN_SEV_NONE;
    if (v > l4_gt)   return WOMO_WARN_SEV_EXTREME;
    if (v >= l3_ge)  return WOMO_WARN_SEV_SEVERE;
    if (v >= l2_ge)  return WOMO_WARN_SEV_MODERATE;
    return WOMO_WARN_SEV_NONE;
}

static void rule_rain_heavy(const womo_weather_hourly_t *in, int today_key, womo_weather_warn_result_t *out)
{
    int s1 = 0, s6 = 0;
    float m1  = max_window_sum(in->precip_mm, in->count, 1, &s1);
    float m6  = max_window_sum(in->precip_mm, in->count, 6, &s6);
    womo_warn_severity_t l1 = level_sum(m1, WW_RAIN1H_L2_GE, WW_RAIN1H_L3_GE, WW_RAIN1H_L4_GT);
    womo_warn_severity_t l6 = level_sum(m6, WW_RAIN6H_L2_GE, WW_RAIN6H_L3_GE, WW_RAIN6H_L4_GT);

    if (l1 >= l6) {
        add_warning(out, in, today_key, WOMO_WARN_TYPE_RAIN_HEAVY, l1, m1, 1, s1, s1);
    } else {
        add_warning(out, in, today_key, WOMO_WARN_TYPE_RAIN_HEAVY, l6, m6, 6, s6, s6 + 5);
    }
}

static void rule_rain_long(const womo_weather_hourly_t *in, int today_key, womo_weather_warn_result_t *out)
{
    int s12 = 0, s24 = 0;
    float m12 = max_window_sum(in->precip_mm, in->count, 12, &s12);
    float m24 = max_window_sum(in->precip_mm, in->count, 24, &s24);
    womo_warn_severity_t l12 = level_sum(m12, WW_RAIN12H_L2_GE, WW_RAIN12H_L3_GE, WW_RAIN12H_L4_GT);
    womo_warn_severity_t l24 = level_sum(m24, WW_RAIN24H_L2_GE, WW_RAIN24H_L3_GE, WW_RAIN24H_L4_GT);

    if (l12 >= l24) {
        add_warning(out, in, today_key, WOMO_WARN_TYPE_RAIN_LONG, l12, m12, 12, s12, s12 + 11);
    } else {
        add_warning(out, in, today_key, WOMO_WARN_TYPE_RAIN_LONG, l24, m24, 24, s24, s24 + 23);
    }
}

static womo_warn_severity_t level_snow(float v, int hours, bool high)
{
    if (isnan(v)) return WOMO_WARN_SEV_NONE;
    float l2, l3, l4;
    switch (hours) {
    case 6:
        l2 = WW_SNOW_L2_6H_GT;
        l3 = high ? WW_SNOW_HI_L3_6H_GT : WW_SNOW_LO_L3_6H_GT;
        l4 = high ? WW_SNOW_HI_L4_6H_GT : WW_SNOW_LO_L4_6H_GT;
        break;
    case 12:
        l2 = WW_SNOW_L2_12H_GT;
        l3 = high ? WW_SNOW_HI_L3_12H_GT : WW_SNOW_LO_L3_12H_GT;
        l4 = high ? WW_SNOW_HI_L4_12H_GT : WW_SNOW_LO_L4_12H_GT;
        break;
    default:
        l2 = WW_SNOW_L2_24H_GT;
        l3 = high ? WW_SNOW_HI_L3_24H_GT : WW_SNOW_LO_L3_24H_GT;
        l4 = high ? WW_SNOW_HI_L4_24H_GT : WW_SNOW_LO_L4_24H_GT;
        break;
    }
    if (v > l4) return WOMO_WARN_SEV_EXTREME;
    if (v > l3) return WOMO_WARN_SEV_SEVERE;
    if (v > l2) return WOMO_WARN_SEV_MODERATE;
    if (hours == 6 && v >= WW_SNOW_L1_6H_GE) return WOMO_WARN_SEV_MINOR;
    return WOMO_WARN_SEV_NONE;
}

static void rule_snow(const womo_weather_hourly_t *in, int today_key, womo_weather_warn_result_t *out)
{
    const bool high = !isnan(in->elevation_m) && in->elevation_m > WW_SNOW_ALT_SPLIT_M;
    static const uint8_t windows[] = {6, 12, 24};

    womo_warn_severity_t best_sev = WOMO_WARN_SEV_NONE;
    float best_val = 0.0f;
    uint8_t best_h = 0;
    int best_start = 0;
    for (size_t k = 0; k < sizeof(windows); k++) {
        int start = 0;
        float sum = max_window_sum(in->snow_cm, in->count, windows[k], &start);
        womo_warn_severity_t sev = level_snow(sum, windows[k], high);
        if (sev > best_sev) {   // bei Gleichstand gewinnt das kürzere Fenster
            best_sev   = sev;
            best_val   = sum;
            best_h     = windows[k];
            best_start = start;
        }
    }
    add_warning(out, in, today_key, WOMO_WARN_TYPE_SNOW, best_sev, best_val, best_h,
                best_start, best_start + best_h - 1);
}

/* WMO-Codes: 95 Gewitter, 96/99 Gewitter mit Hagel */
static void rule_thunder(const womo_weather_hourly_t *in, int today_key, womo_weather_warn_result_t *out)
{
    womo_warn_severity_t sev = WOMO_WARN_SEV_NONE;
    int first = -1, last = -1;
    for (int i = 0; i < in->count; i++) {
        int c = in->code[i];
        if (c == 95 || c == 96 || c == 99) {
            if (first < 0) first = i;
            last = i;
            womo_warn_severity_t s = (c == 95) ? WOMO_WARN_SEV_MINOR : WOMO_WARN_SEV_MODERATE;
            if (s > sev) sev = s;
        }
    }
    if (first >= 0) {
        add_warning(out, in, today_key, WOMO_WARN_TYPE_THUNDER, sev, 0.0f, 0, first, last);
    }
}

/* WMO-Codes: 56/57 gefrierender Sprühregen (Stufe 2), 66/67 gefrierender Regen (Stufe 3) */
static void rule_ice(const womo_weather_hourly_t *in, int today_key, womo_weather_warn_result_t *out)
{
    womo_warn_severity_t sev = WOMO_WARN_SEV_NONE;
    int first = -1, last = -1;
    for (int i = 0; i < in->count; i++) {
        int c = in->code[i];
        womo_warn_severity_t s = WOMO_WARN_SEV_NONE;
        if (c == 56 || c == 57) s = WOMO_WARN_SEV_MODERATE;
        if (c == 66 || c == 67) s = WOMO_WARN_SEV_SEVERE;
        if (s != WOMO_WARN_SEV_NONE) {
            if (first < 0) first = i;
            last = i;
            if (s > sev) sev = s;
        }
    }
    if (first >= 0) {
        add_warning(out, in, today_key, WOMO_WARN_TYPE_ICE, sev, 0.0f, 0, first, last);
    }
}

static void rule_frost(const womo_weather_hourly_t *in, int today_key, womo_weather_warn_result_t *out)
{
    float tmin = NAN;
    for (int i = 0; i < in->count; i++) {
        if (!isnan(in->temp_c[i]) && (isnan(tmin) || in->temp_c[i] < tmin)) tmin = in->temp_c[i];
    }
    if (isnan(tmin) || tmin >= WW_FROST_L1_LT) return;

    womo_warn_severity_t sev = (tmin < WW_FROST_L2_LT) ? WOMO_WARN_SEV_MODERATE : WOMO_WARN_SEV_MINOR;
    float thr = (sev == WOMO_WARN_SEV_MODERATE) ? WW_FROST_L2_LT : WW_FROST_L1_LT;
    int first = -1, last = -1;
    for (int i = 0; i < in->count; i++) {
        if (!isnan(in->temp_c[i]) && in->temp_c[i] < thr) {
            if (first < 0) first = i;
            last = i;
        }
    }
    add_warning(out, in, today_key, WOMO_WARN_TYPE_FROST, sev, tmin, 0, first, last);
}

static void rule_heat(const womo_weather_hourly_t *in, int today_key, womo_weather_warn_result_t *out)
{
    float fmax = NAN;
    for (int i = 0; i < in->count; i++) {
        if (!isnan(in->feels_c[i]) && (isnan(fmax) || in->feels_c[i] > fmax)) fmax = in->feels_c[i];
    }
    if (isnan(fmax) || fmax <= WW_HEAT_L1_GT) return;

    womo_warn_severity_t sev = (fmax > WW_HEAT_L3_GT) ? WOMO_WARN_SEV_SEVERE : WOMO_WARN_SEV_MINOR;
    float thr = (sev == WOMO_WARN_SEV_SEVERE) ? WW_HEAT_L3_GT : WW_HEAT_L1_GT;
    int first = -1, last = -1;
    for (int i = 0; i < in->count; i++) {
        if (!isnan(in->feels_c[i]) && in->feels_c[i] > thr) {
            if (first < 0) first = i;
            last = i;
        }
    }
    add_warning(out, in, today_key, WOMO_WARN_TYPE_HEAT, sev, fmax, 0, first, last);
}

/* ── Öffentliche API ─────────────────────────────────────────── */

void womo_weather_warn_evaluate(const womo_weather_hourly_t *in, womo_weather_warn_result_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!in || !in->valid || in->count == 0) return;

    womo_warn_time_t dummy;
    int today_key = 0;
    if (!parse_hour(in->time[0], &dummy, &today_key)) {
        ESP_LOGW(TAG, "Ungültige Zeitangabe '%s'", in->time[0]);
        return;
    }

    rule_wind(in, today_key, out);
    rule_rain_heavy(in, today_key, out);
    rule_rain_long(in, today_key, out);
    rule_snow(in, today_key, out);
    rule_thunder(in, today_key, out);
    rule_ice(in, today_key, out);
    rule_frost(in, today_key, out);
    rule_heat(in, today_key, out);

    /* Absteigend nach Stufe sortieren (Insertion-Sort, max. 8 Einträge) */
    for (int i = 1; i < out->count; i++) {
        womo_weather_warning_t tmp = out->warnings[i];
        int j = i - 1;
        while (j >= 0 && out->warnings[j].severity < tmp.severity) {
            out->warnings[j + 1] = out->warnings[j];
            j--;
        }
        out->warnings[j + 1] = tmp;
    }

    for (int i = 0; i < out->count; i++) {
        uint8_t sev = (uint8_t)out->warnings[i].severity;
        if (sev > out->max_severity) out->max_severity = sev;
        if (sev >= WOMO_WARN_SEV_MODERATE) out->count_major++;
    }
    out->valid = true;

    ESP_LOGI(TAG, "Auswertung: %u Warnungen (%u ab Stufe 2), max. Stufe %u",
             out->count, out->count_major, out->max_severity);
}

/* Bezeichnung je Typ und Stufe bzw. Wert (DWD-Begriffe) */
static womo_string_id_t warn_name_id(const womo_weather_warning_t *w)
{
    switch (w->type) {
    case WOMO_WARN_TYPE_WIND:
        if (w->value > WW_GUST_L4_GT)         return STR_WARN_WIND_EXTREME;
        if (w->value >= WW_GUST_HURRICANE_GE) return STR_WARN_WIND_HURRICANE;
        if (w->value >= WW_GUST_L3_GE)        return STR_WARN_WIND_HURRICANE_LIKE;
        if (w->value >= WW_GUST_SEVERE_GE)    return STR_WARN_WIND_STORM_SEVERE;
        if (w->value >= WW_GUST_L2_GE)        return STR_WARN_WIND_STORM;
        return STR_WARN_WIND_GUSTS;
    case WOMO_WARN_TYPE_RAIN_HEAVY:
        if (w->severity >= WOMO_WARN_SEV_EXTREME) return STR_WARN_RAIN_HEAVY_EXTREME;
        if (w->severity >= WOMO_WARN_SEV_SEVERE)  return STR_WARN_RAIN_HEAVY_SEVERE;
        return STR_WARN_RAIN_HEAVY;
    case WOMO_WARN_TYPE_RAIN_LONG:
        if (w->severity >= WOMO_WARN_SEV_EXTREME) return STR_WARN_RAIN_LONG_EXTREME;
        if (w->severity >= WOMO_WARN_SEV_SEVERE)  return STR_WARN_RAIN_LONG_SEVERE;
        return STR_WARN_RAIN_LONG;
    case WOMO_WARN_TYPE_SNOW:
        if (w->severity >= WOMO_WARN_SEV_EXTREME)  return STR_WARN_SNOW_EXTREME;
        if (w->severity >= WOMO_WARN_SEV_SEVERE)   return STR_WARN_SNOW_SEVERE;
        if (w->severity >= WOMO_WARN_SEV_MODERATE) return STR_WARN_SNOW;
        return STR_WARN_SNOW_LIGHT;
    case WOMO_WARN_TYPE_THUNDER:
        return (w->severity >= WOMO_WARN_SEV_MODERATE) ? STR_WARN_THUNDER_HAIL : STR_WARN_THUNDER;
    case WOMO_WARN_TYPE_ICE:
        return (w->severity >= WOMO_WARN_SEV_SEVERE) ? STR_WARN_ICE_GLAZE : STR_WARN_ICE;
    case WOMO_WARN_TYPE_FROST:
        return (w->severity >= WOMO_WARN_SEV_MODERATE) ? STR_WARN_FROST_SEVERE : STR_WARN_FROST;
    case WOMO_WARN_TYPE_HEAT:
    default:
        return (w->severity >= WOMO_WARN_SEV_SEVERE) ? STR_WARN_HEAT_EXTREME : STR_WARN_HEAT;
    }
}

static void format_time(const womo_warn_time_t *t, char *buf, size_t len)
{
    if (t->today) {
        snprintf(buf, len, "%02u:%02u", t->hour, t->minute);
    } else {
        snprintf(buf, len, "%s %02u:%02u", womo_locale_get_weekday(t->wday), t->hour, t->minute);
    }
}

void womo_weather_warn_format(const womo_weather_warning_t *w,
                              char *title, size_t title_len,
                              char *when, size_t when_len)
{
    if (!w) return;
    const char *name = womo_locale_get_string(warn_name_id(w));
    /* + 0.0f macht aus -0 eine 0 (z. B. Frost bei -0,3 °C) */
    const float value = roundf(w->value) + 0.0f;

    if (title && title_len) {
        switch (w->type) {
        case WOMO_WARN_TYPE_WIND:
            snprintf(title, title_len, womo_locale_get_string(STR_WARN_FMT_UPTO),
                     name, value, "km/h");
            break;
        case WOMO_WARN_TYPE_RAIN_HEAVY:
        case WOMO_WARN_TYPE_RAIN_LONG:
            snprintf(title, title_len, womo_locale_get_string(STR_WARN_FMT_SUM),
                     name, value, "mm", w->hours);
            break;
        case WOMO_WARN_TYPE_SNOW:
            snprintf(title, title_len, womo_locale_get_string(STR_WARN_FMT_SUM),
                     name, value, "cm", w->hours);
            break;
        case WOMO_WARN_TYPE_FROST:
            snprintf(title, title_len, womo_locale_get_string(STR_WARN_FMT_DOWNTO),
                     name, value);
            break;
        case WOMO_WARN_TYPE_HEAT:
            snprintf(title, title_len, womo_locale_get_string(STR_WARN_FMT_FEELS),
                     name, value);
            break;
        default:
            snprintf(title, title_len, "%s", name);
            break;
        }
    }

    if (when && when_len) {
        char from[16], until[16];
        format_time(&w->from, from, sizeof(from));
        format_time(&w->until, until, sizeof(until));
        snprintf(when, when_len, "%s - %s", from, until);
    }
}
