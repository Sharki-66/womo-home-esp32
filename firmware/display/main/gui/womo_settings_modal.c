/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * womo_settings_modal.c – Einstellungs-Modal mit 5 Tabs
 *
 * Tabs: Sprache | RTC | Grenzwerte | System | AP
 * Größe: 640×420 px, Header 48 px, Tabview 372 px (Tabbar 38 px, Inhalt 334 px).
 */

#include "womo_settings_modal.h"
#include "womo_locale.h"
#include "womo_thresholds.h"
#include "womo_fonts_german.h"
#include "../time/womo_time.h"
#include "../sensorboard/womo_sensorboard.h"
#include "../hardware/buzzer.h"
#include "../hardware/lvgl_port.h"
#include "../network/womo_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "settings_modal";

/* ── Dimensionen ────────────────────────────────────────────── */
#define MODAL_W      640
#define MODAL_H      420
#define HDR_H         48
#define TAB_BAR_H     38
#define PAD           16
#define NVS_AP_NS    "womo_ap_cfg"
#define MAX_AP_SCAN   16
#define AP_TASK_STACK 4096
#define AP_TASK_PRIO  5

/* ── Statische Objekte ──────────────────────────────────────── */
static lv_obj_t *s_overlay  = NULL;
static lv_obj_t *s_panel    = NULL;

/* Grenzwert-Arbeitskopie */
static womo_thresholds_t s_te;
static lv_obj_t *s_title_lbl     = NULL;
static lv_obj_t *s_thr_title_lbl = NULL;
static lv_obj_t *s_warn_hdr_lbl  = NULL;
static lv_obj_t *s_row_lbl[3]    = {NULL, NULL, NULL};
static bool      s_locale_cb_reg  = false;

/* Ton-Buttons */
static lv_obj_t *s_btn_tones = NULL;
static lv_obj_t *s_btn_touch = NULL;

/* AP-Tab */
static lv_obj_t *s_ap_keyboard        = NULL;
static lv_obj_t *s_ap_saved_lbl       = NULL;  /* Gespeicherte SSID anzeigen */
static lv_obj_t *s_ap_scan_btn        = NULL;
static lv_obj_t *s_ap_scan_spinner    = NULL;
static lv_obj_t *s_ap_scan_status_lbl = NULL;
static lv_obj_t *s_ap_password_lbl    = NULL;
static lv_obj_t *s_ap_password_area   = NULL;
static lv_obj_t *s_ap_save_btn        = NULL;
static lv_obj_t *s_ap_scan_popup      = NULL;
static lv_obj_t *s_ap_scan_list       = NULL;
static bool      s_ap_scan_in_progress = false;
static char      s_ap_selected_ssid[33];
static womo_wifi_scan_result_t s_ap_scan_results[MAX_AP_SCAN];
static size_t    s_ap_scan_result_count = 0;
static TaskHandle_t s_ap_connect_task  = NULL;

/* ── thresh_ud_t ────────────────────────────────────────────── */
typedef struct {
    uint8_t *val;
    uint8_t *partner;
    bool     val_gt_partner;
    uint8_t  abs_min;
    uint8_t  abs_max;
    int8_t   step;
    lv_obj_t *lbl;
} thresh_ud_t;

static thresh_ud_t s_tud[12];
static int         s_tud_cnt = 0;

/* ── QWERTZ-Tastaturmap (identisch mit connectivity_modal) ─── */
#define WOMO_KB_BTN(w) (LV_BTNMATRIX_CTRL_POPOVER | (w))

static const char *s_keyboard_map_lc[] = {
    "1#", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "y", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};
static const lv_btnmatrix_ctrl_t s_keyboard_ctrl_lc[] = {
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5,
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    LV_BTNMATRIX_CTRL_CHECKED | 7,
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6,
    WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3),
    WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3),
    LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1),
    WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1),
    WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1),
    LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1),
    LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1),
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6,
    LV_BTNMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2
};
static const char *s_keyboard_map_uc[] = {
    "1#", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "Y", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};
static const lv_btnmatrix_ctrl_t s_keyboard_ctrl_uc[] = {
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5,
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    LV_BTNMATRIX_CTRL_CHECKED | 7,
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6,
    WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3),
    WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3),
    LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1),
    WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1),
    WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1),
    LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1),
    LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1),
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6,
    LV_BTNMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2
};
#undef WOMO_KB_BTN

/* ── NVS-Helfer ─────────────────────────────────────────────── */

esp_err_t womo_ap_cfg_load(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz)
{
    if (!ssid || !pass) return ESP_ERR_INVALID_ARG;
    ssid[0] = '\0';
    pass[0] = '\0';

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_AP_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    err = nvs_get_str(h, "ssid", ssid, &ssid_sz);
    if (err == ESP_OK) {
        err = nvs_get_str(h, "pass", pass, &pass_sz);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            pass[0] = '\0';
            err = ESP_OK;  /* Passwort optional */
        }
    }
    nvs_close(h);
    return err;
}

static esp_err_t ap_cfg_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_AP_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, "ssid", ssid ? ssid : "");
    nvs_set_str(h, "pass", pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

/* ── Locale-Callback ────────────────────────────────────────── */

static void settings_locale_cb(void)
{
    if (!s_panel) return;
    if (s_title_lbl)     lv_label_set_text(s_title_lbl,     womo_locale_get_string(STR_SETTINGS_TITLE));
    if (s_thr_title_lbl) lv_label_set_text(s_thr_title_lbl, womo_locale_get_string(STR_THRESH_TITLE));
    if (s_warn_hdr_lbl)  lv_label_set_text(s_warn_hdr_lbl,  womo_locale_get_string(STR_THRESH_WARNING));
    if (s_row_lbl[0])    lv_label_set_text(s_row_lbl[0],    womo_locale_get_string(STR_THRESH_GAS));
    if (s_row_lbl[1])    lv_label_set_text(s_row_lbl[1],    womo_locale_get_string(STR_THRESH_FRESH));
    if (s_row_lbl[2])    lv_label_set_text(s_row_lbl[2],    womo_locale_get_string(STR_THRESH_GREY));
}

/* ── Grenzwert-Callback ─────────────────────────────────────── */

static void thresh_btn_cb(lv_event_t *e)
{
    thresh_ud_t *ud = (thresh_ud_t *)lv_event_get_user_data(e);
    if (!ud || !ud->val) return;

    int v = (int)*ud->val + (int)ud->step;
    if (v < (int)ud->abs_min) v = (int)ud->abs_min;
    if (v > (int)ud->abs_max) v = (int)ud->abs_max;
    if (ud->partner) {
        if (ud->val_gt_partner) {
            int lo = (int)*ud->partner + 5;
            if (v < lo) v = lo;
        } else {
            int hi = (int)*ud->partner - 5;
            if (v > hi) v = hi;
        }
    }
    if (v < (int)ud->abs_min) v = (int)ud->abs_min;
    if (v > (int)ud->abs_max) v = (int)ud->abs_max;

    *ud->val = (uint8_t)v;
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)v);
    lv_label_set_text(ud->lbl, buf);
    womo_thresholds_set(&s_te);
}

static void make_thresh_row(lv_obj_t *parent, int y,
                            const char *name,
                            uint8_t *warn_val, uint8_t *crit_val,
                            bool high_is_bad,
                            lv_obj_t **out_nl)
{
    enum { xNAME=16, xWM=210, xWV=238, xWP=282,
                     xCM=390, xCV=418, xCP=462 };

    lv_obj_t *nl = lv_label_create(parent);
    lv_label_set_text(nl, name);
    lv_obj_set_style_text_font(nl, &lv_font_montserrat_14_german, 0);
    lv_obj_set_style_text_color(nl, lv_color_hex(0x222222), 0);
    lv_obj_set_pos(nl, xNAME, y + 6);

    lv_obj_t *wm = lv_btn_create(parent);
    lv_obj_set_size(wm, 26, 26); lv_obj_set_pos(wm, xWM, y);
    lv_obj_set_style_bg_color(wm, lv_color_hex(0x757575), 0);
    lv_obj_set_style_radius(wm, 4, 0); lv_obj_set_style_border_width(wm, 0, 0);
    lv_obj_set_style_pad_all(wm, 0, 0);
    lv_obj_t *wml = lv_label_create(wm); lv_label_set_text(wml, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(wml, lv_color_white(), 0); lv_obj_center(wml);

    lv_obj_t *wvl = lv_label_create(parent);
    { char b[8]; snprintf(b, sizeof(b), "%u%%", (unsigned)*warn_val);
      lv_label_set_text(wvl, b); }
    lv_obj_set_style_text_font(wvl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wvl, lv_color_hex(0xE65100), 0);
    lv_obj_set_size(wvl, 42, 26);
    lv_obj_set_style_text_align(wvl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(wvl, xWV, y + 2);

    lv_obj_t *wp = lv_btn_create(parent);
    lv_obj_set_size(wp, 26, 26); lv_obj_set_pos(wp, xWP, y);
    lv_obj_set_style_bg_color(wp, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_style_radius(wp, 4, 0); lv_obj_set_style_border_width(wp, 0, 0);
    lv_obj_set_style_pad_all(wp, 0, 0);
    lv_obj_t *wpl = lv_label_create(wp); lv_label_set_text(wpl, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(wpl, lv_color_white(), 0); lv_obj_center(wpl);

    lv_obj_t *cm = lv_btn_create(parent);
    lv_obj_set_size(cm, 26, 26); lv_obj_set_pos(cm, xCM, y);
    lv_obj_set_style_bg_color(cm, lv_color_hex(0x757575), 0);
    lv_obj_set_style_radius(cm, 4, 0); lv_obj_set_style_border_width(cm, 0, 0);
    lv_obj_set_style_pad_all(cm, 0, 0);
    lv_obj_t *cml = lv_label_create(cm); lv_label_set_text(cml, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(cml, lv_color_white(), 0); lv_obj_center(cml);

    lv_obj_t *cvl = lv_label_create(parent);
    { char b[8]; snprintf(b, sizeof(b), "%u%%", (unsigned)*crit_val);
      lv_label_set_text(cvl, b); }
    lv_obj_set_style_text_font(cvl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cvl, lv_color_hex(0xC62828), 0);
    lv_obj_set_size(cvl, 42, 26);
    lv_obj_set_style_text_align(cvl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(cvl, xCV, y + 2);

    lv_obj_t *cp = lv_btn_create(parent);
    lv_obj_set_size(cp, 26, 26); lv_obj_set_pos(cp, xCP, y);
    lv_obj_set_style_bg_color(cp, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_style_radius(cp, 4, 0); lv_obj_set_style_border_width(cp, 0, 0);
    lv_obj_set_style_pad_all(cp, 0, 0);
    lv_obj_t *cpl = lv_label_create(cp); lv_label_set_text(cpl, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(cpl, lv_color_white(), 0); lv_obj_center(cpl);

    thresh_ud_t *wm_ud = &s_tud[s_tud_cnt++];
    wm_ud->val = warn_val; wm_ud->partner = crit_val;
    wm_ud->val_gt_partner = !high_is_bad;
    wm_ud->abs_min = high_is_bad ? 50 : 5;
    wm_ud->abs_max = high_is_bad ? 90 : 70;
    wm_ud->step = -5; wm_ud->lbl = wvl;
    lv_obj_add_event_cb(wm, thresh_btn_cb, LV_EVENT_CLICKED, wm_ud);

    thresh_ud_t *wp_ud = &s_tud[s_tud_cnt++];
    *wp_ud = *wm_ud; wp_ud->step = +5;
    lv_obj_add_event_cb(wp, thresh_btn_cb, LV_EVENT_CLICKED, wp_ud);

    thresh_ud_t *cm_ud = &s_tud[s_tud_cnt++];
    cm_ud->val = crit_val; cm_ud->partner = warn_val;
    cm_ud->val_gt_partner = high_is_bad;
    cm_ud->abs_min = high_is_bad ? 55 : 5;
    cm_ud->abs_max = high_is_bad ? 95 : 65;
    cm_ud->step = -5; cm_ud->lbl = cvl;
    lv_obj_add_event_cb(cm, thresh_btn_cb, LV_EVENT_CLICKED, cm_ud);

    thresh_ud_t *cp_ud = &s_tud[s_tud_cnt++];
    *cp_ud = *cm_ud; cp_ud->step = +5;
    lv_obj_add_event_cb(cp, thresh_btn_cb, LV_EVENT_CLICKED, cp_ud);

    if (out_nl) *out_nl = nl;
}

/* ── Ton-Callbacks ──────────────────────────────────────────── */

static void sound_btn_update(lv_obj_t *btn, bool on)
{
    lv_obj_set_style_bg_color(btn, on ? lv_color_hex(0x2E7D32) : lv_color_hex(0x757575), 0);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) lv_label_set_text(lbl, on ? "EIN" : "AUS");
}

static void sound_tones_cb(lv_event_t *e)
{
    (void)e;
    if (!s_btn_tones) return;
    lv_obj_t *lbl = lv_obj_get_child(s_btn_tones, 0);
    bool new_state = !(lbl && lv_label_get_text(lbl)[0] == 'E');
    sound_btn_update(s_btn_tones, new_state);
    womo_sensorboard_send_buzzer(new_state);
    display_buzzer_set_enabled(new_state);
    ESP_LOGI(TAG, "Systemtöne → %s", new_state ? "EIN" : "AUS");
}

static void sound_touch_cb(lv_event_t *e)
{
    (void)e;
    if (!s_btn_touch) return;
    lv_obj_t *lbl = lv_obj_get_child(s_btn_touch, 0);
    bool new_state = !(lbl && lv_label_get_text(lbl)[0] == 'E');
    sound_btn_update(s_btn_touch, new_state);
    womo_sensorboard_send_touch_click(new_state);
    display_buzzer_set_click_enabled(new_state);
    ESP_LOGI(TAG, "Touch-Klick → %s", new_state ? "EIN" : "AUS");
}

/* ── Sprach-Callbacks ───────────────────────────────────────── */

static void lang_de_cb(lv_event_t *e)
{
    womo_locale_set(WOMO_LOCALE_DE);
    lv_obj_t *btn_de = lv_event_get_target(e);
    lv_obj_t *btn_en = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_set_style_bg_color(btn_de, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(btn_de, 0), lv_color_white(), 0);
    if (btn_en) {
        lv_obj_set_style_bg_color(btn_en, lv_color_hex(0xC0C0C0), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(btn_en, 0), lv_color_black(), 0);
    }
}

static void lang_en_cb(lv_event_t *e)
{
    womo_locale_set(WOMO_LOCALE_EN);
    lv_obj_t *btn_en = lv_event_get_target(e);
    lv_obj_t *btn_de = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_set_style_bg_color(btn_en, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(btn_en, 0), lv_color_white(), 0);
    if (btn_de) {
        lv_obj_set_style_bg_color(btn_de, lv_color_hex(0xC0C0C0), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(btn_de, 0), lv_color_black(), 0);
    }
}

/* ── Modal schließen ────────────────────────────────────────── */

static void close_modal(void)
{
    if (!s_overlay) return;

    /* Keyboard sauber entfernen */
    if (s_ap_keyboard) {
        lv_keyboard_set_textarea(s_ap_keyboard, NULL);
    }

    lv_obj_del_async(s_overlay);

    s_overlay          = NULL;
    s_panel            = NULL;
    s_title_lbl        = NULL;
    s_thr_title_lbl    = NULL;
    s_warn_hdr_lbl     = NULL;
    s_row_lbl[0]       = NULL;
    s_row_lbl[1]       = NULL;
    s_row_lbl[2]       = NULL;
    s_btn_tones        = NULL;
    s_btn_touch        = NULL;
    s_ap_keyboard      = NULL;
    s_ap_saved_lbl     = NULL;
    s_ap_scan_btn      = NULL;
    s_ap_scan_spinner  = NULL;
    s_ap_scan_status_lbl = NULL;
    s_ap_password_lbl  = NULL;
    s_ap_password_area = NULL;
    s_ap_save_btn      = NULL;
    s_ap_scan_popup    = NULL;
    s_ap_scan_list     = NULL;
    s_ap_scan_in_progress = false;
}

static void close_btn_cb(lv_event_t *e)
{
    (void)e;
    close_modal();
}

static void overlay_click_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    if (target == s_overlay) {
        close_modal();
    }
}

/* ── AP-Tab: Hilfsfunktionen ────────────────────────────────── */

static void ap_hide_keyboard(void)
{
    if (s_ap_keyboard) {
        lv_keyboard_set_textarea(s_ap_keyboard, NULL);
        lv_obj_add_flag(s_ap_keyboard, LV_OBJ_FLAG_HIDDEN);
        lvgl_touch_set_fast_mode(false);
    }
    if (s_panel) lv_obj_align(s_panel, LV_ALIGN_CENTER, 0, 0);
}

static void close_ap_scan_popup(void)
{
    if (s_ap_scan_popup) {
        lv_obj_del(s_ap_scan_popup);
        s_ap_scan_popup = NULL;
        s_ap_scan_list  = NULL;
    }
}

static void ap_set_status(const char *text)
{
    if (s_ap_scan_status_lbl)
        lv_label_set_text(s_ap_scan_status_lbl, text ? text : "");
}

static void ap_update_saved_label(void)
{
    if (!s_ap_saved_lbl) return;
    char ssid[33] = {0}, pass[65] = {0};
    if (womo_ap_cfg_load(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK && ssid[0]) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Gespeichert: %s", ssid);
        lv_label_set_text(s_ap_saved_lbl, buf);
        lv_obj_set_style_text_color(s_ap_saved_lbl, lv_color_hex(0x1565C0), 0);
    } else {
        lv_label_set_text(s_ap_saved_lbl, "Kein AP konfiguriert");
        lv_obj_set_style_text_color(s_ap_saved_lbl, lv_color_hex(0x888888), 0);
    }
}

/* ── AP-Tab: Scan-Popup ─────────────────────────────────────── */

static uint8_t ap_rssi_to_pct(int8_t rssi)
{
    if (rssi >= -50) return 100;
    if (rssi <= -100) return 0;
    return (uint8_t)((rssi + 100) * 2);
}

static void ap_scan_popup_close_cb(lv_event_t *e)
{
    (void)e;
    close_ap_scan_popup();
}

static void ap_scan_list_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t *btn = lv_event_get_target(e);
    uint32_t idx = lv_obj_get_index(btn);
    if (idx >= s_ap_scan_result_count) {
        close_ap_scan_popup();
        return;
    }

    strncpy(s_ap_selected_ssid,
            s_ap_scan_results[idx].ssid,
            sizeof(s_ap_selected_ssid) - 1);
    s_ap_selected_ssid[sizeof(s_ap_selected_ssid) - 1] = '\0';

    close_ap_scan_popup();

    /* Vorhandenes NVS-Passwort vorausfüllen */
    char pwd_buf[65] = "";
    womo_wifi_get_known_credentials(s_ap_selected_ssid, pwd_buf, sizeof(pwd_buf));
    /* Prüfe auch im womo_ap_cfg-Namespace */
    if (pwd_buf[0] == '\0') {
        char saved_ssid[33] = {0}, saved_pass[65] = {0};
        if (womo_ap_cfg_load(saved_ssid, sizeof(saved_ssid),
                             saved_pass, sizeof(saved_pass)) == ESP_OK &&
            strcmp(saved_ssid, s_ap_selected_ssid) == 0) {
            strncpy(pwd_buf, saved_pass, sizeof(pwd_buf) - 1);
        }
    }

    char status_buf[48];
    snprintf(status_buf, sizeof(status_buf), "Ausgewaehlt: %s", s_ap_selected_ssid);
    ap_set_status(status_buf);

    if (s_ap_password_lbl)
        lv_obj_clear_flag(s_ap_password_lbl, LV_OBJ_FLAG_HIDDEN);
    if (s_ap_password_area) {
        lv_obj_clear_flag(s_ap_password_area, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(s_ap_password_area, pwd_buf);
    }
    if (s_ap_save_btn)
        lv_obj_clear_flag(s_ap_save_btn, LV_OBJ_FLAG_HIDDEN);
}

static void show_ap_scan_popup(void)
{
    close_ap_scan_popup();
    if (s_ap_scan_result_count == 0 || !s_overlay) return;

    s_ap_scan_popup = lv_obj_create(s_overlay);
    lv_obj_set_size(s_ap_scan_popup, 420, 320);
    lv_obj_center(s_ap_scan_popup);
    lv_obj_set_style_bg_color(s_ap_scan_popup, lv_color_white(), 0);
    lv_obj_set_style_radius(s_ap_scan_popup, 8, 0);
    lv_obj_set_style_pad_all(s_ap_scan_popup, 12, 0);
    lv_obj_set_flex_flow(s_ap_scan_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_shadow_width(s_ap_scan_popup, 20, 0);
    lv_obj_set_style_shadow_opa(s_ap_scan_popup, LV_OPA_30, 0);
    lv_obj_move_foreground(s_ap_scan_popup);

    lv_obj_t *popup_hdr = lv_obj_create(s_ap_scan_popup);
    lv_obj_remove_style_all(popup_hdr);
    lv_obj_set_size(popup_hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(popup_hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(popup_hdr,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_bottom(popup_hdr, 8, 0);

    lv_obj_t *popup_title = lv_label_create(popup_hdr);
    lv_label_set_text(popup_title, "WLAN-Netzwerk auswahlen");
    lv_obj_set_style_text_font(popup_title, &lv_font_montserrat_16, 0);
    lv_obj_set_flex_grow(popup_title, 1);

    lv_obj_t *popup_close = lv_btn_create(popup_hdr);
    lv_obj_set_style_pad_all(popup_close, 6, 0);
    lv_obj_add_event_cb(popup_close, ap_scan_popup_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_lbl = lv_label_create(popup_close);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);

    s_ap_scan_list = lv_list_create(s_ap_scan_popup);
    lv_obj_set_width(s_ap_scan_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_ap_scan_list, 1);

    for (size_t i = 0; i < s_ap_scan_result_count; i++) {
        char ssid_buf[34];
        if (s_ap_scan_results[i].ssid[0]) {
            strncpy(ssid_buf, s_ap_scan_results[i].ssid, 33);
            ssid_buf[33] = '\0';
        } else {
            strcpy(ssid_buf, "<versteckt>");
        }
        int8_t rssi = s_ap_scan_results[i].rssi;
        uint8_t pct = ap_rssi_to_pct(rssi);
        char text[80];
        snprintf(text, sizeof(text), "%s  (%d/%u%%)", ssid_buf, (int)rssi, (unsigned)pct);
        lv_obj_t *btn = lv_list_add_btn(s_ap_scan_list, LV_SYMBOL_WIFI, text);
        lv_obj_add_event_cb(btn, ap_scan_list_btn_cb, LV_EVENT_CLICKED, NULL);
    }
}

/* ── AP-Tab: Scan-Callback (außerhalb LVGL-Task) ────────────── */

static void ap_scan_callback(const womo_wifi_scan_result_t *results,
                              size_t count, esp_err_t status, void *user_data)
{
    (void)user_data;

    size_t stored = 0;
    if (results && count > 0 && status == ESP_OK) {
        for (size_t i = 0; i < count && stored < MAX_AP_SCAN; i++) {
            if (results[i].ssid[0] == '\0') continue;
            s_ap_scan_results[stored++] = results[i];
        }
    }
    s_ap_scan_result_count = stored;

    if (!lvgl_port_lock(-1)) {
        s_ap_scan_in_progress = false;
        return;
    }

    s_ap_scan_in_progress = false;

    if (!s_overlay) {
        lvgl_port_unlock();
        return;
    }

    if (s_ap_scan_spinner) lv_obj_add_flag(s_ap_scan_spinner, LV_OBJ_FLAG_HIDDEN);
    if (s_ap_scan_btn)     lv_obj_clear_state(s_ap_scan_btn, LV_STATE_DISABLED);

    if (status != ESP_OK || stored == 0) {
        ap_set_status("Keine Netze gefunden");
    } else {
        char buf[48];
        snprintf(buf, sizeof(buf), "%zu Netzwerk%s gefunden",
                 stored, stored == 1 ? "" : "e");
        ap_set_status(buf);
        show_ap_scan_popup();
    }

    lvgl_port_unlock();
}

/* ── AP-Tab: Connect-Task ───────────────────────────────────── */

typedef struct {
    char ssid[33];
    char password[65];
} ap_connect_params_t;

static void ap_connect_task_fn(void *arg)
{
    ap_connect_params_t params = {0};
    if (arg) {
        memcpy(&params, arg, sizeof(params));
        free(arg);
    }

    if (params.ssid[0] == '\0') {
        s_ap_connect_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "AP-Connect: '%s'", params.ssid);

    /* Laufenden Verbindungsversuch sauber abbrechen */
    if (womo_wifi_get_status() == WOMO_WIFI_CONNECTING) {
        womo_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(800));
    }

    esp_err_t err = womo_wifi_connect(params.ssid,
                                       params.password[0] ? params.password : NULL,
                                       3);

    if (lvgl_port_lock(-1)) {
        if (s_overlay) {
            if (err == ESP_OK) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Verbunden: %s", params.ssid);
                ap_set_status(buf);
                ap_update_saved_label();
            } else {
                char err_buf[80];
                snprintf(err_buf, sizeof(err_buf), "Fehler: %s", esp_err_to_name(err));
                ap_set_status(err_buf);
                if (s_ap_save_btn) lv_obj_clear_flag(s_ap_save_btn, LV_OBJ_FLAG_HIDDEN);
            }
        }
        lvgl_port_unlock();
    }

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AP-Verbindung zu '%s' fehlgeschlagen: %s",
                 params.ssid, esp_err_to_name(err));
    }

    s_ap_connect_task = NULL;
    vTaskDelete(NULL);
}

/* ── AP-Tab: Event-Callbacks ────────────────────────────────── */

static void ap_password_event_cb(lv_event_t *e)
{
    if (!s_ap_password_area || !s_ap_keyboard) return;
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(s_ap_keyboard, s_ap_password_area);
        lv_obj_clear_flag(s_ap_keyboard, LV_OBJ_FLAG_HIDDEN);
        lvgl_touch_set_fast_mode(true);
        if (s_panel) lv_obj_align(s_panel, LV_ALIGN_TOP_MID, 0, 10);
    } else if (code == LV_EVENT_DEFOCUSED) {
        ap_hide_keyboard();
    } else if (code == LV_EVENT_READY) {
        ap_hide_keyboard();
    }
}

static void ap_scan_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_ap_scan_in_progress || womo_wifi_is_scan_in_progress()) return;

    s_ap_scan_in_progress = true;
    s_ap_scan_result_count = 0;
    s_ap_selected_ssid[0] = '\0';

    if (s_ap_scan_btn)    lv_obj_add_state(s_ap_scan_btn, LV_STATE_DISABLED);
    if (s_ap_scan_spinner) lv_obj_clear_flag(s_ap_scan_spinner, LV_OBJ_FLAG_HIDDEN);
    if (s_ap_password_lbl) lv_obj_add_flag(s_ap_password_lbl, LV_OBJ_FLAG_HIDDEN);
    if (s_ap_password_area) lv_obj_add_flag(s_ap_password_area, LV_OBJ_FLAG_HIDDEN);
    if (s_ap_save_btn)     lv_obj_add_flag(s_ap_save_btn, LV_OBJ_FLAG_HIDDEN);
    ap_set_status("Suche laeuft ...");

    esp_err_t err = womo_wifi_scan_async(ap_scan_callback, NULL);
    if (err != ESP_OK) {
        s_ap_scan_in_progress = false;
        if (s_ap_scan_btn)    lv_obj_clear_state(s_ap_scan_btn, LV_STATE_DISABLED);
        if (s_ap_scan_spinner) lv_obj_add_flag(s_ap_scan_spinner, LV_OBJ_FLAG_HIDDEN);
        ap_set_status("Scan nicht moeglich");
        ESP_LOGW(TAG, "AP-Scan fehlgeschlagen: %s", esp_err_to_name(err));
    }
}

static void ap_save_btn_cb(lv_event_t *e)
{
    (void)e;
    if (s_ap_connect_task) return;
    if (s_ap_selected_ssid[0] == '\0') {
        ap_set_status("Kein Netzwerk ausgewaehlt");
        return;
    }

    /* Keyboard schließen falls offen */
    ap_hide_keyboard();

    const char *pwd = "";
    if (s_ap_password_area) {
        pwd = lv_textarea_get_text(s_ap_password_area);
    }

    /* 1. Sofort in NVS speichern (auch wenn Verbindung scheitert) */
    esp_err_t nvs_err = ap_cfg_save(s_ap_selected_ssid, pwd);
    if (nvs_err == ESP_OK) {
        ESP_LOGI(TAG, "AP-Konfiguration gespeichert: '%s'", s_ap_selected_ssid);
        ap_update_saved_label();
    } else {
        ESP_LOGW(TAG, "NVS-Speichern fehlgeschlagen: %s", esp_err_to_name(nvs_err));
    }

    /* 2. Credentials ans Sensorboard senden */
    womo_sensorboard_send_wifi_credentials(s_ap_selected_ssid, pwd);

    /* 3. Verbindungsversuch starten */
    ap_connect_params_t *params = calloc(1, sizeof(ap_connect_params_t));
    if (!params) {
        ap_set_status("Kein Speicher");
        return;
    }
    strncpy(params->ssid, s_ap_selected_ssid, sizeof(params->ssid) - 1);
    strncpy(params->password, pwd, sizeof(params->password) - 1);

    BaseType_t created = xTaskCreate(ap_connect_task_fn,
                                      "ap_cfg_conn",
                                      AP_TASK_STACK,
                                      params,
                                      AP_TASK_PRIO,
                                      &s_ap_connect_task);
    if (created != pdPASS) {
        free(params);
        s_ap_connect_task = NULL;
        ap_set_status("Task-Fehler");
        return;
    }

    ap_set_status("Verbinde ...");
    if (s_ap_save_btn) lv_obj_add_flag(s_ap_save_btn, LV_OBJ_FLAG_HIDDEN);
}

/* ── Tab-Aufbau ─────────────────────────────────────────────── */

static lv_obj_t *make_tab_base(lv_obj_t *tab)
{
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab, PAD, 0);
    lv_obj_set_style_bg_color(tab, lv_color_hex(0xF5F5F5), 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    return tab;
}

static lv_obj_t *make_section_sep(lv_obj_t *parent, int y, int w)
{
    lv_obj_t *sep = lv_obj_create(parent);
    lv_obj_set_size(sep, w, 1);
    lv_obj_set_pos(sep, PAD, y);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return sep;
}

static void build_tab_sprache(lv_obj_t *tab)
{
    make_tab_base(tab);

    lv_obj_t *title = lv_label_create(tab);
    lv_label_set_text(title, "Sprache / Language");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x333333), 0);
    lv_obj_set_pos(title, 0, 0);

    make_section_sep(tab, 26, MODAL_W - 2 * PAD);

    bool is_de = (womo_locale_get() == WOMO_LOCALE_DE);

    lv_obj_t *btn_de = lv_btn_create(tab);
    lv_obj_set_size(btn_de, 120, 48);
    lv_obj_set_pos(btn_de, 0, 38);
    lv_obj_set_style_bg_color(btn_de, is_de ? lv_color_hex(0x1565C0) : lv_color_hex(0xC0C0C0), 0);
    lv_obj_set_style_radius(btn_de, 8, 0);
    lv_obj_set_style_border_width(btn_de, 0, 0);
    lv_obj_t *lbl_de = lv_label_create(btn_de);
    lv_label_set_text(lbl_de, "Deutsch");
    lv_obj_set_style_text_font(lbl_de, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_de, is_de ? lv_color_white() : lv_color_black(), 0);
    lv_obj_center(lbl_de);

    lv_obj_t *btn_en = lv_btn_create(tab);
    lv_obj_set_size(btn_en, 120, 48);
    lv_obj_set_pos(btn_en, 136, 38);
    lv_obj_set_style_bg_color(btn_en, !is_de ? lv_color_hex(0x1565C0) : lv_color_hex(0xC0C0C0), 0);
    lv_obj_set_style_radius(btn_en, 8, 0);
    lv_obj_set_style_border_width(btn_en, 0, 0);
    lv_obj_t *lbl_en = lv_label_create(btn_en);
    lv_label_set_text(lbl_en, "English");
    lv_obj_set_style_text_font(lbl_en, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_en, !is_de ? lv_color_white() : lv_color_black(), 0);
    lv_obj_center(lbl_en);

    lv_obj_add_event_cb(btn_de, lang_de_cb, LV_EVENT_CLICKED, btn_en);
    lv_obj_add_event_cb(btn_en, lang_en_cb, LV_EVENT_CLICKED, btn_de);
}

static void build_tab_rtc(lv_obj_t *tab)
{
    make_tab_base(tab);

    lv_obj_t *title = lv_label_create(tab);
    lv_label_set_text(title, "Batterie (RTC)");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x333333), 0);
    lv_obj_set_pos(title, 0, 0);

    make_section_sep(tab, 26, MODAL_W - 2 * PAD);

    const char *src_name;
    switch (womo_time_get_source()) {
        case TIME_SOURCE_NTP:          src_name = "NTP (Internet)";  break;
        case TIME_SOURCE_GPS:          src_name = "GPS (Router)";    break;
        case TIME_SOURCE_SENSOR:        src_name = "Sensorboard ESP-NOW";    break;
        case TIME_SOURCE_INTERNAL_RTC: src_name = "RTC (intern)";    break;
        default:                       src_name = "---"; break;
    }

    char sync_buf[48];
    if (womo_time_is_synced()) {
        uint32_t secs = womo_time_get_seconds_since_sync();
        if (secs < 60)        snprintf(sync_buf, sizeof(sync_buf), "vor %lu s",   (unsigned long)secs);
        else if (secs < 3600) snprintf(sync_buf, sizeof(sync_buf), "vor %lu min", (unsigned long)(secs / 60));
        else                  snprintf(sync_buf, sizeof(sync_buf), "vor %lu h",   (unsigned long)(secs / 3600));
    } else {
        snprintf(sync_buf, sizeof(sync_buf), "nie");
    }

    extern void womo_get_rtc_battery_status(bool *valid, bool *low, bool *switched);
    bool rtc_valid = false, rtc_low = false, rtc_sw = false;
    womo_get_rtc_battery_status(&rtc_valid, &rtc_low, &rtc_sw);

    char rtc_bat_buf[32];
    if (!rtc_valid) snprintf(rtc_bat_buf, sizeof(rtc_bat_buf), "unbekannt");
    else            snprintf(rtc_bat_buf, sizeof(rtc_bat_buf), "%s", rtc_low ? "SCHWACH !!" : "OK");

    char rtc_info_text[300];
    int n = snprintf(rtc_info_text, sizeof(rtc_info_text),
             "Quelle:       %s\n"
             "Letzte Sync:  %s\n"
             "RTC-Batterie: %s",
             src_name, sync_buf, rtc_bat_buf);
    if (rtc_valid && rtc_sw && n < (int)sizeof(rtc_info_text) - 2)
        strncat(rtc_info_text, "\nRTC war ohne Strom",
                sizeof(rtc_info_text) - strlen(rtc_info_text) - 1);

    lv_obj_t *info = lv_label_create(tab);
    lv_label_set_text(info, rtc_info_text);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(0x444444), 0);
    lv_obj_set_pos(info, 0, 38);
}

static void build_tab_grenzwerte(lv_obj_t *tab)
{
    make_tab_base(tab);

    s_thr_title_lbl = lv_label_create(tab);
    lv_label_set_text(s_thr_title_lbl, womo_locale_get_string(STR_THRESH_TITLE));
    lv_obj_set_style_text_font(s_thr_title_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_thr_title_lbl, lv_color_hex(0x333333), 0);
    lv_obj_set_pos(s_thr_title_lbl, 0, 0);

    make_section_sep(tab, 26, MODAL_W - 2 * PAD);

    s_warn_hdr_lbl = lv_label_create(tab);
    lv_label_set_text(s_warn_hdr_lbl, womo_locale_get_string(STR_THRESH_WARNING));
    lv_obj_set_style_text_font(s_warn_hdr_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_warn_hdr_lbl, lv_color_hex(0xE65100), 0);
    lv_obj_set_pos(s_warn_hdr_lbl, 214, 34);

    lv_obj_t *h_alarm = lv_label_create(tab);
    lv_label_set_text(h_alarm, "Alarm");
    lv_obj_set_style_text_font(h_alarm, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(h_alarm, lv_color_hex(0xC62828), 0);
    lv_obj_set_pos(h_alarm, 404, 34);

    /* Zeilen: y relativ zum Tab-Inhalt (ohne PAD, da make_tab_base pad_all=PAD setzt) */
    make_thresh_row(tab, 54,  womo_locale_get_string(STR_THRESH_GAS),
                    &s_te.gas_warn,   &s_te.gas_crit,   false, &s_row_lbl[0]);
    make_thresh_row(tab, 98,  womo_locale_get_string(STR_THRESH_FRESH),
                    &s_te.fresh_warn, &s_te.fresh_crit, false, &s_row_lbl[1]);
    make_thresh_row(tab, 142, womo_locale_get_string(STR_THRESH_GREY),
                    &s_te.grey_warn,  &s_te.grey_crit,  true,  &s_row_lbl[2]);
}

static void build_tab_system(lv_obj_t *tab)
{
    make_tab_base(tab);

    lv_obj_t *title = lv_label_create(tab);
    lv_label_set_text(title, "Toene / Sounds");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x333333), 0);
    lv_obj_set_pos(title, 0, 0);

    make_section_sep(tab, 26, MODAL_W - 2 * PAD);

    bool buzzer_on   = display_buzzer_is_enabled();
    bool touch_click = display_buzzer_is_click_enabled();

    lv_obj_t *lbl_tones = lv_label_create(tab);
    lv_label_set_text(lbl_tones, womo_locale_get_string(STR_SOUND_TONES));
    lv_obj_set_style_text_font(lbl_tones, &lv_font_montserrat_14_german, 0);
    lv_obj_set_style_text_color(lbl_tones, lv_color_hex(0x333333), 0);
    lv_obj_set_pos(lbl_tones, 0, 44);

    s_btn_tones = lv_btn_create(tab);
    lv_obj_set_size(s_btn_tones, 64, 30);
    lv_obj_set_pos(s_btn_tones, 164, 40);
    lv_obj_set_style_radius(s_btn_tones, 6, 0);
    lv_obj_set_style_border_width(s_btn_tones, 0, 0);
    lv_obj_set_style_pad_all(s_btn_tones, 0, 0);
    lv_obj_t *tones_lbl = lv_label_create(s_btn_tones);
    lv_obj_set_style_text_font(tones_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(tones_lbl);
    sound_btn_update(s_btn_tones, buzzer_on);
    lv_obj_add_event_cb(s_btn_tones, sound_tones_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_touch = lv_label_create(tab);
    lv_label_set_text(lbl_touch, womo_locale_get_string(STR_SOUND_TOUCH_CLICK));
    lv_obj_set_style_text_font(lbl_touch, &lv_font_montserrat_14_german, 0);
    lv_obj_set_style_text_color(lbl_touch, lv_color_hex(0x333333), 0);
    lv_obj_set_pos(lbl_touch, 0, 90);

    s_btn_touch = lv_btn_create(tab);
    lv_obj_set_size(s_btn_touch, 64, 30);
    lv_obj_set_pos(s_btn_touch, 164, 86);
    lv_obj_set_style_radius(s_btn_touch, 6, 0);
    lv_obj_set_style_border_width(s_btn_touch, 0, 0);
    lv_obj_set_style_pad_all(s_btn_touch, 0, 0);
    lv_obj_t *touch_lbl = lv_label_create(s_btn_touch);
    lv_obj_set_style_text_font(touch_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(touch_lbl);
    sound_btn_update(s_btn_touch, touch_click);
    lv_obj_add_event_cb(s_btn_touch, sound_touch_cb, LV_EVENT_CLICKED, NULL);
}

static void build_tab_ap(lv_obj_t *tab)
{
    make_tab_base(tab);

    lv_obj_t *title = lv_label_create(tab);
    lv_label_set_text(title, "WLAN-Verbindung (AP)");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x333333), 0);
    lv_obj_set_pos(title, 0, 0);

    make_section_sep(tab, 26, MODAL_W - 2 * PAD);

    /* Gespeicherter AP */
    s_ap_saved_lbl = lv_label_create(tab);
    lv_obj_set_style_text_font(s_ap_saved_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_ap_saved_lbl, 0, 36);
    ap_update_saved_label();

    /* Scan-Zeile: Button + Spinner */
    lv_obj_t *scan_row = lv_obj_create(tab);
    lv_obj_remove_style_all(scan_row);
    lv_obj_set_size(scan_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_pos(scan_row, 0, 66);
    lv_obj_set_flex_flow(scan_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scan_row,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(scan_row, 8, 0);
    lv_obj_clear_flag(scan_row, LV_OBJ_FLAG_SCROLLABLE);

    s_ap_scan_btn = lv_btn_create(scan_row);
    lv_obj_set_style_bg_color(s_ap_scan_btn, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_pad_ver(s_ap_scan_btn, 6, 0);
    lv_obj_set_style_pad_hor(s_ap_scan_btn, 14, 0);
    lv_obj_add_event_cb(s_ap_scan_btn, ap_scan_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_lbl = lv_label_create(s_ap_scan_btn);
    lv_label_set_text(scan_lbl, "Netze suchen");
    lv_obj_set_style_text_font(scan_lbl, &lv_font_montserrat_14, 0);

    s_ap_scan_spinner = lv_spinner_create(scan_row);
    lv_spinner_set_anim_params(s_ap_scan_spinner, 1000, 60);
    lv_obj_set_size(s_ap_scan_spinner, 28, 28);
    lv_obj_add_flag(s_ap_scan_spinner, LV_OBJ_FLAG_HIDDEN);

    /* Status-Label */
    s_ap_scan_status_lbl = lv_label_create(tab);
    lv_label_set_text(s_ap_scan_status_lbl, "");
    lv_obj_set_style_text_font(s_ap_scan_status_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_ap_scan_status_lbl, lv_color_hex(0x444444), 0);
    lv_obj_set_pos(s_ap_scan_status_lbl, 0, 110);
    lv_obj_set_width(s_ap_scan_status_lbl, MODAL_W - 2 * PAD);
    lv_label_set_long_mode(s_ap_scan_status_lbl, LV_LABEL_LONG_DOT);

    /* Passwort-Label + Textarea (initial verborgen) */
    s_ap_password_lbl = lv_label_create(tab);
    lv_label_set_text(s_ap_password_lbl, "Passwort:");
    lv_obj_set_style_text_font(s_ap_password_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_ap_password_lbl, lv_color_hex(0x333333), 0);
    lv_obj_set_pos(s_ap_password_lbl, 0, 134);
    lv_obj_add_flag(s_ap_password_lbl, LV_OBJ_FLAG_HIDDEN);

    s_ap_password_area = lv_textarea_create(tab);
    lv_textarea_set_password_mode(s_ap_password_area, true);
    lv_textarea_set_password_show_time(s_ap_password_area, 800);
    lv_textarea_set_placeholder_text(s_ap_password_area, "Passwort eingeben");
    lv_textarea_set_max_length(s_ap_password_area, 64);
    lv_obj_set_size(s_ap_password_area, MODAL_W - 2 * PAD, 38);
    lv_obj_set_pos(s_ap_password_area, 0, 156);
    lv_obj_set_style_pad_all(s_ap_password_area, 6, 0);
    lv_obj_add_event_cb(s_ap_password_area, ap_password_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(s_ap_password_area, LV_OBJ_FLAG_HIDDEN);

    /* Speichern-Button (initial verborgen) */
    s_ap_save_btn = lv_btn_create(tab);
    lv_obj_set_size(s_ap_save_btn, 140, 38);
    lv_obj_set_pos(s_ap_save_btn, 0, 202);
    lv_obj_set_style_bg_color(s_ap_save_btn, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_style_radius(s_ap_save_btn, 6, 0);
    lv_obj_set_style_border_width(s_ap_save_btn, 0, 0);
    lv_obj_add_event_cb(s_ap_save_btn, ap_save_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_lbl = lv_label_create(s_ap_save_btn);
    lv_label_set_text(save_lbl, "Speichern");
    lv_obj_set_style_text_font(save_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(save_lbl);
    lv_obj_add_flag(s_ap_save_btn, LV_OBJ_FLAG_HIDDEN);
}

/* ── Public API ─────────────────────────────────────────────── */

bool womo_settings_modal_is_open(void)
{
    return (s_overlay != NULL);
}

void womo_settings_modal_show(lv_obj_t *parent)
{
    if (s_overlay) {
        lv_obj_move_foreground(s_overlay);
        return;
    }

    womo_thresholds_get(&s_te);
    s_tud_cnt = 0;
    s_ap_selected_ssid[0] = '\0';
    s_ap_scan_in_progress = false;
    s_ap_connect_task = NULL;

    /* ── Overlay ──────────────────────────────────────────── */
    s_overlay = lv_obj_create(parent);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, overlay_click_cb, LV_EVENT_CLICKED, NULL);

    /* ── Panel ────────────────────────────────────────────── */
    s_panel = lv_obj_create(s_overlay);
    lv_obj_set_size(s_panel, MODAL_W, MODAL_H);
    lv_obj_align(s_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0xF5F5F5), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_panel, 12, 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_pad_all(s_panel, 0, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_CLICKABLE);

    /* ── Kopfzeile ────────────────────────────────────────── */
    lv_obj_t *header = lv_obj_create(s_panel);
    lv_obj_set_size(header, MODAL_W, HDR_H);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, womo_locale_get_string(STR_SETTINGS_TITLE));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, PAD, 0);
    s_title_lbl = title;

    lv_obj_t *close_btn = lv_btn_create(header);
    lv_obj_set_size(close_btn, 36, 32);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -PAD, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xC62828), 0);
    lv_obj_set_style_radius(close_btn, 4, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_add_event_cb(close_btn, close_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_obj_center(close_lbl);

    /* ── Tabview ──────────────────────────────────────────── */
    lv_obj_t *tabview = lv_tabview_create(s_panel);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, TAB_BAR_H);
    lv_obj_set_pos(tabview, 0, HDR_H);
    lv_obj_set_size(tabview, MODAL_W, MODAL_H - HDR_H);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0xF5F5F5), 0);
    lv_obj_set_style_pad_all(tabview, 0, 0);
    lv_obj_clear_flag(tabview, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_bg_opa(tab_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(tab_bar, &lv_font_montserrat_14, 0);

    lv_obj_t *tab_lang   = lv_tabview_add_tab(tabview, "Sprache");
    lv_obj_t *tab_rtc    = lv_tabview_add_tab(tabview, "RTC");
    lv_obj_t *tab_thresh = lv_tabview_add_tab(tabview, "Grenzwerte");
    lv_obj_t *tab_sys    = lv_tabview_add_tab(tabview, "System");
    lv_obj_t *tab_ap     = lv_tabview_add_tab(tabview, "AP");

    build_tab_sprache(tab_lang);
    build_tab_rtc(tab_rtc);
    build_tab_grenzwerte(tab_thresh);
    build_tab_system(tab_sys);
    build_tab_ap(tab_ap);

    /* ── QWERTZ-Tastatur auf Overlay (für AP-Passwort) ────── */
    s_ap_keyboard = lv_keyboard_create(s_overlay);
    lv_obj_set_size(s_ap_keyboard, LV_PCT(90), 180);
    lv_obj_align(s_ap_keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_keyboard_set_textarea(s_ap_keyboard, NULL);
    lv_obj_add_flag(s_ap_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_map(s_ap_keyboard,
                        LV_KEYBOARD_MODE_TEXT_LOWER,
                        s_keyboard_map_lc,
                        s_keyboard_ctrl_lc);
    lv_keyboard_set_map(s_ap_keyboard,
                        LV_KEYBOARD_MODE_TEXT_UPPER,
                        s_keyboard_map_uc,
                        s_keyboard_ctrl_uc);
    lv_keyboard_set_mode(s_ap_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);

    /* ── Locale-Callback einmalig registrieren ────────────── */
    if (!s_locale_cb_reg) {
        womo_locale_register_change_cb(settings_locale_cb);
        s_locale_cb_reg = true;
    }

    ESP_LOGI(TAG, "Einstellungs-Modal geöffnet");
}
