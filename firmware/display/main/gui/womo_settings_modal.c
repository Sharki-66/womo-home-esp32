/*
 * womo_settings_modal.c – Einstellungs-Modal
 *
 * Größe und Grundstruktur analog zum Connectivity-Modal.
 * Abschnitt 1: Sprachauswahl DE / EN.
 * Abschnitt 2: Grenzwerte Tank / Gas (Warn / Alarm, ±5 %).
 */

#include "womo_settings_modal.h"
#include "womo_locale.h"
#include "womo_thresholds.h"
#include "womo_fonts_german.h"
#include "../time/womo_time.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "settings_modal";

/* ── Dimensionen (identisch mit Connectivity-Modal) ─────── */
#define MODAL_W   640
#define MODAL_H   420
#define HDR_H      48
#define PAD        16

/* ── Interne Zustandsvariablen ───────────────────────────── */
static lv_obj_t *s_overlay  = NULL;
static lv_obj_t *s_panel    = NULL;

/* ── Grenzwert-Editierkopie + Button-User-Data ───────────── */
static womo_thresholds_t s_te;   /* Arbeitskopie beim Öffnen */

/* ── Locale-abhängige Labels im Threshold-Abschnitt ─────── */
static lv_obj_t *s_title_lbl     = NULL;
static lv_obj_t *s_close_lbl     = NULL;
static lv_obj_t *s_thr_title_lbl = NULL;
static lv_obj_t *s_warn_hdr_lbl  = NULL;
static lv_obj_t *s_row_lbl[3]    = {NULL, NULL, NULL};
static bool      s_locale_cb_reg  = false;

/* ── RTC-Batterie-Info-Labels ────────────────────────── */
static lv_obj_t *s_rtc_info_lbl  = NULL;  // Mehrzeiliger Info-Block

static void settings_locale_cb(void)
{
    if (!s_panel) return;
    if (s_title_lbl)     lv_label_set_text(s_title_lbl,     womo_locale_get_string(STR_SETTINGS_TITLE));
    /* s_close_lbl zeigt LV_SYMBOL_CLOSE – kein Locale-Update nötig */
    lv_label_set_text(s_thr_title_lbl, womo_locale_get_string(STR_THRESH_TITLE));
    lv_label_set_text(s_warn_hdr_lbl,  womo_locale_get_string(STR_THRESH_WARNING));
    lv_label_set_text(s_row_lbl[0],    womo_locale_get_string(STR_THRESH_GAS));
    lv_label_set_text(s_row_lbl[1],    womo_locale_get_string(STR_THRESH_FRESH));
    lv_label_set_text(s_row_lbl[2],    womo_locale_get_string(STR_THRESH_GREY));
}

typedef struct {
    uint8_t *val;              /* Zeiger auf Wert in s_te          */
    uint8_t *partner;          /* verkoppelter Grenzwert           */
    bool     val_gt_partner;   /* true: val muss > partner bleiben */
    uint8_t  abs_min;
    uint8_t  abs_max;
    int8_t   step;             /* +5 oder -5                       */
    lv_obj_t *lbl;             /* Value-Label zum Aktualisieren    */
} thresh_ud_t;

/* 3 Zeilen × 2 Werte × 2 Richtungen = 12 Einträge */
static thresh_ud_t s_tud[12];
static int s_tud_cnt = 0;

static void thresh_btn_cb(lv_event_t *e)
{
    thresh_ud_t *ud = (thresh_ud_t *)lv_event_get_user_data(e);
    if (!ud || !ud->val) return;

    int v = (int)*ud->val + (int)ud->step;

    /* absolute Grenzen */
    if (v < (int)ud->abs_min) v = (int)ud->abs_min;
    if (v > (int)ud->abs_max) v = (int)ud->abs_max;

    /* Partner-Constraint: min. 5 % Abstand */
    if (ud->partner) {
        if (ud->val_gt_partner) {
            int lo = (int)*ud->partner + 5;
            if (v < lo) v = lo;
        } else {
            int hi = (int)*ud->partner - 5;
            if (v > hi) v = hi;
        }
    }

    /* erneut auf absolute Grenzen prüfen */
    if (v < (int)ud->abs_min) v = (int)ud->abs_min;
    if (v > (int)ud->abs_max) v = (int)ud->abs_max;

    *ud->val = (uint8_t)v;
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", (unsigned)v);
    lv_label_set_text(ud->lbl, buf);

    womo_thresholds_set(&s_te);
}

/* Vollständige Hilfsfunktion: erzeugt Spin-Triplet und registriert Callbacks */
static void make_thresh_row(lv_obj_t *parent, int y,
                            const char *name,
                            uint8_t *warn_val, uint8_t *crit_val,
                            bool high_is_bad,
                            lv_obj_t **out_nl)
{
    /* Zeilen-Positionen (ohne Inline-Label, Spinner nach rechts verschoben) */
    enum { xNAME=16, xWM=210, xWV=238, xWP=282,
                     xCM=390, xCV=418, xCP=462 };

    /* Zeilenname */
    lv_obj_t *nl = lv_label_create(parent);
    lv_label_set_text(nl, name);
    lv_obj_set_style_text_font(nl, &lv_font_montserrat_14_german, 0);
    lv_obj_set_style_text_color(nl, lv_color_hex(0x222222), 0);
    lv_obj_set_pos(nl, xNAME, y + 6);

    /* warn [-] */
    lv_obj_t *wm = lv_btn_create(parent);
    lv_obj_set_size(wm, 26, 26);
    lv_obj_set_pos(wm, xWM, y);
    lv_obj_set_style_bg_color(wm, lv_color_hex(0x757575), 0);
    lv_obj_set_style_radius(wm, 4, 0);
    lv_obj_set_style_border_width(wm, 0, 0);
    lv_obj_set_style_pad_all(wm, 0, 0);
    lv_obj_t *wml = lv_label_create(wm); lv_label_set_text(wml, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(wml, lv_color_white(), 0); lv_obj_center(wml);

    /* warn value label */
    lv_obj_t *wvl = lv_label_create(parent);
    { char b[8]; snprintf(b, sizeof(b), "%u%%", (unsigned)*warn_val);
      lv_label_set_text(wvl, b); }
    lv_obj_set_style_text_font(wvl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wvl, lv_color_hex(0xE65100), 0); /* orange */
    lv_obj_set_size(wvl, 42, 26);
    lv_obj_set_style_text_align(wvl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(wvl, xWV, y + 2);

    /* warn [+] */
    lv_obj_t *wp = lv_btn_create(parent);
    lv_obj_set_size(wp, 26, 26);
    lv_obj_set_pos(wp, xWP, y);
    lv_obj_set_style_bg_color(wp, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_style_radius(wp, 4, 0);
    lv_obj_set_style_border_width(wp, 0, 0);
    lv_obj_set_style_pad_all(wp, 0, 0);
    lv_obj_t *wpl = lv_label_create(wp); lv_label_set_text(wpl, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(wpl, lv_color_white(), 0); lv_obj_center(wpl);

    /* ── CRIT ── */

    /* crit [-] */
    lv_obj_t *cm = lv_btn_create(parent);
    lv_obj_set_size(cm, 26, 26);
    lv_obj_set_pos(cm, xCM, y);
    lv_obj_set_style_bg_color(cm, lv_color_hex(0x757575), 0);
    lv_obj_set_style_radius(cm, 4, 0);
    lv_obj_set_style_border_width(cm, 0, 0);
    lv_obj_set_style_pad_all(cm, 0, 0);
    lv_obj_t *cml = lv_label_create(cm); lv_label_set_text(cml, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(cml, lv_color_white(), 0); lv_obj_center(cml);

    /* crit value label */
    lv_obj_t *cvl = lv_label_create(parent);
    { char b[8]; snprintf(b, sizeof(b), "%u%%", (unsigned)*crit_val);
      lv_label_set_text(cvl, b); }
    lv_obj_set_style_text_font(cvl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cvl, lv_color_hex(0xC62828), 0); /* rot */
    lv_obj_set_size(cvl, 42, 26);
    lv_obj_set_style_text_align(cvl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(cvl, xCV, y + 2);

    /* crit [+] */
    lv_obj_t *cp = lv_btn_create(parent);
    lv_obj_set_size(cp, 26, 26);
    lv_obj_set_pos(cp, xCP, y);
    lv_obj_set_style_bg_color(cp, lv_color_hex(0x2E7D32), 0);
    lv_obj_set_style_radius(cp, 4, 0);
    lv_obj_set_style_border_width(cp, 0, 0);
    lv_obj_set_style_pad_all(cp, 0, 0);
    lv_obj_t *cpl = lv_label_create(cp); lv_label_set_text(cpl, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(cpl, lv_color_white(), 0); lv_obj_center(cpl);

    /* ── User-Data & Callbacks ── */
    /* warn - */
    thresh_ud_t *wm_ud = &s_tud[s_tud_cnt++];
    wm_ud->val           = warn_val;
    wm_ud->partner       = crit_val;
    wm_ud->val_gt_partner = !high_is_bad; /* warn > crit bei low-is-bad */
    wm_ud->abs_min       = high_is_bad ? 50 : 5;
    wm_ud->abs_max       = high_is_bad ? 90 : 70;
    wm_ud->step          = -5;
    wm_ud->lbl           = wvl;
    lv_obj_add_event_cb(wm, thresh_btn_cb, LV_EVENT_CLICKED, wm_ud);

    /* warn + */
    thresh_ud_t *wp_ud = &s_tud[s_tud_cnt++];
    *wp_ud = *wm_ud; wp_ud->step = +5;
    lv_obj_add_event_cb(wp, thresh_btn_cb, LV_EVENT_CLICKED, wp_ud);

    /* crit - */
    thresh_ud_t *cm_ud = &s_tud[s_tud_cnt++];
    cm_ud->val           = crit_val;
    cm_ud->partner       = warn_val;
    cm_ud->val_gt_partner = high_is_bad; /* crit > warn bei high-is-bad */
    cm_ud->abs_min       = high_is_bad ? 55 : 5;
    cm_ud->abs_max       = high_is_bad ? 95 : 65;
    cm_ud->step          = -5;
    cm_ud->lbl           = cvl;
    lv_obj_add_event_cb(cm, thresh_btn_cb, LV_EVENT_CLICKED, cm_ud);

    /* crit + */
    thresh_ud_t *cp_ud = &s_tud[s_tud_cnt++];
    *cp_ud = *cm_ud; cp_ud->step = +5;
    lv_obj_add_event_cb(cp, thresh_btn_cb, LV_EVENT_CLICKED, cp_ud);

    if (out_nl) *out_nl = nl;
}

/* ─────────────────────────────────────────────────────────────────── */
/*  Hilfsfunktionen                                                    */
/* ─────────────────────────────────────────────────────────────────── */

static void close_modal(void)
{
    if (s_overlay) {
        lv_obj_del_async(s_overlay);  /* async: kein Blockieren im Event-Callback */
        s_overlay      = NULL;
        s_panel        = NULL;
        s_title_lbl    = NULL;
        s_close_lbl    = NULL;
        s_thr_title_lbl= NULL;
        s_warn_hdr_lbl = NULL;
        s_row_lbl[0]   = NULL;
        s_row_lbl[1]   = NULL;
        s_row_lbl[2]   = NULL;
    }
}

/* Schliessen-Button */
static void close_btn_cb(lv_event_t *e)
{
    (void)e;
    close_modal();
}

/* Klick auf Overlay-Hintergrund schließt Modal */
static void overlay_click_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    if (target == s_overlay) {
        close_modal();
    }
}

/* Sprach-Buttons */
static void lang_de_cb(lv_event_t *e)
{
    (void)e;
    womo_locale_set(WOMO_LOCALE_DE);
    ESP_LOGI(TAG, "Sprache → DE");
    /* Buttons optisch aktualisieren */
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
    (void)e;
    womo_locale_set(WOMO_LOCALE_EN);
    ESP_LOGI(TAG, "Sprache → EN");
    lv_obj_t *btn_en = lv_event_get_target(e);
    lv_obj_t *btn_de = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_set_style_bg_color(btn_en, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(btn_en, 0), lv_color_white(), 0);
    if (btn_de) {
        lv_obj_set_style_bg_color(btn_de, lv_color_hex(0xC0C0C0), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(btn_de, 0), lv_color_black(), 0);
    }
}

/* ─────────────────────────────────────────────────────────────────── */
/*  Public API                                                         */
/* ─────────────────────────────────────────────────────────────────── */

bool womo_settings_modal_is_open(void)
{
    return (s_overlay != NULL);
}

void womo_settings_modal_show(lv_obj_t *parent)
{
    if (s_overlay) {
        /* bereits offen – nach vorne bringen */
        lv_obj_move_foreground(s_overlay);
        return;
    }

    /* Arbeitskopie der Grenzwerte laden; Button-Counter zurücksetzen */
    womo_thresholds_get(&s_te);
    s_tud_cnt = 0;

    /* ── Halbtransparentes Overlay ──────────────────────── */
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

    /* ── Modal-Panel ────────────────────────────────────── */
    s_panel = lv_obj_create(s_overlay);
    lv_obj_set_size(s_panel, MODAL_W, MODAL_H);
    lv_obj_align(s_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0xF5F5F5), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_panel, 12, 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_pad_all(s_panel, 0, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* ── Kopfzeile ──────────────────────────────────────── */
    lv_obj_t *header = lv_obj_create(s_panel);
    lv_obj_set_size(header, MODAL_W, HDR_H);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Titel */
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, womo_locale_get_string(STR_SETTINGS_TITLE));
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, PAD, 0);
    s_title_lbl = title;

    /* Schliessen-Button rechts in der Kopfzeile */
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
    s_close_lbl = close_lbl;

    /* ── Inhaltsbereich ─────────────────────────────────── */
    int content_y = HDR_H + PAD;

    /* ── Abschnitt: Sprache ─────────────────────────────── */
    lv_obj_t *lang_title = lv_label_create(s_panel);
    lv_label_set_text(lang_title, "Sprache / Language");
    lv_obj_set_style_text_font(lang_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lang_title, lv_color_hex(0x333333), 0);
    lv_obj_set_pos(lang_title, PAD, content_y);

    /* Trennlinie */
    lv_obj_t *sep = lv_obj_create(s_panel);
    lv_obj_set_size(sep, MODAL_W - 2 * PAD, 1);
    lv_obj_set_pos(sep, PAD, content_y + 24);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* DE / EN Buttons */
    bool is_de = (womo_locale_get() == WOMO_LOCALE_DE);

    lv_obj_t *btn_de = lv_btn_create(s_panel);
    lv_obj_set_size(btn_de, 100, 44);
    lv_obj_set_pos(btn_de, PAD, content_y + 34);
    lv_obj_set_style_bg_color(btn_de, is_de ? lv_color_hex(0x1565C0) : lv_color_hex(0xC0C0C0), 0);
    lv_obj_set_style_radius(btn_de, 8, 0);
    lv_obj_set_style_border_width(btn_de, 0, 0);
    lv_obj_t *lbl_de = lv_label_create(btn_de);
    lv_label_set_text(lbl_de, "Deutsch");
    lv_obj_set_style_text_font(lbl_de, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_de, is_de ? lv_color_white() : lv_color_black(), 0);
    lv_obj_center(lbl_de);

    lv_obj_t *btn_en = lv_btn_create(s_panel);
    lv_obj_set_size(btn_en, 100, 44);
    lv_obj_set_pos(btn_en, PAD + 116, content_y + 34);
    lv_obj_set_style_bg_color(btn_en, !is_de ? lv_color_hex(0x1565C0) : lv_color_hex(0xC0C0C0), 0);
    lv_obj_set_style_radius(btn_en, 8, 0);
    lv_obj_set_style_border_width(btn_en, 0, 0);
    lv_obj_t *lbl_en = lv_label_create(btn_en);
    lv_label_set_text(lbl_en, "English");
    lv_obj_set_style_text_font(lbl_en, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_en, !is_de ? lv_color_white() : lv_color_black(), 0);
    lv_obj_center(lbl_en);

    /* Callbacks: gegenseitiger Verweis als user_data */
    lv_obj_add_event_cb(btn_de, lang_de_cb, LV_EVENT_CLICKED, btn_en);
    lv_obj_add_event_cb(btn_en, lang_en_cb, LV_EVENT_CLICKED, btn_de);

    /* ── Abschnitt: Batterie (RTC) – rechts neben Sprache ─ */
    int rtc_x = 320;  // Rechte Hälfte
    lv_obj_t *rtc_title = lv_label_create(s_panel);
    lv_label_set_text(rtc_title, "Batterie (RTC)");
    lv_obj_set_style_text_font(rtc_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(rtc_title, lv_color_hex(0x333333), 0);
    lv_obj_set_pos(rtc_title, rtc_x, content_y);

    lv_obj_t *rtc_sep = lv_obj_create(s_panel);
    lv_obj_set_size(rtc_sep, 290, 1);
    lv_obj_set_pos(rtc_sep, rtc_x, content_y + 24);
    lv_obj_set_style_bg_color(rtc_sep, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_border_width(rtc_sep, 0, 0);
    lv_obj_set_style_radius(rtc_sep, 0, 0);
    lv_obj_clear_flag(rtc_sep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // RTC-Informationen vom Zeitsystem holen
    const char *src_name;
    switch (womo_time_get_source()) {
        case TIME_SOURCE_NTP:          src_name = "NTP (Internet)";  break;
        case TIME_SOURCE_GPS:          src_name = "GPS (Router)";    break;
        case TIME_SOURCE_RS485:        src_name = "RS485 Sensor";    break;
        case TIME_SOURCE_INTERNAL_RTC: src_name = "RTC (intern)";    break;
        default:                       src_name = "\xE2\x80\x93\xE2\x80\x93\xE2\x80\x93"; break; // –––
    }

    char sync_buf[48];
    if (womo_time_is_synced()) {
        uint32_t secs = womo_time_get_seconds_since_sync();
        if (secs < 60) {
            snprintf(sync_buf, sizeof(sync_buf), "vor %lu s", (unsigned long)secs);
        } else if (secs < 3600) {
            snprintf(sync_buf, sizeof(sync_buf), "vor %lu min", (unsigned long)(secs / 60));
        } else {
            snprintf(sync_buf, sizeof(sync_buf), "vor %lu h", (unsigned long)(secs / 3600));
        }
    } else {
        snprintf(sync_buf, sizeof(sync_buf), "nie");
    }

    // RTC-Batterie-Status: Muss über externe Funktion geholt werden
    // (da latest_sensor_data in main.c static ist)
    extern void womo_get_rtc_battery_status(bool *valid, bool *low, bool *switched);
    bool rtc_valid = false, rtc_low = false, rtc_sw = false;
    womo_get_rtc_battery_status(&rtc_valid, &rtc_low, &rtc_sw);

    char rtc_bat_buf[32];
    if (!rtc_valid) {
        snprintf(rtc_bat_buf, sizeof(rtc_bat_buf), "unbekannt");
    } else {
        snprintf(rtc_bat_buf, sizeof(rtc_bat_buf), "%s", rtc_low ? "SCHWACH !!" : "OK");
    }

    char rtc_info_text[300];
    int n = snprintf(rtc_info_text, sizeof(rtc_info_text),
             "Quelle:       %s\n"
             "Letzte Sync:  %s\n"
             "RTC-Batterie: %s",
             src_name, sync_buf, rtc_bat_buf);
    if (rtc_valid && rtc_sw && n < (int)sizeof(rtc_info_text) - 2) {
        strncat(rtc_info_text, "\nRTC war ohne Strom", sizeof(rtc_info_text) - strlen(rtc_info_text) - 1);
    }

    s_rtc_info_lbl = lv_label_create(s_panel);
    lv_label_set_text(s_rtc_info_lbl, rtc_info_text);
    lv_obj_set_style_text_font(s_rtc_info_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_rtc_info_lbl, lv_color_hex(0x444444), 0);
    lv_obj_set_pos(s_rtc_info_lbl, rtc_x, content_y + 34);

    /* ── Abschnitt: Grenzwerte ──────────────────────────── */
    /* Sprach-Buttons enden bei content_y + 34 + 44 = content_y + 78  */
    int ty = content_y + 78 + PAD;   /* y=158 bei HDR_H=48, PAD=16   */

    s_thr_title_lbl = lv_label_create(s_panel);
    lv_label_set_text(s_thr_title_lbl, womo_locale_get_string(STR_THRESH_TITLE));
    lv_obj_set_style_text_font(s_thr_title_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_thr_title_lbl, lv_color_hex(0x333333), 0);
    lv_obj_set_pos(s_thr_title_lbl, PAD, ty);

    lv_obj_t *sep2 = lv_obj_create(s_panel);
    lv_obj_set_size(sep2, MODAL_W - 2 * PAD, 1);
    lv_obj_set_pos(sep2, PAD, ty + 24);
    lv_obj_set_style_bg_color(sep2, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_border_width(sep2, 0, 0);
    lv_obj_set_style_radius(sep2, 0, 0);
    lv_obj_clear_flag(sep2, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Spalten-Überschriften */
    s_warn_hdr_lbl = lv_label_create(s_panel);
    lv_label_set_text(s_warn_hdr_lbl, womo_locale_get_string(STR_THRESH_WARNING));
    lv_obj_set_style_text_font(s_warn_hdr_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_warn_hdr_lbl, lv_color_hex(0xE65100), 0);
    lv_obj_set_pos(s_warn_hdr_lbl, 230, ty + 32);

    lv_obj_t *h_alarm = lv_label_create(s_panel);
    lv_label_set_text(h_alarm, "Alarm");  /* identisch in DE/EN */
    lv_obj_set_style_text_font(h_alarm, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(h_alarm, lv_color_hex(0xC62828), 0);
    lv_obj_set_pos(h_alarm, 420, ty + 32);

    /* Zeilen: je 44 px Abstand */
    int row_y = ty + 52;
    make_thresh_row(s_panel, row_y,      womo_locale_get_string(STR_THRESH_GAS),   &s_te.gas_warn,   &s_te.gas_crit,   false, &s_row_lbl[0]);
    make_thresh_row(s_panel, row_y + 44, womo_locale_get_string(STR_THRESH_FRESH), &s_te.fresh_warn, &s_te.fresh_crit, false, &s_row_lbl[1]);
    make_thresh_row(s_panel, row_y + 88, womo_locale_get_string(STR_THRESH_GREY),  &s_te.grey_warn,  &s_te.grey_crit,  true,  &s_row_lbl[2]);

    /* Locale-Callback einmalig registrieren */
    if (!s_locale_cb_reg) {
        womo_locale_register_change_cb(settings_locale_cb);
        s_locale_cb_reg = true;
    }

    ESP_LOGI(TAG, "Einstellungs-Modal geöffnet");
}
