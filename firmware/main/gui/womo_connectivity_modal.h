#ifndef WOMO_CONNECTIVITY_MODAL_H
#define WOMO_CONNECTIVITY_MODAL_H

#include "lvgl.h"
#include "network/womo_wifi.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool wifi_connected;
    womo_wifi_status_t wifi_status;
    uint8_t wifi_signal_percent;
    char wifi_ssid[33];
    bool lte_valid;
    bool lte_registered;
    uint8_t lte_signal_percent;
    char lte_operator[32];
} womo_connectivity_snapshot_t;

void womo_connectivity_modal_show(lv_obj_t *parent, const womo_connectivity_snapshot_t *snapshot);
void womo_connectivity_modal_refresh(const womo_connectivity_snapshot_t *snapshot);
bool womo_connectivity_modal_is_open(void);

#endif // WOMO_CONNECTIVITY_MODAL_H
