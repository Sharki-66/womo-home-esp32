/**
 * gasbee_ble_client.c
 *
 * NimBLE Central – verbindet mit GasBee (ESP32-C3):
 *  1. Scan nach Gerätename "GasBee"
 *  2. Verbinden + Service-/Characteristic-Discovery
 *  3. Notify-Subscription für Weight_kg / Gas_pct / Net_gas_kg
 *  4. Werte loggen + Snapshot aktualisieren
 *  5. Bei Trennung: automatisch neu scannen
 */

#include "gasbee_ble_client.h"

#include <string.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>

#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/ble_gap.h>
#include <host/util/util.h>

static const char *TAG = "gasbee_ble";

// ── UUIDs (müssen mit gasbee_config.h übereinstimmen) ──────────────────────

// Service: 4a5b6c7d-8e9f-0a1b-2c3d-4e5f60718293 (little-endian)
static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(
    0x93, 0x82, 0x71, 0x60, 0x5f, 0x4e, 0x3d, 0x2c,
    0x1b, 0x0a, 0x9f, 0x8e, 0x7d, 0x6c, 0x5b, 0x4a
);

#define CHR_WEIGHT_KG  0x2B01
#define CHR_GAS_PCT    0x2B02
#define CHR_NET_GAS_KG 0x2B03

// ── Characteristic-Tracking ─────────────────────────────────────────────────

typedef struct {
    uint16_t uuid16;
    uint16_t val_handle;  // 0 = noch nicht gefunden
} chr_slot_t;

static chr_slot_t s_chrs[3] = {
    { .uuid16 = CHR_WEIGHT_KG  },
    { .uuid16 = CHR_GAS_PCT    },
    { .uuid16 = CHR_NET_GAS_KG },
};
#define NUM_CHRS 3

// ── Zustand ──────────────────────────────────────────────────────────────────

static uint16_t          s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static gasbee_snapshot_t s_snapshot    = {0};
static SemaphoreHandle_t s_mutex       = NULL;
static TimerHandle_t     s_reconnect_timer = NULL;

// ── Vorwärts-Deklarationen ────────────────────────────────────────────────────

static void start_scan(void);
static int  gap_event_cb(struct ble_gap_event *event, void *arg);

// ── Reconnect-Timer (aus GAP-Callback sicher aufrufbar) ──────────────────────

static void reconnect_timer_cb(TimerHandle_t t)
{
    (void)t;
    start_scan();
}

// ── CCCD-Subscription ────────────────────────────────────────────────────────
// NimBLE-Server legt CCCD direkt nach dem Value-Attribut an → val_handle + 1.

static void subscribe_all_chrs(void)
{
    const uint16_t notify_en = 0x0001;
    for (int i = 0; i < NUM_CHRS; i++) {
        if (s_chrs[i].val_handle == 0) continue;
        uint16_t cccd = s_chrs[i].val_handle + 1;
        int rc = ble_gattc_write_flat(s_conn_handle, cccd,
                                      &notify_en, sizeof(notify_en),
                                      NULL, NULL);
        if (rc == 0) {
            ESP_LOGI(TAG, "Notify aktiviert: CHR 0x%04X (CCCD handle=%d)",
                     s_chrs[i].uuid16, cccd);
        } else {
            ESP_LOGW(TAG, "CCCD-Write fehlgeschlagen: CHR 0x%04X rc=%d",
                     s_chrs[i].uuid16, rc);
        }
    }
}

// ── GATT Discovery-Callbacks ─────────────────────────────────────────────────

static int disc_chr_cb(uint16_t conn_handle,
                       const struct ble_gatt_error *error,
                       const struct ble_gatt_chr *chr, void *arg)
{
    if (error->status == BLE_HS_EDONE) {
        // Alle Characteristics entdeckt → subscriben
        ESP_LOGI(TAG, "Chr-Discovery abgeschlossen");
        subscribe_all_chrs();
        return 0;
    }
    if (error->status != 0 || chr == NULL) return 0;

    uint16_t u16 = ble_uuid_u16(&chr->uuid.u);
    for (int i = 0; i < NUM_CHRS; i++) {
        if (s_chrs[i].uuid16 == u16) {
            s_chrs[i].val_handle = chr->val_handle;
            ESP_LOGI(TAG, "CHR 0x%04X: val_handle=%d", u16, chr->val_handle);
            break;
        }
    }
    return 0;
}

static int disc_svc_cb(uint16_t conn_handle,
                       const struct ble_gatt_error *error,
                       const struct ble_gatt_svc *svc, void *arg)
{
    if (error->status == BLE_HS_EDONE) return 0;
    if (error->status != 0 || svc == NULL) {
        ESP_LOGE(TAG, "SVC-Discovery Fehler: %d", error->status);
        return 0;
    }
    ESP_LOGI(TAG, "GasBee Service: start=%d end=%d",
             svc->start_handle, svc->end_handle);
    ble_gattc_disc_all_chrs(conn_handle,
                             svc->start_handle, svc->end_handle,
                             disc_chr_cb, NULL);
    return 0;
}

// ── Notify-Handler ───────────────────────────────────────────────────────────

static void handle_notify(uint16_t attr_handle,
                           const uint8_t *data, uint16_t len)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;

    for (int i = 0; i < NUM_CHRS; i++) {
        if (s_chrs[i].val_handle != attr_handle) continue;

        switch (s_chrs[i].uuid16) {
            case CHR_WEIGHT_KG:
                if (len >= 4) {
                    memcpy(&s_snapshot.weight_kg, data, 4);
                    ESP_LOGI(TAG, "[GasBee] Gewicht   : %.3f kg",
                             s_snapshot.weight_kg);
                }
                break;
            case CHR_GAS_PCT:
                if (len >= 1) {
                    s_snapshot.gas_pct = data[0];
                    ESP_LOGI(TAG, "[GasBee] Füllstand : %u %%",
                             s_snapshot.gas_pct);
                }
                break;
            case CHR_NET_GAS_KG:
                if (len >= 4) {
                    memcpy(&s_snapshot.net_gas_kg, data, 4);
                    s_snapshot.valid        = true;
                    s_snapshot.timestamp_us = esp_timer_get_time();
                    ESP_LOGI(TAG, "[GasBee] Gas-Netto : %.3f kg",
                             s_snapshot.net_gas_kg);
                }
                break;
        }
        break;
    }
    xSemaphoreGive(s_mutex);
}

// ── GAP Event Handler ────────────────────────────────────────────────────────

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    struct ble_hs_adv_fields fields;

    switch (event->type) {

        case BLE_GAP_EVENT_DISC: {
            if (ble_hs_adv_parse_fields(&fields, event->disc.data,
                                         event->disc.length_data) != 0) break;
            if (fields.name == NULL) break;
            if (fields.name_len != 6) break; // strlen("GasBee")
            if (memcmp(fields.name, "GasBee", 6) != 0) break;

            ESP_LOGI(TAG, "GasBee gefunden (%02X:%02X:%02X:%02X:%02X:%02X), verbinde...",
                     event->disc.addr.val[5], event->disc.addr.val[4],
                     event->disc.addr.val[3], event->disc.addr.val[2],
                     event->disc.addr.val[1], event->disc.addr.val[0]);
            ble_gap_disc_cancel();
            ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &event->disc.addr,
                            30000, NULL, gap_event_cb, NULL);
            break;
        }

        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "Verbunden (handle=%d) → Discovery...",
                         s_conn_handle);
                // Chr-Handles zurücksetzen
                for (int i = 0; i < NUM_CHRS; i++) s_chrs[i].val_handle = 0;
                ble_gattc_disc_svc_by_uuid(s_conn_handle,
                                           &s_svc_uuid.u,
                                           disc_svc_cb, NULL);
            } else {
                ESP_LOGW(TAG, "Verbindung fehlgeschlagen (status=%d)",
                         event->connect.status);
                s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                xTimerStart(s_reconnect_timer, 0);
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGW(TAG, "GasBee getrennt (reason=%d)", event->disconnect.reason);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            if (xSemaphoreTake(s_mutex, 0) == pdTRUE) {
                s_snapshot.valid = false;
                xSemaphoreGive(s_mutex);
            }
            xTimerStart(s_reconnect_timer, 0);
            break;

        case BLE_GAP_EVENT_NOTIFY_RX:
            handle_notify(event->notify_rx.attr_handle,
                          OS_MBUF_DATA(event->notify_rx.om, const uint8_t *),
                          (uint16_t)OS_MBUF_PKTLEN(event->notify_rx.om));
            break;

        default:
            break;
    }
    return 0;
}

// ── Scan ─────────────────────────────────────────────────────────────────────

static void start_scan(void)
{
    struct ble_gap_disc_params p = {
        .passive           = 0,
        .filter_duplicates = 1,
        .itvl              = 0x0050,  // ~50 ms
        .window            = 0x0030,  // ~30 ms
    };
    ESP_LOGI(TAG, "Suche nach GasBee...");
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &p,
                          gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Scan-Start fehlgeschlagen: rc=%d", rc);
        xTimerStart(s_reconnect_timer, 0);
    }
}

// ── NimBLE Host ───────────────────────────────────────────────────────────────

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    start_scan();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE Reset: reason=%d", reason);
}

static void nimble_host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ── Public API ────────────────────────────────────────────────────────────────

esp_err_t gasbee_ble_client_start(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    // Reconnect-Timer: 3 s, one-shot
    s_reconnect_timer = xTimerCreate("ble_rc", pdMS_TO_TICKS(3000),
                                      pdFALSE, NULL, reconnect_timer_cb);
    if (!s_reconnect_timer) return ESP_ERR_NO_MEM;

    nimble_port_init();
    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "GasBee BLE Client gestartet");
    return ESP_OK;
}

void gasbee_ble_client_get_snapshot(gasbee_snapshot_t *out)
{
    if (!out) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        *out = s_snapshot;
        xSemaphoreGive(s_mutex);
    } else {
        memset(out, 0, sizeof(*out));
    }
}
