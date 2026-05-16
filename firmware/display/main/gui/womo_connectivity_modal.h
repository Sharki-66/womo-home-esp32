/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef WOMO_CONNECTIVITY_MODAL_H
#define WOMO_CONNECTIVITY_MODAL_H

#include "lvgl.h"
#include "network/womo_wifi.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* ESP32 eigenes WiFi (Hotspot-Spalte) */
    bool esp_wifi_connected;
    char esp_wifi_ssid[33];
    int8_t esp_wifi_rssi;
    uint8_t esp_wifi_signal_percent;
    /* Router erreichbar (steuert WLAN/LTE-Spalten) */
    bool router_reachable;
    /* WLAN (Router STA) */
    bool wifi_enabled;            /**< WiFi-Radio aktiv (UCI disabled=0) */
    bool wifi_connected;
    womo_wifi_status_t wifi_status;
    uint8_t wifi_signal_percent;
    char wifi_ssid[33];
    /* LTE */
    bool lte_valid;
    bool lte_registered;
    uint8_t lte_signal_percent;
    char lte_operator[32];
} womo_connectivity_snapshot_t;

void womo_connectivity_modal_show(lv_obj_t *parent, const womo_connectivity_snapshot_t *snapshot);
void womo_connectivity_modal_refresh(const womo_connectivity_snapshot_t *snapshot);
bool womo_connectivity_modal_is_open(void);

#endif // WOMO_CONNECTIVITY_MODAL_H
