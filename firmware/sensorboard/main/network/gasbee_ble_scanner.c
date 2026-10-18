/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * gasbee_ble_scanner.c
 *
 * Dekodiert das GasBee-Advertising-Format v1 (firmware/gasbee_nrf).
 * Registriert sich beim BLE-Manager – kein eigenes NimBLE, kein Connect.
 *
 * Erkennung: Complete Local Name "GasBee" + Manufacturer Data mit
 * Company-ID 0xFFFF und Format-Version 1. Unbekannte Version → verwerfen.
 *
 * Manufacturer Specific Data:
 *   [0–1]  Company ID 0xFFFF (LE)
 *   [2]    Format-Version = 1
 *   [3–4]  Bruttogewicht int16 LE in 10 g      (0x8000 = ungültig)
 *   [5–6]  Akkuspannung  uint16 LE in mV       (0xFFFF = ungültig)
 *   [7]    Sequenzzähler, +1 pro Messzyklus (alle 10 Pakete eines Zyklus gleich)
 *   [8]    Flags: Bit 0 HX711 ok, Bit 1 lädt, Bit 2 kalibriert,
 *          Bit 3 Konfigurationsmodus (USB, Sekundentakt), Bit 4–7 reserviert
 *
 * Duplikate: Pro Zyklus kommen bis zu 10 Pakete mit gleicher seq – ausgewertet
 * wird nur das erste Paket einer neuen seq. Jede seq ≠ der zuletzt gesehenen
 * gilt als neu (funktioniert auch nach Neustart der GasBee / Überlauf 255 → 0).
 */

#include "gasbee_ble_scanner.h"
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

static const char *TAG = "gasbee_ble";

#define GASBEE_NAME            "GasBee"
#define GASBEE_NAME_LEN        6
#define GASBEE_MFG_LEN         9       // 2 Company-ID + 7 Nutzdaten
#define GASBEE_WEIGHT_INVALID  ((int16_t)0x8000)
#define GASBEE_BATT_INVALID    0xFFFF
#define GASBEE_FLAG_HX_OK      0x01
#define GASBEE_FLAG_CHARGING   0x02
#define GASBEE_FLAG_CALIBRATED 0x04
#define GASBEE_FLAG_CONFIG     0x08

#define SLOT_COUNT 2

// ── Snapshots ─────────────────────────────────────────────────────────────

static gasbee_snapshot_t s_slot[SLOT_COUNT] = {0};
static uint8_t           s_slot_pkts[SLOT_COUNT] = {0}; // Pakete zur aktuellen seq (Coex-Diagnose)
static SemaphoreHandle_t s_mutex = NULL;

// ── MAC-Filter ────────────────────────────────────────────────────────────

typedef struct {
    bool    active;
    uint8_t mac[6]; // MSB-first (wie Nutzereingabe "F7:8A:BE:C9:83:CB")
} mac_filter_t;

static mac_filter_t s_filter[SLOT_COUNT] = {0};

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

// BLE addr.val ist LSB-first, Filter MSB-first. Inaktiver Filter = Slot deaktiviert.
static bool mac_matches(const uint8_t val[6], const mac_filter_t *filter)
{
    if (!filter->active) return false;
    return (val[5] == filter->mac[0] && val[4] == filter->mac[1] &&
            val[3] == filter->mac[2] && val[2] == filter->mac[3] &&
            val[1] == filter->mac[4] && val[0] == filter->mac[5]);
}

// ── MAC-Konfiguration in NVS ──────────────────────────────────────────────

static void load_macs_from_nvs(char *mac_a, size_t a_sz, char *mac_b, size_t b_sz)
{
    mac_a[0] = '\0';
    mac_b[0] = '\0';

    nvs_handle_t nvs;
    if (nvs_open(SENSOR_GASBEE_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        ESP_LOGI(TAG, "NVS: keine GasBee-Config gespeichert, Slots deaktiviert");
        return;
    }
    size_t len = a_sz;
    nvs_get_str(nvs, SENSOR_GASBEE_NVS_KEY_A, mac_a, &len);
    len = b_sz;
    nvs_get_str(nvs, SENSOR_GASBEE_NVS_KEY_B, mac_b, &len);
    nvs_close(nvs);
}

static esp_err_t save_macs_to_nvs(const char *mac_a, const char *mac_b)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(SENSOR_GASBEE_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }
    nvs_set_str(nvs, SENSOR_GASBEE_NVS_KEY_A, mac_a);
    nvs_set_str(nvs, SENSOR_GASBEE_NVS_KEY_B, mac_b);
    err = nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "NVS: GasBee-MACs gespeichert");
    return err;
}

// ── Akku-Kennlinie ────────────────────────────────────────────────────────

typedef struct { uint16_t mv; uint8_t pct; } lipo_point_t;
static const lipo_point_t s_lipo_curve[] = SENSOR_GASBEE_LIPO_CURVE;
#define LIPO_POINTS (sizeof(s_lipo_curve) / sizeof(s_lipo_curve[0]))

uint8_t gasbee_battery_pct(uint16_t mv)
{
    if (mv >= s_lipo_curve[0].mv) return s_lipo_curve[0].pct;
    for (size_t i = 1; i < LIPO_POINTS; i++) {
        const lipo_point_t *hi = &s_lipo_curve[i - 1];
        const lipo_point_t *lo = &s_lipo_curve[i];
        if (mv >= lo->mv) {
            return (uint8_t)(lo->pct + (uint32_t)(mv - lo->mv) * (hi->pct - lo->pct) / (hi->mv - lo->mv));
        }
    }
    return s_lipo_curve[LIPO_POINTS - 1].pct;
}

// ── Scan-Liste (alle gesehenen GasBees, unabhängig vom Filter) ─────────────

static gasbee_scan_entry_t s_scan_list[SENSOR_GASBEE_SCAN_MAX_TAGS];
static size_t              s_scan_list_count = 0;

// Aufruf mit gehaltenem Mutex. Gibt true zurück, wenn die MAC neu in der Liste ist.
static bool scan_list_update_locked(const gasbee_scan_entry_t *e)
{
    bool is_new = false;
    size_t idx = s_scan_list_count;
    for (size_t i = 0; i < s_scan_list_count; i++) {
        if (memcmp(s_scan_list[i].mac, e->mac, 6) == 0) { idx = i; break; }
    }
    if (idx == s_scan_list_count) {
        is_new = true;
        if (s_scan_list_count < SENSOR_GASBEE_SCAN_MAX_TAGS) {
            s_scan_list_count++;
        } else {
            // Voll: ältesten Eintrag verdrängen
            idx = 0;
            for (size_t i = 1; i < s_scan_list_count; i++) {
                if (s_scan_list[i].last_seen_us < s_scan_list[idx].last_seen_us) idx = i;
            }
        }
    }
    s_scan_list[idx] = *e;
    return is_new;
}

// ── Advertisement-Handler ─────────────────────────────────────────────────

static void gasbee_on_adv(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) return;
    if (!fields.name || fields.name_len != GASBEE_NAME_LEN ||
        memcmp(fields.name, GASBEE_NAME, GASBEE_NAME_LEN) != 0) return;
    if (!fields.mfg_data || fields.mfg_data_len < GASBEE_MFG_LEN) return;

    const uint8_t *m = fields.mfg_data;
    uint16_t company = (uint16_t)m[0] | ((uint16_t)m[1] << 8);
    if (company != SENSOR_GASBEE_COMPANY_ID) return;
    if (m[2] != SENSOR_GASBEE_FORMAT_VERSION) {
        ESP_LOGD(TAG, "Unbekannte Format-Version %u – verworfen", m[2]);
        return;
    }

    int16_t  raw_weight = (int16_t)((uint16_t)m[3] | ((uint16_t)m[4] << 8));
    uint16_t raw_batt   = (uint16_t)m[5] | ((uint16_t)m[6] << 8);
    uint8_t  seq        = m[7];
    uint8_t  flags      = m[8];   // Bit 4–7 reserviert → ignoriert

    gasbee_scan_entry_t e = {0};
    memcpy(e.mac, disc->addr.val, 6);
    e.addr_type     = disc->addr.type;
    e.rssi          = disc->rssi;
    e.weight_valid  = (flags & GASBEE_FLAG_HX_OK) && raw_weight != GASBEE_WEIGHT_INVALID;
    e.weight_kg     = e.weight_valid ? raw_weight / 100.0f : 0.0f;
    e.battery_valid = (raw_batt != GASBEE_BATT_INVALID);
    e.battery_mv    = e.battery_valid ? raw_batt : 0;
    e.charging      = (flags & GASBEE_FLAG_CHARGING) != 0;
    e.calibrated    = (flags & GASBEE_FLAG_CALIBRATED) != 0;
    e.config_mode   = (flags & GASBEE_FLAG_CONFIG) != 0;
    e.seq           = seq;
    e.last_seen_us  = esp_timer_get_time();

    const uint8_t *a = disc->addr.val;
    uint8_t battery_pct = e.battery_valid ? gasbee_battery_pct(e.battery_mv) : 0;

    // Log-Infos sammeln und erst nach Freigabe des Mutex ausgeben
    // (Leser warten nur 10 ms – UART-Log unter Mutex würde sie aussperren)
    bool    log_new[SLOT_COUNT]   = {false};
    bool    log_prev[SLOT_COUNT]  = {false};
    uint8_t prev_seq[SLOT_COUNT]  = {0};
    uint8_t prev_pkts[SLOT_COUNT] = {0};

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    // Scan-Liste: jede GasBee wird erfasst (unabhängig vom Filter)
    bool first_seen = scan_list_update_locked(&e);

    for (int slot = 0; slot < SLOT_COUNT; slot++) {
        if (!mac_matches(a, &s_filter[slot])) continue;
        gasbee_snapshot_t *s = &s_slot[slot];
        s->rssi = disc->rssi;

        if (s->valid && s->seq == seq) {
            // Duplikat desselben Messzyklus – nur mitzählen
            if (s_slot_pkts[slot] < UINT8_MAX) s_slot_pkts[slot]++;
            continue;
        }

        // Neue seq: Paketzahl des vorigen Zyklus merken (Koexistenz-Diagnose Scan-Tastverhältnis)
        if (s->valid) {
            log_prev[slot]  = true;
            prev_seq[slot]  = s->seq;
            prev_pkts[slot] = s_slot_pkts[slot];
        }
        log_new[slot]     = true;
        s_slot_pkts[slot] = 1;

        s->weight_kg     = e.weight_kg;
        s->weight_valid  = e.weight_valid;
        s->battery_mv    = e.battery_mv;
        s->battery_valid = e.battery_valid;
        s->battery_pct   = battery_pct;
        s->charging      = e.charging;
        s->calibrated    = e.calibrated;
        s->config_mode   = e.config_mode;
        s->seq           = seq;
        memcpy(s->mac, a, 6);
        s->valid         = true;
        s->timestamp_us  = e.last_seen_us;
    }
    xSemaphoreGive(s_mutex);

    if (first_seen) {
        ESP_LOGI(TAG, "GasBee gesehen: MAC=%02X:%02X:%02X:%02X:%02X:%02X  RSSI=%d",
                 a[5], a[4], a[3], a[2], a[1], a[0], disc->rssi);
    }

    for (int slot = 0; slot < SLOT_COUNT; slot++) {
        if (log_prev[slot]) {
            ESP_LOGI(TAG, "[Flasche %c] seq %u: %u/10 Pakete empfangen",
                     'A' + slot, prev_seq[slot], prev_pkts[slot]);
        }
        if (log_new[slot]) {
            ESP_LOGI(TAG, "[Flasche %c] seq=%u  %s%.2f kg  Akku=%umV (%u%%)%s%s  RSSI=%d",
                     'A' + slot, seq,
                     e.weight_valid ? "" : "HX711-Fehler ", e.weight_kg,
                     e.battery_mv, battery_pct,
                     e.charging ? " lädt" : "", e.calibrated ? "" : " unkalibriert",
                     disc->rssi);
        }
    }
}

// ── Public API ────────────────────────────────────────────────────────────

void gasbee_ble_scanner_get(int slot, gasbee_snapshot_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (slot < 0 || slot >= SLOT_COUNT || !s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        *out = s_slot[slot];
        xSemaphoreGive(s_mutex);
    }
}

esp_err_t gasbee_ble_scanner_set_macs(const char *bottle_a_mac, const char *bottle_b_mac)
{
    const char *mac_a = bottle_a_mac ? bottle_a_mac : "";
    const char *mac_b = bottle_b_mac ? bottle_b_mac : "";

    esp_err_t err = save_macs_to_nvs(mac_a, mac_b);
    if (err != ESP_OK) return err;

    mac_filter_t new_filter[SLOT_COUNT] = {0};
    parse_mac_filter(mac_a, &new_filter[0]);
    parse_mac_filter(mac_b, &new_filter[1]);

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < SLOT_COUNT; i++) {
            // Bei Filteränderung alten Snapshot verwerfen – sonst würden Werte
            // der vorher zugeordneten GasBee weiter gemeldet.
            if (memcmp(&new_filter[i], &s_filter[i], sizeof(mac_filter_t)) != 0) {
                memset(&s_slot[i], 0, sizeof(s_slot[i]));
                s_slot_pkts[i] = 0;
            }
            s_filter[i] = new_filter[i];
        }
        xSemaphoreGive(s_mutex);
    }

    ESP_LOGI(TAG, "GasBee-MACs aktualisiert  A: %s  B: %s",
             new_filter[0].active ? mac_a : "deaktiviert",
             new_filter[1].active ? mac_b : "deaktiviert");
    return ESP_OK;
}

size_t gasbee_ble_scanner_get_scan_list(gasbee_scan_entry_t *out, size_t max_entries)
{
    if (!out || max_entries == 0 || !s_mutex) return 0;

    size_t n = 0;
    int64_t now = esp_timer_get_time();
    int64_t max_age_us = (int64_t)SENSOR_GASBEE_SCAN_MAX_AGE_S * 1000000LL;

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

bool gasbee_ble_scanner_find(const char *mac_str, gasbee_scan_entry_t *out)
{
    mac_filter_t want;
    parse_mac_filter(mac_str, &want);
    if (!want.active || !out || !s_mutex) return false;

    bool found = false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        for (size_t i = 0; i < s_scan_list_count; i++) {
            if (mac_matches(s_scan_list[i].mac, &want)) {
                *out  = s_scan_list[i];
                found = true;
                break;
            }
        }
        xSemaphoreGive(s_mutex);
    }
    return found;
}

esp_err_t gasbee_ble_scanner_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    char mac_a[18] = "";
    char mac_b[18] = "";
    load_macs_from_nvs(mac_a, sizeof(mac_a), mac_b, sizeof(mac_b));
    parse_mac_filter(mac_a, &s_filter[0]);
    parse_mac_filter(mac_b, &s_filter[1]);

    ble_manager_register_adv_handler(gasbee_on_adv);

    ESP_LOGI(TAG, "GasBee Scanner aktiv  A: %s  B: %s",
             s_filter[0].active ? mac_a : "deaktiviert (nicht konfiguriert)",
             s_filter[1].active ? mac_b : "deaktiviert (nicht konfiguriert)");
    return ESP_OK;
}
