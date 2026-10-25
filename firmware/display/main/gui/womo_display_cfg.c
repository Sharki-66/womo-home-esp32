/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * womo_display_cfg.c – Einstellungen der Hintergrundbeleuchtung
 * Die Logik (Timer, Ruhezeit, Aufwecken) liegt in main.c.
 */
#include "womo_display_cfg.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "disp_cfg";
#define NVS_NS  "womo_disp"

static womo_disp_cfg_t s_cfg = {
    .auto_off_min = 0,                  /* Standard: bisheriges Verhalten */
    .wake         = WOMO_DISP_WAKE_ALL,
};
static womo_disp_cfg_change_cb_t s_change_cb = NULL;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

void womo_disp_cfg_init(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "Kein NVS-Eintrag – Standardwerte werden verwendet");
        return;
    }
    uint8_t v8;
    if (nvs_get_u8(h, "auto_off", &v8) == ESP_OK && v8 <= WOMO_DISP_AUTO_OFF_MAX_MIN) s_cfg.auto_off_min = v8;
    if (nvs_get_u8(h, "wake",     &v8) == ESP_OK && v8 < WOMO_DISP_WAKE_COUNT)        s_cfg.wake = v8;
    nvs_close(h);
    ESP_LOGI(TAG, "Display aus NVS: Auto-Aus %u min, Aufwecken %u", s_cfg.auto_off_min, s_cfg.wake);
}

void womo_disp_cfg_get(womo_disp_cfg_t *out)
{
    if (!out) return;
    taskENTER_CRITICAL(&s_lock);
    *out = s_cfg;
    taskEXIT_CRITICAL(&s_lock);
}

void womo_disp_cfg_set(const womo_disp_cfg_t *in)
{
    if (!in || in->auto_off_min > WOMO_DISP_AUTO_OFF_MAX_MIN || in->wake >= WOMO_DISP_WAKE_COUNT) return;
    taskENTER_CRITICAL(&s_lock);
    s_cfg = *in;
    taskEXIT_CRITICAL(&s_lock);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "auto_off", in->auto_off_min);
        nvs_set_u8(h, "wake",     in->wake);
        nvs_commit(h);
        nvs_close(h);
    } else {
        ESP_LOGW(TAG, "NVS nicht beschreibbar – Einstellung gilt nur bis zum Neustart");
    }
    if (s_change_cb) s_change_cb();
}

void womo_disp_cfg_register_change_cb(womo_disp_cfg_change_cb_t cb)
{
    s_change_cb = cb;
}
