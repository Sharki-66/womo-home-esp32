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
 * Slot-Routing ausschließlich per MAC-Adresse, konfiguriert vom Display
 * (RS485 cmd "ruuvi_config") und in NVS persistiert (sensor_config.h: SENSOR_RUUVI_NVS_*).
 * Leerer/unkonfigurierter MAC = Slot deaktiviert (kein fremder Tag wird akzeptiert).
 * Zusätzlich wird eine Scan-Liste aller gesehenen Ruuvi-Tags (unabhängig vom Filter)
 * geführt, damit das Display eine Auswahlliste zur Konfiguration anbieten kann.
 *
 * Ruuvi DF5 Struktur (Manufacturer Specific Data):
 *   [0–1]  Company ID 0x0499 (LE: 0x99, 0x04)
 *   [2]    Data Format 0x05
 *   [3–4]  Temperatur   int16 BE × 0.005 °C        (0x8000 = invalid)
 *   [5–6]  Luftfeuchte  uint16 BE × 0.0025 %RH     (0xFFFF = invalid)
 *   [7–8]  Luftdruck    uint16 BE + 50000 Pa / 100  (0xFFFF = kein Sensor)
 *   [9–14] Beschleunigung X/Y/Z (nicht ausgewertet)
 *   [15–16] Power Info: Bit 15-5 Batteriespannung (mV, +1600 Offset),
 *           Bit 4-0 TX-Power (nicht ausgewertet); 0x7FF = invalid
 *   [17–20] Movement Counter, Measurement Sequence (nicht ausgewertet)
 */

#include "ruuvi_ble_scanner.h"
#include "ble_manager.h"
#include "sensor_config.h"

#include <host/ble_hs.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <nvs.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "ruuvi_ble";

#define RUUVI_MFG_B0        0x99   // Company ID 0x0499, Byte 0 (LE)
#define RUUVI_MFG_B1        0x04   // Company ID 0x0499, Byte 1 (LE)
#define RUUVI_DF5           0x05
#define RUUVI_PRESS_INVALID 0xFFFF // kein Drucksensor
#define RUUVI_BATT_INVALID  0x7FF  // 11-Bit Batteriespannung ungültig
#define RUUVI_BATT_OFFSET_MV 1600  // Basiswert der 11-Bit-Skala

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

// ── MAC-Konfiguration in NVS ──────────────────────────────────────────────

static void ruuvi_load_macs_from_nvs(char *indoor, size_t indoor_sz, char *outdoor, size_t outdoor_sz)
{
    indoor[0]  = '\0';
    outdoor[0] = '\0';

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(SENSOR_RUUVI_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS: keine Ruuvi-Config gespeichert, Slots deaktiviert");
        return;
    }
    size_t len = indoor_sz;
    nvs_get_str(nvs, SENSOR_RUUVI_NVS_KEY_INDOOR, indoor, &len);
    len = outdoor_sz;
    nvs_get_str(nvs, SENSOR_RUUVI_NVS_KEY_OUTDOOR, outdoor, &len);
    nvs_close(nvs);
}

static esp_err_t ruuvi_save_macs_to_nvs(const char *indoor, const char *outdoor)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(SENSOR_RUUVI_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }
    nvs_set_str(nvs, SENSOR_RUUVI_NVS_KEY_INDOOR, indoor);
    nvs_set_str(nvs, SENSOR_RUUVI_NVS_KEY_OUTDOOR, outdoor);
    err = nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "NVS: Ruuvi-MACs gespeichert");
    return err;
}

// ── Scan-Liste (alle gesehenen Tags, unabhängig vom Filter) ─────────────────

static ruuvi_scan_entry_t s_scan_list[SENSOR_RUUVI_SCAN_MAX_TAGS];
static size_t             s_scan_list_count = 0;

static void scan_list_update(const uint8_t mac[6], int8_t rssi, float temp_c, bool has_pressure)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    int64_t now = esp_timer_get_time();

    for (size_t i = 0; i < s_scan_list_count; i++) {
        if (memcmp(s_scan_list[i].mac, mac, 6) == 0) {
            s_scan_list[i].rssi         = rssi;
            s_scan_list[i].temp_c       = temp_c;
            s_scan_list[i].has_pressure = has_pressure;
            s_scan_list[i].last_seen_us = now;
            xSemaphoreGive(s_mutex);
            return;
        }
    }

    size_t idx;
    if (s_scan_list_count < SENSOR_RUUVI_SCAN_MAX_TAGS) {
        idx = s_scan_list_count++;
    } else {
        // Voll: ältesten Eintrag verdrängen
        idx = 0;
        for (size_t i = 1; i < s_scan_list_count; i++) {
            if (s_scan_list[i].last_seen_us < s_scan_list[idx].last_seen_us) idx = i;
        }
    }

    memcpy(s_scan_list[idx].mac, mac, 6);
    s_scan_list[idx].rssi         = rssi;
    s_scan_list[idx].temp_c       = temp_c;
    s_scan_list[idx].has_pressure = has_pressure;
    s_scan_list[idx].last_seen_us = now;

    xSemaphoreGive(s_mutex);
}

// ── Snapshot aktualisieren ────────────────────────────────────────────────

static void update_slot(ruuvi_snapshot_t *slot, float temp_c, float hum_pct,
                        float press_hpa, bool has_pressure,
                        uint16_t battery_mv, bool battery_valid,
                        int8_t rssi, const uint8_t mac[6])
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    slot->temp_c        = temp_c;
    slot->humidity_pct  = hum_pct;
    slot->pressure_hpa  = press_hpa;
    slot->has_pressure  = has_pressure;
    slot->battery_mv    = battery_mv;
    slot->battery_valid = battery_valid;
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

    // Batteriespannung: Byte [15-16], obere 11 Bit des Power-Info-Felds
    uint16_t battery_mv   = 0;
    bool     battery_valid = false;
    if (fields.mfg_data_len >= 17) {
        uint16_t raw_power = ((uint16_t)fields.mfg_data[15] << 8) | fields.mfg_data[16];
        uint16_t raw_batt  = raw_power >> 5;
        if (raw_batt != RUUVI_BATT_INVALID) {
            battery_mv   = raw_batt + RUUVI_BATT_OFFSET_MV;
            battery_valid = true;
        }
    }

    const uint8_t *addr = disc->addr.val;

    // Scan-Liste: jeder gültige Ruuvi-Tag wird erfasst (unabhängig vom Filter),
    // damit das Display eine Auswahlliste zur Konfiguration anbieten kann.
    scan_list_update(addr, disc->rssi, temp_c, has_pressure);

    // TODO(debug, Außensensor-Fehlersuche 2026-08): Nach Abschluss entfernen.
    // Loggt JEDEN erkannten Ruuvi-Tag unabhängig vom MAC-Filter, um zu prüfen
    // ob der Außensensor überhaupt per Funk ankommt.
    ESP_LOGI(TAG, "[Scan  ] MAC=%02X:%02X:%02X:%02X:%02X:%02X  %.2f °C  RSSI=%d  %s",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0],
             temp_c, disc->rssi, has_pressure ? "mit Druck" : "kein Druck");

    // Routing ausschließlich per MAC – leerer Slot = deaktiviert
    mac_filter_t filt_indoor, filt_outdoor;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    filt_indoor  = s_filter_indoor;
    filt_outdoor = s_filter_outdoor;
    xSemaphoreGive(s_mutex);

    bool is_indoor  = mac_matches(addr, &filt_indoor);
    bool is_outdoor = mac_matches(addr, &filt_outdoor);
    if (!is_indoor && !is_outdoor) return;

    char batt_str[16];
    if (battery_valid) snprintf(batt_str, sizeof(batt_str), "Batt=%umV", battery_mv);
    else                strcpy(batt_str, "Batt=n/a");

    if (is_indoor) {
        update_slot(&s_indoor, temp_c, hum_pct, press_hpa, has_pressure,
                    battery_mv, battery_valid, disc->rssi, addr);
        ESP_LOGI(TAG, "[Innen ] %.2f °C  %.1f %%RH  %s  %s  RSSI=%d",
                 temp_c, hum_pct,
                 has_pressure ? "mit Druck" : "kein Druck",
                 batt_str, disc->rssi);
    }
    if (is_outdoor) {
        update_slot(&s_outdoor, temp_c, hum_pct, press_hpa, has_pressure,
                    battery_mv, battery_valid, disc->rssi, addr);
        ESP_LOGI(TAG, "[Außen ] %.2f °C  %.1f %%RH  %s  %s  RSSI=%d",
                 temp_c, hum_pct,
                 has_pressure ? "mit Druck" : "kein Druck",
                 batt_str, disc->rssi);
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

esp_err_t ruuvi_ble_scanner_set_macs(const char *indoor_mac, const char *outdoor_mac)
{
    const char *indoor  = indoor_mac  ? indoor_mac  : "";
    const char *outdoor = outdoor_mac ? outdoor_mac : "";

    esp_err_t err = ruuvi_save_macs_to_nvs(indoor, outdoor);
    if (err != ESP_OK) return err;

    mac_filter_t new_indoor = {0}, new_outdoor = {0};
    parse_mac_filter(indoor, &new_indoor);
    parse_mac_filter(outdoor, &new_outdoor);

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        // Bei Filteränderung alten Snapshot verwerfen – sonst bleibt "valid"
        // dauerhaft gesetzt und veraltete Werte werden weiter gemeldet, auch
        // wenn der Slot gerade deaktiviert oder auf einen anderen Tag
        // umkonfiguriert wurde (valid wird sonst nirgends zurückgesetzt).
        if (memcmp(&new_indoor, &s_filter_indoor, sizeof(mac_filter_t)) != 0) {
            memset(&s_indoor, 0, sizeof(s_indoor));
        }
        if (memcmp(&new_outdoor, &s_filter_outdoor, sizeof(mac_filter_t)) != 0) {
            memset(&s_outdoor, 0, sizeof(s_outdoor));
        }
        s_filter_indoor  = new_indoor;
        s_filter_outdoor = new_outdoor;
        xSemaphoreGive(s_mutex);
    }

    ESP_LOGI(TAG, "Ruuvi-MACs aktualisiert  Innen: %s  Außen: %s",
             new_indoor.active  ? indoor  : "deaktiviert",
             new_outdoor.active ? outdoor : "deaktiviert");
    return ESP_OK;
}

size_t ruuvi_ble_scanner_get_scan_list(ruuvi_scan_entry_t *out, size_t max_entries)
{
    if (!out || max_entries == 0) return 0;

    size_t n = 0;
    int64_t now = esp_timer_get_time();
    int64_t max_age_us = (int64_t)SENSOR_RUUVI_SCAN_MAX_AGE_S * 1000000LL;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        for (size_t i = 0; i < s_scan_list_count && n < max_entries; i++) {
            if ((now - s_scan_list[i].last_seen_us) <= max_age_us) {
                out[n++] = s_scan_list[i];
            }
        }
        xSemaphoreGive(s_mutex);
    }
    return n;
}

esp_err_t ruuvi_ble_scanner_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    char indoor_mac[18]  = "";
    char outdoor_mac[18] = "";
    ruuvi_load_macs_from_nvs(indoor_mac, sizeof(indoor_mac), outdoor_mac, sizeof(outdoor_mac));
    parse_mac_filter(indoor_mac,  &s_filter_indoor);
    parse_mac_filter(outdoor_mac, &s_filter_outdoor);

    ble_manager_register_adv_handler(ruuvi_on_adv);

    ESP_LOGI(TAG, "Ruuvi Scanner aktiv  Innen: %s  Außen: %s",
             s_filter_indoor.active  ? indoor_mac  : "deaktiviert (nicht konfiguriert)",
             s_filter_outdoor.active ? outdoor_mac : "deaktiviert (nicht konfiguriert)");
    return ESP_OK;
}
