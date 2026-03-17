#ifndef WOMO_CONNECTIVITY_MODAL_H
#define WOMO_CONNECTIVITY_MODAL_H

#include "lvgl.h"
#include "network/womo_wifi.h"
#include "network/womo_router_uci.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* AP / HotSpot */
    bool ap_enabled;
    char ap_ssid[33];
    uint8_t ap_clients;
    womo_ap_client_info_t ap_client_list[WOMO_AP_CLIENT_MAX];
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
