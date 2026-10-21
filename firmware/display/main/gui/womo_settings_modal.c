/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * womo_settings_modal.c – Einstellungs-Modal mit 5 Tabs
 *
 * Tabs: AP | Sensoren | Grenzwerte | LTE | System
 * Größe: 640×420 px, Header 48 px, Tabview 372 px (Tabbar 38 px, Inhalt 334 px).
 */

#include "womo_settings_modal.h"
#include "womo_locale.h"
#include "womo_thresholds.h"
#include "womo_tariff.h"
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
#include <stdlib.h>
#include <math.h>

static const char *TAG = "settings_modal";

/* ── Dimensionen ────────────────────────────────────────────── */
#define MODAL_W      640
#define MODAL_H      420
#define HDR_H         48
#define TAB_BAR_H     38
#define PAD           16
#define NVS_AP_NS    "womo_ap_cfg"
#define NVS_RUUVI_NS "womo_ruuvi_cfg"
#define NVS_GASBEE_NS "womo_gasbee_cfg"
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

/* Tab-Reihenfolge = Reihenfolge in der Tab-Leiste, erster Tab = Starttab */
enum { TAB_AP = 0, TAB_SENSORS, TAB_THRESH, TAB_LTE, TAB_SYSTEM, TAB_COUNT };
static lv_obj_t *s_tabview = NULL;

/* Ton-Buttons */
static lv_obj_t *s_btn_tones = NULL;
static lv_obj_t *s_btn_touch = NULL;

/* System-Tab: übersetzbare Labels */
static lv_obj_t *s_sound_title_lbl = NULL;
static lv_obj_t *s_tones_lbl       = NULL;
static lv_obj_t *s_touch_lbl       = NULL;
static lv_obj_t *s_lang_title_lbl  = NULL;
static lv_obj_t *s_time_title_lbl  = NULL;
static lv_obj_t *s_time_info_lbl   = NULL;

/* Sensoren-Tab: übersetzbare Labels (Zeilennamen 0=Innen, 1=Außen, 2=Flasche A, 3=Flasche B) */
static lv_obj_t *s_ruuvi_title_lbl  = NULL;
static lv_obj_t *s_gasbee_title_lbl = NULL;
static lv_obj_t *s_sens_row_lbl[4]  = {NULL};

/* LTE-Tab: Mobilfunk-Tarif (Werte in womo_tariff, hier nur Anzeige) */
static struct {
    lv_obj_t *title;
    lv_obj_t *name[4];     /* Vorlauf, Restvolumen, Termin, Rhythmus */
    lv_obj_t *val[3];      /* Werte der −/+-Zeilen */
    lv_obj_t *period_btn;
    lv_obj_t *today_btn;
    lv_obj_t *auto_lbl;
    lv_obj_t *next_lbl;
} s_lte;

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

/* ── Ruuvi-Tag: MAC-Konfiguration ───────────────────────────── */
static lv_obj_t   *s_ruuvi_indoor_lbl    = NULL;
static lv_obj_t   *s_ruuvi_outdoor_lbl   = NULL;
static lv_obj_t   *s_ruuvi_status_lbl    = NULL;
static lv_obj_t   *s_ruuvi_scan_btn_in   = NULL;
static lv_obj_t   *s_ruuvi_scan_btn_out  = NULL;
static lv_obj_t   *s_ruuvi_scan_popup    = NULL;
static lv_obj_t   *s_ruuvi_scan_list     = NULL;
static lv_timer_t *s_ruuvi_scan_timeout_timer = NULL;
static bool        s_ruuvi_scan_in_progress   = false;
static int         s_ruuvi_scan_target        = 0;  /* 0=Innen, 1=Aussen */
#define RUUVI_SCAN_MAX_RETRY 2
static int         s_ruuvi_scan_retry         = 0;  /* Wiederholungen bei ausbleibender Antwort */
static char        s_ruuvi_indoor_mac[18]     = "";
static char        s_ruuvi_outdoor_mac[18]    = "";
static womo_ruuvi_scan_entry_t s_ruuvi_scan_results[WOMO_RUUVI_SCAN_MAX_TAGS];
static size_t       s_ruuvi_scan_result_count = 0;

/* ── GasBee (BLE-Gaswaage): Zuordnung Flasche A/B per Einrichten-Assistent ── */
static lv_obj_t   *s_gasbee_lbl[2]       = {NULL, NULL};  /* 0=A, 1=B */
static lv_obj_t   *s_gasbee_scan_btn[2]  = {NULL, NULL};  /* Button "Einrichten" */
static char        s_gasbee_mac[2][18]   = {"", ""};

/* Abholfach für Ergebnisse aus dem ESP-NOW-RX- bzw. esp_timer-Task. Ausgewertet wird
 * nur im LVGL-Task (s_rtc_poll_timer) – kein lvgl_port_lock() in fremden Tasks. */
static portMUX_TYPE s_inbox_lock = portMUX_INITIALIZER_UNLOCKED;
static bool        s_ruuvi_inbox_ready = false;
static womo_ruuvi_scan_entry_t s_ruuvi_inbox[WOMO_RUUVI_SCAN_MAX_TAGS];
static size_t      s_ruuvi_inbox_count = 0;
static bool        s_gasbee_inbox_ready = false;
static womo_gasbee_scan_entry_t s_gasbee_inbox[WOMO_GASBEE_SCAN_MAX_TAGS];
static size_t      s_gasbee_inbox_count = 0;
static bool        s_gbcal_inbox_ready = false;
static bool        s_gbcal_inbox_zero  = false;
static bool        s_gbcal_inbox_ok    = false;
static int         s_gbcal_inbox_err   = 0;
static lv_timer_t *s_rtc_poll_timer    = NULL;   /* 100 ms, nur solange das Modal offen ist */

/* Zustand des GasBee-Einrichten-Assistenten (Ablauf siehe gbw_render) */
typedef enum {
    GBW_OFF = 0,
    GBW_SEARCH,       /* wartet auf genau eine Gaswaage im Konfigurationsmodus */
    GBW_NOT_FOUND,
    GBW_FOUND,        /* Übernehmen? */
    GBW_CAL_ASK,      /* Jetzt kalibrieren? */
    GBW_ZERO,         /* Waage leeren, Weiter */
    GBW_ZERO_BUSY,
    GBW_REF,          /* Gewicht auflegen + eingeben */
    GBW_REF_BUSY,
    GBW_VERIFY,       /* nächste Messung mit Flag "kalibriert" abwarten */
    GBW_ERROR,        /* Fehler eines Schritts, Wiederholen möglich */
    GBW_DONE,
} gbw_state_t;

static gbw_state_t s_gbw_state = GBW_OFF;
static int         s_gbw_slot = 0;            /* 0 = A, 1 = B */
static womo_gasbee_scan_entry_t s_gbw_cand;   /* gefundene Gaswaage */
static uint32_t    s_gbw_t0 = 0;              /* Beginn des aktuellen Zustands (lv_tick) */
static uint32_t    s_gbw_last_poll = 0;
static bool        s_gbw_session = false;     /* gasbee_cal_start gesendet */
static uint32_t    s_gbw_ref_g = 0;
static uint8_t     s_gbw_seq_ref = 0;         /* letzte bekannte bee_seq vor dem Referenz-Write */
static gbw_state_t s_gbw_retry_state = GBW_ZERO;
static char        s_gbw_msg[200] = "";
static lv_obj_t   *s_gbw_popup = NULL;
static lv_obj_t   *s_gbw_text  = NULL;
static lv_obj_t   *s_gbw_ta    = NULL;        /* Gewichtseingabe (Gramm) */
static lv_obj_t   *s_gbw_kb    = NULL;

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

static void ruuvi_cfg_load(char *indoor, size_t indoor_sz, char *outdoor, size_t outdoor_sz)
{
    indoor[0] = '\0';
    outdoor[0] = '\0';

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_RUUVI_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return;
    size_t len = indoor_sz;
    nvs_get_str(h, "indoor_mac", indoor, &len);
    len = outdoor_sz;
    nvs_get_str(h, "outdoor_mac", outdoor, &len);
    nvs_close(h);
}

static esp_err_t ruuvi_cfg_save(const char *indoor, const char *outdoor)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_RUUVI_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, "indoor_mac", indoor ? indoor : "");
    nvs_set_str(h, "outdoor_mac", outdoor ? outdoor : "");
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

static void gasbee_cfg_load(char *mac_a, size_t a_sz, char *mac_b, size_t b_sz)
{
    mac_a[0] = '\0';
    mac_b[0] = '\0';

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_GASBEE_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return;
    size_t len = a_sz;
    nvs_get_str(h, "bottle_a_mac", mac_a, &len);
    len = b_sz;
    nvs_get_str(h, "bottle_b_mac", mac_b, &len);
    nvs_close(h);
}

static esp_err_t gasbee_cfg_save(const char *mac_a, const char *mac_b)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_GASBEE_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, "bottle_a_mac", mac_a ? mac_a : "");
    nvs_set_str(h, "bottle_b_mac", mac_b ? mac_b : "");
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

/* ── Locale-Callback ────────────────────────────────────────── */

static void sound_btn_update(lv_obj_t *btn, bool on);
static void system_time_info_refresh(void);
static void lte_tab_refresh(void);

static void set_label_str(lv_obj_t *lbl, womo_string_id_t id)
{
    if (lbl) lv_label_set_text(lbl, womo_locale_get_string(id));
}

/* Text des ersten Kind-Labels eines Buttons setzen */
static void set_btn_str(lv_obj_t *btn, womo_string_id_t id)
{
    if (btn) set_label_str(lv_obj_get_child(btn, 0), id);
}

static void settings_locale_cb(void)
{
    if (!s_panel) return;
    static const womo_string_id_t tab_str[TAB_COUNT] = {
        STR_SET_TAB_AP, STR_SET_TAB_SENSORS, STR_SET_TAB_THRESH, STR_SET_TAB_LTE, STR_SET_TAB_SYSTEM,
    };
    if (s_tabview) {
        for (int i = 0; i < TAB_COUNT; i++) {
            lv_tabview_set_tab_text(s_tabview, i, womo_locale_get_string(tab_str[i]));
        }
    }

    /* System-Tab */
    set_label_str(s_sound_title_lbl, STR_SOUND_SECTION);
    set_label_str(s_tones_lbl,       STR_SOUND_TONES);
    set_label_str(s_touch_lbl,       STR_SOUND_TOUCH_CLICK);
    set_label_str(s_lang_title_lbl,  STR_LANG_SECTION);
    set_label_str(s_time_title_lbl,  STR_TIME_SECTION);
    if (s_btn_tones) sound_btn_update(s_btn_tones, display_buzzer_is_enabled());
    if (s_btn_touch) sound_btn_update(s_btn_touch, display_buzzer_is_click_enabled());
    system_time_info_refresh();

    /* LTE-Tab */
    set_label_str(s_lte.title,   STR_TARIFF_SECTION);
    set_label_str(s_lte.name[0], STR_TARIFF_LEAD);
    set_label_str(s_lte.name[1], STR_TARIFF_VOLUME);
    set_label_str(s_lte.name[2], STR_TARIFF_MANUAL);
    set_label_str(s_lte.name[3], STR_TARIFF_PERIOD);
    set_btn_str(s_lte.today_btn, STR_TARIFF_TODAY_BTN);
    lte_tab_refresh();

    /* Sensoren-Tab */
    set_label_str(s_ruuvi_title_lbl,  STR_SENS_RUUVI_SECTION);
    set_label_str(s_gasbee_title_lbl, STR_SENS_GASBEE_SECTION);
    set_label_str(s_sens_row_lbl[0],  STR_SENS_INDOOR);
    set_label_str(s_sens_row_lbl[1],  STR_SENS_OUTDOOR);
    set_label_str(s_sens_row_lbl[2],  STR_SENS_BOTTLE_A);
    set_label_str(s_sens_row_lbl[3],  STR_SENS_BOTTLE_B);
    set_btn_str(s_ruuvi_scan_btn_in,  STR_SENS_SCAN_BTN);
    set_btn_str(s_ruuvi_scan_btn_out, STR_SENS_SCAN_BTN);
    set_btn_str(s_gasbee_scan_btn[0], STR_SENS_SETUP_BTN);
    set_btn_str(s_gasbee_scan_btn[1], STR_SENS_SETUP_BTN);

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
    if (lbl) lv_label_set_text(lbl, womo_locale_get_string(on ? STR_ON : STR_OFF));
}

/* Zustand aus dem Buzzer-Modul lesen, nicht aus dem (übersetzten) Button-Text */
static void sound_tones_cb(lv_event_t *e)
{
    (void)e;
    if (!s_btn_tones) return;
    bool new_state = !display_buzzer_is_enabled();
    sound_btn_update(s_btn_tones, new_state);
    womo_sensorboard_send_buzzer(new_state);
    display_buzzer_set_enabled(new_state);
    ESP_LOGI(TAG, "Systemtöne → %s", new_state ? "EIN" : "AUS");
}

static void sound_touch_cb(lv_event_t *e)
{
    (void)e;
    if (!s_btn_touch) return;
    bool new_state = !display_buzzer_is_click_enabled();
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
    s_tabview          = NULL;
    s_sound_title_lbl  = NULL;
    s_tones_lbl        = NULL;
    s_touch_lbl        = NULL;
    s_lang_title_lbl   = NULL;
    s_time_title_lbl   = NULL;
    s_time_info_lbl    = NULL;
    s_ruuvi_title_lbl  = NULL;
    s_gasbee_title_lbl = NULL;
    for (int i = 0; i < 4; i++) s_sens_row_lbl[i] = NULL;
    memset(&s_lte, 0, sizeof(s_lte));
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

    if (s_ruuvi_scan_timeout_timer) {
        lv_timer_del(s_ruuvi_scan_timeout_timer);
        s_ruuvi_scan_timeout_timer = NULL;
    }
    womo_sensorboard_set_ruuvi_scan_callback(NULL, NULL);
    s_ruuvi_indoor_lbl    = NULL;
    s_ruuvi_outdoor_lbl   = NULL;
    s_ruuvi_status_lbl    = NULL;
    s_ruuvi_scan_btn_in   = NULL;
    s_ruuvi_scan_btn_out  = NULL;
    s_ruuvi_scan_popup    = NULL;
    s_ruuvi_scan_list     = NULL;
    s_ruuvi_scan_in_progress = false;

    womo_sensorboard_set_gasbee_scan_callback(NULL, NULL);
    womo_sensorboard_set_gasbee_cal_result_callback(NULL, NULL);
    if (s_rtc_poll_timer) {
        lv_timer_del(s_rtc_poll_timer);
        s_rtc_poll_timer = NULL;
    }
    /* Assistent: Popup/Tastatur werden mit dem Overlay gelöscht, offene Sitzung beenden */
    if (s_gbw_session) {
        womo_sensorboard_send_gasbee_cal_session(s_gbw_slot, false);
        s_gbw_session = false;
    }
    s_gbw_state = GBW_OFF;
    s_gbw_popup = NULL;
    s_gbw_text  = NULL;
    s_gbw_ta    = NULL;
    s_gbw_kb    = NULL;
    s_gasbee_lbl[0]      = NULL;
    s_gasbee_lbl[1]      = NULL;
    s_gasbee_scan_btn[0] = NULL;
    s_gasbee_scan_btn[1] = NULL;
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

static lv_obj_t *make_section_sep_at(lv_obj_t *parent, int x, int y, int w)
{
    lv_obj_t *sep = lv_obj_create(parent);
    lv_obj_set_size(sep, w, 1);
    lv_obj_set_pos(sep, x, y);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return sep;
}

static lv_obj_t *make_section_sep(lv_obj_t *parent, int y, int w)
{
    return make_section_sep_at(parent, PAD, y, w);
}

/* Abschnittstitel (übersetzt, 16 px mit Umlauten) + Trennlinie 26 px darunter */
static lv_obj_t *make_section_title(lv_obj_t *parent, int x, int y, int w, womo_string_id_t id)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, womo_locale_get_string(id));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16_german, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x333333), 0);
    lv_obj_set_pos(title, x, y);
    make_section_sep_at(parent, x, y + 26, w);
    return title;
}

/* Sprachwahl als Abschnitt im System-Tab (Buttons 48 px hoch, ab y + 38) */
static void build_lang_section(lv_obj_t *tab, int x, int y, int w)
{
    s_lang_title_lbl = make_section_title(tab, x, y, w, STR_LANG_SECTION);

    bool is_de = (womo_locale_get() == WOMO_LOCALE_DE);

    lv_obj_t *btn_de = lv_btn_create(tab);
    lv_obj_set_size(btn_de, 120, 48);
    lv_obj_set_pos(btn_de, x, y + 38);
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
    lv_obj_set_pos(btn_en, x + 136, y + 38);
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

/* ── Ruuvi-Tag: Hilfsfunktionen ─────────────────────────────── */

static void rtc_tab_set_status(const char *text)
{
    if (s_ruuvi_status_lbl) lv_label_set_text(s_ruuvi_status_lbl, text ? text : "");
}

/* Verkabelter Bus-Sensor (BME680/280) hat immer Vorrang vor dem Ruuvi-Fallback
 * (siehe womo_sensorboard.h: is_ruuvi/batt_valid) – Scan/Config ist dann sinnlos. */
static bool ruuvi_wired_active(bool indoor, const womo_sensor_data_t *sd, bool have_sd)
{
    if (!have_sd) return false;
    return indoor
        ? (sd->bme680_indoor.valid && !sd->bme680_indoor.is_ruuvi)
        : (sd->bme680.valid && !sd->bme680.is_ruuvi);
}

/* Warnschwelle der Ruuvi-Knopfzelle (CR2477) abhängig von der Tag-Temperatur –
 * gleiche Bänder wie die Ruuvi-Station-App (Quelle: https://ruuvi.com/battery-usage/).
 * Prozent ist bei Lithium-Knopfzellen nicht sinnvoll (Spannung bis kurz vor Ende flach). */
static float ruuvi_batt_min_v(float temp_c)
{
    if (temp_c < -20.0f) return 2.0f;
    if (temp_c < 0.0f)   return 2.3f;
    return 2.5f;
}

static void ruuvi_refresh_labels(void)
{
    womo_sensor_data_t sd = {0};
    bool have_sd = womo_sensorboard_get_latest_data(&sd);
    bool wired_in  = ruuvi_wired_active(true, &sd, have_sd);
    bool wired_out = ruuvi_wired_active(false, &sd, have_sd);

    if (s_ruuvi_indoor_lbl) {
        char buf[64];
        if (wired_in) {
            snprintf(buf, sizeof(buf), "Bus-Sensor aktiv (Vorrang)");
            lv_obj_set_style_text_color(s_ruuvi_indoor_lbl, lv_color_hex(0x888888), 0);
        } else if (s_ruuvi_indoor_mac[0] == '\0') {
            snprintf(buf, sizeof(buf), "nicht konfiguriert");
            lv_obj_set_style_text_color(s_ruuvi_indoor_lbl, lv_color_hex(0x888888), 0);
        } else if (have_sd && sd.bme680_indoor.is_ruuvi && sd.bme680_indoor.batt_valid) {
            float v   = sd.bme680_indoor.batt_mv / 1000.0f;
            float v_min = ruuvi_batt_min_v(sd.bme680_indoor.temperature_c);
            snprintf(buf, sizeof(buf), "%s  Batt %.2fV (min %.1fV)%s",
                     s_ruuvi_indoor_mac, v, v_min, v < v_min ? " !" : "");
            lv_obj_set_style_text_color(s_ruuvi_indoor_lbl,
                v < v_min ? lv_color_hex(0xC62828) : lv_color_hex(0x2E7D32), 0);
        } else {
            snprintf(buf, sizeof(buf), "%s", s_ruuvi_indoor_mac);
            lv_obj_set_style_text_color(s_ruuvi_indoor_lbl, lv_color_hex(0x333333), 0);
        }
        lv_label_set_text(s_ruuvi_indoor_lbl, buf);
    }

    if (s_ruuvi_outdoor_lbl) {
        char buf[64];
        if (wired_out) {
            snprintf(buf, sizeof(buf), "Bus-Sensor aktiv (Vorrang)");
            lv_obj_set_style_text_color(s_ruuvi_outdoor_lbl, lv_color_hex(0x888888), 0);
        } else if (s_ruuvi_outdoor_mac[0] == '\0') {
            snprintf(buf, sizeof(buf), "nicht konfiguriert");
            lv_obj_set_style_text_color(s_ruuvi_outdoor_lbl, lv_color_hex(0x888888), 0);
        } else if (have_sd && sd.bme680.is_ruuvi && sd.bme680.batt_valid) {
            float v   = sd.bme680.batt_mv / 1000.0f;
            float v_min = ruuvi_batt_min_v(sd.bme680.temperature_c);
            snprintf(buf, sizeof(buf), "%s  Batt %.2fV (min %.1fV)%s",
                     s_ruuvi_outdoor_mac, v, v_min, v < v_min ? " !" : "");
            lv_obj_set_style_text_color(s_ruuvi_outdoor_lbl,
                v < v_min ? lv_color_hex(0xC62828) : lv_color_hex(0x2E7D32), 0);
        } else {
            snprintf(buf, sizeof(buf), "%s", s_ruuvi_outdoor_mac);
            lv_obj_set_style_text_color(s_ruuvi_outdoor_lbl, lv_color_hex(0x333333), 0);
        }
        lv_label_set_text(s_ruuvi_outdoor_lbl, buf);
    }

    if (s_ruuvi_scan_btn_in) {
        if (wired_in) lv_obj_add_state(s_ruuvi_scan_btn_in, LV_STATE_DISABLED);
        else          lv_obj_clear_state(s_ruuvi_scan_btn_in, LV_STATE_DISABLED);
    }
    if (s_ruuvi_scan_btn_out) {
        if (wired_out) lv_obj_add_state(s_ruuvi_scan_btn_out, LV_STATE_DISABLED);
        else           lv_obj_clear_state(s_ruuvi_scan_btn_out, LV_STATE_DISABLED);
    }
}

static void ruuvi_apply_config(void)
{
    ruuvi_cfg_save(s_ruuvi_indoor_mac, s_ruuvi_outdoor_mac);
    womo_sensorboard_send_ruuvi_config(s_ruuvi_indoor_mac, s_ruuvi_outdoor_mac);
    ruuvi_refresh_labels();
    rtc_tab_set_status("Ruuvi-Konfiguration gespeichert");
}

static void ruuvi_clear_indoor_cb(lv_event_t *e)
{
    (void)e;
    s_ruuvi_indoor_mac[0] = '\0';
    ruuvi_apply_config();
}

static void ruuvi_clear_outdoor_cb(lv_event_t *e)
{
    (void)e;
    s_ruuvi_outdoor_mac[0] = '\0';
    ruuvi_apply_config();
}

static void close_ruuvi_scan_popup(void)
{
    if (s_ruuvi_scan_popup) {
        lv_obj_del(s_ruuvi_scan_popup);
        s_ruuvi_scan_popup = NULL;
        s_ruuvi_scan_list  = NULL;
    }
}

static void ruuvi_scan_popup_close_cb(lv_event_t *e)
{
    (void)e;
    close_ruuvi_scan_popup();
}

static void ruuvi_scan_list_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t *btn = lv_event_get_target(e);
    uint32_t idx = lv_obj_get_index(btn);
    if (idx >= s_ruuvi_scan_result_count) {
        close_ruuvi_scan_popup();
        return;
    }

    const char *mac = s_ruuvi_scan_results[idx].mac;
    if (s_ruuvi_scan_target == 0) {
        strncpy(s_ruuvi_indoor_mac, mac, sizeof(s_ruuvi_indoor_mac) - 1);
        s_ruuvi_indoor_mac[sizeof(s_ruuvi_indoor_mac) - 1] = '\0';
    } else {
        strncpy(s_ruuvi_outdoor_mac, mac, sizeof(s_ruuvi_outdoor_mac) - 1);
        s_ruuvi_outdoor_mac[sizeof(s_ruuvi_outdoor_mac) - 1] = '\0';
    }

    close_ruuvi_scan_popup();
    ruuvi_apply_config();
}

static void show_ruuvi_scan_popup(void)
{
    close_ruuvi_scan_popup();
    if (s_ruuvi_scan_result_count == 0 || !s_overlay) return;

    s_ruuvi_scan_popup = lv_obj_create(s_overlay);
    lv_obj_set_size(s_ruuvi_scan_popup, 420, 320);
    lv_obj_center(s_ruuvi_scan_popup);
    lv_obj_set_style_bg_color(s_ruuvi_scan_popup, lv_color_white(), 0);
    lv_obj_set_style_radius(s_ruuvi_scan_popup, 8, 0);
    lv_obj_set_style_pad_all(s_ruuvi_scan_popup, 12, 0);
    lv_obj_set_flex_flow(s_ruuvi_scan_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_shadow_width(s_ruuvi_scan_popup, 20, 0);
    lv_obj_set_style_shadow_opa(s_ruuvi_scan_popup, LV_OPA_30, 0);
    lv_obj_move_foreground(s_ruuvi_scan_popup);

    lv_obj_t *popup_hdr = lv_obj_create(s_ruuvi_scan_popup);
    lv_obj_remove_style_all(popup_hdr);
    lv_obj_set_size(popup_hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(popup_hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(popup_hdr,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_bottom(popup_hdr, 8, 0);

    lv_obj_t *popup_title = lv_label_create(popup_hdr);
    lv_label_set_text(popup_title,
        s_ruuvi_scan_target == 0 ? "Ruuvi-Tag waehlen (Innen)" : "Ruuvi-Tag waehlen (Aussen)");
    lv_obj_set_style_text_font(popup_title, &lv_font_montserrat_16, 0);
    lv_obj_set_flex_grow(popup_title, 1);

    lv_obj_t *popup_close = lv_btn_create(popup_hdr);
    lv_obj_set_style_pad_all(popup_close, 6, 0);
    lv_obj_add_event_cb(popup_close, ruuvi_scan_popup_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_lbl = lv_label_create(popup_close);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);

    s_ruuvi_scan_list = lv_list_create(s_ruuvi_scan_popup);
    lv_obj_set_width(s_ruuvi_scan_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_ruuvi_scan_list, 1);

    for (size_t i = 0; i < s_ruuvi_scan_result_count; i++) {
        char text[160];
        snprintf(text, sizeof(text), "%.17s  %.1f\xC2\xB0" "C  %d dBm%s",
                 s_ruuvi_scan_results[i].mac, (double)s_ruuvi_scan_results[i].temp_c,
                 (int)s_ruuvi_scan_results[i].rssi,
                 s_ruuvi_scan_results[i].has_pressure ? "" : "  (kein Druck)");
        lv_obj_t *btn = lv_list_add_btn(s_ruuvi_scan_list, LV_SYMBOL_BLUETOOTH, text);
        lv_obj_add_event_cb(btn, ruuvi_scan_list_btn_cb, LV_EVENT_CLICKED, NULL);
    }
}

static void ruuvi_scan_timeout_cb(lv_timer_t *t)
{
    (void)t;
    s_ruuvi_scan_timeout_timer = NULL;
    if (!s_ruuvi_scan_in_progress) return;

    if (s_ruuvi_scan_retry < RUUVI_SCAN_MAX_RETRY) {
        s_ruuvi_scan_retry++;
        char buf[48];
        snprintf(buf, sizeof(buf), "Keine Antwort - Versuch %d/%d", s_ruuvi_scan_retry + 1, RUUVI_SCAN_MAX_RETRY + 1);
        rtc_tab_set_status(buf);

        s_ruuvi_scan_timeout_timer = lv_timer_create(ruuvi_scan_timeout_cb, 4000, NULL);
        if (!s_ruuvi_scan_timeout_timer) {
            s_ruuvi_scan_in_progress = false;
            rtc_tab_set_status("Scan-Timer konnte nicht angelegt werden");
            return;
        }
        lv_timer_set_repeat_count(s_ruuvi_scan_timeout_timer, 1);

        esp_err_t err = womo_sensorboard_send_ruuvi_scan_request();
        if (err != ESP_OK) {
            s_ruuvi_scan_in_progress = false;
            if (s_ruuvi_scan_timeout_timer) {
                lv_timer_del(s_ruuvi_scan_timeout_timer);
                s_ruuvi_scan_timeout_timer = NULL;
            }
            rtc_tab_set_status("Sensorboard nicht erreichbar");
        }
        return;
    }

    s_ruuvi_scan_in_progress = false;
    rtc_tab_set_status("Keine Antwort vom Sensorboard");
}

/* Läuft im ESP-NOW-RX- bzw. esp_timer-Task: nur ins Abholfach legen,
 * ausgewertet wird in rtc_poll_timer_cb() (LVGL-Task). */
static void ruuvi_scan_result_cb(const womo_ruuvi_scan_entry_t *entries, size_t count, void *user_data)
{
    (void)user_data;
    if (count > WOMO_RUUVI_SCAN_MAX_TAGS) count = WOMO_RUUVI_SCAN_MAX_TAGS;
    taskENTER_CRITICAL(&s_inbox_lock);
    memcpy(s_ruuvi_inbox, entries, count * sizeof(entries[0]));
    s_ruuvi_inbox_count = count;
    s_ruuvi_inbox_ready = true;
    taskEXIT_CRITICAL(&s_inbox_lock);
}

/* LVGL-Task: Ergebnis einer Ruuvi-Scan-Anfrage anzeigen */
static void ruuvi_scan_process(const womo_ruuvi_scan_entry_t *entries, size_t count)
{
    size_t stored = 0;
    for (size_t i = 0; i < count && stored < WOMO_RUUVI_SCAN_MAX_TAGS; i++) {
        s_ruuvi_scan_results[stored++] = entries[i];
    }
    s_ruuvi_scan_result_count = stored;

    if (!s_ruuvi_scan_in_progress) return;   /* verspätete Antwort nach Timeout */
    s_ruuvi_scan_in_progress = false;
    if (s_ruuvi_scan_timeout_timer) {
        lv_timer_del(s_ruuvi_scan_timeout_timer);
        s_ruuvi_scan_timeout_timer = NULL;
    }
    if (!s_overlay) return;

    if (stored == 0) {
        rtc_tab_set_status("Keine Ruuvi-Tags gefunden");
    } else {
        char buf[48];
        snprintf(buf, sizeof(buf), "%zu Tag%s gefunden", stored, stored == 1 ? "" : "s");
        rtc_tab_set_status(buf);
        show_ruuvi_scan_popup();
    }
}

static void ruuvi_scan_btn_cb(lv_event_t *e)
{
    if (s_ruuvi_scan_in_progress) {
        rtc_tab_set_status("Scan läuft bereits - bitte warten");
        return;
    }

    int target = (int)(intptr_t)lv_event_get_user_data(e);

    womo_sensor_data_t sd = {0};
    bool have_sd = womo_sensorboard_get_latest_data(&sd);
    if (ruuvi_wired_active(target == 0, &sd, have_sd)) {
        rtc_tab_set_status("Bus-Sensor hat Vorrang - kein Scan noetig");
        return;
    }

    s_ruuvi_scan_target = target;
    s_ruuvi_scan_in_progress = true;
    s_ruuvi_scan_result_count = 0;
    s_ruuvi_scan_retry = 0;
    rtc_tab_set_status("Suche Ruuvi-Tags...");

    if (s_ruuvi_scan_timeout_timer) {
        lv_timer_del(s_ruuvi_scan_timeout_timer);
        s_ruuvi_scan_timeout_timer = NULL;
    }
    s_ruuvi_scan_timeout_timer = lv_timer_create(ruuvi_scan_timeout_cb, 4000, NULL);
    if (!s_ruuvi_scan_timeout_timer) {
        // Absicherung: ohne Timer bliebe s_ruuvi_scan_in_progress für immer
        // haengen (Button reagiert dann dauerhaft nicht mehr, bis das Modal
        // neu geoeffnet wird). Lieber sofort mit Fehler abbrechen.
        s_ruuvi_scan_in_progress = false;
        rtc_tab_set_status("Scan-Timer konnte nicht angelegt werden");
        return;
    }
    lv_timer_set_repeat_count(s_ruuvi_scan_timeout_timer, 1);

    esp_err_t err = womo_sensorboard_send_ruuvi_scan_request();
    if (err != ESP_OK) {
        s_ruuvi_scan_in_progress = false;
        if (s_ruuvi_scan_timeout_timer) {
            lv_timer_del(s_ruuvi_scan_timeout_timer);
            s_ruuvi_scan_timeout_timer = NULL;
        }
        rtc_tab_set_status("Sensorboard nicht erreichbar");
    }
}

/* ── GasBee (BLE-Gaswaage): Hilfsfunktionen ─────────────────── */

/* Kabel-HX711 hat Vorrang vor den GasBees (Sensorboard meldet src "hx") –
 * Einrichten ist dann wirkungslos. */
static bool gasbee_wired_active(const womo_sensor_data_t *sd, bool have_sd)
{
    return have_sd && sd->hx711.valid && sd->hx711.is_wired;
}

static void gasbee_refresh_labels(void)
{
    womo_sensor_data_t sd = {0};
    bool have_sd = womo_sensorboard_get_latest_data(&sd);
    bool wired = gasbee_wired_active(&sd, have_sd);

    for (int i = 0; i < 2; i++) {
        if (s_gasbee_lbl[i]) {
            char buf[80];
            lv_color_t col = lv_color_hex(0x333333);
            if (wired) {
                snprintf(buf, sizeof(buf), "Kabelwaage aktiv (Vorrang)");
                col = lv_color_hex(0x888888);
            } else if (s_gasbee_mac[i][0] == '\0') {
                snprintf(buf, sizeof(buf), "nicht eingerichtet");
                col = lv_color_hex(0x888888);
            } else if (have_sd && sd.hx711.is_gasbee && sd.hx711.bee[i].present) {
                const __typeof__(sd.hx711.bee[0]) *b = &sd.hx711.bee[i];
                char batt[32] = "Akku ?";
                if (b->batt_valid) {
                    snprintf(batt, sizeof(batt), "Akku %u%%%s", b->batt_pct, b->charging ? " (laedt)" : "");
                }
                snprintf(buf, sizeof(buf), "Gaswaage  %s%s%s", batt,
                         b->calibrated ? "" : "  nicht kalibriert",
                         b->config_mode ? "  (USB)" : "");
                col = (!b->calibrated || (b->batt_valid && b->batt_pct < 20 && !b->charging))
                      ? lv_color_hex(0xC62828) : lv_color_hex(0x2E7D32);
            } else {
                snprintf(buf, sizeof(buf), "Gaswaage  (keine Daten)");
            }
            lv_obj_set_style_text_color(s_gasbee_lbl[i], col, 0);
            lv_label_set_text(s_gasbee_lbl[i], buf);
        }
        if (s_gasbee_scan_btn[i]) {
            if (wired) lv_obj_add_state(s_gasbee_scan_btn[i], LV_STATE_DISABLED);
            else       lv_obj_clear_state(s_gasbee_scan_btn[i], LV_STATE_DISABLED);
        }
    }
}

static void gasbee_apply_config(void)
{
    gasbee_cfg_save(s_gasbee_mac[0], s_gasbee_mac[1]);
    womo_sensorboard_send_gasbee_config(s_gasbee_mac[0], s_gasbee_mac[1]);
    gasbee_refresh_labels();
}

static void gasbee_clear_cb(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    s_gasbee_mac[slot][0] = '\0';
    gasbee_apply_config();
    rtc_tab_set_status("Gaswaage entfernt");
}

/* ── GasBee: Einrichten-Assistent ───────────────────────────────
 *
 * Der Nutzer tippt bei Flasche A/B auf "Einrichten" und steckt die Gaswaage an USB.
 * Nur eine Gaswaage an USB sendet mit Flag "Konfigurationsmodus" (Bit 3) – das
 * Sensorboard meldet auf gasbee_scan_request mit cfg_only nur diese. Dadurch ist
 * keine MAC-Auswahl nötig, auch nicht mit fremden Gaswaagen in Reichweite.
 * Danach optional Kalibrierung (Nullpunkt + Referenzgewicht) per Sensorboard/GATT.
 */

#define GBW_SEARCH_TIMEOUT_MS  60000   /* so lange auf eine Gaswaage an USB warten */
#define GBW_POLL_MS             2000   /* Abfrageintervall Scan (cfg_only) */
#define GBW_STEP_TIMEOUT_MS    25000   /* Antwort auf einen Kalibrierschritt */
#define GBW_VERIFY_TIMEOUT_MS  20000   /* Messung nach Kalibrierung abwarten */
#define GBW_REF_MIN_G            500
#define GBW_REF_MAX_G         200000
#define GBW_TOL_REL           0.01f    /* Kalibrierung gilt als gelungen bei ±1 % … */
#define GBW_TOL_ABS_KG        0.05f    /* … mindestens aber ±50 g */


static void gbw_render(void);

static void gbw_set_state(gbw_state_t st)
{
    s_gbw_state = st;
    s_gbw_t0 = lv_tick_get();
    s_gbw_last_poll = 0;
    gbw_render();
}

/* Asynchron löschen: wird auch aus dem Klick-Callback eines Popup-Buttons aufgerufen */
static void gbw_close_popup(void)
{
    if (s_gbw_kb) {
        lv_keyboard_set_textarea(s_gbw_kb, NULL);
        lv_obj_del_async(s_gbw_kb);
        s_gbw_kb = NULL;
    }
    if (s_gbw_popup) {
        lv_obj_del_async(s_gbw_popup);
        s_gbw_popup = NULL;
    }
    s_gbw_text = NULL;
    s_gbw_ta   = NULL;
}

/* Assistent beenden; eine laufende Kalibrier-Sitzung wird beim Sensorboard geschlossen */
static void gbw_finish(void)
{
    if (s_gbw_session) {
        womo_sensorboard_send_gasbee_cal_session(s_gbw_slot, false);
        s_gbw_session = false;
    }
    gbw_close_popup();
    s_gbw_state = GBW_OFF;
    gasbee_refresh_labels();
}

static const char *gbw_err_text(int err)
{
    switch (err) {
        case 0x03: case -2:
            return "Die Gaswaage ist nicht mehr im Einrichtungsmodus.\n"
                   "Bitte das USB-Kabel angesteckt lassen.";
        case 0x13:
            return "Die Gaswaage hat den Wert abgelehnt.";
        case -1:
            return "Ein anderer Schritt laeuft noch. Bitte kurz warten.";
        case -3: case -5: case -6:
            return "Keine Verbindung zur Gaswaage.\n"
                   "Bitte naeher an das Sensorboard bringen und wiederholen.";
        default:
            return "Der Schritt ist fehlgeschlagen.";
    }
}

static void gbw_start_cal(void)
{
    if (!s_gbw_session) {
        womo_sensorboard_send_gasbee_cal_session(s_gbw_slot, true);
        s_gbw_session = true;
    }
    gbw_set_state(GBW_ZERO);
}

static void gbw_send_zero(void)
{
    s_gbw_retry_state = GBW_ZERO;
    if (womo_sensorboard_send_gasbee_cal(s_gbw_cand.mac, true, 0) != ESP_OK) {
        snprintf(s_gbw_msg, sizeof(s_gbw_msg), "Sensorboard nicht erreichbar.");
        gbw_set_state(GBW_ERROR);
        return;
    }
    gbw_set_state(GBW_ZERO_BUSY);
}

static void gbw_send_ref(void)
{
    const char *txt = s_gbw_ta ? lv_textarea_get_text(s_gbw_ta) : "";
    long g = strtol(txt ? txt : "", NULL, 10);
    if (g < GBW_REF_MIN_G || g > GBW_REF_MAX_G) {
        if (s_gbw_text) {
            lv_label_set_text_fmt(s_gbw_text,
                "Schritt 2 von 2\nBitte ein Gewicht von %d bis %d Gramm eingeben.",
                GBW_REF_MIN_G, GBW_REF_MAX_G);
        }
        return;
    }
    s_gbw_ref_g = (uint32_t)g;
    s_gbw_seq_ref = s_gbw_cand.seq;
    s_gbw_retry_state = GBW_REF;
    if (womo_sensorboard_send_gasbee_cal(s_gbw_cand.mac, false, s_gbw_ref_g) != ESP_OK) {
        snprintf(s_gbw_msg, sizeof(s_gbw_msg), "Sensorboard nicht erreichbar.");
        gbw_set_state(GBW_ERROR);
        return;
    }
    gbw_set_state(GBW_REF_BUSY);
}

/* Gefundene Gaswaage der Flasche zuordnen (von der anderen Flasche ggf. entfernen) */
static void gbw_assign(void)
{
    int other = 1 - s_gbw_slot;
    if (strcmp(s_gasbee_mac[other], s_gbw_cand.mac) == 0) s_gasbee_mac[other][0] = '\0';
    snprintf(s_gasbee_mac[s_gbw_slot], sizeof(s_gasbee_mac[0]), "%s", s_gbw_cand.mac);
    gasbee_apply_config();
}

/* Button 1 (rechts, Hauptaktion) bzw. 2 (links, Abbrechen/Später) */
static void gbw_btn_cb(lv_event_t *e)
{
    int btn = (int)(intptr_t)lv_event_get_user_data(e);
    switch (s_gbw_state) {
        case GBW_SEARCH:
        case GBW_ZERO_BUSY:
        case GBW_REF_BUSY:
        case GBW_VERIFY:
            gbw_finish();                         /* einziger Button: Abbrechen */
            break;
        case GBW_NOT_FOUND:
            if (btn == 1) gbw_set_state(GBW_SEARCH);
            else          gbw_finish();
            break;
        case GBW_FOUND:
            if (btn == 1) {
                if (strcmp(s_gasbee_mac[s_gbw_slot], s_gbw_cand.mac) == 0) {
                    gbw_start_cal();              /* schon zugeordnet → Kalibrieren */
                } else {
                    gbw_assign();
                    gbw_set_state(GBW_CAL_ASK);
                }
            } else {
                gbw_finish();
            }
            break;
        case GBW_CAL_ASK:
            if (btn == 1) gbw_start_cal();
            else          gbw_set_state(GBW_DONE);
            break;
        case GBW_ZERO:
            if (btn == 1) gbw_send_zero();
            else          gbw_finish();
            break;
        case GBW_REF:
            if (btn == 1) gbw_send_ref();
            else          gbw_finish();
            break;
        case GBW_ERROR:
            if (btn == 1) gbw_set_state(s_gbw_retry_state);
            else          gbw_finish();
            break;
        case GBW_DONE:
        default:
            gbw_finish();
            break;
    }
}

static void gbw_kb_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY && s_gbw_state == GBW_REF) gbw_send_ref();
}

static lv_obj_t *gbw_add_btn(lv_obj_t *row, const char *text, int id, bool primary)
{
    lv_obj_t *b = lv_btn_create(row);
    lv_obj_set_size(b, 150, 40);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_bg_color(b, primary ? lv_color_hex(0x1565C0) : lv_color_hex(0x757575), 0);
    lv_obj_add_event_cb(b, gbw_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)id);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_center(l);
    return b;
}

/* Popup für den aktuellen Zustand neu aufbauen */
static void gbw_render(void)
{
    gbw_close_popup();
    if (s_gbw_state == GBW_OFF || !s_overlay) return;

    char text[320] = "";
    const char *b1 = NULL;   /* Hauptaktion (rechts) */
    const char *b2 = NULL;   /* Abbrechen / Später (links) */
    bool input = false;
    const char bottle = (char)('A' + s_gbw_slot);

    switch (s_gbw_state) {
        case GBW_SEARCH:
            snprintf(text, sizeof(text),
                     "Gaswaage per USB-Kabel mit Strom verbinden\n"
                     "(ein USB-Ladegeraet genuegt).\n\nSuche laeuft ...");
            b2 = "Abbrechen";
            break;
        case GBW_NOT_FOUND:
            snprintf(text, sizeof(text),
                     "Keine Gaswaage gefunden.\n\n"
                     "Bitte pruefen, ob das USB-Kabel steckt und das\n"
                     "Ladegeraet Strom hat.");
            b1 = "Nochmal"; b2 = "Schliessen";
            break;
        case GBW_FOUND: {
            char batt[24] = "";
            char weight[24] = "";
            if (s_gbw_cand.batt_valid) snprintf(batt, sizeof(batt), "Akku %u %%", s_gbw_cand.batt_pct);
            if (s_gbw_cand.weight_valid) snprintf(weight, sizeof(weight), "   %.1f kg", (double)s_gbw_cand.weight_kg);
            int other = 1 - s_gbw_slot;
            if (strcmp(s_gasbee_mac[s_gbw_slot], s_gbw_cand.mac) == 0) {
                snprintf(text, sizeof(text), "Gaswaage gefunden: %s%s\n\n"
                         "Diese Gaswaage ist bereits Flasche %c.", batt, weight, bottle);
                b1 = "Kalibrieren"; b2 = "Fertig";
            } else if (strcmp(s_gasbee_mac[other], s_gbw_cand.mac) == 0) {
                snprintf(text, sizeof(text), "Gaswaage gefunden: %s%s\n\n"
                         "Sie ist bisher Flasche %c und wird jetzt Flasche %c.",
                         batt, weight, (char)('A' + other), bottle);
                b1 = "Uebernehmen"; b2 = "Abbrechen";
            } else {
                snprintf(text, sizeof(text), "Gaswaage gefunden: %s%s\n\n"
                         "Als Flasche %c uebernehmen?", batt, weight, bottle);
                b1 = "Uebernehmen"; b2 = "Abbrechen";
            }
            break;
        }
        case GBW_CAL_ASK:
            if (s_gbw_cand.calibrated) {
                snprintf(text, sizeof(text), "Die Gaswaage ist jetzt Flasche %c.\n\n"
                         "Sie ist bereits kalibriert. Neu kalibrieren?", bottle);
                b1 = "Kalibrieren"; b2 = "Fertig";
            } else {
                snprintf(text, sizeof(text), "Die Gaswaage ist jetzt Flasche %c.\n\n"
                         "Sie ist noch nicht kalibriert und zeigt bis dahin\n"
                         "keinen Gasstand. Dafuer wird ein Gegenstand mit\n"
                         "bekanntem Gewicht benoetigt.", bottle);
                b1 = "Kalibrieren"; b2 = "Spaeter";
            }
            break;
        case GBW_ZERO:
            snprintf(text, sizeof(text), "Schritt 1 von 2\n\n"
                     "Waage leeren: keine Flasche, nichts auflegen.\n"
                     "Dann auf Weiter tippen.");
            b1 = "Weiter"; b2 = "Abbrechen";
            break;
        case GBW_ZERO_BUSY:
            snprintf(text, sizeof(text), "Schritt 1 von 2\n\nNullpunkt wird gemessen ...");
            b2 = "Abbrechen";
            break;
        case GBW_REF:
            snprintf(text, sizeof(text), "Schritt 2 von 2\n"
                     "Gegenstand mit bekanntem Gewicht auflegen\n"
                     "(z. B. Wasserkanister) und Gewicht in Gramm eingeben:");
            b1 = "Weiter"; b2 = "Abbrechen";
            input = true;
            break;
        case GBW_REF_BUSY:
            snprintf(text, sizeof(text), "Schritt 2 von 2\n\nGewicht wird gemessen ...");
            b2 = "Abbrechen";
            break;
        case GBW_VERIFY:
            snprintf(text, sizeof(text), "Kalibrierung wird geprueft ...");
            b2 = "Abbrechen";
            break;
        case GBW_ERROR:
            snprintf(text, sizeof(text), "%s", s_gbw_msg);
            b1 = "Wiederholen"; b2 = "Abbrechen";
            break;
        case GBW_DONE:
            snprintf(text, sizeof(text), "%s%sFertig.\nDas USB-Kabel kann jetzt abgezogen werden.",
                     s_gbw_msg, s_gbw_msg[0] ? "\n\n" : "");
            b1 = "OK";
            break;
        default:
            break;
    }

    s_gbw_popup = lv_obj_create(s_overlay);
    lv_obj_set_size(s_gbw_popup, 480, input ? 230 : 260);
    if (input) lv_obj_align(s_gbw_popup, LV_ALIGN_TOP_MID, 0, 10);
    else       lv_obj_center(s_gbw_popup);
    lv_obj_set_style_bg_color(s_gbw_popup, lv_color_white(), 0);
    lv_obj_set_style_radius(s_gbw_popup, 8, 0);
    lv_obj_set_style_pad_all(s_gbw_popup, 14, 0);
    lv_obj_set_style_shadow_width(s_gbw_popup, 20, 0);
    lv_obj_set_style_shadow_opa(s_gbw_popup, LV_OPA_30, 0);
    lv_obj_set_flex_flow(s_gbw_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_gbw_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(s_gbw_popup);

    lv_obj_t *title = lv_label_create(s_gbw_popup);
    lv_label_set_text_fmt(title, "Gaswaage einrichten (Flasche %c)", bottle);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    s_gbw_text = lv_label_create(s_gbw_popup);
    lv_label_set_text(s_gbw_text, text);
    lv_obj_set_width(s_gbw_text, LV_PCT(100));
    lv_obj_set_style_text_font(s_gbw_text, &lv_font_montserrat_14, 0);
    lv_obj_set_flex_grow(s_gbw_text, 1);

    if (input) {
        s_gbw_ta = lv_textarea_create(s_gbw_popup);
        lv_textarea_set_one_line(s_gbw_ta, true);
        lv_textarea_set_accepted_chars(s_gbw_ta, "0123456789");
        lv_textarea_set_max_length(s_gbw_ta, 6);
        lv_textarea_set_placeholder_text(s_gbw_ta, "Gramm, z. B. 10000");
        lv_obj_set_width(s_gbw_ta, 220);
        if (s_gbw_ref_g) {
            char g[12];
            snprintf(g, sizeof(g), "%lu", (unsigned long)s_gbw_ref_g);
            lv_textarea_set_text(s_gbw_ta, g);
        }

        s_gbw_kb = lv_keyboard_create(s_overlay);
        lv_keyboard_set_mode(s_gbw_kb, LV_KEYBOARD_MODE_NUMBER);
        lv_obj_set_size(s_gbw_kb, LV_PCT(70), 180);
        lv_obj_align(s_gbw_kb, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_keyboard_set_textarea(s_gbw_kb, s_gbw_ta);
        lv_obj_add_event_cb(s_gbw_kb, gbw_kb_cb, LV_EVENT_READY, NULL);
    }

    lv_obj_t *row = lv_obj_create(s_gbw_popup);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (b2) gbw_add_btn(row, b2, 2, false);
    else    { lv_obj_t *sp = lv_obj_create(row); lv_obj_remove_style_all(sp); lv_obj_set_size(sp, 1, 1); }
    if (b1) gbw_add_btn(row, b1, 1, true);
}

/* LVGL-Task: Scan-Ergebnis (cfg_only) für den Assistenten auswerten */
static void gbw_on_scan(const womo_gasbee_scan_entry_t *entries, size_t count)
{
    if (s_gbw_state == GBW_SEARCH) {
        size_t usb = 0;
        const womo_gasbee_scan_entry_t *hit = NULL;
        for (size_t i = 0; i < count; i++) {
            if (entries[i].config_mode) { usb++; hit = &entries[i]; }
        }
        if (usb == 1) {
            s_gbw_cand = *hit;
            gbw_set_state(GBW_FOUND);
        } else if (usb > 1 && s_gbw_text) {
            lv_label_set_text(s_gbw_text,
                "Mehrere Gaswaagen sind gerade an USB angesteckt.\n\n"
                "Bitte nur die Gaswaage fuer diese Flasche anstecken.\n"
                "Suche laeuft ...");
        }
    } else if (s_gbw_state == GBW_ZERO || s_gbw_state == GBW_REF) {
        /* Werte der Gaswaage aktuell halten (bee_seq als Bezug für die Prüfung) */
        for (size_t i = 0; i < count; i++) {
            if (strcmp(entries[i].mac, s_gbw_cand.mac) == 0) { s_gbw_cand = entries[i]; break; }
        }
    } else if (s_gbw_state == GBW_VERIFY) {
        for (size_t i = 0; i < count; i++) {
            if (strcmp(entries[i].mac, s_gbw_cand.mac) != 0) continue;
            s_gbw_cand = entries[i];
            /* Mindestens 2 Messzyklen nach dem letzten bekannten Stand: eine Messung
             * zwischen letzter Abfrage und Write könnte sonst noch unkalibriert sein */
            uint8_t seq_diff = (uint8_t)(entries[i].seq - s_gbw_seq_ref);
            if (!entries[i].calibrated || seq_diff < 2 || !entries[i].weight_valid) break;
            float want = (float)s_gbw_ref_g / 1000.0f;
            float got  = entries[i].weight_kg;
            float tol  = want * GBW_TOL_REL;
            if (tol < GBW_TOL_ABS_KG) tol = GBW_TOL_ABS_KG;
            if (fabsf(got - want) <= tol) {
                snprintf(s_gbw_msg, sizeof(s_gbw_msg),
                         "Kalibrierung erfolgreich.\nGemessen: %.2f kg (eingegeben %.2f kg).",
                         (double)got, (double)want);
                if (s_gbw_session) {
                    womo_sensorboard_send_gasbee_cal_session(s_gbw_slot, false);
                    s_gbw_session = false;
                }
                gbw_set_state(GBW_DONE);
            } else {
                snprintf(s_gbw_msg, sizeof(s_gbw_msg),
                         "Die Waage misst %.2f kg statt %.2f kg.\n"
                         "Bitte Gewicht pruefen und die Kalibrierung wiederholen.",
                         (double)got, (double)want);
                s_gbw_retry_state = GBW_ZERO;
                gbw_set_state(GBW_ERROR);
            }
            break;
        }
    }
}

/* LVGL-Task: Ergebnis eines Kalibrierschritts */
static void gbw_on_cal_result(bool zero_step, bool ok, int err)
{
    if (zero_step && s_gbw_state == GBW_ZERO_BUSY) {
        if (ok) {
            gbw_set_state(GBW_REF);
        } else {
            snprintf(s_gbw_msg, sizeof(s_gbw_msg), "Nullpunkt nicht gespeichert.\n%s", gbw_err_text(err));
            gbw_set_state(GBW_ERROR);
        }
    } else if (!zero_step && s_gbw_state == GBW_REF_BUSY) {
        if (ok) {
            gbw_set_state(GBW_VERIFY);
        } else {
            snprintf(s_gbw_msg, sizeof(s_gbw_msg), "Gewicht nicht gespeichert.\n%s", gbw_err_text(err));
            gbw_set_state(GBW_ERROR);
        }
    }
}

/* LVGL-Task, alle 100 ms: Abholfach leeren, Zeitabläufe des Assistenten */
static void gbw_tick(void)
{
    uint32_t in_state = lv_tick_elaps(s_gbw_t0);
    bool polling = (s_gbw_state == GBW_SEARCH || s_gbw_state == GBW_ZERO ||
                    s_gbw_state == GBW_REF || s_gbw_state == GBW_VERIFY);

    if (polling && (s_gbw_last_poll == 0 || lv_tick_elaps(s_gbw_last_poll) >= GBW_POLL_MS)) {
        s_gbw_last_poll = lv_tick_get();
        if (s_gbw_last_poll == 0) s_gbw_last_poll = 1;
        womo_sensorboard_send_gasbee_setup_scan();
    }

    switch (s_gbw_state) {
        case GBW_SEARCH:
            if (in_state > GBW_SEARCH_TIMEOUT_MS) gbw_set_state(GBW_NOT_FOUND);
            break;
        case GBW_ZERO_BUSY:
        case GBW_REF_BUSY:
            if (in_state > GBW_STEP_TIMEOUT_MS) {
                snprintf(s_gbw_msg, sizeof(s_gbw_msg), "Keine Antwort vom Sensorboard.");
                gbw_set_state(GBW_ERROR);
            }
            break;
        case GBW_VERIFY:
            if (in_state > GBW_VERIFY_TIMEOUT_MS) {
                snprintf(s_gbw_msg, sizeof(s_gbw_msg),
                         "Die Gaswaage hat die Kalibrierung nicht bestaetigt.\n"
                         "Bitte USB angesteckt lassen und wiederholen.");
                s_gbw_retry_state = GBW_ZERO;
                gbw_set_state(GBW_ERROR);
            }
            break;
        default:
            break;
    }
}

/* Läuft im ESP-NOW-RX- bzw. esp_timer-Task: nur ins Abholfach legen */
static void gasbee_scan_result_cb(const womo_gasbee_scan_entry_t *entries, size_t count, void *user_data)
{
    (void)user_data;
    if (count > WOMO_GASBEE_SCAN_MAX_TAGS) count = WOMO_GASBEE_SCAN_MAX_TAGS;
    taskENTER_CRITICAL(&s_inbox_lock);
    memcpy(s_gasbee_inbox, entries, count * sizeof(entries[0]));
    s_gasbee_inbox_count = count;
    s_gasbee_inbox_ready = true;
    taskEXIT_CRITICAL(&s_inbox_lock);
}

static void gasbee_cal_result_cb(bool zero_step, bool ok, int err, const char *msg, void *user_data)
{
    (void)msg; (void)user_data;
    taskENTER_CRITICAL(&s_inbox_lock);
    s_gbcal_inbox_zero  = zero_step;
    s_gbcal_inbox_ok    = ok;
    s_gbcal_inbox_err   = err;
    s_gbcal_inbox_ready = true;
    taskEXIT_CRITICAL(&s_inbox_lock);
}

/* LVGL-Timer (100 ms) des Tabs "RTC / Temp / Gas": Ergebnisse abholen, Assistent takten */
static void rtc_poll_timer_cb(lv_timer_t *t)
{
    (void)t;
    static womo_ruuvi_scan_entry_t  ruuvi[WOMO_RUUVI_SCAN_MAX_TAGS];
    static womo_gasbee_scan_entry_t bees[WOMO_GASBEE_SCAN_MAX_TAGS];
    static uint32_t last_label_refresh = 0;
    size_t n_ruuvi = 0, n_bees = 0;
    bool have_ruuvi, have_bees, have_cal, cal_zero = false, cal_ok = false;
    int cal_err = 0;

    taskENTER_CRITICAL(&s_inbox_lock);
    have_ruuvi = s_ruuvi_inbox_ready;
    if (have_ruuvi) {
        n_ruuvi = s_ruuvi_inbox_count;
        memcpy(ruuvi, s_ruuvi_inbox, n_ruuvi * sizeof(ruuvi[0]));
        s_ruuvi_inbox_ready = false;
    }
    have_bees = s_gasbee_inbox_ready;
    if (have_bees) {
        n_bees = s_gasbee_inbox_count;
        memcpy(bees, s_gasbee_inbox, n_bees * sizeof(bees[0]));
        s_gasbee_inbox_ready = false;
    }
    have_cal = s_gbcal_inbox_ready;
    if (have_cal) {
        cal_zero = s_gbcal_inbox_zero;
        cal_ok   = s_gbcal_inbox_ok;
        cal_err  = s_gbcal_inbox_err;
        s_gbcal_inbox_ready = false;
    }
    taskEXIT_CRITICAL(&s_inbox_lock);

    if (have_ruuvi) ruuvi_scan_process(ruuvi, n_ruuvi);
    if (have_bees)  gbw_on_scan(bees, n_bees);
    if (have_cal)   gbw_on_cal_result(cal_zero, cal_ok, cal_err);
    if (s_gbw_state != GBW_OFF) gbw_tick();

    if (lv_tick_elaps(last_label_refresh) >= 2000) {
        last_label_refresh = lv_tick_get();
        gasbee_refresh_labels();
    }
}

static void gasbee_scan_btn_cb(lv_event_t *e)
{
    womo_sensor_data_t sd = {0};
    bool have_sd = womo_sensorboard_get_latest_data(&sd);
    if (gasbee_wired_active(&sd, have_sd)) {
        rtc_tab_set_status("Kabelwaage hat Vorrang - Einrichten nicht noetig");
        return;
    }
    if (s_gbw_state != GBW_OFF) return;

    s_gbw_slot = (int)(intptr_t)lv_event_get_user_data(e);
    memset(&s_gbw_cand, 0, sizeof(s_gbw_cand));
    s_gbw_ref_g = 0;
    s_gbw_msg[0] = '\0';
    rtc_tab_set_status("");
    gbw_set_state(GBW_SEARCH);
}

/* Eine Konfigurationszeile: Label, MAC/Status, Aktions-Button (btn_text), Löschen-Button */
static lv_obj_t *make_cfg_row(lv_obj_t *tab, int y, womo_string_id_t name, lv_obj_t **name_out,
                              lv_obj_t **val_lbl, womo_string_id_t btn_text, int btn_w,
                              lv_event_cb_t scan_cb, lv_event_cb_t clear_cb, void *user_data)
{
    lv_obj_t *name_lbl = lv_label_create(tab);
    lv_label_set_text(name_lbl, womo_locale_get_string(name));
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_12_german, 0);
    if (name_out) *name_out = name_lbl;
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(0x333333), 0);
    lv_obj_set_pos(name_lbl, 0, y);

    *val_lbl = lv_label_create(tab);
    lv_obj_set_style_text_font(*val_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(*val_lbl, 64, y);
    lv_obj_set_width(*val_lbl, 330);

    lv_obj_t *scan_btn = lv_btn_create(tab);
    lv_obj_set_size(scan_btn, btn_w, 26);
    lv_obj_set_pos(scan_btn, 460 - btn_w, y - 6);
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_radius(scan_btn, 6, 0);
    lv_obj_set_style_border_width(scan_btn, 0, 0);
    lv_obj_add_event_cb(scan_btn, scan_cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, womo_locale_get_string(btn_text));
    lv_obj_set_style_text_font(scan_lbl, &lv_font_montserrat_12_german, 0);
    lv_obj_center(scan_lbl);

    lv_obj_t *clear_btn = lv_btn_create(tab);
    lv_obj_set_size(clear_btn, 28, 26);
    lv_obj_set_pos(clear_btn, 468, y - 6);
    lv_obj_set_style_bg_color(clear_btn, lv_color_hex(0x757575), 0);
    lv_obj_set_style_radius(clear_btn, 6, 0);
    lv_obj_set_style_border_width(clear_btn, 0, 0);
    lv_obj_add_event_cb(clear_btn, clear_cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *clear_lbl = lv_label_create(clear_btn);
    lv_label_set_text(clear_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(clear_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(clear_lbl);

    return scan_btn;
}

/* Zeit-/RTC-Info im System-Tab (Text beim Öffnen und bei Sprachwechsel neu setzen) */
static void system_time_info_refresh(void)
{
    if (!s_time_info_lbl) return;

    const char *src_name;
    switch (womo_time_get_source()) {
        case TIME_SOURCE_NTP:          src_name = "NTP (Internet)";       break;
        case TIME_SOURCE_GPS:          src_name = "GPS (Router)";         break;
        case TIME_SOURCE_SENSOR:       src_name = "Sensorboard ESP-NOW";  break;
        case TIME_SOURCE_INTERNAL_RTC: src_name = womo_locale_get_string(STR_TIME_SRC_RTC); break;
        default:                       src_name = "---"; break;
    }

    char sync_buf[48];
    if (womo_time_is_synced()) {
        uint32_t secs = womo_time_get_seconds_since_sync();
        if (secs < 60)
            snprintf(sync_buf, sizeof(sync_buf), womo_locale_get_string(STR_TIME_AGO_S),   (unsigned long)secs);
        else if (secs < 3600)
            snprintf(sync_buf, sizeof(sync_buf), womo_locale_get_string(STR_TIME_AGO_MIN), (unsigned long)(secs / 60));
        else
            snprintf(sync_buf, sizeof(sync_buf), womo_locale_get_string(STR_TIME_AGO_H),   (unsigned long)(secs / 3600));
    } else {
        snprintf(sync_buf, sizeof(sync_buf), "%s", womo_locale_get_string(STR_TIME_SYNC_NEVER));
    }

    extern void womo_get_rtc_battery_status(bool *valid, bool *low, bool *switched);
    bool rtc_valid = false, rtc_low = false, rtc_sw = false;
    womo_get_rtc_battery_status(&rtc_valid, &rtc_low, &rtc_sw);

    const char *rtc_bat = !rtc_valid ? womo_locale_get_string(STR_TIME_BATT_UNKNOWN)
                        : rtc_low    ? womo_locale_get_string(STR_TIME_BATT_LOW)
                        :              womo_locale_get_string(STR_STATUS_OK);

    char text[300];
    int n = snprintf(text, sizeof(text), "%s\n  %s\n%s\n  %s\n%s\n  %s",
                     womo_locale_get_string(STR_TIME_SOURCE),    src_name,
                     womo_locale_get_string(STR_TIME_LAST_SYNC), sync_buf,
                     womo_locale_get_string(STR_TIME_RTC_BATT),  rtc_bat);
    if (rtc_valid && rtc_sw && n > 0 && n < (int)sizeof(text) - 2) {
        snprintf(text + n, sizeof(text) - n, "\n%s", womo_locale_get_string(STR_TIME_RTC_POWER_LOST));
    }
    lv_label_set_text(s_time_info_lbl, text);
}

static void build_tab_sensoren(lv_obj_t *tab)
{
    make_tab_base(tab);

    /* Layout (Tab nicht scrollbar, ~300 px nutzbar): Ruuvi ab y 0, GasBee ab y 106,
     * gemeinsame Statuszeile y 212 – Rest frei für weitere Sensoren. */

    /* Ruuvi-Tag: MAC per Scan auswählen, Batteriespannung anzeigen (nur belegt,
     * wenn statt eines verkabelten BME280/680 ein Ruuvi-BLE-Tag im Einsatz ist) */
    ruuvi_cfg_load(s_ruuvi_indoor_mac, sizeof(s_ruuvi_indoor_mac),
                   s_ruuvi_outdoor_mac, sizeof(s_ruuvi_outdoor_mac));

    s_ruuvi_title_lbl = make_section_title(tab, 0, 0, MODAL_W - 2 * PAD, STR_SENS_RUUVI_SECTION);

    s_ruuvi_scan_btn_in  = make_cfg_row(tab, 44, STR_SENS_INDOOR, &s_sens_row_lbl[0],
                                        &s_ruuvi_indoor_lbl, STR_SENS_SCAN_BTN, 60,
                                        ruuvi_scan_btn_cb, ruuvi_clear_indoor_cb, (void *)0);
    s_ruuvi_scan_btn_out = make_cfg_row(tab, 74, STR_SENS_OUTDOOR, &s_sens_row_lbl[1],
                                        &s_ruuvi_outdoor_lbl, STR_SENS_SCAN_BTN, 60,
                                        ruuvi_scan_btn_cb, ruuvi_clear_outdoor_cb, (void *)1);

    /* GasBee: BLE-Gaswaage je Flasche per Einrichten-Assistent (USB) zuordnen und
     * kalibrieren, Akku anzeigen (nur belegt, wenn statt der Kabelwaage (HX711)
     * GasBees im Einsatz sind) */
    gasbee_cfg_load(s_gasbee_mac[0], sizeof(s_gasbee_mac[0]),
                    s_gasbee_mac[1], sizeof(s_gasbee_mac[1]));

    s_gasbee_title_lbl = make_section_title(tab, 0, 106, MODAL_W - 2 * PAD, STR_SENS_GASBEE_SECTION);

    s_gasbee_scan_btn[0] = make_cfg_row(tab, 150, STR_SENS_BOTTLE_A, &s_sens_row_lbl[2],
                                        &s_gasbee_lbl[0], STR_SENS_SETUP_BTN, 90,
                                        gasbee_scan_btn_cb, gasbee_clear_cb, (void *)0);
    s_gasbee_scan_btn[1] = make_cfg_row(tab, 180, STR_SENS_BOTTLE_B, &s_sens_row_lbl[3],
                                        &s_gasbee_lbl[1], STR_SENS_SETUP_BTN, 90,
                                        gasbee_scan_btn_cb, gasbee_clear_cb, (void *)1);

    /* Status-/Scan-Feedback (gemeinsam für Ruuvi und GasBee) */
    s_ruuvi_status_lbl = lv_label_create(tab);
    lv_label_set_text(s_ruuvi_status_lbl, "");
    lv_obj_set_style_text_font(s_ruuvi_status_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_ruuvi_status_lbl, lv_color_hex(0x1565C0), 0);
    lv_obj_set_pos(s_ruuvi_status_lbl, 0, 212);
    lv_obj_set_width(s_ruuvi_status_lbl, MODAL_W - 2 * PAD);

    ruuvi_refresh_labels();
    gasbee_refresh_labels();
    womo_sensorboard_set_ruuvi_scan_callback(ruuvi_scan_result_cb, NULL);
    womo_sensorboard_set_gasbee_scan_callback(gasbee_scan_result_cb, NULL);
    womo_sensorboard_set_gasbee_cal_result_callback(gasbee_cal_result_cb, NULL);
    if (!s_rtc_poll_timer) s_rtc_poll_timer = lv_timer_create(rtc_poll_timer_cb, 100, NULL);
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

/* ── LTE-Tab: Mobilfunk-Tarif ───────────────────────────────── */

enum { LTE_ROW_LEAD = 0, LTE_ROW_VOLUME, LTE_ROW_MANUAL };
#define LTE_STEP_PLUS  0x10   /* user_data: Zeile | LTE_STEP_PLUS */

static void lte_fmt_date(uint32_t d, char *buf, size_t len)
{
    if (d) {
        snprintf(buf, len, "%02u.%02u.%04u", (unsigned)(d % 100),
                 (unsigned)(d / 100 % 100), (unsigned)(d / 10000));
    } else {
        snprintf(buf, len, "–");
    }
}

static womo_string_id_t lte_period_str(uint8_t period)
{
    switch (period) {
        case WOMO_TARIFF_PERIOD_30D:   return STR_TARIFF_PERIOD_30D;
        case WOMO_TARIFF_PERIOD_MONTH: return STR_TARIFF_PERIOD_MONTH;
        default:                       return STR_TARIFF_PERIOD_4W;
    }
}

/* Heutiges Datum, nur mit plausibler Uhrzeit (wie die Auswertung in main.c) */
static bool lte_get_today(struct tm *tm, uint32_t *today)
{
    if (womo_time_get(tm) != ESP_OK || tm->tm_year < (2024 - 1900)) return false;
    *today = womo_tariff_date_from_tm(tm);
    return true;
}

static void lte_tab_refresh(void)
{
    if (!s_lte.val[0]) return;

    womo_tariff_cfg_t cfg;
    womo_tariff_get_cfg(&cfg);
    char buf[96];
    char date[16];

    if (cfg.lead_days) {
        snprintf(buf, sizeof(buf), womo_locale_get_string(STR_TARIFF_DAYS_FMT), (unsigned)cfg.lead_days);
    } else {
        snprintf(buf, sizeof(buf), "%s", womo_locale_get_string(STR_OFF));
    }
    lv_label_set_text(s_lte.val[0], buf);

    if (cfg.volume_pct) {
        snprintf(buf, sizeof(buf), "%u %%", (unsigned)cfg.volume_pct);
    } else {
        snprintf(buf, sizeof(buf), "%s", womo_locale_get_string(STR_OFF));
    }
    lv_label_set_text(s_lte.val[1], buf);

    lte_fmt_date(cfg.manual_due, date, sizeof(date));
    lv_label_set_text(s_lte.val[2], date);
    set_btn_str(s_lte.period_btn, lte_period_str(cfg.period));

    const uint32_t auto_due = womo_tariff_get_auto_due();
    if (auto_due) {
        lte_fmt_date(auto_due, date, sizeof(date));
        snprintf(buf, sizeof(buf), womo_locale_get_string(STR_TARIFF_AUTO_FMT), date);
    } else {
        snprintf(buf, sizeof(buf), "%s", womo_locale_get_string(STR_TARIFF_AUTO_NONE));
    }
    lv_label_set_text(s_lte.auto_lbl, buf);

    struct tm tm;
    uint32_t today;
    womo_tariff_state_t st;
    if (lte_get_today(&tm, &today) && womo_tariff_evaluate(&tm, &st) && st.due) {
        lte_fmt_date(st.due, date, sizeof(date));
        if (st.days_left < 0) {
            snprintf(buf, sizeof(buf), womo_locale_get_string(STR_TARIFF_NEXT_EXPIRED), date);
        } else {
            snprintf(buf, sizeof(buf), womo_locale_get_string(STR_TARIFF_NEXT_FMT), date, st.days_left);
        }
    } else {
        snprintf(buf, sizeof(buf), "%s", womo_locale_get_string(STR_TARIFF_NEXT_NONE));
    }
    lv_label_set_text(s_lte.next_lbl, buf);
}

static void lte_step_cb(lv_event_t *e)
{
    const int code = (int)(intptr_t)lv_event_get_user_data(e);
    const int dir  = (code & LTE_STEP_PLUS) ? 1 : -1;
    womo_tariff_cfg_t cfg;
    womo_tariff_get_cfg(&cfg);

    switch (code & 0x0F) {
        case LTE_ROW_LEAD: {
            int v = (int)cfg.lead_days + dir;
            if (v < 0) v = 0;
            if (v > TARIFF_LEAD_DAYS_MAX) v = TARIFF_LEAD_DAYS_MAX;
            cfg.lead_days = (uint8_t)v;
            break;
        }
        case LTE_ROW_VOLUME: {
            int v = (int)cfg.volume_pct + 5 * dir;
            if (v < 0) v = 0;
            if (v > TARIFF_VOLUME_PCT_MAX) v = TARIFF_VOLUME_PCT_MAX;
            cfg.volume_pct = (uint8_t)v;
            break;
        }
        case LTE_ROW_MANUAL:
            if (cfg.manual_due) {
                cfg.manual_due = womo_tariff_add_days(cfg.manual_due, dir);
            } else {
                /* Noch kein Termin: bei heute beginnen */
                struct tm tm;
                if (!lte_get_today(&tm, &cfg.manual_due)) return;
            }
            break;
        default:
            return;
    }
    womo_tariff_set_cfg(&cfg);
    lte_tab_refresh();
}

static void lte_period_cb(lv_event_t *e)
{
    (void)e;
    womo_tariff_cfg_t cfg;
    womo_tariff_get_cfg(&cfg);
    cfg.period = (uint8_t)((cfg.period + 1) % WOMO_TARIFF_PERIOD_COUNT);
    womo_tariff_set_cfg(&cfg);
    lte_tab_refresh();
}

/* "Heute aufgeladen": nächster Termin = heute + Rhythmus */
static void lte_today_cb(lv_event_t *e)
{
    (void)e;
    struct tm tm;
    uint32_t today;
    if (!lte_get_today(&tm, &today)) return;
    womo_tariff_cfg_t cfg;
    womo_tariff_get_cfg(&cfg);
    cfg.manual_due = womo_tariff_add_period(today, cfg.period, +1);
    womo_tariff_set_cfg(&cfg);
    lte_tab_refresh();
}

/* user_data 0: manuellen Termin löschen, 1: automatischen Termin verwerfen
 * (z.B. nach Tarifwechsel; kommt beim nächsten Router-Poll wieder, falls vorhanden) */
static void lte_clear_cb(lv_event_t *e)
{
    if ((intptr_t)lv_event_get_user_data(e) == 1) {
        womo_tariff_set_auto_due(0);
    } else {
        womo_tariff_cfg_t cfg;
        womo_tariff_get_cfg(&cfg);
        cfg.manual_due = 0;
        womo_tariff_set_cfg(&cfg);
    }
    lte_tab_refresh();
}

static lv_obj_t *lte_make_btn(lv_obj_t *tab, int x, int y, int w, uint32_t color,
                              const char *text, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_btn_create(tab);
    lv_obj_set_size(btn, w, 30);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14_german, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
    return btn;
}

/* −/+/✕-Taste im Stil der Grenzwert-Zeilen (make_thresh_row): 26×26, Symbol in Standardschrift */
static lv_obj_t *lte_make_sym_btn(lv_obj_t *tab, int x, int y, uint32_t color,
                                  const char *symbol, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_btn_create(tab);
    lv_obj_set_size(btn, 26, 26);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
    return btn;
}

static lv_obj_t *lte_make_label(lv_obj_t *tab, int x, int y, int w, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(tab);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14_german, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_pos(lbl, x, y);
    if (w > 0) lv_obj_set_width(lbl, w);
    return lbl;
}

/* Zeile: Name | − Wert + */
static void lte_make_step_row(lv_obj_t *tab, int y, int row, womo_string_id_t name)
{
    enum { xMINUS = 210, xVAL = 238, wVAL = 110, xPLUS = 350 };
    s_lte.name[row] = lte_make_label(tab, 0, y + 6, 200, 0x222222);
    lv_label_set_text(s_lte.name[row], womo_locale_get_string(name));
    lte_make_sym_btn(tab, xMINUS, y + 2, 0x757575, LV_SYMBOL_MINUS, lte_step_cb, (void *)(intptr_t)row);
    s_lte.val[row] = lte_make_label(tab, xVAL, y + 6, wVAL, 0x1565C0);
    lv_obj_set_style_text_align(s_lte.val[row], LV_TEXT_ALIGN_CENTER, 0);
    lte_make_sym_btn(tab, xPLUS, y + 2, 0x2E7D32, LV_SYMBOL_PLUS, lte_step_cb,
                     (void *)(intptr_t)(row | LTE_STEP_PLUS));
}

static void build_tab_lte(lv_obj_t *tab)
{
    make_tab_base(tab);
    const int w = MODAL_W - 2 * PAD;

    s_lte.title = make_section_title(tab, 0, 0, w, STR_TARIFF_SECTION);
    lte_make_step_row(tab, 40,  LTE_ROW_LEAD,   STR_TARIFF_LEAD);
    lte_make_step_row(tab, 80,  LTE_ROW_VOLUME, STR_TARIFF_VOLUME);
    lte_make_step_row(tab, 120, LTE_ROW_MANUAL, STR_TARIFF_MANUAL);
    lte_make_sym_btn(tab, 396, 122, 0x757575, LV_SYMBOL_CLOSE, lte_clear_cb, (void *)(intptr_t)0);
    s_lte.today_btn = lte_make_btn(tab, 436, 120, w - 436, 0x1565C0,
                                   womo_locale_get_string(STR_TARIFF_TODAY_BTN), lte_today_cb, NULL);

    s_lte.name[3] = lte_make_label(tab, 0, 166, 200, 0x222222);
    lv_label_set_text(s_lte.name[3], womo_locale_get_string(STR_TARIFF_PERIOD));
    s_lte.period_btn = lte_make_btn(tab, 210, 160, 178, 0x1565C0, "", lte_period_cb, NULL);

    make_section_sep_at(tab, 0, 206, w);
    s_lte.auto_lbl = lte_make_label(tab, 0, 220, w - 44, 0x444444);
    lte_make_sym_btn(tab, w - 26, 216, 0x757575, LV_SYMBOL_CLOSE, lte_clear_cb, (void *)(intptr_t)1);
    s_lte.next_lbl = lte_make_label(tab, 0, 256, w, 0x222222);
    lv_obj_set_style_text_font(s_lte.next_lbl, &lv_font_montserrat_16_german, 0);

    lte_tab_refresh();
}

/* System-Tab, zwei Spalten: links Töne + Sprache, rechts Zeit / RTC */
#define SYS_COL_W   290
#define SYS_COL2_X  (MODAL_W - 2 * PAD - SYS_COL_W)

static lv_obj_t *make_sound_row(lv_obj_t *tab, int y, womo_string_id_t name,
                                lv_obj_t **name_out, bool on, lv_event_cb_t cb)
{
    lv_obj_t *lbl = lv_label_create(tab);
    lv_label_set_text(lbl, womo_locale_get_string(name));
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14_german, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x333333), 0);
    lv_obj_set_pos(lbl, 0, y + 4);
    *name_out = lbl;

    lv_obj_t *btn = lv_btn_create(tab);
    lv_obj_set_size(btn, 64, 30);
    lv_obj_set_pos(btn, 164, y);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_lbl);
    sound_btn_update(btn, on);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

static void build_tab_system(lv_obj_t *tab)
{
    make_tab_base(tab);

    /* Linke Spalte: Töne (y 0) + Sprache (y 134) */
    s_sound_title_lbl = make_section_title(tab, 0, 0, SYS_COL_W, STR_SOUND_SECTION);
    s_btn_tones = make_sound_row(tab, 40, STR_SOUND_TONES, &s_tones_lbl,
                                 display_buzzer_is_enabled(), sound_tones_cb);
    s_btn_touch = make_sound_row(tab, 86, STR_SOUND_TOUCH_CLICK, &s_touch_lbl,
                                 display_buzzer_is_click_enabled(), sound_touch_cb);

    build_lang_section(tab, 0, 134, SYS_COL_W);

    /* Rechte Spalte: Zeit / RTC */
    s_time_title_lbl = make_section_title(tab, SYS_COL2_X, 0, SYS_COL_W, STR_TIME_SECTION);
    s_time_info_lbl = lv_label_create(tab);
    lv_obj_set_style_text_font(s_time_info_lbl, &lv_font_montserrat_14_german, 0);
    lv_obj_set_style_text_color(s_time_info_lbl, lv_color_hex(0x444444), 0);
    lv_obj_set_pos(s_time_info_lbl, SYS_COL2_X, 40);
    lv_obj_set_width(s_time_info_lbl, SYS_COL_W);
    system_time_info_refresh();
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
    s_ruuvi_scan_in_progress = false;
    s_ruuvi_scan_result_count = 0;

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
    lv_obj_set_style_text_font(tab_bar, &lv_font_montserrat_14_german, 0);

    /* Reihenfolge = enum TAB_*, erster Tab (AP) ist beim Öffnen aktiv */
    lv_obj_t *tab_ap     = lv_tabview_add_tab(tabview, womo_locale_get_string(STR_SET_TAB_AP));
    lv_obj_t *tab_sens   = lv_tabview_add_tab(tabview, womo_locale_get_string(STR_SET_TAB_SENSORS));
    lv_obj_t *tab_thresh = lv_tabview_add_tab(tabview, womo_locale_get_string(STR_SET_TAB_THRESH));
    lv_obj_t *tab_lte    = lv_tabview_add_tab(tabview, womo_locale_get_string(STR_SET_TAB_LTE));
    lv_obj_t *tab_sys    = lv_tabview_add_tab(tabview, womo_locale_get_string(STR_SET_TAB_SYSTEM));

    s_tabview = tabview;  /* für Tab-Texte bei Sprachwechsel */

    build_tab_ap(tab_ap);
    build_tab_sensoren(tab_sens);
    build_tab_grenzwerte(tab_thresh);
    build_tab_lte(tab_lte);
    build_tab_system(tab_sys);

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
