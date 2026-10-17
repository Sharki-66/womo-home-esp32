/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * gas_source.c
 *
 * Wählt die Gewichtsquelle der Gasflaschen: HX711 am Kabel (Vorrang) oder
 * GasBee per BLE-Advertising. Analog zum Ruuvi-Fallback der BME-Sensoren
 * wird die Quelle bei jedem Aufruf neu bestimmt.
 */

#include "gas_source.h"
#include "network/gasbee_ble_scanner.h"
#include "sensor_config.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>

static const char *TAG = "gas_src";

static gas_src_t s_last_src = GAS_SRC_NONE;

// GasBee-Slot in Kanal A/B übertragen. Gibt true zurück, wenn der Slot aktuell ist.
static bool bee_to_channel(int slot, int64_t now, float *kg, bool *valid,
                           int64_t *ts, gas_bee_info_t *info)
{
    gasbee_snapshot_t s;
    gasbee_ble_scanner_get(slot, &s);

    // timestamp_us/now in µs, STALE_MS in ms → umrechnen
    if (!s.valid || (now - s.timestamp_us) > SENSOR_GASBEE_STALE_MS * 1000LL) return false;

    info->present       = true;
    info->battery_valid = s.battery_valid;
    info->battery_mv    = s.battery_mv;
    info->battery_pct   = s.battery_pct;
    info->charging      = s.charging;
    info->calibrated    = s.calibrated;

    *valid = s.weight_valid;
    *kg    = s.weight_valid ? s.weight_kg : 0.0f;
    if (s.timestamp_us > *ts) *ts = s.timestamp_us;
    return true;
}

void gas_source_get_snapshot(gas_source_snapshot_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    // 1. HX711 am Kabel: Modul vorhanden → Vorrang
    hx711_snapshot_t hx = {0};
    if (hx711_app_get_snapshot(&hx) == ESP_OK && (hx.valid_a || hx.valid_b)) {
        out->src = GAS_SRC_HX711;
        out->w   = hx;
    } else {
        // 2. GasBee (BLE)
        int64_t now = esp_timer_get_time();
        bool a = bee_to_channel(0, now, &out->w.kg_a, &out->w.valid_a, &out->w.timestamp_us, &out->bee_a);
        bool b = bee_to_channel(1, now, &out->w.kg_b, &out->w.valid_b, &out->w.timestamp_us, &out->bee_b);
        out->src = (a || b) ? GAS_SRC_GASBEE : GAS_SRC_NONE;
    }

    if (out->src != s_last_src) {
        ESP_LOGI(TAG, "Gas-Quelle: %s → %s", gas_source_name(s_last_src), gas_source_name(out->src));
        s_last_src = out->src;
    }
}

const char *gas_source_name(gas_src_t src)
{
    switch (src) {
        case GAS_SRC_HX711:  return "hx";
        case GAS_SRC_GASBEE: return "ble";
        default:             return "none";
    }
}
