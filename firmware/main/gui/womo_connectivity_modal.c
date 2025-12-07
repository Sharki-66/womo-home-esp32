#include "womo_connectivity_modal.h"

#include "gui/womo_locale.h"
#include "gui/womo_fonts_german.h"
#include "hardware/lvgl_port.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SCAN_RESULTS          16
#define WIFI_CONNECT_STACK_SIZE   4096
#define WIFI_CONNECT_TASK_PRIO    5
#define WIFI_CONNECT_MAX_RETRY    5

typedef struct {
    lv_obj_t *overlay;
    lv_obj_t *panel;
    lv_obj_t *wifi_status_label;
    lv_obj_t *wifi_switch;
    lv_obj_t *wifi_scan_btn;
    lv_obj_t *wifi_scan_status_label;
    lv_obj_t *wifi_spinner;
    lv_obj_t *wifi_dropdown;
    lv_obj_t *wifi_password_label;
    lv_obj_t *wifi_password_area;
    lv_obj_t *wifi_keyboard;
    lv_obj_t *lte_status_label;
    lv_obj_t *lte_switch;
    lv_obj_t *lte_info_label;
    bool wifi_switch_internal;
    bool lte_switch_internal;
    bool scan_in_progress;
    char selected_ssid[33];
    char entered_password[65];
    char softap_ssid[33];
    womo_wifi_scan_result_t scan_results[MAX_SCAN_RESULTS];
    size_t scan_result_count;
    TaskHandle_t wifi_connect_task;
    TaskHandle_t wifi_disconnect_task;
} connectivity_modal_ctx_t;

typedef struct {
    char ssid[33];
    char password[65];
} wifi_connect_params_t;

static connectivity_modal_ctx_t s_ctx = {0};
static womo_connectivity_snapshot_t s_latest_snapshot = {0};
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

static void build_modal(lv_obj_t *parent);
static void destroy_modal(void);
static void wifi_switch_event_cb(lv_event_t *e);
static void wifi_scan_event_cb(lv_event_t *e);
static void wifi_dropdown_event_cb(lv_event_t *e);
static void wifi_password_event_cb(lv_event_t *e);
static void lte_switch_event_cb(lv_event_t *e);
static void close_button_event_cb(lv_event_t *e);
static void overlay_event_cb(lv_event_t *e);
static void panel_event_cb(lv_event_t *e);
static void update_wifi_status_label(void);
static void update_lte_status_label(void);
static void set_wifi_activity_text(const char *text);
static void ensure_selected_ssid_from_snapshot(void);
static void start_wifi_connect_task(const char *ssid, const char *password);
static void start_wifi_disconnect_task(void);
static void wifi_connect_task(void *arg);
static void wifi_disconnect_task(void *arg);
static void wifi_scan_callback(const womo_wifi_scan_result_t *results,
                               size_t count,
                               esp_err_t status,
                               void *user_data);
static void refresh_hidden_ssid_sources(void);
static bool wifi_should_hide_ssid(const char *ssid);
static uint8_t wifi_rssi_to_percent(int8_t rssi);
static bool point_inside_panel(const lv_point_t *point);
static void configure_wifi_keyboard(void);

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
        update_wifi_status_label();
        update_lte_status_label();
        return;
    }

    if (!parent) {
        parent = lv_scr_act();
    }

    build_modal(parent);
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

    update_wifi_status_label();
    update_lte_status_label();
}

static void build_modal(lv_obj_t *parent)
{
    refresh_hidden_ssid_sources();

    s_ctx.overlay = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ctx.overlay);
    lv_obj_set_size(s_ctx.overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_ctx.overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ctx.overlay, LV_OPA_70, 0);
    lv_obj_add_flag(s_ctx.overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ctx.overlay, overlay_event_cb, LV_EVENT_CLICKED, NULL);

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

    lv_obj_t *title_row = lv_obj_create(s_ctx.panel);
    lv_obj_remove_style_all(title_row);
    lv_obj_set_size(title_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_bottom(title_row, 8, 0);

    lv_obj_t *title_label = lv_label_create(title_row);
    lv_label_set_text(title_label, womo_locale_get_string(STR_CONNECTIVITY_TITLE));
    lv_obj_set_style_text_font(title_label, WOMO_FONT_LARGE, 0);
    lv_obj_set_style_pad_right(title_label, 12, 0);
    lv_obj_set_flex_grow(title_label, 1);

    lv_obj_t *close_btn = lv_btn_create(title_row);
    lv_obj_set_style_pad_all(close_btn, 8, 0);
    lv_obj_add_event_cb(close_btn, close_button_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, womo_locale_get_string(STR_MODAL_CLOSE_BUTTON));

    lv_obj_t *wifi_section = lv_obj_create(s_ctx.panel);
    lv_obj_remove_style_all(wifi_section);
    lv_obj_set_size(wifi_section, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wifi_section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_bottom(wifi_section, 12, 0);

    lv_obj_t *wifi_header_row = lv_obj_create(wifi_section);
    lv_obj_remove_style_all(wifi_header_row);
    lv_obj_set_size(wifi_header_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wifi_header_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_bottom(wifi_header_row, 2, 0);

    lv_obj_t *wifi_title = lv_label_create(wifi_header_row);
    lv_label_set_text(wifi_title, womo_locale_get_string(STR_WIFI_SECTION_TITLE));
    lv_obj_set_style_text_font(wifi_title, WOMO_FONT_MEDIUM, 0);
    lv_obj_set_flex_grow(wifi_title, 1);

    s_ctx.wifi_status_label = lv_label_create(wifi_header_row);
    lv_label_set_text(s_ctx.wifi_status_label, womo_locale_get_string(STR_WIFI_STATUS_DISCONNECTED));
    lv_obj_set_style_text_font(s_ctx.wifi_status_label, WOMO_FONT_SMALL, 0);
    lv_obj_set_style_text_align(s_ctx.wifi_status_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *wifi_switch_row = lv_obj_create(wifi_section);
    lv_obj_remove_style_all(wifi_switch_row);
    lv_obj_set_size(wifi_switch_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wifi_switch_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_top(wifi_switch_row, 4, 0);
    lv_obj_set_style_pad_bottom(wifi_switch_row, 4, 0);

    lv_obj_t *wifi_switch_label = lv_label_create(wifi_switch_row);
    lv_label_set_text(wifi_switch_label, womo_locale_get_string(STR_WIFI_ENABLE_SWITCH));
    lv_obj_set_style_text_font(wifi_switch_label, WOMO_FONT_SMALL, 0);
    lv_obj_set_flex_grow(wifi_switch_label, 1);

    s_ctx.wifi_switch = lv_switch_create(wifi_switch_row);
    lv_obj_add_event_cb(s_ctx.wifi_switch, wifi_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *scan_row = lv_obj_create(wifi_section);
    lv_obj_remove_style_all(scan_row);
    lv_obj_set_size(scan_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(scan_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_top(scan_row, 6, 0);
    lv_obj_set_style_pad_bottom(scan_row, 4, 0);
    lv_obj_set_style_pad_column(scan_row, 10, 0);

    s_ctx.wifi_scan_btn = lv_btn_create(scan_row);
    lv_obj_add_event_cb(s_ctx.wifi_scan_btn, wifi_scan_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_label = lv_label_create(s_ctx.wifi_scan_btn);
    lv_label_set_text(scan_label, womo_locale_get_string(STR_WIFI_SCAN_BUTTON));

    s_ctx.wifi_spinner = lv_spinner_create(scan_row, 1000, 60);
    lv_obj_set_size(s_ctx.wifi_spinner, 32, 32);
    lv_obj_add_flag(s_ctx.wifi_spinner, LV_OBJ_FLAG_HIDDEN);

    s_ctx.wifi_dropdown = lv_dropdown_create(scan_row);
    lv_dropdown_clear_options(s_ctx.wifi_dropdown);
    lv_dropdown_add_option(s_ctx.wifi_dropdown,
                           womo_locale_get_string(STR_WIFI_NO_RESULTS),
                           LV_DROPDOWN_POS_LAST);
    lv_obj_add_event_cb(s_ctx.wifi_dropdown, wifi_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_state(s_ctx.wifi_dropdown, LV_STATE_DISABLED);
    lv_obj_set_width(s_ctx.wifi_dropdown, LV_PCT(65));
    lv_obj_set_flex_grow(s_ctx.wifi_dropdown, 1);

    s_ctx.wifi_scan_status_label = lv_label_create(wifi_section);
    lv_label_set_text(s_ctx.wifi_scan_status_label, womo_locale_get_string(STR_WIFI_SELECT_PLACEHOLDER));
    lv_obj_set_style_text_font(s_ctx.wifi_scan_status_label, WOMO_FONT_SMALL, 0);
    lv_obj_set_style_pad_top(s_ctx.wifi_scan_status_label, 2, 0);
    lv_obj_set_width(s_ctx.wifi_scan_status_label, LV_PCT(100));

    s_ctx.wifi_password_label = lv_label_create(wifi_section);
    lv_label_set_text(s_ctx.wifi_password_label, womo_locale_get_string(STR_WIFI_PASSWORD_LABEL));
    lv_obj_set_style_text_font(s_ctx.wifi_password_label, WOMO_FONT_SMALL, 0);
    lv_obj_set_style_pad_top(s_ctx.wifi_password_label, 6, 0);

    s_ctx.wifi_password_area = lv_textarea_create(wifi_section);
    lv_textarea_set_password_mode(s_ctx.wifi_password_area, true);
    lv_textarea_set_password_show_time(s_ctx.wifi_password_area, 800);
    lv_textarea_set_placeholder_text(s_ctx.wifi_password_area,
                                     womo_locale_get_string(STR_WIFI_PASSWORD_PLACEHOLDER));
    lv_textarea_set_max_length(s_ctx.wifi_password_area, sizeof(s_ctx.entered_password) - 1);
    lv_obj_set_width(s_ctx.wifi_password_area, LV_PCT(65));
    lv_obj_set_height(s_ctx.wifi_password_area, 38);
    lv_obj_set_style_pad_top(s_ctx.wifi_password_area, 4, 0);
    lv_obj_set_style_pad_bottom(s_ctx.wifi_password_area, 4, 0);
    lv_obj_set_style_pad_left(s_ctx.wifi_password_area, 6, 0);
    lv_obj_set_style_pad_right(s_ctx.wifi_password_area, 6, 0);
    lv_obj_add_event_cb(s_ctx.wifi_password_area, wifi_password_event_cb, LV_EVENT_ALL, NULL);

    s_ctx.wifi_keyboard = lv_keyboard_create(s_ctx.overlay);
    lv_obj_set_size(s_ctx.wifi_keyboard, LV_PCT(90), 180);
    lv_obj_align(s_ctx.wifi_keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_keyboard_set_textarea(s_ctx.wifi_keyboard, NULL);
    lv_obj_add_flag(s_ctx.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
    configure_wifi_keyboard();

    lv_obj_t *lte_section = lv_obj_create(s_ctx.panel);
    lv_obj_remove_style_all(lte_section);
    lv_obj_set_size(lte_section, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(lte_section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(lte_section, 12, 0);

    lv_obj_t *lte_title = lv_label_create(lte_section);
    lv_label_set_text(lte_title, womo_locale_get_string(STR_LTE_SECTION_TITLE));
    lv_obj_set_style_text_font(lte_title, WOMO_FONT_MEDIUM, 0);

    lv_obj_t *lte_switch_row = lv_obj_create(lte_section);
    lv_obj_remove_style_all(lte_switch_row);
    lv_obj_set_size(lte_switch_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(lte_switch_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_top(lte_switch_row, 6, 0);
    lv_obj_set_style_pad_bottom(lte_switch_row, 6, 0);

    lv_obj_t *lte_switch_label = lv_label_create(lte_switch_row);
    lv_label_set_text(lte_switch_label, womo_locale_get_string(STR_LTE_ENABLE_SWITCH));
    lv_obj_set_style_text_font(lte_switch_label, WOMO_FONT_SMALL, 0);
    lv_obj_set_flex_grow(lte_switch_label, 1);

    s_ctx.lte_switch = lv_switch_create(lte_switch_row);
    lv_obj_add_event_cb(s_ctx.lte_switch, lte_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_ctx.lte_status_label = lv_label_create(lte_section);
    lv_label_set_text(s_ctx.lte_status_label, womo_locale_get_string(STR_LTE_STATUS_WAITING));
    lv_obj_set_style_text_font(s_ctx.lte_status_label, WOMO_FONT_SMALL, 0);

    s_ctx.lte_info_label = lv_label_create(lte_section);
    lv_label_set_text_fmt(s_ctx.lte_info_label,
                          "%s (RS485)",
                          womo_locale_get_string(STR_WIFI_ACTION_NOT_AVAILABLE));
    lv_obj_set_style_text_font(s_ctx.lte_info_label, WOMO_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_ctx.lte_info_label, lv_palette_main(LV_PALETTE_GREY), 0);
}

static void destroy_modal(void)
{
    if (!s_ctx.overlay) {
        return;
    }

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

static void update_wifi_status_label(void)
{
    if (!s_ctx.wifi_status_label || !s_ctx.wifi_switch) {
        return;
    }

    char buffer[96];

    if (s_latest_snapshot.wifi_status == WOMO_WIFI_CONNECTING) {
        snprintf(buffer, sizeof(buffer), "%s", womo_locale_get_string(STR_WIFI_STATUS_CONNECTING));
        s_ctx.wifi_switch_internal = true;
        lv_obj_add_state(s_ctx.wifi_switch, LV_STATE_CHECKED);
        s_ctx.wifi_switch_internal = false;
    } else if (s_latest_snapshot.wifi_connected) {
        const char *ssid = s_latest_snapshot.wifi_ssid[0] ? s_latest_snapshot.wifi_ssid : "WiFi";
        snprintf(buffer, sizeof(buffer), "%s (%u%%)", ssid, s_latest_snapshot.wifi_signal_percent);
        s_ctx.wifi_switch_internal = true;
        lv_obj_add_state(s_ctx.wifi_switch, LV_STATE_CHECKED);
        s_ctx.wifi_switch_internal = false;
        if (s_ctx.selected_ssid[0] == '\0') {
            strncpy(s_ctx.selected_ssid, ssid, sizeof(s_ctx.selected_ssid) - 1);
            s_ctx.selected_ssid[sizeof(s_ctx.selected_ssid) - 1] = '\0';
        }
    } else {
        snprintf(buffer, sizeof(buffer), "%s", womo_locale_get_string(STR_WIFI_STATUS_DISCONNECTED));
        s_ctx.wifi_switch_internal = true;
        lv_obj_clear_state(s_ctx.wifi_switch, LV_STATE_CHECKED);
        s_ctx.wifi_switch_internal = false;
    }

    lv_label_set_text(s_ctx.wifi_status_label, buffer);
}

static void update_lte_status_label(void)
{
    if (!s_ctx.lte_status_label || !s_ctx.lte_switch) {
        return;
    }

    char buffer[96];

    if (s_latest_snapshot.lte_valid && s_latest_snapshot.lte_registered) {
        const char *network = s_latest_snapshot.lte_operator[0] ? s_latest_snapshot.lte_operator : "LTE";
        snprintf(buffer, sizeof(buffer), "%s (%u%%)", network, s_latest_snapshot.lte_signal_percent);
        s_ctx.lte_switch_internal = true;
        lv_obj_add_state(s_ctx.lte_switch, LV_STATE_CHECKED);
        s_ctx.lte_switch_internal = false;
    } else {
        snprintf(buffer, sizeof(buffer), "%s", womo_locale_get_string(STR_LTE_STATUS_OFFLINE));
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

    if (s_ctx.scan_in_progress) {
        return;
    }

    esp_err_t err = womo_wifi_scan_async(wifi_scan_callback, NULL);
    if (err != ESP_OK) {
        set_wifi_activity_text(womo_locale_get_string(STR_WIFI_ACTION_NOT_AVAILABLE));
        ESP_LOGW(TAG, "WiFi scan start failed: %s", esp_err_to_name(err));
        return;
    }

    s_ctx.scan_in_progress = true;
    lv_obj_add_state(s_ctx.wifi_scan_btn, LV_STATE_DISABLED);
    lv_obj_clear_flag(s_ctx.wifi_spinner, LV_OBJ_FLAG_HIDDEN);
    set_wifi_activity_text(womo_locale_get_string(STR_WIFI_SCANNING_STATUS));
}

static void wifi_dropdown_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || !s_ctx.wifi_dropdown) {
        return;
    }

    uint16_t idx = lv_dropdown_get_selected(s_ctx.wifi_dropdown);
    if (s_ctx.scan_result_count == 0 || idx >= s_ctx.scan_result_count) {
        s_ctx.selected_ssid[0] = '\0';
        return;
    }

    strncpy(s_ctx.selected_ssid,
            s_ctx.scan_results[idx].ssid,
            sizeof(s_ctx.selected_ssid) - 1);
    s_ctx.selected_ssid[sizeof(s_ctx.selected_ssid) - 1] = '\0';
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
    }
}

static void lte_switch_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED || s_ctx.lte_switch_internal) {
        return;
    }

    set_wifi_activity_text(womo_locale_get_string(STR_WIFI_ACTION_NOT_AVAILABLE));
    if (s_ctx.lte_switch) {
        s_ctx.lte_switch_internal = true;
        lv_obj_clear_state(s_ctx.lte_switch, LV_STATE_CHECKED);
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
                                     WIFI_CONNECT_STACK_SIZE,
                                     params,
                                     WIFI_CONNECT_TASK_PRIO,
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
                                     2048,
                                     NULL,
                                     WIFI_CONNECT_TASK_PRIO,
                                     &s_ctx.wifi_disconnect_task);
    if (created != pdPASS) {
        s_ctx.wifi_disconnect_task = NULL;
        set_wifi_activity_text(womo_locale_get_string(STR_WIFI_ACTION_NOT_AVAILABLE));
    } else {
        set_wifi_activity_text(womo_locale_get_string(STR_WIFI_STATUS_DISCONNECTED));
    }
}

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

    esp_err_t err = womo_wifi_disconnect();
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    womo_wifi_set_auto_reconnect(true);
    err = womo_wifi_connect(params.ssid, params.password, WIFI_CONNECT_MAX_RETRY);

    if (lvgl_port_lock(-1)) {
        if (!s_ctx.overlay) {
            lvgl_port_unlock();
        } else if (err == ESP_OK) {
            char buffer[96];
            snprintf(buffer, sizeof(buffer), "%s (%s)",
                     womo_locale_get_string(STR_WIFI_SECTION_TITLE),
                     params.ssid);
            set_wifi_activity_text(buffer);
        } else {
            set_wifi_activity_text(womo_locale_get_string(STR_WIFI_STATUS_ERROR));
            s_ctx.wifi_switch_internal = true;
            lv_obj_clear_state(s_ctx.wifi_switch, LV_STATE_CHECKED);
            s_ctx.wifi_switch_internal = false;
        }
        lvgl_port_unlock();
    }

    s_ctx.wifi_connect_task = NULL;
    vTaskDelete(NULL);
}

static void wifi_disconnect_task(void *arg)
{
    (void)arg;
    womo_wifi_disconnect();

    if (lvgl_port_lock(-1)) {
        if (s_ctx.overlay) {
            set_wifi_activity_text(womo_locale_get_string(STR_WIFI_STATUS_DISCONNECTED));
            s_ctx.wifi_switch_internal = true;
            if (s_ctx.wifi_switch) {
                lv_obj_clear_state(s_ctx.wifi_switch, LV_STATE_CHECKED);
            }
            s_ctx.wifi_switch_internal = false;
        }
        lvgl_port_unlock();
    }

    s_ctx.wifi_disconnect_task = NULL;
    vTaskDelete(NULL);
}

static void wifi_scan_callback(const womo_wifi_scan_result_t *results,
                               size_t count,
                               esp_err_t status,
                               void *user_data)
{
    (void)user_data;

    if (!lvgl_port_lock(-1)) {
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
        return;
    }

    if (status != ESP_OK) {
        set_wifi_activity_text(womo_locale_get_string(STR_WIFI_ACTION_NOT_AVAILABLE));
        lvgl_port_unlock();
        return;
    }

    memset(s_ctx.scan_results, 0, sizeof(s_ctx.scan_results));

    size_t stored = 0;
    if (results && count > 0) {
        for (size_t i = 0; i < count && stored < MAX_SCAN_RESULTS; i++) {
            if (wifi_should_hide_ssid(results[i].ssid)) {
                continue;
            }
            s_ctx.scan_results[stored++] = results[i];
        }
    }
    s_ctx.scan_result_count = stored;

    if (s_ctx.wifi_dropdown) {
        lv_dropdown_clear_options(s_ctx.wifi_dropdown);
        if (s_ctx.scan_result_count == 0) {
            lv_dropdown_add_option(s_ctx.wifi_dropdown,
                                   womo_locale_get_string(STR_WIFI_NO_RESULTS),
                                   LV_DROPDOWN_POS_LAST);
            lv_obj_add_state(s_ctx.wifi_dropdown, LV_STATE_DISABLED);
        } else {
            for (size_t i = 0; i < s_ctx.scan_result_count; i++) {
                const char *ssid = s_ctx.scan_results[i].ssid[0] ? s_ctx.scan_results[i].ssid : "<hidden>";
                uint8_t percent = wifi_rssi_to_percent(s_ctx.scan_results[i].rssi);
                char option_text[96];
                snprintf(option_text,
                         sizeof(option_text),
                         "%s (%d dBm / %u%%)",
                         ssid,
                         s_ctx.scan_results[i].rssi,
                         percent);
                lv_dropdown_add_option(s_ctx.wifi_dropdown, option_text, LV_DROPDOWN_POS_LAST);
            }
            lv_obj_clear_state(s_ctx.wifi_dropdown, LV_STATE_DISABLED);
        }
    }

    set_wifi_activity_text(womo_locale_get_string(STR_WIFI_SELECT_PLACEHOLDER));
    lvgl_port_unlock();
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