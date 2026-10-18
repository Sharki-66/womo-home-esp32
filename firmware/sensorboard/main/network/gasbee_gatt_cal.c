/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * gasbee_gatt_cal.c
 *
 * GATT-Client für die Kalibrierung der GasBee (Vorgabe: firmware/gasbee_nrf/README.md).
 * Nutzt den NimBLE-Stack des BLE-Managers (Central-Rolle, eine Verbindung).
 *
 *   Service 4a5b6c7d-8e9f-0a1b-2c3d-4e5f60718293
 *   0x2B11  Write  01           Nullpunkt (Waage leer)
 *   0x2B12  Write  uint32 LE    Referenzgewicht in Gramm
 *
 * Zustandsfolge: connect → disc_svc_by_uuid → disc_all_chrs → write → terminate.
 * Das Ergebnis wird beim Write-Callback (bzw. Fehler/Timeout) gemeldet, danach
 * wird getrennt. Ein neuer Schritt ist erst nach dem Disconnect möglich.
 */

#include "gasbee_gatt_cal.h"
#include "gasbee_ble_scanner.h"
#include "ble_manager.h"
#include "sensor_config.h"

#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <string.h>

static const char *TAG = "gasbee_cal";

// Service-UUID little-endian (wie im früheren gasbee_ble_client)
static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(
    0x93, 0x82, 0x71, 0x60, 0x5f, 0x4e, 0x3d, 0x2c,
    0x1b, 0x0a, 0x9f, 0x8e, 0x7d, 0x6c, 0x5b, 0x4a
);
#define CHR_CAL_ZERO   0x2B11
#define CHR_CAL_REF_G  0x2B12
#define CAL_REF_MAX_G  200000U   // wie GASBEE_CAL_REF_MAX_G in firmware/gasbee_nrf

static portMUX_TYPE        s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool                s_busy = false;          // Schritt aktiv bis zum Disconnect
static bool                s_reported = false;      // Ergebnis bereits gemeldet
static uint16_t            s_conn = BLE_HS_CONN_HANDLE_NONE;
static gasbee_cal_step_t   s_step;
static uint8_t             s_payload[4];
static uint16_t            s_payload_len;
static uint16_t            s_chr_uuid;
static uint16_t            s_chr_handle;
static bool                s_svc_found;
static gasbee_cal_done_cb_t s_cb;
static void               *s_cb_arg;
static esp_timer_handle_t  s_timeout;

// Ergebnis genau einmal melden; bestehende Verbindung danach trennen
static void cal_finish(int result)
{
    taskENTER_CRITICAL(&s_lock);
    bool first = !s_reported;
    s_reported = true;
    uint16_t conn = s_conn;
    gasbee_cal_done_cb_t cb = s_cb;
    void *arg = s_cb_arg;
    gasbee_cal_step_t step = s_step;
    if (conn == BLE_HS_CONN_HANDLE_NONE) s_busy = false;   // ohne Verbindung sofort frei
    taskEXIT_CRITICAL(&s_lock);

    if (!first) return;
    esp_timer_stop(s_timeout);   // ESP_ERR_INVALID_STATE, wenn nicht aktiv – egal

    if (result == GASBEE_CAL_OK) {
        ESP_LOGI(TAG, "Schritt %s: Write bestätigt", step == GASBEE_CAL_STEP_ZERO ? "Nullpunkt" : "Referenz");
    } else {
        ESP_LOGW(TAG, "Schritt %s fehlgeschlagen: %s (%d)",
                 step == GASBEE_CAL_STEP_ZERO ? "Nullpunkt" : "Referenz",
                 gasbee_gatt_cal_result_str(result), result);
    }
    if (conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    if (cb) cb(step, result, arg);
}

static void timeout_cb(void *arg)
{
    (void)arg;
    taskENTER_CRITICAL(&s_lock);
    bool connecting = s_busy && s_conn == BLE_HS_CONN_HANDLE_NONE;
    taskEXIT_CRITICAL(&s_lock);
    if (connecting) {
        // Verbindungsaufbau hängt → abbrechen; BLE_GAP_EVENT_CONNECT mit Fehler folgt
        ble_gap_conn_cancel();
    }
    cal_finish(GASBEE_CAL_ERR_TIMEOUT);
}

static int write_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                    struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle; (void)attr; (void)arg;
    int st = error ? error->status : 0;
    if (st == 0) {
        cal_finish(GASBEE_CAL_OK);
    } else if (st > BLE_HS_ERR_ATT_BASE && st < BLE_HS_ERR_ATT_BASE + 0x100) {
        cal_finish(st - BLE_HS_ERR_ATT_BASE);   // ATT-Fehlercode der GasBee
    } else {
        cal_finish(GASBEE_CAL_ERR_BLE);
    }
    return 0;
}

static int disc_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                       const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (error->status == 0 && chr) {
        if (chr->uuid.u.type == BLE_UUID_TYPE_16 && ble_uuid_u16(&chr->uuid.u) == s_chr_uuid) {
            s_chr_handle = chr->val_handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        cal_finish(GASBEE_CAL_ERR_NO_SERVICE);
        return 0;
    }
    if (s_chr_handle == 0) {
        cal_finish(GASBEE_CAL_ERR_NO_SERVICE);
        return 0;
    }
    int rc = ble_gattc_write_flat(conn_handle, s_chr_handle, s_payload, s_payload_len, write_cb, NULL);
    if (rc != 0) cal_finish(GASBEE_CAL_ERR_BLE);
    return 0;
}

static int disc_svc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                       const struct ble_gatt_svc *svc, void *arg)
{
    (void)arg;
    if (error->status == 0 && svc) {
        if (!s_svc_found) {
            s_svc_found = true;
            int rc = ble_gattc_disc_all_chrs(conn_handle, svc->start_handle, svc->end_handle,
                                             disc_chr_cb, NULL);
            if (rc != 0) cal_finish(GASBEE_CAL_ERR_BLE);
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (!s_svc_found) cal_finish(GASBEE_CAL_ERR_NO_SERVICE);
        return 0;
    }
    cal_finish(GASBEE_CAL_ERR_NO_SERVICE);
    return 0;
}

static int conn_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                taskENTER_CRITICAL(&s_lock);
                s_conn = event->connect.conn_handle;
                bool done = s_reported;
                taskEXIT_CRITICAL(&s_lock);
                if (done) {
                    // Timeout lief schon ab → gleich wieder trennen
                    ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    break;
                }
                ESP_LOGI(TAG, "Verbunden (handle=%d) → Service suchen", event->connect.conn_handle);
                int rc = ble_gattc_disc_svc_by_uuid(event->connect.conn_handle, &s_svc_uuid.u,
                                                    disc_svc_cb, NULL);
                if (rc != 0) cal_finish(GASBEE_CAL_ERR_BLE);
            } else {
                cal_finish(GASBEE_CAL_ERR_CONNECT);
                taskENTER_CRITICAL(&s_lock);
                s_busy = false;
                taskEXIT_CRITICAL(&s_lock);
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            cal_finish(GASBEE_CAL_ERR_DISCONNECT);   // ohne Wirkung, wenn schon gemeldet
            taskENTER_CRITICAL(&s_lock);
            s_conn = BLE_HS_CONN_HANDLE_NONE;
            s_busy = false;
            taskEXIT_CRITICAL(&s_lock);
            ESP_LOGI(TAG, "Getrennt (reason=%d)", event->disconnect.reason);
            break;

        default:
            break;
    }
    return 0;
}

void gasbee_gatt_cal_init(void)
{
    const esp_timer_create_args_t args = { .callback = timeout_cb, .name = "gasbee_cal_to" };
    if (esp_timer_create(&args, &s_timeout) != ESP_OK) {
        ESP_LOGE(TAG, "Timeout-Timer konnte nicht angelegt werden");
        s_timeout = NULL;
    }
    ble_manager_set_conn_handler(conn_cb, NULL);
}

int gasbee_gatt_cal_start(const char *mac, gasbee_cal_step_t step, uint32_t grams,
                          gasbee_cal_done_cb_t cb, void *arg)
{
    if (!mac || !s_timeout) return GASBEE_CAL_ERR_ARG;
    if (step == GASBEE_CAL_STEP_REF && (grams == 0 || grams > CAL_REF_MAX_G)) return GASBEE_CAL_ERR_ARG;

    gasbee_scan_entry_t e;
    if (!gasbee_ble_scanner_find(mac, &e)) return GASBEE_CAL_ERR_NOT_FOUND;
    int64_t age_us = esp_timer_get_time() - e.last_seen_us;
    if (!e.config_mode || age_us > (int64_t)SENSOR_GASBEE_CFG_MAX_AGE_S * 1000000LL) {
        return GASBEE_CAL_ERR_NOT_FOUND;
    }

    taskENTER_CRITICAL(&s_lock);
    if (s_busy) {
        taskEXIT_CRITICAL(&s_lock);
        return GASBEE_CAL_ERR_BUSY;
    }
    s_busy       = true;
    s_reported   = false;
    s_conn       = BLE_HS_CONN_HANDLE_NONE;
    s_step       = step;
    s_cb         = cb;
    s_cb_arg     = arg;
    s_chr_handle = 0;
    s_svc_found  = false;
    if (step == GASBEE_CAL_STEP_ZERO) {
        s_chr_uuid    = CHR_CAL_ZERO;
        s_payload[0]  = 0x01;
        s_payload_len = 1;
    } else {
        s_chr_uuid    = CHR_CAL_REF_G;
        s_payload[0]  = (uint8_t)(grams);
        s_payload[1]  = (uint8_t)(grams >> 8);
        s_payload[2]  = (uint8_t)(grams >> 16);
        s_payload[3]  = (uint8_t)(grams >> 24);
        s_payload_len = 4;
    }
    taskEXIT_CRITICAL(&s_lock);

    ble_addr_t addr = { .type = e.addr_type };
    memcpy(addr.val, e.mac, 6);

    ESP_LOGI(TAG, "Schritt %s für %s (%lu g) → verbinden",
             step == GASBEE_CAL_STEP_ZERO ? "Nullpunkt" : "Referenz", mac,
             (unsigned long)(step == GASBEE_CAL_STEP_REF ? grams : 0));

    esp_timer_start_once(s_timeout, (uint64_t)SENSOR_GASBEE_CAL_TIMEOUT_MS * 1000ULL);
    int rc = ble_manager_connect(&addr, SENSOR_GASBEE_CAL_TIMEOUT_MS);
    if (rc != 0) {
        esp_timer_stop(s_timeout);
        taskENTER_CRITICAL(&s_lock);
        s_busy = false;
        s_reported = true;
        taskEXIT_CRITICAL(&s_lock);
        return (rc == BLE_HS_EBUSY || rc == BLE_HS_EALREADY) ? GASBEE_CAL_ERR_BUSY : GASBEE_CAL_ERR_CONNECT;
    }
    return GASBEE_CAL_OK;
}

const char *gasbee_gatt_cal_result_str(int result)
{
    switch (result) {
        case GASBEE_CAL_OK:             return "ok";
        case 0x03:                      return "nicht im Konfigurationsmodus";
        case 0x0D:                      return "falsche Laenge";
        case 0x13:                      return "Wert ungueltig";
        case GASBEE_CAL_ERR_BUSY:       return "beschaeftigt";
        case GASBEE_CAL_ERR_NOT_FOUND:  return "nicht im Konfigurationsmodus gefunden";
        case GASBEE_CAL_ERR_CONNECT:    return "Verbindung fehlgeschlagen";
        case GASBEE_CAL_ERR_NO_SERVICE: return "Service nicht gefunden";
        case GASBEE_CAL_ERR_TIMEOUT:    return "Zeitueberschreitung";
        case GASBEE_CAL_ERR_DISCONNECT: return "Verbindung abgebrochen";
        case GASBEE_CAL_ERR_ARG:        return "ungueltige Eingabe";
        default:                        return "BLE-Fehler";
    }
}
