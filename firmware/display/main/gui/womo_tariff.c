/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * womo_tariff.c – Mobilfunk-Tarif: Ablauf-Hinweis und Restvolumen-Warnung
 */
#include "womo_tariff.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "tariff";
#define NVS_NS  "womo_tariff"

static womo_tariff_cfg_t s_cfg = {
    .lead_days  = TARIFF_LEAD_DAYS_DEFAULT,
    .volume_pct = TARIFF_VOLUME_PCT_DEFAULT,
    .period     = WOMO_TARIFF_PERIOD_4W,
    .manual_due = 0,
};
static uint32_t s_auto_due = 0;
static bool     s_auto_dirty = false;   /* Auto-Termin geändert, NVS ausstehend */
static bool     s_volume_valid = false;
static uint8_t  s_volume_pct = 0;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

/* ── Datumsrechnung (proleptischer Gregorianischer Kalender, ganze Tage) ── */

/* Tage seit 1970-01-01 (Algorithmus "days_from_civil", H. Hinnant) */
static int32_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static uint32_t civil_from_days(int32_t z)
{
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const int doe = z - era * 146097;
    const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int mp  = (5 * doy + 2) / 153;
    const int d   = doy - (153 * mp + 2) / 5 + 1;
    const int m   = mp + (mp < 10 ? 3 : -9);
    const int y   = yoe + era * 400 + (m <= 2);
    return (uint32_t)(y * 10000 + m * 100 + d);
}

static bool date_valid(uint32_t v)
{
    const int y = (int)(v / 10000), m = (int)(v / 100 % 100), d = (int)(v % 100);
    return y >= 2020 && y <= 2099 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

static int32_t to_days(uint32_t v)
{
    return days_from_civil((int)(v / 10000), (int)(v / 100 % 100), (int)(v % 100));
}

uint32_t womo_tariff_date_from_tm(const struct tm *tm)
{
    if (!tm) return 0;
    return (uint32_t)((tm->tm_year + 1900) * 10000 + (tm->tm_mon + 1) * 100 + tm->tm_mday);
}

uint32_t womo_tariff_add_days(uint32_t yyyymmdd, int days)
{
    if (!date_valid(yyyymmdd)) return 0;
    return civil_from_days(to_days(yyyymmdd) + days);
}

uint32_t womo_tariff_add_period(uint32_t yyyymmdd, uint8_t period, int dir)
{
    if (!date_valid(yyyymmdd)) return 0;
    const int sign = dir < 0 ? -1 : 1;
    switch (period) {
        case WOMO_TARIFF_PERIOD_30D:
            return womo_tariff_add_days(yyyymmdd, 30 * sign);
        case WOMO_TARIFF_PERIOD_MONTH: {
            int y = (int)(yyyymmdd / 10000), m = (int)(yyyymmdd / 100 % 100), d = (int)(yyyymmdd % 100);
            m += sign;
            if (m > 12) { m = 1;  y++; }
            if (m < 1)  { m = 12; y--; }
            /* 31. → letzter Tag des Zielmonats */
            static const uint8_t mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            int max_d = mdays[m - 1];
            if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) max_d = 29;
            if (d > max_d) d = max_d;
            return (uint32_t)(y * 10000 + m * 100 + d);
        }
        case WOMO_TARIFF_PERIOD_4W:
        default:
            return womo_tariff_add_days(yyyymmdd, 28 * sign);
    }
}

uint32_t womo_tariff_parse_iso(const char *iso)
{
    int y = 0, m = 0, d = 0;
    if (!iso || sscanf(iso, "%d-%d-%d", &y, &m, &d) != 3) return 0;
    const uint32_t v = (uint32_t)(y * 10000 + m * 100 + d);
    return date_valid(v) ? v : 0;
}

/* ── NVS ──────────────────────────────────────────────────────────────── */

static void nvs_save_cfg(const womo_tariff_cfg_t *cfg)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h,  "lead_days",  cfg->lead_days);
    nvs_set_u8(h,  "vol_pct",    cfg->volume_pct);
    nvs_set_u8(h,  "period",     cfg->period);
    nvs_set_u32(h, "manual_due", cfg->manual_due);
    nvs_commit(h);
    nvs_close(h);
}

static void nvs_save_auto(uint32_t due)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "auto_due", due);
    nvs_commit(h);
    nvs_close(h);
}

void womo_tariff_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "Kein NVS-Eintrag – Standardwerte werden verwendet");
        return;
    }
    uint8_t v8;
    uint32_t v32;
    if (nvs_get_u8(h, "lead_days", &v8) == ESP_OK && v8 <= TARIFF_LEAD_DAYS_MAX)  s_cfg.lead_days  = v8;
    if (nvs_get_u8(h, "vol_pct",   &v8) == ESP_OK && v8 <= TARIFF_VOLUME_PCT_MAX) s_cfg.volume_pct = v8;
    if (nvs_get_u8(h, "period",    &v8) == ESP_OK && v8 < WOMO_TARIFF_PERIOD_COUNT) s_cfg.period = v8;
    if (nvs_get_u32(h, "manual_due", &v32) == ESP_OK && (v32 == 0 || date_valid(v32))) s_cfg.manual_due = v32;
    if (nvs_get_u32(h, "auto_due",   &v32) == ESP_OK && (v32 == 0 || date_valid(v32))) s_auto_due = v32;
    nvs_close(h);
    ESP_LOGI(TAG, "Tarif aus NVS: Vorlauf %u Tage, Volumen < %u %%, Rhythmus %u, manuell %lu, auto %lu",
             s_cfg.lead_days, s_cfg.volume_pct, s_cfg.period,
             (unsigned long)s_cfg.manual_due, (unsigned long)s_auto_due);
}

void womo_tariff_get_cfg(womo_tariff_cfg_t *out)
{
    if (!out) return;
    taskENTER_CRITICAL(&s_lock);
    *out = s_cfg;
    taskEXIT_CRITICAL(&s_lock);
}

void womo_tariff_set_cfg(const womo_tariff_cfg_t *in)
{
    if (!in) return;
    womo_tariff_cfg_t c = *in;
    if (c.lead_days > TARIFF_LEAD_DAYS_MAX)   c.lead_days = TARIFF_LEAD_DAYS_MAX;
    if (c.volume_pct > TARIFF_VOLUME_PCT_MAX) c.volume_pct = TARIFF_VOLUME_PCT_MAX;
    if (c.period >= WOMO_TARIFF_PERIOD_COUNT) c.period = WOMO_TARIFF_PERIOD_4W;
    if (c.manual_due && !date_valid(c.manual_due)) c.manual_due = 0;
    taskENTER_CRITICAL(&s_lock);
    s_cfg = c;
    taskEXIT_CRITICAL(&s_lock);
    nvs_save_cfg(&c);
}

void womo_tariff_set_auto_due(uint32_t yyyymmdd)
{
    if (yyyymmdd && !date_valid(yyyymmdd)) return;
    bool changed;
    taskENTER_CRITICAL(&s_lock);
    changed = (s_auto_due != yyyymmdd);
    s_auto_due = yyyymmdd;
    if (changed) s_auto_dirty = true;
    taskEXIT_CRITICAL(&s_lock);
    if (changed) {
        ESP_LOGI(TAG, "Automatischer Tarif-Termin: %lu", (unsigned long)yyyymmdd);
    }
}

void womo_tariff_flush(void)
{
    uint32_t due;
    bool dirty;
    taskENTER_CRITICAL(&s_lock);
    dirty = s_auto_dirty;
    due = s_auto_due;
    s_auto_dirty = false;
    taskEXIT_CRITICAL(&s_lock);
    if (dirty) nvs_save_auto(due);
}

uint32_t womo_tariff_get_auto_due(void)
{
    taskENTER_CRITICAL(&s_lock);
    uint32_t v = s_auto_due;
    taskEXIT_CRITICAL(&s_lock);
    return v;
}

/* "29,99" → 29.99 (Dezimalkomma der Telekom-Seite) */
static bool parse_volume(const char *s, float *out)
{
    if (!s || !s[0]) return false;
    char buf[16];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (char *p = buf; *p; p++) {
        if (*p == ',') *p = '.';
    }
    char *end = NULL;
    *out = strtof(buf, &end);
    return end != buf;
}

void womo_tariff_set_volume(const char *remaining, const char *total)
{
    float rem = 0.0f, tot = 0.0f;
    const bool ok = parse_volume(remaining, &rem) && parse_volume(total, &tot)
                    && tot > 0.0f && rem >= 0.0f && rem <= tot * 1.01f;
    int pct = ok ? (int)(rem * 100.0f / tot + 0.5f) : 0;
    if (pct > 100) pct = 100;
    taskENTER_CRITICAL(&s_lock);
    s_volume_valid = ok;
    s_volume_pct = (uint8_t)pct;
    taskEXIT_CRITICAL(&s_lock);
}

bool womo_tariff_evaluate(const struct tm *now, womo_tariff_state_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    const uint32_t today = womo_tariff_date_from_tm(now);
    if (!date_valid(today)) return false;

    womo_tariff_cfg_t cfg;
    uint32_t auto_due;
    taskENTER_CRITICAL(&s_lock);
    cfg = s_cfg;
    auto_due = s_auto_due;
    out->volume_valid = s_volume_valid;
    out->volume_pct   = s_volume_pct;
    taskEXIT_CRITICAL(&s_lock);

    out->volume_low = cfg.volume_pct > 0 && out->volume_valid && out->volume_pct < cfg.volume_pct;

    const int32_t today_d = to_days(today);
    if (auto_due) {
        out->due = auto_due;
        out->due_auto = true;
    } else if (cfg.manual_due) {
        /* Manueller Termin rollt weiter – ob verlängert wurde, ist unbekannt */
        uint32_t due = cfg.manual_due;
        for (int i = 0; i < 48 && due && to_days(due) < today_d; i++) {
            due = womo_tariff_add_period(due, cfg.period, +1);
        }
        out->due = due;
    }
    if (!out->due) return true;

    /* "Gültig bis" schließt den Tag ein → abgelaufen erst am Folgetag */
    out->days_left = (int)(to_days(out->due) - today_d);
    if (cfg.lead_days == 0) {
        out->phase = WOMO_TARIFF_OK;
    } else if (out->days_left < 0) {
        out->phase = WOMO_TARIFF_EXPIRED;
    } else if (out->days_left <= cfg.lead_days) {
        out->phase = WOMO_TARIFF_SOON;
    }
    return true;
}
