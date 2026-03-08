/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 1: LVGL v9 + esp_lvgl_port v2
 * - Display-Setup (RGB 800×480, Bounce-Buffer, Avoid-Tearing) via esp_lvgl_port
 * - Touch: eigener Read-Callback für Wake-Callback + adaptives Sampling
 */

#include "freertos/FreeRTOS.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "esp_check.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_lvgl_port_disp.h"
#include "lvgl_port.h"
#include "womo_theme.h"

static const char *TAG = "lv_port";

/* ── Touch-Sampling-Perioden ─────────────────────────────────────── */
#define TOUCH_FAST_PERIOD_MS     4U
#define TOUCH_NORMAL_PERIOD_MS   12U
#define TOUCH_FAST_HOLD_MS       180U
#define TOUCH_RELEASE_FILTER_MS  40U

static uint32_t              touch_last_activity  = 0;
static uint32_t              touch_current_period = TOUCH_NORMAL_PERIOD_MS;
static bool                  touch_fast_forced    = false;
static lvgl_touch_wake_cb_t  s_touch_wake_cb      = NULL;
static lv_indev_t           *touch_indev          = NULL;

typedef struct {
    bool       is_pressed;
    lv_point_t last_point;
    uint32_t   last_sample_tick;
} touch_state_t;

static touch_state_t touch_state = {0};

/* ── Sampling-Rate setzen (LVGL v9: über lv_indev Read-Timer) ────── */
static void touch_set_read_period(uint32_t period_ms)
{
    if (!touch_indev || touch_current_period == period_ms) return;
    lv_timer_t *timer = lv_indev_get_read_timer(touch_indev);
    if (timer) lv_timer_set_period(timer, period_ms);
    touch_current_period = period_ms;
}

void lvgl_touch_set_fast_mode(bool enable)
{
    touch_fast_forced = enable;
    if (enable) {
        touch_last_activity = lv_tick_get();
        touch_set_read_period(TOUCH_FAST_PERIOD_MS);
    } else if (!touch_state.is_pressed) {
        touch_set_read_period(TOUCH_NORMAL_PERIOD_MS);
    }
}

void lvgl_touch_set_wake_cb(lvgl_touch_wake_cb_t cb)
{
    s_touch_wake_cb = cb;
}

/* ── Touch Read-Callback (LVGL v9 Signatur) ─────────────────────── */
static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    assert(tp);

    esp_lcd_touch_point_data_t point = {0};
    uint8_t  touchpad_cnt = 0;
    uint32_t now = lv_tick_get();

    esp_lcd_touch_read_data(tp);
    esp_err_t ret = esp_lcd_touch_get_data(tp, &point, &touchpad_cnt, 1);

    if (ret == ESP_OK && touchpad_cnt > 0) {
        touch_state.is_pressed        = true;
        touch_state.last_point.x      = point.x;
        touch_state.last_point.y      = point.y;
        touch_state.last_sample_tick  = now;
        data->point.x = point.x;
        data->point.y = point.y;
        data->state   = LV_INDEV_STATE_PRESSED;
        ESP_LOGD(TAG, "Touch: %d,%d", point.x, point.y);
        touch_last_activity = now;
        touch_set_read_period(TOUCH_FAST_PERIOD_MS);

        /* Wake-Callback: gibt true zurück → Touch unterdrücken */
        if (s_touch_wake_cb && s_touch_wake_cb()) {
            data->state            = LV_INDEV_STATE_RELEASED;
            touch_state.is_pressed = false;
        }
    } else {
        /* Release-Filter: kurze Flacker-Unterdrückung */
        if (touch_state.is_pressed &&
            lv_tick_elaps(touch_state.last_sample_tick) < TOUCH_RELEASE_FILTER_MS) {
            data->point = touch_state.last_point;
            data->state = LV_INDEV_STATE_PRESSED;
            return;
        }
        touch_state.is_pressed = false;
        data->state            = LV_INDEV_STATE_RELEASED;
        if (!touch_fast_forced && touch_last_activity != 0 &&
            lv_tick_elaps(touch_last_activity) > TOUCH_FAST_HOLD_MS) {
            touch_set_read_period(TOUCH_NORMAL_PERIOD_MS);
        }
    }
}

/* ── Haupt-Init ──────────────────────────────────────────────────── */
esp_err_t womo_lvgl_port_init(esp_lcd_panel_handle_t lcd_handle,
                              esp_lcd_touch_handle_t  tp_handle)
{
    /* 1. esp_lvgl_port initialisieren (LVGL-Task, Mutex, Tick) */
    const lvgl_port_cfg_t port_cfg = {
        .task_priority     = LVGL_PORT_TASK_PRIORITY,
        .task_stack        = LVGL_PORT_TASK_STACK_SIZE,
        .task_affinity     = LVGL_PORT_TASK_CORE,
        .task_max_sleep_ms = LVGL_PORT_TASK_MAX_DELAY_MS,
        .timer_period_ms   = LVGL_PORT_TICK_PERIOD_MS,
        /* WICHTIG: Stack muss im internen RAM liegen!
         * SPIRAM-Stack + spi_flash-Operationen (NVS, SPIFFS) = Assertion-Crash,
         * da der Cache beim Flash-Zugriff deaktiviert wird und SPIRAM
         * dann nicht mehr erreichbar ist. */
        .task_stack_caps   = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "esp_lvgl_port init failed");

    /* 2. RGB-Display hinzufügen: Bounce-Buffer + Avoid-Tearing (Mode 3) */
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = NULL,
        .panel_handle  = lcd_handle,
        .buffer_size   = LVGL_PORT_H_RES * LVGL_PORT_V_RES,
        .double_buffer = true,
        .hres          = LVGL_PORT_H_RES,
        .vres          = LVGL_PORT_V_RES,
        .monochrome    = false,
        .flags = {
            .buff_spiram  = true,
            .direct_mode  = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode       = true,   /* Bounce-Buffer-Modus (on_bounce_frame_finish) */
            .avoid_tearing = true,   /* Double-Buffer, Direct-Mode, VSync-Sync */
        },
    };
    lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    ESP_RETURN_ON_FALSE(disp, ESP_ERR_NO_MEM, TAG, "lvgl_port_add_disp_rgb failed");

    /* 3. Default-Screen sofort mit Theme-Hintergrundfarbe füllen
     *    Verhindert weißen Blitz beim ersten lv_timer_handler()-Aufruf */
    if (lvgl_port_lock(0)) {
        lv_obj_t *scr = lv_screen_active();
        lv_color_t bg = womo_theme_get_background_color();
        lv_obj_set_style_bg_color(scr, bg, 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lvgl_port_unlock();
    }

    /* 4. Touch-Indev registrieren (eigener Callback für Wake + Fast-Mode) */
    if (tp_handle) {
        lv_indev_t *indev = lv_indev_create();
        ESP_RETURN_ON_FALSE(indev, ESP_ERR_NO_MEM, TAG, "lv_indev_create failed");
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touchpad_read);
        lv_indev_set_user_data(indev, tp_handle);
        lv_indev_set_display(indev, disp);

        touch_indev          = indev;
        touch_current_period = TOUCH_NORMAL_PERIOD_MS;
        /* Start-Sampling-Rate setzen (Timer ist nach lv_indev_create aktiv) */
        touch_set_read_period(TOUCH_NORMAL_PERIOD_MS);

        ESP_LOGI(TAG, "Touch indev registered");
    }

    ESP_LOGI(TAG, "womo_lvgl_port_init done (LVGL v%d.%d.%d + esp_lvgl_port v2)",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    return ESP_OK;
}
