#include "womo_connectivity_modal.h"

#include "gui/womo_locale.h"
#include "gui/womo_fonts_german.h"
#include "hardware/lvgl_port.h"
#include "network/womo_router_uci.h"
#include "rs485/womo_rs485_display.h"
#include "time/womo_time.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* ═══════════════════════ Defines ═════════════════════════ */
#define MAX_SCAN_RESULTS          16
#define TASK_STACK_SIZE           4096
#define TASK_PRIO                 5
#define ROUTER_KNOWN_MAX          20
#define NVS_NS_RTR_WIFI           "rtr_wifi"
#define LED_SIZE                  12
#define SWITCH_COOLDOWN_MS        45000  /* 45s Cooldown nach Toggle */

/* ═══════════════════════ Typen ═══════════════════════════ */

/** NVS-Eintrag für Router-WiFi-Zugangsdaten */
typedef struct {
    char ssid[33];
    char pwd[64];
} router_wifi_entry_t;

typedef struct {
    lv_obj_t *overlay;
    lv_obj_t *panel;
    lv_obj_t *wifi_keyboard;
    /* ── AP ── */
    lv_obj_t *ap_led;
    lv_obj_t *ap_status_label;
    lv_obj_t *ap_info_label;
    lv_obj_t *ap_client_list_label;    /**< Liste der verbundenen Geräte */
    /* ── WiFi ── */
    lv_obj_t *wifi_led;
    lv_obj_t *wifi_status_label;
    lv_obj_t *wifi_switch;
    lv_obj_t *wifi_scan_row;           /**< Container für Scan-Button + Spinner */
    lv_obj_t *wifi_scan_btn;
    lv_obj_t *wifi_spinner;
    lv_obj_t *wifi_scan_popup;        /**< Scan-Ergebnis-Popup (auf Overlay) */
    lv_obj_t *wifi_scan_list;         /**< lv_list im Popup */
    lv_obj_t *wifi_password_label;
    lv_obj_t *wifi_password_area;
    lv_obj_t *wifi_connect_btn;
    lv_obj_t *wifi_cancel_btn;
    lv_obj_t *wifi_btn_row;           /**< Container für Verbinden/Abbrechen */
    lv_obj_t *wifi_scan_status_label;
    bool      wifi_switch_internal;
    /* ── LTE ── */
    lv_obj_t *lte_led;
    lv_obj_t *lte_status_label;
    lv_obj_t *lte_switch;
    bool      lte_switch_internal;
    /* ── Zustand ── */
    bool      scan_in_progress;
    char      selected_ssid[33];
    char      entered_password[65];
    char      softap_ssid[33];
    womo_router_scan_result_t scan_results[MAX_SCAN_RESULTS];
    size_t    scan_result_count;
    TaskHandle_t wifi_connect_task;
    TaskHandle_t wifi_disconnect_task;
    TaskHandle_t wifi_scan_task;
    TaskHandle_t lte_toggle_task;
    int64_t   wifi_cooldown_until;
    int64_t   lte_cooldown_until;
} connectivity_modal_ctx_t;

typedef struct {
    char ssid[33];
    char password[65];
} wifi_connect_params_t;

static connectivity_modal_ctx_t s_ctx = {0};
static womo_connectivity_snapshot_t s_latest_snapshot = {0};
static router_wifi_entry_t s_router_known[ROUTER_KNOWN_MAX];
static size_t s_router_known_count = 0;
static const char *TAG = "womo_modal";

#define WOMO_KB_BTN(width) (LV_BTNMATRIX_CTRL_POPOVER | (width))

static const char * s_keyboard_map_lc[] = {
    "1#", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "y", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t s_keyboard_ctrl_lc[] = {
    LV_KEYBOARD_CTRL_BTN_FLAGS | 5,
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    LV_BTNMATRIX_CTRL_CHECKED | 7,
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    LV_KEYBOARD_CTRL_BTN_FLAGS | 6,
    WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3),
    WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3),
    LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1),
    WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1),
    WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1),
    LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1),
    LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1),
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6,
    LV_BTNMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};

static const char * s_keyboard_map_uc[] = {
    "1#", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "Y", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t s_keyboard_ctrl_uc[] = {
    LV_KEYBOARD_CTRL_BTN_FLAGS | 5,
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    LV_BTNMATRIX_CTRL_CHECKED | 7,
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4), WOMO_KB_BTN(4),
    LV_KEYBOARD_CTRL_BTN_FLAGS | 6,
    WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3),
    WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3), WOMO_KB_BTN(3),
    LV_BTNMATRIX_CTRL_CHECKED | 7,
    LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1),
    WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1),
    WOMO_KB_BTN(1), WOMO_KB_BTN(1), WOMO_KB_BTN(1),
    LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1),
    LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1), LV_BTNMATRIX_CTRL_CHECKED | WOMO_KB_BTN(1),
    LV_KEYBOARD_CTRL_BTN_FLAGS | 2, LV_BTNMATRIX_CTRL_CHECKED | 2, 6,
    LV_BTNMATRIX_CTRL_CHECKED | 2, LV_KEYBOARD_CTRL_BTN_FLAGS | 2
};

#undef WOMO_KB_BTN

/* ═══════════════════════ Forward-Deklarationen ══════════ */
static void build_modal(lv_obj_t *parent);
static void destroy_modal(void);
/* Event-Callbacks */
static void wifi_switch_event_cb(lv_event_t *e);
static void wifi_scan_event_cb(lv_event_t *e);
static void wifi_password_event_cb(lv_event_t *e);
static void lte_switch_event_cb(lv_event_t *e);
static void close_button_event_cb(lv_event_t *e);
static void overlay_event_cb(lv_event_t *e);
static void panel_event_cb(lv_event_t *e);
static void scan_list_btn_event_cb(lv_event_t *e);
static void scan_popup_close_event_cb(lv_event_t *e);
static void wifi_connect_btn_event_cb(lv_event_t *e);
static void wifi_cancel_btn_event_cb(lv_event_t *e);
/* Status-Aktualisierung */
static void update_ap_status(void);
static void update_wifi_status_label(void);
static void update_lte_status_label(void);
static void set_wifi_activity_text(const char *text);
static void ensure_selected_ssid_from_snapshot(void);
/* Tasks */
static void start_wifi_connect_task(const char *ssid, const char *password);
static void start_wifi_disconnect_task(void);
static void wifi_connect_task(void *arg);
static void wifi_disconnect_task(void *arg);
static void router_scan_task(void *arg);
static void lte_toggle_task_fn(void *arg);
/* Hilfsfunktionen */
static void refresh_hidden_ssid_sources(void);
static bool wifi_should_hide_ssid(const char *ssid);
static uint8_t wifi_rssi_to_percent(int8_t rssi);
static bool point_inside_panel(const lv_point_t *point);
static void configure_wifi_keyboard(void);
/* Scan-Popup */
static void show_scan_popup(void);
static void close_scan_popup(void);
/* LED-Helfer */
static lv_obj_t *create_status_led(lv_obj_t *parent, bool active);
static void set_led_active(lv_obj_t *led, bool active);
/* NVS Router-WiFi-Zugangsdaten */
static void router_wifi_nvs_load(void);
static void router_wifi_nvs_save(void);
static int  router_wifi_find(const char *ssid);
static void router_wifi_promote(const char *ssid, const char *pwd);

/* ═══════════════════════ LED-Helfer ═════════════════════ */

static lv_obj_t *create_status_led(lv_obj_t *parent, bool active)
{
    lv_obj_t *led = lv_obj_create(parent);
    lv_obj_remove_style_all(led);
    lv_obj_set_size(led, LED_SIZE, LED_SIZE);
    lv_obj_set_style_radius(led, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(led, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(led,
        active ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_clear_flag(led, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return led;
}

static void set_led_active(lv_obj_t *led, bool active)
{
    if (!led) return;
    lv_obj_set_style_bg_color(led,
        active ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_RED), 0);
}

/* ═══════════════════════ NVS Router-WiFi ════════════════ */

static void router_wifi_nvs_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_RTR_WIFI, NVS_READONLY, &h);
    if (err != ESP_OK) { s_router_known_count = 0; return; }

    uint8_t cnt = 0;
    err = nvs_get_u8(h, "count", &cnt);
    if (err != ESP_OK) { s_router_known_count = 0; nvs_close(h); return; }
    if (cnt > ROUTER_KNOWN_MAX) cnt = ROUTER_KNOWN_MAX;

    size_t blob_size = cnt * sizeof(router_wifi_entry_t);
    if (cnt > 0) {
        err = nvs_get_blob(h, "list", s_router_known, &blob_size);
        if (err != ESP_OK) cnt = 0;
    }
    s_router_known_count = cnt;
    nvs_close(h);
}

static void router_wifi_nvs_save(void)
{
    if (s_router_known_count == 0) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NS_RTR_WIFI, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "count", (uint8_t)s_router_known_count);
    nvs_set_blob(h, "list", s_router_known,
                 s_router_known_count * sizeof(router_wifi_entry_t));
    nvs_commit(h);
    nvs_close(h);
}

static int router_wifi_find(const char *ssid)
{
    if (!ssid) return -1;
    for (size_t i = 0; i < s_router_known_count; i++) {
        if (strncmp(s_router_known[i].ssid, ssid, sizeof(s_router_known[i].ssid)) == 0)
            return (int)i;
    }
    return -1;
}

static void router_wifi_promote(const char *ssid, const char *pwd)
{
    if (!ssid || !pwd || ssid[0] == '\0') return;

    int idx = router_wifi_find(ssid);
    router_wifi_entry_t entry = {0};
    strncpy(entry.ssid, ssid, sizeof(entry.ssid) - 1);
    strncpy(entry.pwd,  pwd,  sizeof(entry.pwd)  - 1);

    if (idx >= 0) {
        for (int i = idx; i > 0; i--) s_router_known[i] = s_router_known[i - 1];
        s_router_known[0] = entry;
    } else {
        if (s_router_known_count < ROUTER_KNOWN_MAX) s_router_known_count++;
        for (size_t i = s_router_known_count - 1; i > 0; i--)
            s_router_known[i] = s_router_known[i - 1];
        s_router_known[0] = entry;
    }
    router_wifi_nvs_save();
}

/* ═══════════════════════ Public API ═════════════════════ */

bool womo_connectivity_modal_is_open(void)
{
    return (s_ctx.overlay != NULL);
}

void womo_connectivity_modal_show(lv_obj_t *parent, const womo_connectivity_snapshot_t *snapshot)
{
    if (snapshot) {
        s_latest_snapshot = *snapshot;
    } else {
        memset(&s_latest_snapshot, 0, sizeof(s_latest_snapshot));
    }

    if (s_ctx.overlay) {
        lv_obj_move_foreground(s_ctx.overlay);
        update_ap_status();
        update_wifi_status_label();
        update_lte_status_label();
        return;
    }

    if (!parent) {
        parent = lv_scr_act();
    }

    build_modal(parent);
    update_ap_status();
    update_wifi_status_label();
    update_lte_status_label();
}

void womo_connectivity_modal_refresh(const womo_connectivity_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }

    s_latest_snapshot = *snapshot;

    if (!s_ctx.overlay) {
        return;
    }

    update_ap_status();
    update_wifi_status_label();
    update_lte_status_label();
}

static void build_modal(lv_obj_t *parent)
{
    refresh_hidden_ssid_sources();
    router_wifi_nvs_load();

    /* ── Overlay ── */
    s_ctx.overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ctx.overlay);
    lv_obj_set_size(s_ctx.overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_ctx.overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ctx.overlay, LV_OPA_70, 0);
    lv_obj_add_flag(s_ctx.overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ctx.overlay, overlay_event_cb, LV_EVENT_CLICKED, NULL);

    /* ── Panel ── */
    s_ctx.panel = lv_obj_create(s_ctx.overlay);
    lv_obj_set_size(s_ctx.panel, 640, 420);
    lv_obj_center(s_ctx.panel);
    lv_obj_set_style_bg_color(s_ctx.panel, lv_color_white(), 0);
    lv_obj_set_style_radius(s_ctx.panel, 8, 0);
    lv_obj_set_style_pad_all(s_ctx.panel, 16, 0);
    lv_obj_set_flex_flow(s_ctx.panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_ctx.panel,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_add_event_cb(s_ctx.panel, panel_event_cb, LV_EVENT_CLICKED, NULL);

    /* ── Titel-Zeile ── */
    lv_obj_t *title_row = lv_obj_create(s_ctx.panel);
    lv_obj_remove_style_all(title_row);
    lv_obj_set_size(title_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_bottom(title_row, 10, 0);

    lv_obj_t *title_label = lv_label_create(title_row);
    lv_label_set_text(title_label, womo_locale_get_string(STR_CONNECTIVITY_TITLE));
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20_german, 0);
    lv_obj_set_flex_grow(title_label, 1);

    lv_obj_t *close_btn = lv_btn_create(title_row);
    lv_obj_set_style_pad_all(close_btn, 8, 0);
    lv_obj_add_event_cb(close_btn, close_button_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, womo_locale_get_string(STR_MODAL_CLOSE_BUTTON));

    /* ── Spalten-Container ── */
    lv_obj_t *cols = lv_obj_create(s_ctx.panel);
    lv_obj_remove_style_all(cols);
    lv_obj_set_width(cols, LV_PCT(100));
    lv_obj_set_height(cols, 0);
    lv_obj_set_flex_grow(cols, 1);
    lv_obj_set_flex_flow(cols, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(cols, 12, 0);
    lv_obj_set_flex_align(cols,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    /* ════════════════ AP-Spalte (HotSpot) ═══════════════ */
    lv_obj_t *ap_col = lv_obj_create(cols);
    lv_obj_remove_style_all(ap_col);
    lv_obj_set_width(ap_col, 0);
    lv_obj_set_flex_grow(ap_col, 1);
    lv_obj_set_flex_flow(ap_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ap_col, 4, 0);
    lv_obj_clear_flag(ap_col, LV_OBJ_FLAG_SCROLLABLE);

    /* AP Header (Titel + LED) */
    lv_obj_t *ap_hdr = lv_obj_create(ap_col);
    lv_obj_remove_style_all(ap_hdr);
    lv_obj_set_size(ap_hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ap_hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ap_hdr,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(ap_hdr, 6, 0);

    lv_obj_t *ap_title = lv_label_create(ap_hdr);
    lv_label_set_text(ap_title, womo_locale_get_string(STR_AP_SECTION_TITLE));
    lv_obj_set_style_text_font(ap_title, WOMO_FONT_MEDIUM, 0);
    lv_obj_set_flex_grow(ap_title, 1);

    s_ctx.ap_led = create_status_led(ap_hdr, s_latest_snapshot.ap_enabled);

    /* AP Status-Label (SSID oder Inaktiv) */
    s_ctx.ap_status_label = lv_label_create(ap_col);
    lv_label_set_text(s_ctx.ap_status_label, "");
    lv_obj_set_style_text_font(s_ctx.ap_status_label, WOMO_FONT_SMALL, 0);
    lv_obj_set_width(s_ctx.ap_status_label, LV_PCT(100));
    lv_label_set_long_mode(s_ctx.ap_status_label, LV_LABEL_LONG_DOT);

    /* AP Info-Label (Anzahl Clients, grau, klein) */
    s_ctx.ap_info_label = lv_label_create(ap_col);
    lv_label_set_text(s_ctx.ap_info_label, "");
    lv_obj_set_style_text_font(s_ctx.ap_info_label, WOMO_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_ctx.ap_info_label,
                                lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_width(s_ctx.ap_info_label, LV_PCT(100));

    /* AP Client-Liste (Gerätenamen, mehrzeilig) */
    s_ctx.ap_client_list_label = lv_label_create(ap_col);
    lv_label_set_text(s_ctx.ap_client_list_label, "");
    lv_obj_set_style_text_font(s_ctx.ap_client_list_label, WOMO_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_ctx.ap_client_list_label,
                                lv_color_hex(0x333333), 0);
    lv_obj_set_width(s_ctx.ap_client_list_label, LV_PCT(100));
    lv_label_set_long_mode(s_ctx.ap_client_list_label, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_ctx.ap_client_list_label, LV_OBJ_FLAG_HIDDEN);

    /* ════════════════ WLAN-Spalte ═══════════════════════ */
    lv_obj_t *wifi_col = lv_obj_create(cols);
    lv_obj_remove_style_all(wifi_col);
    lv_obj_set_width(wifi_col, 0);
    lv_obj_set_height(wifi_col, LV_PCT(100));
    lv_obj_set_flex_grow(wifi_col, 1);
    lv_obj_set_flex_flow(wifi_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wifi_col, 4, 0);
    /* Trennlinie links */
    lv_obj_set_style_border_side(wifi_col, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(wifi_col, 1, 0);
    lv_obj_set_style_border_color(wifi_col,
                                  lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_set_style_pad_left(wifi_col, 10, 0);

    /* WiFi Header (Titel + LED) */
    lv_obj_t *wifi_hdr = lv_obj_create(wifi_col);
    lv_obj_remove_style_all(wifi_hdr);
    lv_obj_set_size(wifi_hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wifi_hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wifi_hdr,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(wifi_hdr, 6, 0);

    lv_obj_t *wifi_title = lv_label_create(wifi_hdr);
    lv_label_set_text(wifi_title, womo_locale_get_string(STR_WIFI_SECTION_TITLE));
    lv_obj_set_style_text_font(wifi_title, WOMO_FONT_MEDIUM, 0);
    lv_obj_set_flex_grow(wifi_title, 1);

    s_ctx.wifi_led = create_status_led(wifi_hdr, s_latest_snapshot.wifi_connected);

    /* WiFi Status-Label (SSID + Signal oder Disconnected) */
    s_ctx.wifi_status_label = lv_label_create(wifi_col);
    lv_label_set_text(s_ctx.wifi_status_label, "");
    lv_obj_set_style_text_font(s_ctx.wifi_status_label, WOMO_FONT_SMALL, 0);
    lv_obj_set_width(s_ctx.wifi_status_label, LV_PCT(100));
    lv_label_set_long_mode(s_ctx.wifi_status_label, LV_LABEL_LONG_DOT);

    /* WiFi Switch */
    lv_obj_t *wifi_spacer = lv_obj_create(wifi_col);
    lv_obj_remove_style_all(wifi_spacer);
    lv_obj_set_size(wifi_spacer, 0, 4);
    s_ctx.wifi_switch = lv_switch_create(wifi_col);
    lv_obj_add_event_cb(s_ctx.wifi_switch, wifi_switch_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* Scan-Zeile (Button + Spinner) – unter dem Switch */
    s_ctx.wifi_scan_row = lv_obj_create(wifi_col);
    lv_obj_remove_style_all(s_ctx.wifi_scan_row);
    lv_obj_set_size(s_ctx.wifi_scan_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_ctx.wifi_scan_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_ctx.wifi_scan_row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(s_ctx.wifi_scan_row, 8, 0);
    lv_obj_set_style_pad_top(s_ctx.wifi_scan_row, 4, 0);

    s_ctx.wifi_scan_btn = lv_btn_create(s_ctx.wifi_scan_row);
    lv_obj_add_event_cb(s_ctx.wifi_scan_btn, wifi_scan_event_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_lbl = lv_label_create(s_ctx.wifi_scan_btn);
    lv_label_set_text(scan_lbl, womo_locale_get_string(STR_WIFI_SCAN_BUTTON));

    s_ctx.wifi_spinner = lv_spinner_create(s_ctx.wifi_scan_row, 1000, 60);
    lv_obj_set_size(s_ctx.wifi_spinner, 26, 26);
    lv_obj_add_flag(s_ctx.wifi_spinner, LV_OBJ_FLAG_HIDDEN);

    /* Scan-Status-Label */
    s_ctx.wifi_scan_status_label = lv_label_create(wifi_col);
    lv_label_set_text(s_ctx.wifi_scan_status_label, "");
    lv_obj_set_style_text_font(s_ctx.wifi_scan_status_label, WOMO_FONT_TINY, 0);
    lv_obj_set_width(s_ctx.wifi_scan_status_label, LV_PCT(100));

    /* Passwort-Label + Textfeld (initial verborgen) */
    s_ctx.wifi_password_label = lv_label_create(wifi_col);
    lv_label_set_text(s_ctx.wifi_password_label,
                      womo_locale_get_string(STR_WIFI_PASSWORD_LABEL));
    lv_obj_set_style_text_font(s_ctx.wifi_password_label, WOMO_FONT_SMALL, 0);
    lv_obj_add_flag(s_ctx.wifi_password_label, LV_OBJ_FLAG_HIDDEN);

    s_ctx.wifi_password_area = lv_textarea_create(wifi_col);
    lv_textarea_set_password_mode(s_ctx.wifi_password_area, true);
    lv_textarea_set_password_show_time(s_ctx.wifi_password_area, 800);
    lv_textarea_set_placeholder_text(s_ctx.wifi_password_area,
                                     womo_locale_get_string(STR_WIFI_PASSWORD_PLACEHOLDER));
    lv_textarea_set_max_length(s_ctx.wifi_password_area,
                               sizeof(s_ctx.entered_password) - 1);
    lv_obj_set_width(s_ctx.wifi_password_area, LV_PCT(100));
    lv_obj_set_height(s_ctx.wifi_password_area, 36);
    lv_obj_set_style_pad_all(s_ctx.wifi_password_area, 4, 0);
    lv_obj_add_event_cb(s_ctx.wifi_password_area, wifi_password_event_cb,
                        LV_EVENT_ALL, NULL);
    lv_obj_add_flag(s_ctx.wifi_password_area, LV_OBJ_FLAG_HIDDEN);

    /* Verbinden- / Abbrechen-Buttons (initial verborgen) */
    s_ctx.wifi_btn_row = lv_obj_create(wifi_col);
    lv_obj_remove_style_all(s_ctx.wifi_btn_row);
    lv_obj_set_size(s_ctx.wifi_btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_ctx.wifi_btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_ctx.wifi_btn_row, 8, 0);
    lv_obj_set_style_pad_top(s_ctx.wifi_btn_row, 4, 0);

    s_ctx.wifi_connect_btn = lv_btn_create(s_ctx.wifi_btn_row);
    lv_obj_set_style_bg_color(s_ctx.wifi_connect_btn,
                              lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_add_event_cb(s_ctx.wifi_connect_btn, wifi_connect_btn_event_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *conn_lbl = lv_label_create(s_ctx.wifi_connect_btn);
    lv_label_set_text(conn_lbl, womo_locale_get_string(STR_WIFI_CONNECT_BUTTON));

    s_ctx.wifi_cancel_btn = lv_btn_create(s_ctx.wifi_btn_row);
    lv_obj_set_style_bg_color(s_ctx.wifi_cancel_btn,
                              lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_add_event_cb(s_ctx.wifi_cancel_btn, wifi_cancel_btn_event_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(s_ctx.wifi_cancel_btn);
    lv_label_set_text(cancel_lbl, womo_locale_get_string(STR_WIFI_CANCEL_BUTTON));

    /* Gesamte Button-Row anfangs verborgen */
    lv_obj_add_flag(s_ctx.wifi_btn_row, LV_OBJ_FLAG_HIDDEN);

    /* ════════════════ LTE-Spalte ═══════════════════════ */
    lv_obj_t *lte_col = lv_obj_create(cols);
    lv_obj_remove_style_all(lte_col);
    lv_obj_set_width(lte_col, 0);
    lv_obj_set_flex_grow(lte_col, 1);
    lv_obj_set_flex_flow(lte_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(lte_col, 4, 0);
    lv_obj_clear_flag(lte_col, LV_OBJ_FLAG_SCROLLABLE);
    /* Trennlinie links */
    lv_obj_set_style_border_side(lte_col, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(lte_col, 1, 0);
    lv_obj_set_style_border_color(lte_col,
                                  lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_set_style_pad_left(lte_col, 10, 0);

    /* LTE Header (Titel + LED) */
    lv_obj_t *lte_hdr = lv_obj_create(lte_col);
    lv_obj_remove_style_all(lte_hdr);
    lv_obj_set_size(lte_hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(lte_hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lte_hdr,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(lte_hdr, 6, 0);

    lv_obj_t *lte_title = lv_label_create(lte_hdr);
    lv_label_set_text(lte_title, womo_locale_get_string(STR_LTE_SECTION_TITLE));
    lv_obj_set_style_text_font(lte_title, WOMO_FONT_MEDIUM, 0);
    lv_obj_set_flex_grow(lte_title, 1);

    s_ctx.lte_led = create_status_led(lte_hdr,
        s_latest_snapshot.lte_valid && s_latest_snapshot.lte_registered);

    /* LTE Status-Label (Provider + Signal oder Offline) */
    s_ctx.lte_status_label = lv_label_create(lte_col);
    lv_label_set_text(s_ctx.lte_status_label, "");
    lv_obj_set_style_text_font(s_ctx.lte_status_label, WOMO_FONT_SMALL, 0);
    lv_obj_set_width(s_ctx.lte_status_label, LV_PCT(100));
    lv_label_set_long_mode(s_ctx.lte_status_label, LV_LABEL_LONG_DOT);

    /* LTE Switch */
    lv_obj_t *lte_spacer = lv_obj_create(lte_col);
    lv_obj_remove_style_all(lte_spacer);
    lv_obj_set_size(lte_spacer, 0, 4);
    s_ctx.lte_switch = lv_switch_create(lte_col);
    lv_obj_add_event_cb(s_ctx.lte_switch, lte_switch_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* ══════════════ Tastatur (auf Overlay) ═════════════ */
    s_ctx.wifi_keyboard = lv_keyboard_create(s_ctx.overlay);
    lv_obj_set_size(s_ctx.wifi_keyboard, LV_PCT(90), 180);
    lv_obj_align(s_ctx.wifi_keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_keyboard_set_textarea(s_ctx.wifi_keyboard, NULL);
    lv_obj_add_flag(s_ctx.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
    configure_wifi_keyboard();
}

static void destroy_modal(void)
{
    if (!s_ctx.overlay) {
        return;
    }

    close_scan_popup();
    lvgl_touch_set_fast_mode(false);
    lv_obj_del(s_ctx.overlay);
    memset(&s_ctx, 0, sizeof(s_ctx));
}

static void close_button_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    destroy_modal();
}

static void overlay_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (lv_event_get_target(e) != s_ctx.overlay) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (indev) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        if (point_inside_panel(&point)) {
            return;
        }
    }

    destroy_modal();
}

static void panel_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    lv_event_stop_bubbling(e);
    lv_event_stop_processing(e);
}

static void update_ap_status(void)
{
    if (!s_ctx.ap_status_label || !s_ctx.ap_led) return;

    set_led_active(s_ctx.ap_led, s_latest_snapshot.ap_enabled);

    if (s_latest_snapshot.ap_enabled && s_latest_snapshot.ap_ssid[0]) {
        lv_label_set_text(s_ctx.ap_status_label, s_latest_snapshot.ap_ssid);
    } else if (s_latest_snapshot.ap_enabled) {
        lv_label_set_text(s_ctx.ap_status_label,
                          womo_locale_get_string(STR_AP_STATUS_ACTIVE));
    } else {
        lv_label_set_text(s_ctx.ap_status_label,
                          womo_locale_get_string(STR_AP_STATUS_INACTIVE));
    }

    /* Kurzinfo: Anzahl Clients */
    if (s_ctx.ap_info_label) {
        if (s_latest_snapshot.ap_enabled && s_latest_snapshot.ap_clients > 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%u Gerät%s verbunden",
                     s_latest_snapshot.ap_clients,
                     s_latest_snapshot.ap_clients == 1 ? "" : "e");
            lv_label_set_text(s_ctx.ap_info_label, buf);
        } else if (s_latest_snapshot.ap_enabled) {
            lv_label_set_text(s_ctx.ap_info_label, "Keine Geräte");
        } else {
            lv_label_set_text(s_ctx.ap_info_label, "");
        }
    }

    /* Detailliste der verbundenen Geräte */
    if (s_ctx.ap_client_list_label) {
        if (s_latest_snapshot.ap_enabled && s_latest_snapshot.ap_clients > 0) {
            char list_buf[256] = {0};
            size_t off = 0;
            for (uint8_t i = 0; i < s_latest_snapshot.ap_clients && i < WOMO_AP_CLIENT_MAX; i++) {
                const womo_ap_client_info_t *c = &s_latest_snapshot.ap_client_list[i];
                const char *name = c->hostname[0] ? c->hostname : c->mac;
                int n = snprintf(list_buf + off, sizeof(list_buf) - off,
                                 "%s%s", (i > 0) ? "\n" : "", name);
                if (n < 0 || (size_t)n >= sizeof(list_buf) - off) break;
                off += (size_t)n;
            }
            lv_label_set_text(s_ctx.ap_client_list_label, list_buf);
            lv_obj_clear_flag(s_ctx.ap_client_list_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(s_ctx.ap_client_list_label, "");
            lv_obj_add_flag(s_ctx.ap_client_list_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void update_wifi_status_label(void)
{
    if (!s_ctx.wifi_status_label || !s_ctx.wifi_switch) {
        return;
    }

    /* Während ein Connect/Disconnect-Task läuft oder Cooldown aktiv, Switch nicht überschreiben */
    if (s_ctx.wifi_connect_task || s_ctx.wifi_disconnect_task) return;
    if (s_ctx.wifi_cooldown_until > esp_timer_get_time()) return;

    char buffer[96];

    if (s_latest_snapshot.wifi_status == WOMO_WIFI_CONNECTING) {
        snprintf(buffer, sizeof(buffer), "%s",
                 womo_locale_get_string(STR_WIFI_STATUS_CONNECTING));
        if (s_ctx.wifi_led) set_led_active(s_ctx.wifi_led, false);
        s_ctx.wifi_switch_internal = true;
        lv_obj_add_state(s_ctx.wifi_switch, LV_STATE_CHECKED);
        s_ctx.wifi_switch_internal = false;
        /* Scan-Zeile sichtbar (Radio wird aktiviert) */
        if (s_ctx.wifi_scan_row)
            lv_obj_clear_flag(s_ctx.wifi_scan_row, LV_OBJ_FLAG_HIDDEN);
    } else if (s_latest_snapshot.wifi_connected) {
        const char *ssid = s_latest_snapshot.wifi_ssid[0]
                               ? s_latest_snapshot.wifi_ssid : "WiFi";
        snprintf(buffer, sizeof(buffer), "%s (%u%%)",
                 ssid, s_latest_snapshot.wifi_signal_percent);
        if (s_ctx.wifi_led) set_led_active(s_ctx.wifi_led, true);
        s_ctx.wifi_switch_internal = true;
        lv_obj_add_state(s_ctx.wifi_switch, LV_STATE_CHECKED);
        s_ctx.wifi_switch_internal = false;
        /* Scan-Zeile sichtbar (WLAN aktiv) */
        if (s_ctx.wifi_scan_row)
            lv_obj_clear_flag(s_ctx.wifi_scan_row, LV_OBJ_FLAG_HIDDEN);
        if (s_ctx.selected_ssid[0] == '\0') {
            strncpy(s_ctx.selected_ssid, ssid,
                    sizeof(s_ctx.selected_ssid) - 1);
            s_ctx.selected_ssid[sizeof(s_ctx.selected_ssid) - 1] = '\0';
        }
    } else {
        snprintf(buffer, sizeof(buffer), "%s",
                 womo_locale_get_string(STR_WIFI_STATUS_DISCONNECTED));
        if (s_ctx.wifi_led) set_led_active(s_ctx.wifi_led, false);
        s_ctx.wifi_switch_internal = true;
        lv_obj_clear_state(s_ctx.wifi_switch, LV_STATE_CHECKED);
        s_ctx.wifi_switch_internal = false;
        /* Scan-UI ausblenden (ohne WLAN kein Scan möglich) */
        if (s_ctx.wifi_scan_row)
            lv_obj_add_flag(s_ctx.wifi_scan_row, LV_OBJ_FLAG_HIDDEN);
        if (s_ctx.wifi_scan_status_label)
            lv_label_set_text(s_ctx.wifi_scan_status_label, "");
        if (s_ctx.wifi_password_label)
            lv_obj_add_flag(s_ctx.wifi_password_label, LV_OBJ_FLAG_HIDDEN);
        if (s_ctx.wifi_password_area)
            lv_obj_add_flag(s_ctx.wifi_password_area, LV_OBJ_FLAG_HIDDEN);
        if (s_ctx.wifi_btn_row)
            lv_obj_add_flag(s_ctx.wifi_btn_row, LV_OBJ_FLAG_HIDDEN);
        close_scan_popup();
    }

    lv_label_set_text(s_ctx.wifi_status_label, buffer);
}

static void update_lte_status_label(void)
{
    if (!s_ctx.lte_status_label || !s_ctx.lte_switch) {
        return;
    }

    /* Während ein Toggle-Task läuft oder Cooldown aktiv, Switch nicht überschreiben */
    if (s_ctx.lte_toggle_task) return;
    if (s_ctx.lte_cooldown_until > esp_timer_get_time()) return;

    char buffer[96];

    if (s_latest_snapshot.lte_valid && s_latest_snapshot.lte_registered) {
        const char *op = s_latest_snapshot.lte_operator[0]
                             ? s_latest_snapshot.lte_operator : "LTE";
        snprintf(buffer, sizeof(buffer), "%s (%u%%)",
                 op, s_latest_snapshot.lte_signal_percent);
        if (s_ctx.lte_led) set_led_active(s_ctx.lte_led, true);
        s_ctx.lte_switch_internal = true;
        lv_obj_add_state(s_ctx.lte_switch, LV_STATE_CHECKED);
        s_ctx.lte_switch_internal = false;
    } else {
        snprintf(buffer, sizeof(buffer), "%s",
                 womo_locale_get_string(STR_LTE_STATUS_OFFLINE));
        if (s_ctx.lte_led) set_led_active(s_ctx.lte_led, false);
        s_ctx.lte_switch_internal = true;
        lv_obj_clear_state(s_ctx.lte_switch, LV_STATE_CHECKED);
        s_ctx.lte_switch_internal = false;
    }

    lv_label_set_text(s_ctx.lte_status_label, buffer);
}

static void set_wifi_activity_text(const char *text)
{
    if (!s_ctx.wifi_scan_status_label) {
        return;
    }

    lv_label_set_text(s_ctx.wifi_scan_status_label, text);
}

static void wifi_switch_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || !s_ctx.wifi_switch || s_ctx.wifi_switch_internal) {
        return;
    }

    bool target_state = lv_obj_has_state(s_ctx.wifi_switch, LV_STATE_CHECKED);

    if (target_state) {
        ensure_selected_ssid_from_snapshot();
        const char *password = s_ctx.wifi_password_area ? lv_textarea_get_text(s_ctx.wifi_password_area) : "";
        if (s_ctx.selected_ssid[0] == '\0') {
            set_wifi_activity_text(womo_locale_get_string(STR_WIFI_SELECT_PLACEHOLDER));
            s_ctx.wifi_switch_internal = true;
            lv_obj_clear_state(s_ctx.wifi_switch, LV_STATE_CHECKED);
            s_ctx.wifi_switch_internal = false;
            return;
        }

        start_wifi_connect_task(s_ctx.selected_ssid, password);
    } else {
        start_wifi_disconnect_task();
    }
}

static void wifi_scan_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !s_ctx.wifi_scan_btn) {
        return;
    }

    if (s_ctx.scan_in_progress || s_ctx.wifi_scan_task) {
        return;
    }

    s_ctx.scan_in_progress = true;
    lv_obj_add_state(s_ctx.wifi_scan_btn, LV_STATE_DISABLED);
    if (s_ctx.wifi_spinner) {
        lv_obj_clear_flag(s_ctx.wifi_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    set_wifi_activity_text(womo_locale_get_string(STR_WIFI_SCANNING_STATUS));

    BaseType_t created = xTaskCreate(router_scan_task,
                                     "router_scan",
                                     8192,
                                     NULL,
                                     TASK_PRIO,
                                     &s_ctx.wifi_scan_task);
    if (created != pdPASS) {
        s_ctx.wifi_scan_task = NULL;
        s_ctx.scan_in_progress = false;
        lv_obj_clear_state(s_ctx.wifi_scan_btn, LV_STATE_DISABLED);
        if (s_ctx.wifi_spinner) {
            lv_obj_add_flag(s_ctx.wifi_spinner, LV_OBJ_FLAG_HIDDEN);
        }
        set_wifi_activity_text(womo_locale_get_string(STR_WIFI_ACTION_NOT_AVAILABLE));
    }
}

/* ─── Scan-Popup: Liste der gefundenen Netzwerke ─────── */

static void close_scan_popup(void)
{
    if (s_ctx.wifi_scan_popup) {
        lv_obj_del(s_ctx.wifi_scan_popup);
        s_ctx.wifi_scan_popup = NULL;
        s_ctx.wifi_scan_list  = NULL;
    }
}

static void scan_popup_close_event_cb(lv_event_t *e)
{
    (void)e;
    close_scan_popup();
}

/**
 * Ein Netzwerk aus der Scan-Liste ausgewählt.
 * Der Index in scan_results[] wird über lv_obj_get_index() bestimmt.
 */
static void scan_list_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t *btn = lv_event_get_target(e);
    /* Welcher Index? (0-basiert innerhalb der Liste) */
    uint32_t idx = lv_obj_get_index(btn);
    if (idx >= s_ctx.scan_result_count) {
        close_scan_popup();
        return;
    }

    strncpy(s_ctx.selected_ssid,
            s_ctx.scan_results[idx].ssid,
            sizeof(s_ctx.selected_ssid) - 1);
    s_ctx.selected_ssid[sizeof(s_ctx.selected_ssid) - 1] = '\0';

    close_scan_popup();

    /* Passwort-Bereich einblenden */
    if (s_ctx.wifi_password_label) {
        lv_obj_clear_flag(s_ctx.wifi_password_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ctx.wifi_password_area) {
        lv_obj_clear_flag(s_ctx.wifi_password_area, LV_OBJ_FLAG_HIDDEN);

        /* Gespeichertes Passwort vorausfüllen */
        int known_idx = router_wifi_find(s_ctx.selected_ssid);
        if (known_idx >= 0 && s_router_known[known_idx].pwd[0]) {
            lv_textarea_set_text(s_ctx.wifi_password_area,
                                 s_router_known[known_idx].pwd);
            strncpy(s_ctx.entered_password,
                    s_router_known[known_idx].pwd,
                    sizeof(s_ctx.entered_password) - 1);
            s_ctx.entered_password[sizeof(s_ctx.entered_password) - 1] = '\0';
        } else {
            lv_textarea_set_text(s_ctx.wifi_password_area, "");
            s_ctx.entered_password[0] = '\0';
        }
    }

    if (s_ctx.wifi_scan_status_label) {
        char buf[64];
        snprintf(buf, sizeof(buf), "→ %s", s_ctx.selected_ssid);
        lv_label_set_text(s_ctx.wifi_scan_status_label, buf);
    }

    /* Verbinden/Abbrechen-Buttons einblenden */
    if (s_ctx.wifi_btn_row) {
        lv_obj_clear_flag(s_ctx.wifi_btn_row, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * Zeigt die Scan-Ergebnisse als Popup-Liste über dem Modal.
 */
static void show_scan_popup(void)
{
    close_scan_popup(); /* Falls offen */

    if (s_ctx.scan_result_count == 0 || !s_ctx.overlay) return;

    /* Popup-Container auf dem Overlay */
    s_ctx.wifi_scan_popup = lv_obj_create(s_ctx.overlay);
    lv_obj_set_size(s_ctx.wifi_scan_popup, 400, 320);
    lv_obj_center(s_ctx.wifi_scan_popup);
    lv_obj_set_style_bg_color(s_ctx.wifi_scan_popup, lv_color_white(), 0);
    lv_obj_set_style_radius(s_ctx.wifi_scan_popup, 8, 0);
    lv_obj_set_style_pad_all(s_ctx.wifi_scan_popup, 12, 0);
    lv_obj_set_flex_flow(s_ctx.wifi_scan_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_shadow_width(s_ctx.wifi_scan_popup, 20, 0);
    lv_obj_set_style_shadow_opa(s_ctx.wifi_scan_popup, LV_OPA_30, 0);
    lv_obj_move_foreground(s_ctx.wifi_scan_popup);

    /* Titel-Zeile */
    lv_obj_t *popup_hdr = lv_obj_create(s_ctx.wifi_scan_popup);
    lv_obj_remove_style_all(popup_hdr);
    lv_obj_set_size(popup_hdr, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(popup_hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(popup_hdr,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_bottom(popup_hdr, 8, 0);

    lv_obj_t *popup_title = lv_label_create(popup_hdr);
    lv_label_set_text(popup_title,
                      womo_locale_get_string(STR_WIFI_SELECT_PLACEHOLDER));
    lv_obj_set_style_text_font(popup_title, WOMO_FONT_MEDIUM, 0);
    lv_obj_set_flex_grow(popup_title, 1);

    lv_obj_t *popup_close = lv_btn_create(popup_hdr);
    lv_obj_set_style_pad_all(popup_close, 6, 0);
    lv_obj_add_event_cb(popup_close, scan_popup_close_event_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_lbl = lv_label_create(popup_close);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);

    /* Scrollbare Liste */
    s_ctx.wifi_scan_list = lv_list_create(s_ctx.wifi_scan_popup);
    lv_obj_set_width(s_ctx.wifi_scan_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_ctx.wifi_scan_list, 1);

    for (size_t i = 0; i < s_ctx.scan_result_count; i++) {
        const char *ssid = s_ctx.scan_results[i].ssid[0]
                               ? s_ctx.scan_results[i].ssid : "<hidden>";
        uint8_t percent = wifi_rssi_to_percent(s_ctx.scan_results[i].rssi);
        char text[96];
        snprintf(text, sizeof(text), "%s  (%d dBm / %u%%)",
                 ssid, s_ctx.scan_results[i].rssi, percent);
        lv_obj_t *btn = lv_list_add_btn(s_ctx.wifi_scan_list,
                                         LV_SYMBOL_WIFI, text);
        lv_obj_add_event_cb(btn, scan_list_btn_event_cb,
                            LV_EVENT_CLICKED, NULL);
    }
}

static void wifi_password_event_cb(lv_event_t *e)
{
    if (!s_ctx.wifi_password_area || !s_ctx.wifi_keyboard) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(s_ctx.wifi_keyboard, s_ctx.wifi_password_area);
        lv_obj_clear_flag(s_ctx.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
        lvgl_touch_set_fast_mode(true);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(s_ctx.wifi_keyboard, NULL);
        lv_obj_add_flag(s_ctx.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
        lvgl_touch_set_fast_mode(false);
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        const char *pwd = lv_textarea_get_text(s_ctx.wifi_password_area);
        strncpy(s_ctx.entered_password, pwd, sizeof(s_ctx.entered_password) - 1);
        s_ctx.entered_password[sizeof(s_ctx.entered_password) - 1] = '\0';
    } else if (code == LV_EVENT_READY) {
        const char *pwd = lv_textarea_get_text(s_ctx.wifi_password_area);
        strncpy(s_ctx.entered_password, pwd, sizeof(s_ctx.entered_password) - 1);
        s_ctx.entered_password[sizeof(s_ctx.entered_password) - 1] = '\0';

        // Tastatur schließen
        lv_keyboard_set_textarea(s_ctx.wifi_keyboard, NULL);
        lv_obj_add_flag(s_ctx.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
        lvgl_touch_set_fast_mode(false);

        // Enter/OK gedrückt: sofort WLAN aktivieren (wie Kippschalter auf ON)
        ensure_selected_ssid_from_snapshot();
        if (s_ctx.selected_ssid[0] == '\0') {
            set_wifi_activity_text(womo_locale_get_string(STR_WIFI_SELECT_PLACEHOLDER));
            return;
        }

        // Kippschalter visuell setzen, ohne Event-Rekursion
        if (s_ctx.wifi_switch) {
            s_ctx.wifi_switch_internal = true;
            lv_obj_add_state(s_ctx.wifi_switch, LV_STATE_CHECKED);
            s_ctx.wifi_switch_internal = false;
        }

        start_wifi_connect_task(s_ctx.selected_ssid, s_ctx.entered_password);
        set_wifi_activity_text(womo_locale_get_string(STR_WIFI_STATUS_CONNECTING));

        /* Buttons ausblenden nach Verbindungsstart */
        if (s_ctx.wifi_btn_row)
            lv_obj_add_flag(s_ctx.wifi_btn_row, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ─── Verbinden-Button: startet WLAN-Verbindung ─────── */
static void wifi_connect_btn_event_cb(lv_event_t *e)
{
    (void)e;
    if (s_ctx.selected_ssid[0] == '\0') {
        set_wifi_activity_text(womo_locale_get_string(STR_WIFI_SELECT_PLACEHOLDER));
        return;
    }

    /* Passwort aus Textarea übernehmen */
    if (s_ctx.wifi_password_area) {
        const char *pwd = lv_textarea_get_text(s_ctx.wifi_password_area);
        strncpy(s_ctx.entered_password, pwd, sizeof(s_ctx.entered_password) - 1);
        s_ctx.entered_password[sizeof(s_ctx.entered_password) - 1] = '\0';
    }

    /* Tastatur schließen, falls offen */
    if (s_ctx.wifi_keyboard) {
        lv_keyboard_set_textarea(s_ctx.wifi_keyboard, NULL);
        lv_obj_add_flag(s_ctx.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
        lvgl_touch_set_fast_mode(false);
    }

    /* Kippschalter visuell auf ON */
    if (s_ctx.wifi_switch) {
        s_ctx.wifi_switch_internal = true;
        lv_obj_add_state(s_ctx.wifi_switch, LV_STATE_CHECKED);
        s_ctx.wifi_switch_internal = false;
    }

    start_wifi_connect_task(s_ctx.selected_ssid, s_ctx.entered_password);
    set_wifi_activity_text(womo_locale_get_string(STR_WIFI_STATUS_CONNECTING));

    /* Passwort- und Button-Bereich ausblenden */
    if (s_ctx.wifi_password_label)
        lv_obj_add_flag(s_ctx.wifi_password_label, LV_OBJ_FLAG_HIDDEN);
    if (s_ctx.wifi_password_area)
        lv_obj_add_flag(s_ctx.wifi_password_area, LV_OBJ_FLAG_HIDDEN);
    if (s_ctx.wifi_btn_row)
        lv_obj_add_flag(s_ctx.wifi_btn_row, LV_OBJ_FLAG_HIDDEN);
}

/* ─── Abbrechen-Button: Auswahl zurücksetzen ─────────── */
static void wifi_cancel_btn_event_cb(lv_event_t *e)
{
    (void)e;

    /* Tastatur schließen, falls offen */
    if (s_ctx.wifi_keyboard) {
        lv_keyboard_set_textarea(s_ctx.wifi_keyboard, NULL);
        lv_obj_add_flag(s_ctx.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
        lvgl_touch_set_fast_mode(false);
    }

    /* Auswahl zurücksetzen */
    s_ctx.selected_ssid[0] = '\0';
    s_ctx.entered_password[0] = '\0';

    /* Passwort-, Button- und Status-Bereich ausblenden/leeren */
    if (s_ctx.wifi_password_label)
        lv_obj_add_flag(s_ctx.wifi_password_label, LV_OBJ_FLAG_HIDDEN);
    if (s_ctx.wifi_password_area) {
        lv_textarea_set_text(s_ctx.wifi_password_area, "");
        lv_obj_add_flag(s_ctx.wifi_password_area, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ctx.wifi_btn_row)
        lv_obj_add_flag(s_ctx.wifi_btn_row, LV_OBJ_FLAG_HIDDEN);
    if (s_ctx.wifi_scan_status_label)
        lv_label_set_text(s_ctx.wifi_scan_status_label, "");
}

static void lte_switch_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || s_ctx.lte_switch_internal) {
        return;
    }

    if (s_ctx.lte_toggle_task) return; /* Task läuft bereits */

    bool target = s_ctx.lte_switch && lv_obj_has_state(s_ctx.lte_switch, LV_STATE_CHECKED);

    /* Status sofort anzeigen */
    if (s_ctx.lte_status_label) {
        lv_label_set_text(s_ctx.lte_status_label,
            womo_locale_get_string(target ? STR_LTE_STATUS_ENABLING
                                          : STR_LTE_STATUS_DISABLING));
    }

    BaseType_t ok = xTaskCreate(lte_toggle_task_fn, "lte_toggle",
                                TASK_STACK_SIZE,
                                (void *)(uintptr_t)target,
                                TASK_PRIO,
                                &s_ctx.lte_toggle_task);
    if (ok != pdPASS) {
        s_ctx.lte_toggle_task = NULL;
        /* Schalter zurücksetzen */
        s_ctx.lte_switch_internal = true;
        if (target) lv_obj_clear_state(s_ctx.lte_switch, LV_STATE_CHECKED);
        else        lv_obj_add_state(s_ctx.lte_switch, LV_STATE_CHECKED);
        s_ctx.lte_switch_internal = false;
    }
}

static void ensure_selected_ssid_from_snapshot(void)
{
    if (s_ctx.selected_ssid[0] != '\0') {
        return;
    }

    if (s_latest_snapshot.wifi_connected && s_latest_snapshot.wifi_ssid[0]) {
        strncpy(s_ctx.selected_ssid,
                s_latest_snapshot.wifi_ssid,
                sizeof(s_ctx.selected_ssid) - 1);
        s_ctx.selected_ssid[sizeof(s_ctx.selected_ssid) - 1] = '\0';
    }
}

static void start_wifi_connect_task(const char *ssid, const char *password)
{
    if (s_ctx.wifi_connect_task) {
        return;
    }

    ensure_selected_ssid_from_snapshot();

    wifi_connect_params_t *params = calloc(1, sizeof(wifi_connect_params_t));
    if (!params) {
        set_wifi_activity_text(womo_locale_get_string(STR_WIFI_ACTION_NOT_AVAILABLE));
        return;
    }

    strncpy(params->ssid, ssid, sizeof(params->ssid) - 1);
    if (password) {
        strncpy(params->password, password, sizeof(params->password) - 1);
    }

    BaseType_t created = xTaskCreate(wifi_connect_task,
                                     "wifi_cfg_conn",
                                     TASK_STACK_SIZE,
                                     params,
                                     TASK_PRIO,
                                     &s_ctx.wifi_connect_task);
    if (created != pdPASS) {
        free(params);
        s_ctx.wifi_connect_task = NULL;
        set_wifi_activity_text(womo_locale_get_string(STR_WIFI_ACTION_NOT_AVAILABLE));
        return;
    }

    set_wifi_activity_text(womo_locale_get_string(STR_WIFI_STATUS_CONNECTING));
}

static void start_wifi_disconnect_task(void)
{
    if (s_ctx.wifi_disconnect_task) {
        return;
    }

    BaseType_t created = xTaskCreate(wifi_disconnect_task,
                                     "wifi_cfg_disc",
                                     4096,
                                     NULL,
                                     TASK_PRIO,
                                     &s_ctx.wifi_disconnect_task);
    if (created != pdPASS) {
        s_ctx.wifi_disconnect_task = NULL;
        set_wifi_activity_text(womo_locale_get_string(STR_WIFI_ACTION_NOT_AVAILABLE));
    } else {
        set_wifi_activity_text(womo_locale_get_string(STR_WIFI_STATUS_DISCONNECTED));
    }
}

/**
 * WiFi-Connect-Task: Konfiguriert den Router-WAN-WiFi-Client (STA) per UCI.
 */
static void wifi_connect_task(void *arg)
{
    wifi_connect_params_t params = {0};
    if (arg) {
        memcpy(&params, arg, sizeof(params));
        free(arg);
    }

    if (params.ssid[0] == '\0') {
        s_ctx.wifi_connect_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Router WiFi STA → SSID='%s'", params.ssid);

    /* Router WAN-Client per UCI konfigurieren + aktivieren */
    esp_err_t err = womo_router_wifi_set_sta(params.ssid,
                                              params.password[0] ? params.password : NULL,
                                              NULL);  /* encryption = auto (psk2) */

    /* Bei Erfolg Zugangsdaten in NVS speichern */
    if (err == ESP_OK && params.password[0]) {
        router_wifi_promote(params.ssid, params.password);
    }

    if (lvgl_port_lock(-1)) {
        if (!s_ctx.overlay) {
            lvgl_port_unlock();
        } else if (err == ESP_OK) {
            char buffer[96];
            snprintf(buffer, sizeof(buffer), "%s (%s)",
                     womo_locale_get_string(STR_WIFI_SECTION_TITLE),
                     params.ssid);
            set_wifi_activity_text(buffer);
            /* Passwort-Bereich und Buttons ausblenden */
            if (s_ctx.wifi_password_label)
                lv_obj_add_flag(s_ctx.wifi_password_label, LV_OBJ_FLAG_HIDDEN);
            if (s_ctx.wifi_password_area) {
                lv_textarea_set_text(s_ctx.wifi_password_area, "");
                lv_obj_add_flag(s_ctx.wifi_password_area, LV_OBJ_FLAG_HIDDEN);
            }
            if (s_ctx.wifi_btn_row)
                lv_obj_add_flag(s_ctx.wifi_btn_row, LV_OBJ_FLAG_HIDDEN);
        } else {
            set_wifi_activity_text(womo_locale_get_string(STR_WIFI_STATUS_ERROR));
            s_ctx.wifi_switch_internal = true;
            if (s_ctx.wifi_switch) {
                lv_obj_clear_state(s_ctx.wifi_switch, LV_STATE_CHECKED);
            }
            s_ctx.wifi_switch_internal = false;
        }
        lvgl_port_unlock();
    }

    s_ctx.wifi_cooldown_until = esp_timer_get_time() + (int64_t)SWITCH_COOLDOWN_MS * 1000;
    s_ctx.wifi_connect_task = NULL;
    vTaskDelete(NULL);
}

/**
 * WiFi-Disconnect-Task: Deaktiviert den Router-WAN-WiFi-Client per UCI.
 */
static void wifi_disconnect_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Router WiFi STA deaktivieren …");
    esp_err_t err = womo_router_wifi_enable_sta(false);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Router WiFi STA deaktiviert → OK (Cooldown %ds)",
                 SWITCH_COOLDOWN_MS / 1000);
    } else {
        ESP_LOGW(TAG, "Router WiFi STA deaktivieren → FEHLER: %s",
                 esp_err_to_name(err));
    }

    if (lvgl_port_lock(-1)) {
        if (s_ctx.overlay) {
            set_wifi_activity_text(womo_locale_get_string(STR_WIFI_STATUS_DISCONNECTED));
            s_ctx.wifi_switch_internal = true;
            if (s_ctx.wifi_switch) {
                lv_obj_clear_state(s_ctx.wifi_switch, LV_STATE_CHECKED);
            }
            s_ctx.wifi_switch_internal = false;

            /* LED sofort auf rot */
            if (s_ctx.wifi_led) set_led_active(s_ctx.wifi_led, false);

            /* Scan-UI ausblenden (ohne WLAN kein Scan möglich) */
            if (s_ctx.wifi_scan_row)
                lv_obj_add_flag(s_ctx.wifi_scan_row, LV_OBJ_FLAG_HIDDEN);
            if (s_ctx.wifi_scan_status_label)
                lv_label_set_text(s_ctx.wifi_scan_status_label, "");
            if (s_ctx.wifi_password_label)
                lv_obj_add_flag(s_ctx.wifi_password_label, LV_OBJ_FLAG_HIDDEN);
            if (s_ctx.wifi_password_area) {
                lv_textarea_set_text(s_ctx.wifi_password_area, "");
                lv_obj_add_flag(s_ctx.wifi_password_area, LV_OBJ_FLAG_HIDDEN);
            }
            if (s_ctx.wifi_btn_row)
                lv_obj_add_flag(s_ctx.wifi_btn_row, LV_OBJ_FLAG_HIDDEN);
            close_scan_popup();
        }
        lvgl_port_unlock();
    }

    s_ctx.wifi_cooldown_until = esp_timer_get_time() + (int64_t)SWITCH_COOLDOWN_MS * 1000;
    s_ctx.wifi_disconnect_task = NULL;
    vTaskDelete(NULL);
}

/**
 * Router-WiFi-Scan-Task: Scannt verfügbare Netze über den Router (iwinfo).
 */
static void router_scan_task(void *arg)
{
    (void)arg;

    womo_router_scan_result_t results[MAX_SCAN_RESULTS];
    size_t count = 0;
    esp_err_t err = womo_router_wifi_scan(results, MAX_SCAN_RESULTS, &count);

    if (!lvgl_port_lock(-1)) {
        s_ctx.wifi_scan_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    s_ctx.scan_in_progress = false;
    if (s_ctx.wifi_scan_btn) {
        lv_obj_clear_state(s_ctx.wifi_scan_btn, LV_STATE_DISABLED);
    }
    if (s_ctx.wifi_spinner) {
        lv_obj_add_flag(s_ctx.wifi_spinner, LV_OBJ_FLAG_HIDDEN);
    }

    if (!s_ctx.overlay) {
        lvgl_port_unlock();
        s_ctx.wifi_scan_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    if (err != ESP_OK) {
        set_wifi_activity_text(womo_locale_get_string(STR_WIFI_ACTION_NOT_AVAILABLE));
        lvgl_port_unlock();
        s_ctx.wifi_scan_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    memset(s_ctx.scan_results, 0, sizeof(s_ctx.scan_results));

    size_t stored = 0;
    for (size_t i = 0; i < count && stored < MAX_SCAN_RESULTS; i++) {
        if (wifi_should_hide_ssid(results[i].ssid)) {
            continue;
        }
        s_ctx.scan_results[stored++] = results[i];
    }
    s_ctx.scan_result_count = stored;

    if (s_ctx.scan_result_count == 0) {
        set_wifi_activity_text(womo_locale_get_string(STR_WIFI_NO_RESULTS));
    } else {
        char buf[48];
        snprintf(buf, sizeof(buf), "%zu Netzwerk%s",
                 s_ctx.scan_result_count,
                 s_ctx.scan_result_count == 1 ? "" : "e");
        set_wifi_activity_text(buf);
        show_scan_popup();
    }

    lvgl_port_unlock();

    s_ctx.wifi_scan_task = NULL;
    vTaskDelete(NULL);
}

/**
 * LTE-Toggle-Task: Schaltet den Router-Mobilfunk per UCI ein/aus.
 */
static void lte_toggle_task_fn(void *arg)
{
    bool enable = (bool)(uintptr_t)arg;
    ESP_LOGI(TAG, "Router LTE %s …", enable ? "aktivieren" : "deaktivieren");

    esp_err_t err = womo_router_lte_enable(enable);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Router LTE %s → OK (Cooldown %ds)",
                 enable ? "aktiviert" : "deaktiviert",
                 SWITCH_COOLDOWN_MS / 1000);
    } else {
        ESP_LOGW(TAG, "Router LTE %s → FEHLER: %s",
                 enable ? "aktivieren" : "deaktivieren",
                 esp_err_to_name(err));
    }

    if (lvgl_port_lock(-1)) {
        if (s_ctx.overlay && err != ESP_OK) {
            s_ctx.lte_switch_internal = true;
            if (enable) lv_obj_clear_state(s_ctx.lte_switch, LV_STATE_CHECKED);
            else        lv_obj_add_state(s_ctx.lte_switch, LV_STATE_CHECKED);
            s_ctx.lte_switch_internal = false;
            if (s_ctx.lte_status_label) {
                lv_label_set_text(s_ctx.lte_status_label,
                    womo_locale_get_string(STR_WIFI_ACTION_NOT_AVAILABLE));
            }
        }
        lvgl_port_unlock();
    }

    s_ctx.lte_cooldown_until = esp_timer_get_time() + (int64_t)SWITCH_COOLDOWN_MS * 1000;
    s_ctx.lte_toggle_task = NULL;
    vTaskDelete(NULL);
}

static void refresh_hidden_ssid_sources(void)
{
    memset(s_ctx.softap_ssid, 0, sizeof(s_ctx.softap_ssid));

    wifi_config_t ap_cfg = {0};
    if (esp_wifi_get_config(WIFI_IF_AP, &ap_cfg) == ESP_OK) {
        size_t len = ap_cfg.ap.ssid_len;
        if (len == 0) {
            len = strlen((const char *)ap_cfg.ap.ssid);
        }
        if (len > 0) {
            if (len >= sizeof(s_ctx.softap_ssid)) {
                len = sizeof(s_ctx.softap_ssid) - 1;
            }
            memcpy(s_ctx.softap_ssid, ap_cfg.ap.ssid, len);
            s_ctx.softap_ssid[len] = '\0';
        }
    }
}

static bool wifi_should_hide_ssid(const char *ssid)
{
    if (!ssid || ssid[0] == '\0') {
        return false;
    }

    if (s_ctx.softap_ssid[0] != '\0' && strcmp(ssid, s_ctx.softap_ssid) == 0) {
        return true;
    }

    /* Eigenen Router-AP ausblenden (soll nicht als Ziel erscheinen) */
#ifdef CONFIG_WOMO_WIFI_SSID
    if (strcmp(ssid, CONFIG_WOMO_WIFI_SSID) == 0) {
        return true;
    }
#endif

    return false;
}

static uint8_t wifi_rssi_to_percent(int8_t rssi)
{
    if (rssi <= -100) {
        return 0;
    }
    if (rssi >= -50) {
        return 100;
    }
    return (uint8_t)((rssi + 100) * 2);
}

static bool point_inside_panel(const lv_point_t *point)
{
    if (!point || !s_ctx.panel) {
        return false;
    }

    lv_area_t panel_area;
    lv_obj_get_coords(s_ctx.panel, &panel_area);
    return (point->x >= panel_area.x1 &&
            point->x <= panel_area.x2 &&
            point->y >= panel_area.y1 &&
            point->y <= panel_area.y2);
}

static void configure_wifi_keyboard(void)
{
    if (!s_ctx.wifi_keyboard) {
        return;
    }

    lv_keyboard_set_map(s_ctx.wifi_keyboard,
                        LV_KEYBOARD_MODE_TEXT_LOWER,
                        s_keyboard_map_lc,
                        s_keyboard_ctrl_lc);
    lv_keyboard_set_map(s_ctx.wifi_keyboard,
                        LV_KEYBOARD_MODE_TEXT_UPPER,
                        s_keyboard_map_uc,
                        s_keyboard_ctrl_uc);
    lv_keyboard_set_mode(s_ctx.wifi_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
}