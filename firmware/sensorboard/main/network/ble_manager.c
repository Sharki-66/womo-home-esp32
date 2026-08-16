/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * ble_manager.c
 *
 * Zentraler NimBLE-Stack-Eigentümer:
 *  - Initialisiert NimBLE einmalig
 *  - Verwaltet BLE-Scan (filter_duplicates=0 für periodische Ruuvi-Updates)
 *  - Dispatcht Advertisement-Events an alle registrierten Handler
 *  - Leitet Connection-Events an den registrierten Connection-Handler weiter
 *  - Startet Scan nach Connect und Disconnect automatisch neu
 */

#include "ble_manager.h"

#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <host/ble_hs.h>
#include <host/util/util.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <esp_log.h>

static const char *TAG = "ble_manager";

#define MAX_ADV_HANDLERS 4

static ble_adv_handler_t        s_adv_handlers[MAX_ADV_HANDLERS];
static int                      s_adv_handler_count = 0;
static ble_conn_event_handler_t s_conn_handler      = NULL;
static void                    *s_conn_handler_arg  = NULL;
static TimerHandle_t            s_scan_retry_timer  = NULL;

// ── Vorwärts-Deklaration ──────────────────────────────────────────────────

static int gap_cb(struct ble_gap_event *event, void *arg);

// ── Öffentliche API ───────────────────────────────────────────────────────

void ble_manager_register_adv_handler(ble_adv_handler_t fn)
{
    if (fn && s_adv_handler_count < MAX_ADV_HANDLERS) {
        s_adv_handlers[s_adv_handler_count++] = fn;
    }
}

void ble_manager_set_conn_handler(ble_conn_event_handler_t fn, void *arg)
{
    s_conn_handler     = fn;
    s_conn_handler_arg = arg;
}

void ble_manager_start_scan(void)
{
    struct ble_gap_disc_params p = {
        .passive           = 0,
        // 0 = Wiederholungen erlaubt → Ruuvi-Tags senden periodisch neue Messwerte
        .filter_duplicates = 0,
        .itvl              = 0x0050,  // ~50 ms
        .window            = 0x0030,  // ~30 ms
    };
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &p, gap_cb, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "Scan gestartet");
    } else if (rc == BLE_HS_EALREADY) {
        ESP_LOGD(TAG, "Scan läuft bereits");
    } else {
        ESP_LOGW(TAG, "Scan-Start fehlgeschlagen (rc=%d) → Retry in 3s", rc);
        xTimerStart(s_scan_retry_timer, 0);
    }
}

void ble_manager_connect(const ble_addr_t *addr)
{
    ble_gap_disc_cancel();
    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, addr, 30000, NULL, gap_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Connect fehlgeschlagen (rc=%d) → Scan neu starten", rc);
        xTimerStart(s_scan_retry_timer, 0);
    }
}

// ── Interner GAP-Callback (Scan + Connection) ─────────────────────────────

static int gap_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {

        case BLE_GAP_EVENT_DISC:
            for (int i = 0; i < s_adv_handler_count; i++) {
                s_adv_handlers[i](&event->disc);
            }
            break;

        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "Verbunden (handle=%d) → Scan für passive Geräte neu starten",
                         event->connect.conn_handle);
                // Scan neu starten damit Ruuvi-Advertisements weiter empfangen werden
                ble_manager_start_scan();
            } else {
                ESP_LOGW(TAG, "Verbindungsaufbau fehlgeschlagen (status=%d)",
                         event->connect.status);
                xTimerStart(s_scan_retry_timer, 0);
            }
            if (s_conn_handler) s_conn_handler(event, s_conn_handler_arg);
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Verbindung getrennt (reason=%d) → Scan neu starten",
                     event->disconnect.reason);
            if (s_conn_handler) s_conn_handler(event, s_conn_handler_arg);
            // Sicherstellen dass Scan läuft (könnte durch Connect unterbrochen worden sein)
            ble_manager_start_scan();
            break;

        case BLE_GAP_EVENT_NOTIFY_RX:
            if (s_conn_handler) s_conn_handler(event, s_conn_handler_arg);
            break;

        default:
            break;
    }
    return 0;
}

// ── NimBLE-Host ───────────────────────────────────────────────────────────

static void scan_retry_cb(TimerHandle_t t)
{
    (void)t;
    ble_manager_start_scan();
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_manager_start_scan();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE Reset: reason=%d", reason);
}

static void nimble_host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_manager_init(void)
{
    s_scan_retry_timer = xTimerCreate("ble_scan_retry", pdMS_TO_TICKS(3000),
                                       pdFALSE, NULL, scan_retry_cb);
    if (!s_scan_retry_timer) return ESP_ERR_NO_MEM;

    nimble_port_init();
    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "BLE Manager initialisiert");
    return ESP_OK;
}
