/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * ruuvi_ble_scanner.c
 *
 * Dekodiert Ruuvi Data Format 5 (RAWv2) aus BLE-Advertisements.
 * Registriert sich beim BLE-Manager – kein eigenes NimBLE.
 *
 * Slot-Routing ausschließlich per MAC-Adresse (sensor_config.h):
 *   SENSOR_RUUVI_INDOOR_MAC  → Indoor-Slot
 *   SENSOR_RUUVI_OUTDOOR_MAC → Outdoor-Slot
 *   Leerer MAC = Slot deaktiviert (kein fremder Tag wird akzeptiert).
 *
 * Ruuvi DF5 Struktur (Manufacturer Specific Data):
 *   [0–1]  Company ID 0x0499 (LE: 0x99, 0x04)
 *   [2]    Data Format 0x05
 *   [3–4]  Temperatur   int16 BE × 0.005 °C        (0x8000 = invalid)
 *   [5–6]  Luftfeuchte  uint16 BE × 0.0025 %RH     (0xFFFF = invalid)
 *   [7–8]  Luftdruck    uint16 BE + 50000 Pa / 100  (0xFFFF = kein Sensor)
 *   [9–20] Beschleunigung, Spannung, Sequenz (nicht ausgewertet)
 */

#include "ruuvi_ble_scanner.h"
#include "ble_manager.h"
#include "sensor_config.h"

#include <host/ble_hs.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "ruuvi_ble";

#define RUUVI_MFG_B0        0x99   // Company ID 0x0499, Byte 0 (LE)
#define RUUVI_MFG_B1        0x04   // Company ID 0x0499, Byte 1 (LE)
#define RUUVI_DF5           0x05
#define RUUVI_PRESS_INVALID 0xFFFF // kein Drucksensor

// ── Snapshots ─────────────────────────────────────────────────────────────

static ruuvi_snapshot_t  s_indoor  = {0};
static ruuvi_snapshot_t  s_outdoor = {0};
static SemaphoreHandle_t s_mutex   = NULL;

// ── MAC-Filter ────────────────────────────────────────────────────────────

typedef struct {
    bool    active;
    uint8_t mac[6]; // MSB-first (wie Nutzereingabe "CE:F1:47:5C:CC:C9")
} mac_filter_t;

static mac_filter_t s_filter_indoor  = {0};
static mac_filter_t s_filter_outdoor = {0};

static void parse_mac_filter(const char *mac_str, mac_filter_t *out)
{
    if (!mac_str || mac_str[0] == '\0') {
        out->active = false;
        return;
    }
    unsigned int b[6];
    if (sscanf(mac_str, "%x:%x:%x:%x:%x:%x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
        for (int i = 0; i < 6; i++) out->mac[i] = (uint8_t)b[i];
        out->active = true;
    } else {
        out->active = false;
        ESP_LOGW(TAG, "MAC '%s' ungültig – kein Filter", mac_str);
    }
}

// BLE addr.val: LSB-first → val[5]=CE, val[4]=F1, … val[0]=C9 für "CE:F1:…:C9"
// Gibt false wenn Filter nicht aktiv (leerer MAC = Slot deaktiviert).
static bool mac_matches(const uint8_t val[6], const mac_filter_t *filter)
{
    if (!filter->active) return false;
    return (val[5] == filter->mac[0] && val[4] == filter->mac[1] &&
            val[3] == filter->mac[2] && val[2] == filter->mac[3] &&
            val[1] == filter->mac[4] && val[0] == filter->mac[5]);
}

// ── Snapshot aktualisieren ────────────────────────────────────────────────

static void update_slot(ruuvi_snapshot_t *slot, float temp_c, float hum_pct,
                        float press_hpa, bool has_pressure,
                        int8_t rssi, const uint8_t mac[6])
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    slot->temp_c        = temp_c;
    slot->humidity_pct  = hum_pct;
    slot->pressure_hpa  = press_hpa;
    slot->has_pressure  = has_pressure;
    slot->rssi          = rssi;
    memcpy(slot->mac, mac, 6);
    slot->valid         = true;
    slot->timestamp_us  = esp_timer_get_time();
    xSemaphoreGive(s_mutex);
}

// ── Advertisement-Handler ─────────────────────────────────────────────────

static void ruuvi_on_adv(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) return;
    if (!fields.mfg_data || fields.mfg_data_len < 9) return;

    if (fields.mfg_data[0] != RUUVI_MFG_B0 || fields.mfg_data[1] != RUUVI_MFG_B1) return;
    if (fields.mfg_data[2] != RUUVI_DF5) return;

    // Temperatur: int16 BE × 0.005 °C
    int16_t raw_temp = (int16_t)(((uint16_t)fields.mfg_data[3] << 8) | fields.mfg_data[4]);
    float temp_c = raw_temp * 0.005f;

    // Luftfeuchte: uint16 BE × 0.0025 %RH
    uint16_t raw_hum = ((uint16_t)fields.mfg_data[5] << 8) | fields.mfg_data[6];
    float hum_pct = raw_hum * 0.0025f;

    // Luftdruck: 0xFFFF = kein Sensor (IP68-Außentag)
    uint16_t raw_press = ((uint16_t)fields.mfg_data[7] << 8) | fields.mfg_data[8];
    bool has_pressure = (raw_press != RUUVI_PRESS_INVALID);
    float press_hpa = has_pressure ? (raw_press + 50000U) / 100.0f : 0.0f;

    const uint8_t *addr = disc->addr.val;

    // Routing ausschließlich per MAC – leerer Slot = deaktiviert
    bool is_indoor  = mac_matches(addr, &s_filter_indoor);
    bool is_outdoor = mac_matches(addr, &s_filter_outdoor);
    if (!is_indoor && !is_outdoor) return;

    if (is_indoor) {
        update_slot(&s_indoor, temp_c, hum_pct, press_hpa, has_pressure, disc->rssi, addr);
        ESP_LOGI(TAG, "[Innen ] %.2f °C  %.1f %%RH  %s  RSSI=%d",
                 temp_c, hum_pct,
                 has_pressure ? "mit Druck" : "kein Druck",
                 disc->rssi);
    }
    if (is_outdoor) {
        update_slot(&s_outdoor, temp_c, hum_pct, press_hpa, has_pressure, disc->rssi, addr);
        ESP_LOGI(TAG, "[Außen ] %.2f °C  %.1f %%RH  %s  RSSI=%d",
                 temp_c, hum_pct,
                 has_pressure ? "mit Druck" : "kein Druck",
                 disc->rssi);
    }
}

// ── Public API ────────────────────────────────────────────────────────────

void ruuvi_ble_scanner_get_indoor(ruuvi_snapshot_t *out)
{
    if (!out) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        *out = s_indoor;
        xSemaphoreGive(s_mutex);
    } else {
        memset(out, 0, sizeof(*out));
    }
}

void ruuvi_ble_scanner_get_outdoor(ruuvi_snapshot_t *out)
{
    if (!out) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        *out = s_outdoor;
        xSemaphoreGive(s_mutex);
    } else {
        memset(out, 0, sizeof(*out));
    }
}

esp_err_t ruuvi_ble_scanner_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    parse_mac_filter(SENSOR_RUUVI_INDOOR_MAC,  &s_filter_indoor);
    parse_mac_filter(SENSOR_RUUVI_OUTDOOR_MAC, &s_filter_outdoor);

    ble_manager_register_adv_handler(ruuvi_on_adv);

    ESP_LOGI(TAG, "Ruuvi Scanner aktiv  Innen: %s  Außen: %s",
             s_filter_indoor.active  ? SENSOR_RUUVI_INDOOR_MAC  : "deaktiviert",
             s_filter_outdoor.active ? SENSOR_RUUVI_OUTDOOR_MAC : "deaktiviert");
    return ESP_OK;
}
