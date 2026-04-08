/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * womo_thresholds.c – Grenzwert-Speicher mit Change-Callbacks und NVS-Persistenz
 */
#include "womo_thresholds.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "thresholds";
#define NVS_NS  "womo_thr"

static womo_thresholds_t s_thresh = {
    .gas_warn   = THRESH_GAS_WARN_DEFAULT,
    .gas_crit   = THRESH_GAS_CRIT_DEFAULT,
    .fresh_warn = THRESH_FRESH_WARN_DEFAULT,
    .fresh_crit = THRESH_FRESH_CRIT_DEFAULT,
    .grey_warn  = THRESH_GREY_WARN_DEFAULT,
    .grey_crit  = THRESH_GREY_CRIT_DEFAULT,
};

#define MAX_CBS 4
static void (*s_cbs[MAX_CBS])(void);
static int s_cb_cnt = 0;

void womo_thresholds_init(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Kein NVS-Eintrag – Standardwerte werden verwendet");
        return;
    }
    uint8_t v;
    if (nvs_get_u8(h, "gas_warn",   &v) == ESP_OK) s_thresh.gas_warn   = v;
    if (nvs_get_u8(h, "gas_crit",   &v) == ESP_OK) s_thresh.gas_crit   = v;
    if (nvs_get_u8(h, "frsh_warn",  &v) == ESP_OK) s_thresh.fresh_warn = v;
    if (nvs_get_u8(h, "frsh_crit",  &v) == ESP_OK) s_thresh.fresh_crit = v;
    if (nvs_get_u8(h, "grey_warn",  &v) == ESP_OK) s_thresh.grey_warn  = v;
    if (nvs_get_u8(h, "grey_crit",  &v) == ESP_OK) s_thresh.grey_crit  = v;
    nvs_close(h);
    ESP_LOGI(TAG, "Grenzwerte aus NVS: gas %u/%u  frisch %u/%u  grau %u/%u",
             s_thresh.gas_warn, s_thresh.gas_crit,
             s_thresh.fresh_warn, s_thresh.fresh_crit,
             s_thresh.grey_warn, s_thresh.grey_crit);
}

void womo_thresholds_get(womo_thresholds_t *out)
{
    if (out) *out = s_thresh;
}

void womo_thresholds_set(const womo_thresholds_t *in)
{
    if (!in) return;
    s_thresh = *in;

    /* Im NVS sichern */
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "gas_warn",  s_thresh.gas_warn);
        nvs_set_u8(h, "gas_crit",  s_thresh.gas_crit);
        nvs_set_u8(h, "frsh_warn", s_thresh.fresh_warn);
        nvs_set_u8(h, "frsh_crit", s_thresh.fresh_crit);
        nvs_set_u8(h, "grey_warn", s_thresh.grey_warn);
        nvs_set_u8(h, "grey_crit", s_thresh.grey_crit);
        nvs_commit(h);
        nvs_close(h);
    }

    for (int i = 0; i < s_cb_cnt; i++) {
        if (s_cbs[i]) s_cbs[i]();
    }
}

void womo_thresholds_register_change_cb(void (*cb)(void))
{
    if (cb && s_cb_cnt < MAX_CBS) {
        s_cbs[s_cb_cnt++] = cb;
    }
}
