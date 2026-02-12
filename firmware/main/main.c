/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "hardware/waveshare_rgb_lcd_port.h"
#include "lvgl.h"
#include "time/womo_time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gui/womo_theme.h"
#include "gui/womo_locale.h"
#include "gui/womo_gas_bottle.h"
#include "gui/womo_weather.h"
#include "gui/womo_battery.h"
#include "gui/womo_connectivity_modal.h"
#include "gui/womo_tank.h"
#include "gui/womo_fonts_german.h"
#include "network/womo_wifi.h"
#include "network/womo_weather_http.h"
#include "network/womo_geocode.h"
#include "network/womo_router_uci.h"
#include "storage/womo_sd.h"
#include "rs485/womo_rs485_display.h"
#include "i2cdev.h"  // i2cdev for CH422G GPIO expander on display
#include "nvs.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include <stdbool.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "womo_main";

static const char *PLACEHOLDER_PRESSURE = "---- hPa";
static const char *PLACEHOLDER_IAQ = "IAQ --";
static const char *PLACEHOLDER_CO2 = "CO2 ---- ppm";
static const char *PLACEHOLDER_BVOC = "bVOC --.- ppm";
static const char *PLACEHOLDER_HUMIDITY = "--.-- %";
static const char *PLACEHOLDER_TEMPERATURE = "--.- °C";
static const char *PLACEHOLDER_GPS = "GPS : ---";
#define GEOCODE_MIN_INTERVAL_US (60LL * 1000000LL) // min. 60s zwischen Reverse-Geocode-Requests
#define GEOCODE_MIN_DELTA_DEG   0.01
#define QUIET_HOUR_START 22
#define QUIET_HOUR_END   8
#define QUIET_TOUCH_TIMEOUT_MS (5 * 60 * 1000)

// Default thresholds (percentage) for warn/critical states
static const uint8_t GAS_WARN_PERCENT = 30;
static const uint8_t GAS_CRIT_PERCENT = 10;
static const uint8_t FRESH_WARN_PERCENT = 30;
static const uint8_t FRESH_CRIT_PERCENT = 10;
static const uint8_t GREY_WARN_PERCENT = 70;  // high fill is bad
static const uint8_t GREY_CRIT_PERCENT = 90;

static const lv_point_t BACKLIGHT_RAY_POINTS[][2] = {
    {{24, 8},  {24, 2}},   // oben (bleibt innerhalb des Randes)
    {{33, 12}, {41, 4}},   // oben rechts
    {{38, 24}, {46, 24}},  // rechts
    {{10, 24}, {2, 24}},   // links
    {{15, 12}, {7, 4}},    // oben links
};


// WiFi credentials from Kconfig
#define WIFI_SSID      CONFIG_WOMO_WIFI_SSID
#define WIFI_PASSWORD  CONFIG_WOMO_WIFI_PASSWORD
#define WIFI_MAX_RETRY CONFIG_WOMO_WIFI_MAX_RETRY

// Global LVGL objects
static lv_obj_t *title_label = NULL;
static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *status_label = NULL;
static char status_label_last_text[80] = "";
static lv_obj_t *wifi_label = NULL;
static lv_obj_t *backlight_btn = NULL;
static lv_obj_t *backlight_ring = NULL;
static lv_obj_t *backlight_icon_rays[8] = {0};
static lv_obj_t *backlight_bulb_outline = NULL;
static lv_obj_t *backlight_bulb_base = NULL;
static lv_obj_t *backlight_strike = NULL;
static lv_obj_t *classic_btn = NULL;
static lv_obj_t *classic_label = NULL;
static lv_obj_t *classic_arc = NULL;
static lv_obj_t *classic_tick = NULL;
static lv_obj_t *radio_btn = NULL;
static lv_obj_t *radio_label = NULL;
static lv_obj_t *radio_arc = NULL;
static lv_obj_t *shore_label = NULL;
static lv_obj_t *shore_caption_label = NULL;
static lv_obj_t *shore_arc = NULL;
static lv_obj_t *shore_bolt_a = NULL;
static lv_obj_t *shore_bolt_b = NULL;
static lv_obj_t *shore_bolt_c = NULL;
static lv_obj_t *shore_bolt_poly = NULL;
static lv_obj_t *battery_board_label = NULL;
static lv_obj_t *battery_kfz_label = NULL;
static lv_obj_t *fresh_water_caption_label = NULL;
static lv_obj_t *grey_water_caption_label = NULL;
static lv_obj_t *location_label = NULL;
static char location_last_text[128] = "";
static lv_obj_t *temp_label = NULL;   // Temperature display
static lv_obj_t *humid_label = NULL;  // Humidity display
static lv_obj_t *press_label = NULL;  // Pressure display (outdoor)
static lv_obj_t *press_icon_label = NULL;  // Pressure trend icon (outdoor)
static lv_obj_t *press_container = NULL;   // Row container for icon + value
static lv_obj_t *temp_label_in = NULL;   // Indoor temperature display
static lv_obj_t *humid_label_in = NULL;  // Indoor humidity display
static lv_obj_t *press_label_in = NULL;  // Indoor CO2 display (eCO2)
static lv_obj_t *gas_label_in = NULL;    // Indoor IAQ display
static lv_obj_t *voc_label_in = NULL;    // Indoor bVOC display
static lv_obj_t *air_title_label_in = NULL; // Air value heading indoor
static lv_obj_t *air_title_label = NULL; // Air value heading
static lv_obj_t *imu_pitch_label = NULL;
static lv_obj_t *imu_roll_label = NULL;
static lv_obj_t *imu_heading_label = NULL;
static lv_obj_t *gps_button = NULL;  // Unsichtbarer Klickbereich für GPS-Details
static lv_obj_t *gps_label = NULL;    // GPS position
static char last_gps_text[256] = ""; // Zuletzt berechneter GPS-Text (Detailansicht)
static bool gps_details_visible = false;
static lv_timer_t *gps_hide_timer = NULL;
static lv_timer_t *backlight_quiet_timer = NULL;
static bool quiet_hours_active = false;
static lv_obj_t *bg_img = NULL;  // Background image
static uint8_t *bg_png_data = NULL;  // Loaded background PNG buffer
static size_t bg_png_size = 0;       // Size of loaded PNG
static int bg_last_day_state = -1;   // -1 unknown, 0 night, 1 day
static lv_obj_t *rs485_debug_label = NULL; // RS485 debug status
static womo_weather_t *weather_widget = NULL; // Weather widget
static womo_battery_t *main_battery = NULL;   // Battery 1 widget
static womo_battery_t *secondary_battery = NULL; // Battery 2 widget
static lv_timer_t *ui_update_timer = NULL; // Periodic UI/IMU refresh timer
static int64_t last_time_sync_try_us = 0; // Throttle multi-source time sync attempts
static const uint32_t UI_UPDATE_INTERVAL_DEFAULT_MS = 500;
static bool geocode_in_progress = false;
static int64_t geocode_last_request_us = 0;
static double geocode_last_lat = NAN;
static double geocode_last_lon = NAN;
static bool backlight_on = true;
static bool classic_on = false;
static bool radio_on = false;
static bool shore_power_present = false;
static int64_t s_pwr_cmd_sent_us = 0;     // Grace-Period nach 12V-Befehl
static int64_t s_radio_cmd_sent_us = 0;   // Grace-Period nach Radio-Befehl
#define CTRL_GRACE_PERIOD_US  5000000      // 5 s: ctrl-Daten nicht überschreiben
static TaskHandle_t wifi_autoretry_handle = NULL; // Periodischer Reconnect-Versuch

typedef enum {
    WOMO_IMU_ORIENTATION_FRONT = 0,
    WOMO_IMU_ORIENTATION_RIGHT = 1,
    WOMO_IMU_ORIENTATION_LEFT = 2,
    WOMO_IMU_ORIENTATION_INVERTED = 3,
    WOMO_IMU_ORIENTATION_MAX
} womo_imu_orientation_t;

typedef struct {
    float roll_from_roll;
    float roll_from_pitch;
    float pitch_from_roll;
    float pitch_from_pitch;
} imu_orientation_matrix_t;

static const imu_orientation_matrix_t imu_orientation_matrices[WOMO_IMU_ORIENTATION_MAX] = {
    [WOMO_IMU_ORIENTATION_FRONT] = {1.0f, 0.0f, 0.0f, 1.0f},
    [WOMO_IMU_ORIENTATION_RIGHT] = {0.0f, 1.0f, -1.0f, 0.0f},
    [WOMO_IMU_ORIENTATION_LEFT] = {0.0f, -1.0f, 1.0f, 0.0f},
    [WOMO_IMU_ORIENTATION_INVERTED] = {-1.0f, 0.0f, 0.0f, -1.0f},
};

static const char *imu_orientation_names[WOMO_IMU_ORIENTATION_MAX] = {
    [WOMO_IMU_ORIENTATION_FRONT] = "front",
    [WOMO_IMU_ORIENTATION_RIGHT] = "right",
    [WOMO_IMU_ORIENTATION_LEFT] = "left",
    [WOMO_IMU_ORIENTATION_INVERTED] = "inverted",
};

static const char *IMU_ORIENTATION_NVS_NAMESPACE = "display_cfg";
static const char *IMU_ORIENTATION_NVS_KEY = "imu_orientation";
static const womo_imu_orientation_t IMU_ORIENTATION_DEFAULT = WOMO_IMU_ORIENTATION_RIGHT;
static womo_imu_orientation_t imu_orientation = WOMO_IMU_ORIENTATION_FRONT;

typedef struct {
    bool valid;
    bool registered;
    char operator_name[32];
    float rsrp_dbm;
    uint8_t signal_percent;
} womo_lte_status_t;

static womo_lte_status_t lte_status = {0};

// Router UCI Status (wird per router_poll_task aktualisiert)
static womo_router_wifi_status_t  s_router_wifi = {0};
static womo_router_lte_status_t   s_router_lte  = {0};
static womo_router_ap_status_t    s_router_ap   = {0};
static SemaphoreHandle_t          s_router_mutex = NULL;
static TaskHandle_t              s_router_poll_handle = NULL;
#define ROUTER_POLL_INTERVAL_MS  15000

// Gas bottle widgets
static womo_gas_bottle_t *gas_bottle_a = NULL; // Gas bottle A (HX711 channel A)
static womo_gas_bottle_t *gas_bottle_b = NULL; // Gas bottle B (HX711 channel B)
static lv_obj_t *gas_label_front = NULL; // Label "Vorne" unter Flasche A
static lv_obj_t *gas_label_rear = NULL;  // Label "Hinten" unter Flasche B
static lv_obj_t *gas_info_label = NULL;  // Aktive Flasche + Restzeit/Verbrauch
static lv_obj_t *gas_replace_modal = NULL; // Dialog für Flaschenwechsel
static bool gas_nc_active = false; // true, wenn HX711 als nicht angeschlossen gemeldet wurde

// Water tank widgets
static womo_tank_t *fresh_water_tank = NULL;
static womo_tank_t *grey_water_tank = NULL;

// RS485 packet counter
static uint32_t rs485_packet_count = 0;

static portMUX_TYPE display_data_spinlock = portMUX_INITIALIZER_UNLOCKED;
static womo_sensor_data_t latest_sensor_data = {0};
static uint32_t latest_packet_count = 0;
static bool latest_data_valid = false;
static int64_t rs485_last_packet_time_us = 0;
static int64_t rs485_watchdog_start_us = 0;
static bool rs485_timeout_active = false;
static const int64_t RS485_TIMEOUT_US = 5LL * 60LL * 1000000LL;  // 5 Minuten in Mikrosekunden
static bool rs485_waiting_for_handshake = false;
static bool rs485_invalid_data_active = false;

typedef enum {
    SYSTEM_STATUS_SOURCE_MANUAL = 0,
    SYSTEM_STATUS_SOURCE_RS485_TIMEOUT,
    SYSTEM_STATUS_SOURCE_RS485_WAITING,
    SYSTEM_STATUS_SOURCE_RS485_INVALID,
    SYSTEM_STATUS_SOURCE_WIFI,
    SYSTEM_STATUS_SOURCE_BATTERY,
    SYSTEM_STATUS_SOURCE_SENSOR,
    SYSTEM_STATUS_SOURCE_MAX
} system_status_source_t;

typedef struct {
    bool active;
    womo_status_level_t level;
    womo_string_id_t detail_id;
} system_status_entry_t;

static system_status_entry_t system_status_entries[SYSTEM_STATUS_SOURCE_MAX] = {0};
static womo_status_level_t system_status_current_level = WOMO_STATUS_OK;
static womo_string_id_t system_status_current_detail = STR_MAX;
static womo_status_level_t system_status_sensor_level = WOMO_STATUS_OK;
static womo_status_level_t sensor_latched_level = WOMO_STATUS_OK;
static womo_status_level_t sensor_level_prev = WOMO_STATUS_OK;
static womo_status_level_t sensor_level_raw_last = WOMO_STATUS_OK;
static char sensor_detail_text[12] = "";
static bool sensor_ack_active = false;
static womo_status_level_t sensor_ack_level = WOMO_STATUS_OK;
static portMUX_TYPE system_status_spinlock = portMUX_INITIALIZER_UNLOCKED;

// RS485 data callback
static void rs485_data_received(const womo_sensor_data_t *data, void *user_data);
static void imu_labels_update(bool has_data,
                              float roll_deg,
                              float pitch_deg,
                              float heading_deg,
                              const char *direction);
static const char *press_trend_arrow(const char *state);
static void openweather_update_cb(const womo_weather_http_data_t *data, void *user_data);
static womo_weather_condition_t map_openweather_condition(int weather_id, bool is_night);
static womo_weather_condition_t map_day_condition_to_night(womo_weather_condition_t condition);
static void update_connectivity_label(void);
static void gps_format_coordinate(double value, bool is_latitude, char *out, size_t len);
static void ui_update_timer_cb(lv_timer_t *timer);
static void wifi_label_event_cb(lv_event_t *event);
static void status_label_event_cb(lv_event_t *event);
static void backlight_button_event_cb(lv_event_t *event);
static void classic_button_event_cb(lv_event_t *event);
static void radio_button_event_cb(lv_event_t *event);
static void geocode_result_cb(const womo_geocode_result_t *result, void *user_data);
static void geocode_trigger_if_needed(const womo_sensor_data_t *snapshot);
static void backlight_update_label(void);
static void simple_toggle_button_update(lv_obj_t *btn, lv_obj_t *label, bool active, const char *text, lv_color_t active_color);
static lv_obj_t *ensure_arc(lv_obj_t *arc, lv_obj_t *parent, lv_color_t color, bool open_top);
static void update_classic_icon(lv_color_t color, bool active);
static void update_radio_icon(lv_color_t color, bool active);
static void update_shore_icon(lv_color_t color, bool active);
static void shore_power_update_label(void);
static void backlight_start_quiet_timer(void);
static void backlight_stop_quiet_timer(void);
static void wifi_autoretry_task(void *arg);
static void router_poll_task(void *arg);
static bool is_quiet_hours(const struct tm *timeinfo);
static void backlight_set(bool on);
static void connectivity_snapshot_fill(womo_connectivity_snapshot_t *snapshot);
static void imu_orientation_set(womo_imu_orientation_t orientation, bool persist);
static esp_err_t imu_orientation_store_to_nvs(uint8_t value);
static void imu_orientation_apply(float *roll_deg, float *pitch_deg);
static void imu_orientation_load_from_nvs(void);
static void rs485_event_handler(womo_rs485_event_t event, void *user_data);
static void gas_replace_show_modal(uint8_t slot);
static void gas_replace_close_modal(void);
static void gas_replace_msgbox_event_cb(lv_event_t *event);
static void gas_bottle_clicked_cb(lv_event_t *event);
static void gas_replace_send_timer_cb(lv_timer_t *timer);

static void imu_labels_update(bool has_data,
                              float roll_deg,
                              float pitch_deg,
                              float heading_deg,
                              const char *direction)
{
    if (!imu_pitch_label && !imu_roll_label && !imu_heading_label) {
        return;
    }

    static bool last_has_data = false;
    static char last_pitch_text[48] = "";
    static char last_roll_text[48] = "";
    static char last_heading_text[64] = "";

    const char *pitch_placeholder = "Pitch: --.-°";
    const char *roll_placeholder = "Roll : --.-°";
    const char *heading_placeholder = "-- (---°)";

    if (has_data) {
        char pitch_buf[48];
        char roll_buf[48];
        char heading_buf[64];
        const char *dir = (direction && direction[0]) ? direction : "?";

        snprintf(pitch_buf, sizeof(pitch_buf), "Pitch: %+0.1f°", pitch_deg);
        snprintf(roll_buf, sizeof(roll_buf), "Roll : %+0.1f°", roll_deg);
        snprintf(heading_buf, sizeof(heading_buf), "%s (%.1f°)", dir, heading_deg);

        if (imu_pitch_label && strcmp(pitch_buf, last_pitch_text) != 0) {
            lv_label_set_text(imu_pitch_label, pitch_buf);
            strncpy(last_pitch_text, pitch_buf, sizeof(last_pitch_text) - 1);
            last_pitch_text[sizeof(last_pitch_text) - 1] = '\0';
        }
        if (imu_roll_label && strcmp(roll_buf, last_roll_text) != 0) {
            lv_label_set_text(imu_roll_label, roll_buf);
            strncpy(last_roll_text, roll_buf, sizeof(last_roll_text) - 1);
            last_roll_text[sizeof(last_roll_text) - 1] = '\0';
        }
        if (imu_heading_label && strcmp(heading_buf, last_heading_text) != 0) {
            lv_label_set_text(imu_heading_label, heading_buf);
            strncpy(last_heading_text, heading_buf, sizeof(last_heading_text) - 1);
            last_heading_text[sizeof(last_heading_text) - 1] = '\0';
        }

        last_has_data = true;
        return;
    }

    if (!last_has_data) {
        return;
    }

    if (imu_pitch_label && strcmp(pitch_placeholder, last_pitch_text) != 0) {
        lv_label_set_text(imu_pitch_label, pitch_placeholder);
        strncpy(last_pitch_text, pitch_placeholder, sizeof(last_pitch_text) - 1);
        last_pitch_text[sizeof(last_pitch_text) - 1] = '\0';
    }
    if (imu_roll_label && strcmp(roll_placeholder, last_roll_text) != 0) {
        lv_label_set_text(imu_roll_label, roll_placeholder);
        strncpy(last_roll_text, roll_placeholder, sizeof(last_roll_text) - 1);
        last_roll_text[sizeof(last_roll_text) - 1] = '\0';
    }
    if (imu_heading_label && strcmp(heading_placeholder, last_heading_text) != 0) {
        lv_label_set_text(imu_heading_label, heading_placeholder);
        strncpy(last_heading_text, heading_placeholder, sizeof(last_heading_text) - 1);
        last_heading_text[sizeof(last_heading_text) - 1] = '\0';
    }

    last_has_data = false;
}

static void apply_text_theme_colors(void)
{
    lv_color_t text_color = womo_theme_is_daytime() ? lv_color_black() : lv_color_white();
    lv_color_t classic_color = lv_color_hex(0x2E7D32);
    lv_color_t radio_color = lv_color_hex(0x1565C0);
    lv_color_t shore_color = lv_color_hex(0xF9A825);

    if (title_label) lv_obj_set_style_text_color(title_label, text_color, 0);
    if (time_label) lv_obj_set_style_text_color(time_label, text_color, 0);
    if (date_label) lv_obj_set_style_text_color(date_label, text_color, 0);
    if (status_label) lv_obj_set_style_text_color(status_label, text_color, 0);
    if (status_label) lv_obj_set_style_border_color(status_label, text_color, 0);
    if (wifi_label) lv_obj_set_style_text_color(wifi_label, text_color, 0);
    if (wifi_label) lv_obj_set_style_border_color(wifi_label, text_color, 0);
    if (air_title_label) lv_obj_set_style_text_color(air_title_label, text_color, 0);
    if (press_label) lv_obj_set_style_text_color(press_label, text_color, 0);
    if (press_icon_label) lv_obj_set_style_text_color(press_icon_label, text_color, 0);
    if (humid_label) lv_obj_set_style_text_color(humid_label, text_color, 0);
    if (temp_label) lv_obj_set_style_text_color(temp_label, text_color, 0);
    // Innenraum-Werte: keine Tag/Nacht-Färbung, IAQ/CO2/bVOC werden dynamisch gefärbt
    if (air_title_label_in) lv_obj_set_style_text_color(air_title_label_in, lv_color_black(), 0);
    if (humid_label_in) lv_obj_set_style_text_color(humid_label_in, lv_color_black(), 0);
    if (temp_label_in) lv_obj_set_style_text_color(temp_label_in, lv_color_black(), 0);
    if (rs485_debug_label) lv_obj_set_style_text_color(rs485_debug_label, text_color, 0);
    if (imu_pitch_label) lv_obj_set_style_text_color(imu_pitch_label, lv_color_white(), 0);
    if (imu_roll_label) lv_obj_set_style_text_color(imu_roll_label, lv_color_white(), 0);
    if (imu_heading_label) lv_obj_set_style_text_color(imu_heading_label, lv_color_white(), 0);
    if (gps_label) lv_obj_set_style_text_color(gps_label, text_color, 0);
    if (gps_button) {
        lv_color_t border = womo_theme_is_daytime() ? lv_color_black() : lv_color_white();
        lv_color_t bg = womo_theme_is_daytime() ? lv_color_hex(0xE0E0E0) : lv_color_hex(0x404040);
        lv_obj_set_style_border_color(gps_button, border, 0);
        lv_obj_set_style_bg_color(gps_button, bg, 0);
        lv_obj_set_style_bg_opa(gps_button, LV_OPA_30, 0);
    }
    if (location_label) lv_obj_set_style_text_color(location_label, text_color, 0);
    lv_color_t tank_label_color = lv_color_black();
    if (fresh_water_tank) womo_tank_set_text_color(fresh_water_tank, tank_label_color);
    if (grey_water_tank) womo_tank_set_text_color(grey_water_tank, tank_label_color);
    if (fresh_water_caption_label) lv_obj_set_style_text_color(fresh_water_caption_label, lv_color_black(), 0);
    if (grey_water_caption_label) lv_obj_set_style_text_color(grey_water_caption_label, lv_color_black(), 0);
    if (gas_label_front) lv_obj_set_style_text_color(gas_label_front, lv_color_black(), 0);
    if (gas_label_rear) lv_obj_set_style_text_color(gas_label_rear, lv_color_black(), 0);
    if (classic_btn && classic_label) {
        simple_toggle_button_update(classic_btn, classic_label, classic_on, "", classic_color);
        update_classic_icon(classic_color, classic_on);
    }
    if (radio_btn && radio_label) {
        simple_toggle_button_update(radio_btn, radio_label, radio_on, "Radio", radio_color);
        update_radio_icon(radio_color, radio_on);
    }
    if (shore_label) {
        shore_power_update_label();
        update_shore_icon(shore_color, shore_power_present);
    }

    // Update backlight icon colors to match day/night theme
    backlight_update_label();
}

static const char *press_trend_arrow(const char *state)
{
    // Icons aus Material Symbols Rounded (UTF-8 encodiert)
    // trending_flat (U+E8E4), trending_up (U+E8E5), trending_down (U+E8E3)
    // north_east (U+F1E0), south_east (U+F1E3)
    if (!state || state[0] == '\0' || strcmp(state, "steady") == 0) {
        return "\xEE\xA3\xA4";  // trending_flat
    }
    if (strcmp(state, "rise_slow") == 0) {
        return "\xEF\x87\xA0";  // north_east
    }
    if (strcmp(state, "rise_fast") == 0) {
        return "\xEE\xA3\xA5";  // trending_up
    }
    if (strcmp(state, "fall_slow") == 0) {
        return "\xEF\x87\xA3";  // south_east
    }
    if (strcmp(state, "fall_fast") == 0) {
        return "\xEE\xA3\xA3";  // trending_down
    }
    return "\xEE\xA3\xA4";
}

static lv_color_t press_trend_color(const char *state)
{
    // Abwärts: Orange/Rot, Aufwärts: Blau/Grün, Sonst: Theme-Textfarbe
    if (state && strcmp(state, "rise_slow") == 0) {
        return lv_color_make(30, 144, 255); // DodgerBlue
    }
    if (state && strcmp(state, "rise_fast") == 0) {
        return lv_color_make(0, 170, 0); // Grün
    }
    if (state && strcmp(state, "fall_slow") == 0) {
        return lv_color_make(255, 165, 0); // Orange
    }
    if (state && strcmp(state, "fall_fast") == 0) {
        return lv_color_make(192, 0, 0); // Rot
    }
    // steady/unknown -> Standard-Textfarbe abhängig vom Theme
    return womo_theme_is_daytime() ? lv_color_black() : lv_color_white();
}

static womo_status_level_t evaluate_low_is_bad(uint8_t percent, uint8_t warn_threshold, uint8_t crit_threshold)
{
    if (percent <= crit_threshold) {
        return WOMO_STATUS_ERROR;
    }
    if (percent <= warn_threshold) {
        return WOMO_STATUS_WARNING;
    }
    return WOMO_STATUS_OK;
}

static womo_status_level_t evaluate_high_is_bad(uint8_t percent, uint8_t warn_threshold, uint8_t crit_threshold)
{
    if (percent >= crit_threshold) {
        return WOMO_STATUS_ERROR;
    }
    if (percent >= warn_threshold) {
        return WOMO_STATUS_WARNING;
    }
    return WOMO_STATUS_OK;
}

static womo_status_level_t battery_status_from_voltage(float voltage)
{
    if (!isfinite(voltage)) {
        return WOMO_STATUS_OK;
    }
    if (voltage <= 11.5f) {
        return WOMO_STATUS_ERROR;
    }
    if (voltage <= 12.0f) {
        return WOMO_STATUS_WARNING;
    }
    return WOMO_STATUS_OK;
}

static void gps_format_coordinate(double value, bool is_latitude, char *out, size_t len)
{
    if (!out || len == 0) {
        return;
    }

    const char positive_hemi = is_latitude ? 'N' : 'E';
    const char negative_hemi = is_latitude ? 'S' : 'W';
    const char hemisphere = (value >= 0.0) ? positive_hemi : negative_hemi;
    double abs_val = fabs(value);

    if (!isfinite(abs_val)) {
        snprintf(out, len, "--.-°%c", hemisphere);
        return;
    }

    snprintf(out, len, "%.5f°%c", abs_val, hemisphere);
}

static void geocode_result_cb(const womo_geocode_result_t *result, void *user_data)
{
    geocode_in_progress = false;
    geocode_last_request_us = esp_timer_get_time();

    if (!result || !result->valid || !location_label) {
        return;
    }

    if (lvgl_port_lock(-1)) {
        const char *text = result->short_name[0] ? result->short_name : result->display_name;
        if (text && text[0] != '\0') {
            if (strcmp(text, location_last_text) != 0) {
                lv_label_set_text(location_label, text);
                strlcpy(location_last_text, text, sizeof(location_last_text));
            }
        }
        lvgl_port_unlock();
    }
}

static void geocode_trigger_if_needed(const womo_sensor_data_t *snapshot)
{
    if (!snapshot) {
        return;
    }

    if (!womo_wifi_is_connected()) {
        return; // benötigt Internet
    }

    if (geocode_in_progress) {
        return;
    }

    const bool gps_ok = snapshot->gps.valid;
    if (!gps_ok) {
        return;
    }

    const bool coords_finite = isfinite(snapshot->gps.latitude) && isfinite(snapshot->gps.longitude);
    const bool coords_nonzero = fabs(snapshot->gps.latitude) + fabs(snapshot->gps.longitude) > 0.0001;
    if (!coords_finite || !coords_nonzero) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if (geocode_last_request_us > 0 && (now_us - geocode_last_request_us) < GEOCODE_MIN_INTERVAL_US) {
        return;
    }

    if (isfinite(geocode_last_lat) && isfinite(geocode_last_lon)) {
        double dlat = fabs(snapshot->gps.latitude - geocode_last_lat);
        double dlon = fabs(snapshot->gps.longitude - geocode_last_lon);
        if (dlat < GEOCODE_MIN_DELTA_DEG && dlon < GEOCODE_MIN_DELTA_DEG) {
            return; // keine relevante Bewegung
        }
    }

    geocode_in_progress = true;
    const char *accept_lang = (womo_locale_get() == WOMO_LOCALE_DE) ? "de" : "en";
    esp_err_t err = womo_geocode_reverse_request(snapshot->gps.latitude,
                                                 snapshot->gps.longitude,
                                                 accept_lang,
                                                 geocode_result_cb,
                                                 NULL);
    if (err != ESP_OK) {
        geocode_in_progress = false;
        geocode_last_lat = NAN;
        geocode_last_lon = NAN;
        ESP_LOGW(TAG, "Geocode-Request fehlgeschlagen: %s", esp_err_to_name(err));
    } else {
        geocode_last_lat = snapshot->gps.latitude;
        geocode_last_lon = snapshot->gps.longitude;
    }
}

static void backlight_quiet_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    ESP_LOGI(TAG, "Backlight quiet timer fired (quiet_hours=%d, backlight_on=%d)", quiet_hours_active, backlight_on);
    backlight_set(false);
}

static void backlight_start_quiet_timer(void)
{
    if (backlight_quiet_timer == NULL) {
        backlight_quiet_timer = lv_timer_create(backlight_quiet_timer_cb, QUIET_TOUCH_TIMEOUT_MS, NULL);
        ESP_LOGI(TAG, "Backlight quiet timer started (%u ms)", (unsigned)QUIET_TOUCH_TIMEOUT_MS);
    } else {
        lv_timer_set_period(backlight_quiet_timer, QUIET_TOUCH_TIMEOUT_MS);
        lv_timer_reset(backlight_quiet_timer);
        ESP_LOGI(TAG, "Backlight quiet timer reset (%u ms)", (unsigned)QUIET_TOUCH_TIMEOUT_MS);
    }
}

static void backlight_stop_quiet_timer(void)
{
    if (backlight_quiet_timer) {
        lv_timer_del(backlight_quiet_timer);
        backlight_quiet_timer = NULL;
    }
}

static bool is_quiet_hours(const struct tm *timeinfo)
{
    if (!timeinfo) {
        return false;
    }
    int hour = timeinfo->tm_hour;
    return (hour >= QUIET_HOUR_START) || (hour < QUIET_HOUR_END);
}

static void update_connectivity_label(void)
{
    if (!wifi_label) {
        return;
    }

    char wifi_line[64];
    char lte_line[64];
    char combined[140];

    /* Router-Daten unter Mutex kopieren */
    womo_router_wifi_status_t rw = {0};
    womo_router_lte_status_t  rl = {0};
    if (s_router_mutex) {
        xSemaphoreTake(s_router_mutex, portMAX_DELAY);
        rw = s_router_wifi;
        rl = s_router_lte;
        xSemaphoreGive(s_router_mutex);
    }

    /* WiFi-Zeile: Router WAN WiFi-Client (externes WLAN) */
    if (!womo_wifi_is_connected()) {
        /* ESP32 selbst hat keine Verbindung zum Router-AP */
        snprintf(wifi_line, sizeof(wifi_line), "WiFi: Router offline");
    } else if (rw.connected && rw.ssid[0]) {
        snprintf(wifi_line, sizeof(wifi_line), "WiFi: %s %u%%",
                 rw.ssid, rw.signal_percent);
    } else {
        snprintf(wifi_line, sizeof(wifi_line), "WiFi: nicht verbunden");
    }

    /* LTE-Zeile: Router Mobilfunk */
    if (rl.registered && rl.operator_name[0]) {
        snprintf(lte_line, sizeof(lte_line), "LTE : %s %u%%",
                 rl.operator_name, rl.signal_percent);
    } else if (rl.operator_name[0]) {
        snprintf(lte_line, sizeof(lte_line), "LTE : %s 0%%", rl.operator_name);
    } else {
        snprintf(lte_line, sizeof(lte_line), "LTE : -- 0%%");
    }

    snprintf(combined, sizeof(combined), "%s\n%s", wifi_line, lte_line);
    lv_label_set_text(wifi_label, combined);
}

static void connectivity_snapshot_fill(womo_connectivity_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));

    /* Router-Daten unter Mutex kopieren */
    womo_router_wifi_status_t rw = {0};
    womo_router_lte_status_t  rl = {0};
    womo_router_ap_status_t   ra = {0};
    if (s_router_mutex) {
        xSemaphoreTake(s_router_mutex, portMAX_DELAY);
        rw = s_router_wifi;
        rl = s_router_lte;
        ra = s_router_ap;
        xSemaphoreGive(s_router_mutex);
    }

    /* AP / HotSpot */
    snapshot->ap_enabled = ra.enabled;
    snapshot->ap_clients = ra.clients;
    memcpy(snapshot->ap_client_list, ra.client_list, sizeof(snapshot->ap_client_list));
    if (ra.ssid[0]) {
        strncpy(snapshot->ap_ssid, ra.ssid, sizeof(snapshot->ap_ssid) - 1);
        snapshot->ap_ssid[sizeof(snapshot->ap_ssid) - 1] = '\0';
    }

    /* WiFi = Router WAN Client */
    snapshot->wifi_connected = rw.connected;
    snapshot->wifi_status = rw.connected ? WOMO_WIFI_CONNECTED : WOMO_WIFI_DISCONNECTED;
    snapshot->wifi_signal_percent = rw.signal_percent;
    if (rw.ssid[0]) {
        strncpy(snapshot->wifi_ssid, rw.ssid, sizeof(snapshot->wifi_ssid) - 1);
        snapshot->wifi_ssid[sizeof(snapshot->wifi_ssid) - 1] = '\0';
    }

    /* LTE = Router Mobilfunk */
    snapshot->lte_valid = (rl.operator_name[0] != '\0' || rl.signal_percent > 0);
    snapshot->lte_registered = rl.registered;
    snapshot->lte_signal_percent = rl.signal_percent;
    if (rl.operator_name[0]) {
        strncpy(snapshot->lte_operator, rl.operator_name,
                sizeof(snapshot->lte_operator) - 1);
        snapshot->lte_operator[sizeof(snapshot->lte_operator) - 1] = '\0';
    }
}

static void status_label_apply_text(const char *text)
{
    if (!status_label || !text) {
        return;
    }

    if (strcmp(text, status_label_last_text) == 0) {
        return;
    }

    lv_label_set_text(status_label, text);
    strncpy(status_label_last_text, text, sizeof(status_label_last_text) - 1);
    status_label_last_text[sizeof(status_label_last_text) - 1] = '\0';
}

static womo_string_id_t status_level_to_locale_id(womo_status_level_t level)
{
    switch (level) {
        case WOMO_STATUS_OK:
            return STR_STATUS_OK;
        case WOMO_STATUS_WARNING:
            return STR_STATUS_WARNING;
        case WOMO_STATUS_ERROR:
            return STR_STATUS_ERROR;
        case WOMO_STATUS_CRITICAL:
            return STR_STATUS_CRITICAL;
        default:
            return STR_STATUS_OK;
    }
}

static void status_label_update_default(void)
{
    womo_status_level_t current = womo_theme_get_status();
    womo_string_id_t status_id = status_level_to_locale_id(current);

    char buf[80];
    snprintf(buf,
             sizeof(buf),
             "%s %s",
             womo_locale_get_string(STR_STATUS),
             womo_locale_get_string(status_id));
    status_label_apply_text(buf);
}

static void status_label_show_detail(womo_status_level_t level, const char *detail)
{
    if (!detail || detail[0] == '\0') {
        return;
    }

    char buf[96];
    snprintf(buf,
             sizeof(buf),
             "%s %s (%s)",
             womo_locale_get_string(STR_STATUS),
             womo_locale_get_string(status_level_to_locale_id(level)),
             detail);
    status_label_apply_text(buf);
}

static void system_status_apply(bool force_label_update)
{
    system_status_entry_t entries_copy[SYSTEM_STATUS_SOURCE_MAX];
    taskENTER_CRITICAL(&system_status_spinlock);
    memcpy(entries_copy, system_status_entries, sizeof(entries_copy));
    taskEXIT_CRITICAL(&system_status_spinlock);

    womo_status_level_t resolved = WOMO_STATUS_OK;
    womo_string_id_t detail_id = STR_MAX;
    bool detail_valid = false;
    system_status_source_t resolved_source = SYSTEM_STATUS_SOURCE_MAX;

    for (int i = 0; i < SYSTEM_STATUS_SOURCE_MAX; ++i) {
        if (i == SYSTEM_STATUS_SOURCE_RS485_TIMEOUT ||
            i == SYSTEM_STATUS_SOURCE_RS485_WAITING ||
            i == SYSTEM_STATUS_SOURCE_RS485_INVALID) {
            continue; // RS485 soll das Theme nicht mehr beeinflussen
        }
        if (!entries_copy[i].active) {
            continue;
        }

        if (entries_copy[i].level > resolved) {
            resolved = entries_copy[i].level;
            detail_id = entries_copy[i].detail_id;
            detail_valid = (detail_id < STR_MAX);
            resolved_source = (system_status_source_t)i;
        } else if (entries_copy[i].level == resolved && !detail_valid && entries_copy[i].detail_id < STR_MAX) {
            detail_id = entries_copy[i].detail_id;
            detail_valid = true;
            resolved_source = (system_status_source_t)i;
        }
    }

    bool level_changed = (resolved != system_status_current_level);
    bool detail_changed = (detail_id != system_status_current_detail);

    system_status_current_level = resolved;
    system_status_current_detail = detail_id;

    womo_theme_set_status(resolved);
    if (level_changed) {
        womo_theme_apply_to_screen(NULL);
    }

    if (resolved_source == SYSTEM_STATUS_SOURCE_SENSOR && sensor_detail_text[0] != '\0') {
        char buf[96];
        snprintf(buf,
                 sizeof(buf),
                 "%s %s (%s)",
                 womo_locale_get_string(STR_STATUS),
                 womo_locale_get_string(status_level_to_locale_id(resolved)),
                 sensor_detail_text);
        status_label_apply_text(buf);
    } else if (resolved_source == SYSTEM_STATUS_SOURCE_RS485_TIMEOUT) {
        status_label_show_detail(resolved, womo_locale_get_string(STR_RS485_TIMEOUT_LABEL));
    } else if (resolved_source == SYSTEM_STATUS_SOURCE_RS485_INVALID) {
        status_label_show_detail(resolved, womo_locale_get_string(STR_RS485_INVALID_DATA));
    } else if (resolved_source == SYSTEM_STATUS_SOURCE_RS485_WAITING) {
        status_label_show_detail(resolved, womo_locale_get_string(STR_RS485_WAITING_HELLO));
    } else if (detail_valid) {
        if (force_label_update || level_changed || detail_changed) {
            status_label_show_detail(resolved, womo_locale_get_string(detail_id));
        }
    } else if (force_label_update || level_changed || detail_changed) {
        status_label_update_default();
    }
}

static void system_status_raise(system_status_source_t source,
                                womo_status_level_t level,
                                womo_string_id_t detail_id)
{
    if (source >= SYSTEM_STATUS_SOURCE_MAX) {
        return;
    }

    taskENTER_CRITICAL(&system_status_spinlock);
    system_status_entries[source].active = true;
    system_status_entries[source].level = level;
    system_status_entries[source].detail_id = detail_id;
    taskEXIT_CRITICAL(&system_status_spinlock);

    system_status_apply(false);
}

static void system_status_clear(system_status_source_t source)
{
    if (source >= SYSTEM_STATUS_SOURCE_MAX) {
        return;
    }

    taskENTER_CRITICAL(&system_status_spinlock);
    system_status_entries[source].active = false;
    system_status_entries[source].level = WOMO_STATUS_OK;
    system_status_entries[source].detail_id = STR_MAX;
    taskEXIT_CRITICAL(&system_status_spinlock);

    system_status_apply(false);
}

static void system_status_apply_sensor_level(womo_status_level_t level)
{
    if (level > WOMO_STATUS_OK) {
        system_status_raise(SYSTEM_STATUS_SOURCE_SENSOR, level, STR_MAX);
    } else {
        system_status_clear(SYSTEM_STATUS_SOURCE_SENSOR);
    }
    system_status_sensor_level = level;
}

static esp_err_t imu_orientation_store_to_nvs(uint8_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(IMU_ORIENTATION_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(handle, IMU_ORIENTATION_NVS_KEY, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

static void imu_orientation_set(womo_imu_orientation_t orientation, bool persist)
{
    if (orientation >= WOMO_IMU_ORIENTATION_MAX) {
        ESP_LOGW(TAG, "Invalid IMU orientation request: %d", orientation);
        return;
    }

    if (imu_orientation == orientation && !persist) {
        return;
    }

    bool changed = (imu_orientation != orientation);
    imu_orientation = orientation;

    const char *name = imu_orientation_names[imu_orientation];
    if (changed) {
        ESP_LOGI(TAG, "IMU orientation set to %s (%d)", name ? name : "unknown", imu_orientation);
    }

    if (persist) {
        esp_err_t err = imu_orientation_store_to_nvs((uint8_t)imu_orientation);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to persist IMU orientation: %s", esp_err_to_name(err));
        }
    }
}

static void imu_orientation_apply(float *roll_deg, float *pitch_deg)
{
    if (!roll_deg || !pitch_deg) {
        return;
    }

    if (imu_orientation >= WOMO_IMU_ORIENTATION_MAX) {
        imu_orientation = WOMO_IMU_ORIENTATION_FRONT;
    }

    const imu_orientation_matrix_t *matrix = &imu_orientation_matrices[imu_orientation];
    float roll = *roll_deg;
    float pitch = *pitch_deg;

    *roll_deg = roll * matrix->roll_from_roll + pitch * matrix->roll_from_pitch;
    *pitch_deg = roll * matrix->pitch_from_roll + pitch * matrix->pitch_from_pitch;
}

static void imu_orientation_load_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(IMU_ORIENTATION_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed for IMU orientation: %s", esp_err_to_name(err));
        imu_orientation_set(IMU_ORIENTATION_DEFAULT, false);
        return;
    }

    uint8_t stored = IMU_ORIENTATION_DEFAULT;
    err = nvs_get_u8(handle, IMU_ORIENTATION_NVS_KEY, &stored);
    nvs_close(handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "IMU orientation not set in NVS - defaulting to %s",
                 imu_orientation_names[IMU_ORIENTATION_DEFAULT]);
        imu_orientation_set(IMU_ORIENTATION_DEFAULT, true);
        return;
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read IMU orientation: %s", esp_err_to_name(err));
        imu_orientation_set(IMU_ORIENTATION_DEFAULT, true);
        return;
    }

    if (stored >= WOMO_IMU_ORIENTATION_MAX) {
        ESP_LOGW(TAG, "Invalid IMU orientation value %u in NVS - resetting to default", stored);
        imu_orientation_set(IMU_ORIENTATION_DEFAULT, true);
        return;
    }

    imu_orientation_set((womo_imu_orientation_t)stored, false);
}

static void rs485_event_handler(womo_rs485_event_t event, void *user_data)
{
    (void)user_data;

    switch (event) {
        case WOMO_RS485_EVENT_HELLO:
            ESP_LOGI(TAG, "RS485 hello empfangen - Walter ist bereit");
            if (rs485_waiting_for_handshake) {
                rs485_waiting_for_handshake = false;
            }
            break;
        case WOMO_RS485_EVENT_HEARTBEAT:
            // Heartbeats dienen aktuell nur zur Diagnose
            break;
        case WOMO_RS485_EVENT_INVALID_JSON:
            rs485_invalid_data_active = true;
            break;
        default:
            break;
    }
}


// Load background image from SD card
// `is_day` steuert, ob die helle (day) oder graue (night) Ducato-Variante geladen wird.
static bool load_background_image(lv_obj_t *screen, bool is_day)
{
    if (!womo_sd_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted, skipping background image");
        return false;
    }

    // Keep SD-CS asserted via CH422G before accessing files
    womo_ch422g_assert_sd_cs();
    
    const char *day_path = "/sdcard/images/Ducato-weiss.png";
    const char *night_path = "/sdcard/images/Ducato-grau.png";

    if (bg_last_day_state == (is_day ? 1 : 0) && bg_img != NULL) {
        // Already loaded matching image
        return true;
    }

    const char *img_path = is_day ? day_path : night_path;
    struct stat st;
    
    ESP_LOGI(TAG, "Loading background image (%s): %s", is_day ? "day" : "night", img_path);
    
    womo_ch422g_assert_sd_cs();
    if (stat(img_path, &st) != 0) {
        ESP_LOGW(TAG, "Background image not found: %s", img_path);
        return false;
    }
    
    // Open file with ESP-IDF FATFS
    womo_ch422g_assert_sd_cs();
    FILE *fp = fopen(img_path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open image file: %s", img_path);
        return false;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > 5 * 1024 * 1024) {  // Max 5MB
        ESP_LOGE(TAG, "Invalid file size: %ld bytes", file_size);
        fclose(fp);
        return false;
    }
    
    // Allocate buffer for PNG data
    uint8_t *png_data = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!png_data) {
        ESP_LOGE(TAG, "Failed to allocate %ld bytes for PNG", file_size);
        fclose(fp);
        return false;
    }
    
    // Read file into buffer
    size_t bytes_read = fread(png_data, 1, file_size, fp);
    fclose(fp);
    
    if (bytes_read != file_size) {
        ESP_LOGE(TAG, "Read only %d of %ld bytes", bytes_read, file_size);
        heap_caps_free(png_data);
        return false;
    }
    
    ESP_LOGI(TAG, "PNG file loaded: %ld bytes", file_size);
    
    // Create LVGL image descriptor for PNG
    static lv_img_dsc_t img_dsc;
    img_dsc.header.always_zero = 0;
    img_dsc.header.w = 0;  // PNG decoder will determine size
    img_dsc.header.h = 0;  // PNG decoder will determine size
    img_dsc.data_size = file_size;
    img_dsc.header.cf = LV_IMG_CF_RAW_ALPHA;  // Let PNG decoder handle it
    img_dsc.data = png_data;

    // Free previous PNG buffer if present
    if (bg_png_data) {
        heap_caps_free(bg_png_data);
        bg_png_data = NULL;
        bg_png_size = 0;
    }

    // Create image object if not existing
    if (!bg_img) {
        bg_img = lv_img_create(screen);
        // IMPORTANT: Set as background layer with explicit Z-index
        lv_obj_move_background(bg_img);
        lv_obj_set_style_pad_all(bg_img, 0, 0);
        // Position and size
        lv_obj_set_size(bg_img, 800, 480);
        lv_obj_align(bg_img, LV_ALIGN_CENTER, 0, 0);
    }

    // Set image source (PNG will be decoded once)
    lv_img_set_src(bg_img, &img_dsc);

    bg_png_data = png_data;
    bg_png_size = file_size;
    bg_last_day_state = is_day ? 1 : 0;
    
    // Use PNG's own transparency: Ducato = opaque, free areas = transparent
    // No additional opacity - let PNG alpha channel control transparency
    lv_obj_set_style_img_opa(bg_img, LV_OPA_COVER, 0);
    
    // Ensure image stays in background
    lv_obj_move_to_index(bg_img, 0);
    
    ESP_LOGI(TAG, "Background image applied to screen");
    return true;
}

// GPS-Detailanzeige nach Timeout wieder einklappen
static void gps_hide_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    gps_details_visible = false;
    lv_label_set_text(gps_label, "GPS");
    if (gps_button) {
        lv_obj_clear_flag(gps_button, LV_OBJ_FLAG_HIDDEN);
    }
    if (location_label) {
        lv_obj_clear_flag(location_label, LV_OBJ_FLAG_HIDDEN);
    }
}

// GPS-Label-Klick: Details zeigen und nach 10s wieder ausblenden
static void gps_label_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    gps_details_visible = true;
    const char *text = (last_gps_text[0] != '\0') ? last_gps_text : PLACEHOLDER_GPS;
    lv_label_set_text(gps_label, text);

    // Rahmen ausblenden, solange Details dargestellt werden
    if (gps_button) {
        lv_obj_add_flag(gps_button, LV_OBJ_FLAG_HIDDEN);
    }

    // Ortsname ausblenden, solange Details im Vordergrund sind
    if (location_label) {
        lv_obj_add_flag(location_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (gps_hide_timer == NULL) {
        gps_hide_timer = lv_timer_create(gps_hide_timer_cb, 10000, NULL); // 10s sichtbar
    } else {
        lv_timer_reset(gps_hide_timer);
    }
}

/* ── Touch-Wake-Callback (wird aus touchpad_read aufgerufen) ──────── */
/**
 * Wird bei JEDER Touch-Berührung aufgerufen, BEVOR LVGL das Event an ein
 * Widget dispatcht.  Gibt true zurück → Touch wird unterdrückt (kein Widget-
 * Event).  Das löst das Problem, dass lv_obj-Container (Tank, Batterie,
 * press_container …) den LV_EVENT_CLICKED schlucken, bevor er beim Screen-
 * Handler ankommt.
 */
static bool touch_wake_cb(void)
{
    if (!backlight_on) {
        ESP_LOGI(TAG, "Touch-Wake (raw callback, quiet=%d)", quiet_hours_active);
        backlight_set(true);
        if (quiet_hours_active) {
            backlight_start_quiet_timer();
        }
        return true;   /* Touch unterdrücken – nur aufwecken */
    }

    /* Display ist an, aber Quiet-Hours aktiv → Timer zurücksetzen */
    if (quiet_hours_active) {
        backlight_start_quiet_timer();
    }
    return false;  /* Touch normal weiterleiten */
}

// Touch event handler
static void screen_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_get_act(), &point);

        // Erstes Tap bei ausgeschaltetem Backlight: nur wieder einschalten
        if (!backlight_on) {
            ESP_LOGI(TAG, "Touch wake backlight (quiet=%d)", quiet_hours_active);
            backlight_set(true);
            if (quiet_hours_active) {
                backlight_start_quiet_timer();
            } else {
                backlight_stop_quiet_timer();
            }
            return;
        }

        if (quiet_hours_active) {
            ESP_LOGI(TAG, "Touch during quiet hours -> timer reset/start");
            backlight_start_quiet_timer();
        }
        
        ESP_LOGI(TAG, "Touch at: x=%d, y=%d", point.x, point.y);
        
        // Bottom left corner (date area): toggle language
        if (point.x < 200 && point.y > 400) {
            womo_locale_t current = womo_locale_get();
            womo_locale_t next = (current == WOMO_LOCALE_DE) ? WOMO_LOCALE_EN : WOMO_LOCALE_DE;
            womo_locale_set(next);
            ESP_LOGI(TAG, "Language switched to: %s", (next == WOMO_LOCALE_DE) ? "DE" : "EN");
            
            // Immediately update all static labels
            if (wifi_label && !womo_wifi_is_connected()) {
                update_connectivity_label();
            }
            if (rs485_packet_count == 0 && rs485_debug_label) {
                lv_label_set_text(rs485_debug_label, womo_locale_get_string(STR_RS485_WAITING));
            }
            system_status_apply(true);
            // Timer callback will update time/date with new language on next cycle
        }
    }
}

static void ui_update_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    womo_sensor_data_t snapshot = {0};
    womo_lte_status_t lte_snapshot = {0};
    uint32_t packet_count = 0;
    bool data_valid = false;
    bool rs485_timeout_snapshot = false;
    bool rs485_waiting_snapshot = false;
    bool rs485_invalid_snapshot = false;
    womo_status_level_t sensor_level = WOMO_STATUS_OK;
    const char *sensor_cause = NULL;
    int64_t last_packet_us = 0;
    const int64_t now_us = esp_timer_get_time();
    bool modal_open = womo_connectivity_modal_is_open();

    taskENTER_CRITICAL(&display_data_spinlock);
    snapshot = latest_sensor_data;
    lte_snapshot = lte_status;
    packet_count = latest_packet_count;
    data_valid = latest_data_valid;
    last_packet_us = rs485_last_packet_time_us;
    rs485_timeout_snapshot = rs485_timeout_active;
    rs485_waiting_snapshot = rs485_waiting_for_handshake;
    rs485_invalid_snapshot = rs485_invalid_data_active;
    taskEXIT_CRITICAL(&display_data_spinlock);

    int64_t reference_us = last_packet_us ? last_packet_us : rs485_watchdog_start_us;
    bool timed_out_now = false;
    if (reference_us > 0) {
        timed_out_now = (now_us - reference_us) >= RS485_TIMEOUT_US;
    }

    if (timed_out_now != rs485_timeout_snapshot) {
        taskENTER_CRITICAL(&display_data_spinlock);
        rs485_timeout_active = timed_out_now;
        taskEXIT_CRITICAL(&display_data_spinlock);
        rs485_timeout_snapshot = timed_out_now;

        double elapsed_s = (reference_us > 0) ?
                           (double)(now_us - reference_us) / 1000000.0 : 0.0;
        if (timed_out_now) {
            ESP_LOGE(TAG, "RS485 Timeout: keine neuen Pakete seit %.1f s", elapsed_s);
        } else {
            ESP_LOGI(TAG, "RS485 Timeout aufgehoben (Unterbrechung %.1f s)", elapsed_s);
        }
    }

    // Multi-Source Time Sync (WiFi NTP > GPS > LTE) mit dynamischer Rate
    const int64_t TIME_SYNC_RETRY_US_SYNCED = 60LL * 1000000LL; // 60s Abklingzeit nach Sync
    const int64_t TIME_SYNC_RETRY_US_UNSYNCED = 10LL * 1000000LL; // 10s wenn noch keine gültige Zeit
    bool wifi_connected_now = womo_wifi_is_connected();
    bool gps_has_time = snapshot.gps.valid && snapshot.gps.ts > 0;
    bool rs485_has_time = data_valid && snapshot.timestamp_ms > 0 && !rs485_timeout_snapshot;
    bool lte_online = lte_snapshot.valid && lte_snapshot.registered;
    bool time_synced = womo_time_is_synced();
    static bool last_wifi_connected = false;

    // Sofortiger Versuch beim Übergang auf WiFi connected, falls noch keine gültige Zeit
    if (wifi_connected_now && !last_wifi_connected && !time_synced) {
        womo_time_sync_ntp(false);
        last_time_sync_try_us = now_us;
    }

    int64_t retry_interval = time_synced ? TIME_SYNC_RETRY_US_SYNCED : TIME_SYNC_RETRY_US_UNSYNCED;
    if ((now_us - last_time_sync_try_us) >= retry_interval) {
        if (rs485_has_time) {
            time_t current_time = 0;
            time(&current_time);
            int64_t rs485_secs = (int64_t)(snapshot.timestamp_ms / 1000ULL);
            int64_t delta = llabs((int64_t)current_time - rs485_secs);

            if (!time_synced || delta > 2) {
                womo_time_sync_gps((time_t)rs485_secs);
                last_time_sync_try_us = now_us;
                time_synced = true;
            }
        } else if (gps_has_time) {
            time_t current_time = 0;
            time(&current_time);
            int64_t gps_secs = snapshot.gps.ts;
            int64_t delta = llabs((int64_t)current_time - gps_secs);

            if (!time_synced || delta > 2) {
                womo_time_sync_gps((time_t)gps_secs);
                last_time_sync_try_us = now_us;
                time_synced = true;
            }
        } else if (wifi_connected_now) {
            womo_time_sync_ntp(false);
            last_time_sync_try_us = now_us;
        } else if (lte_online) {
            // LTE-Fallback: NTP versuchen (setzt funktionierende Datenverbindung voraus)
            womo_time_sync_ntp(false);
            last_time_sync_try_us = now_us;
        }
    }
    last_wifi_connected = wifi_connected_now;

    // Connectivity label (WiFi + LTE) – Daten kommen vom Router-Poll-Task
    if (wifi_label) {
        static char last_connectivity_text[140] = "";
        char wifi_line[64];
        char lte_line[64];
        char combined[140];

        /* Router-Daten unter Mutex kopieren */
        womo_router_wifi_status_t rw = {0};
        womo_router_lte_status_t  rl = {0};
        if (s_router_mutex) {
            xSemaphoreTake(s_router_mutex, portMAX_DELAY);
            rw = s_router_wifi;
            rl = s_router_lte;
            xSemaphoreGive(s_router_mutex);
        }

        if (!wifi_connected_now) {
            snprintf(wifi_line, sizeof(wifi_line), "WiFi: Router offline");
        } else if (rw.connected && rw.ssid[0]) {
            snprintf(wifi_line, sizeof(wifi_line), "WiFi: %s %u%%",
                     rw.ssid, rw.signal_percent);
        } else {
            snprintf(wifi_line, sizeof(wifi_line), "WiFi: nicht verbunden");
        }

        if (rl.registered && rl.operator_name[0]) {
            snprintf(lte_line, sizeof(lte_line), "LTE : %s %u%%",
                     rl.operator_name, rl.signal_percent);
        } else if (rl.operator_name[0]) {
            snprintf(lte_line, sizeof(lte_line), "LTE : %s 0%%", rl.operator_name);
        } else {
            snprintf(lte_line, sizeof(lte_line), "LTE : -- 0%%");
        }

        snprintf(combined, sizeof(combined), "%s\n%s", wifi_line, lte_line);
        if (strcmp(combined, last_connectivity_text) != 0) {
            lv_label_set_text(wifi_label, combined);
            strncpy(last_connectivity_text, combined, sizeof(last_connectivity_text) - 1);
            last_connectivity_text[sizeof(last_connectivity_text) - 1] = '\0';
        }

        if (modal_open) {
            womo_connectivity_snapshot_t modal_snapshot;
            connectivity_snapshot_fill(&modal_snapshot);
            static womo_connectivity_snapshot_t last_modal_snapshot = {0};
            if (memcmp(&modal_snapshot, &last_modal_snapshot, sizeof(modal_snapshot)) != 0) {
                womo_connectivity_modal_refresh(&modal_snapshot);
                last_modal_snapshot = modal_snapshot;
            }
        }
    }

    // RS485 debug label
    if (rs485_debug_label) {
        static uint32_t last_packet_count = 0;
        static char last_rs485_text[60] = "";
        if (rs485_timeout_snapshot) {
            const char *timeout_text = womo_locale_get_string(STR_RS485_TIMEOUT_LABEL);
            if (strcmp(timeout_text, last_rs485_text) != 0) {
                lv_label_set_text(rs485_debug_label, timeout_text);
                strncpy(last_rs485_text, timeout_text, sizeof(last_rs485_text) - 1);
                last_rs485_text[sizeof(last_rs485_text) - 1] = '\0';
            }
            lv_obj_set_style_text_color(rs485_debug_label, lv_color_make(255, 0, 0), 0);
        } else if (rs485_invalid_snapshot) {
            const char *invalid = womo_locale_get_string(STR_RS485_INVALID_DATA);
            if (strcmp(invalid, last_rs485_text) != 0) {
                lv_label_set_text(rs485_debug_label, invalid);
                strncpy(last_rs485_text, invalid, sizeof(last_rs485_text) - 1);
                last_rs485_text[sizeof(last_rs485_text) - 1] = '\0';
            }
            lv_obj_set_style_text_color(rs485_debug_label, lv_color_make(255, 0, 0), 0);
        } else if (rs485_waiting_snapshot) {
            const char *waiting_hello = womo_locale_get_string(STR_RS485_WAITING_HELLO);
            if (strcmp(waiting_hello, last_rs485_text) != 0) {
                lv_label_set_text(rs485_debug_label, waiting_hello);
                strncpy(last_rs485_text, waiting_hello, sizeof(last_rs485_text) - 1);
                last_rs485_text[sizeof(last_rs485_text) - 1] = '\0';
            }
            lv_obj_set_style_text_color(rs485_debug_label, lv_color_make(255, 165, 0), 0);
        } else if (!data_valid) {
            const char *waiting = womo_locale_get_string(STR_RS485_WAITING);
            if (strcmp(waiting, last_rs485_text) != 0) {
                lv_label_set_text(rs485_debug_label, waiting);
                strncpy(last_rs485_text, waiting, sizeof(last_rs485_text) - 1);
                last_rs485_text[sizeof(last_rs485_text) - 1] = '\0';
            }
            lv_obj_set_style_text_color(rs485_debug_label, lv_color_make(255, 0, 0), 0);
        } else if (packet_count != last_packet_count) {
            char buf[60];
            snprintf(buf, sizeof(buf), womo_locale_get_string(STR_RS485_PACKETS), packet_count);
            if (strcmp(buf, last_rs485_text) != 0) {
                lv_label_set_text(rs485_debug_label, buf);
                strncpy(last_rs485_text, buf, sizeof(last_rs485_text) - 1);
                last_rs485_text[sizeof(last_rs485_text) - 1] = '\0';
            }
            lv_obj_set_style_text_color(rs485_debug_label, lv_color_make(0, 200, 0), 0);
            last_packet_count = packet_count;
        }
    }

    if (!data_valid || modal_open) {
        return;
    }

    float disp_roll = snapshot.bno.roll_deg;
    float disp_pitch = snapshot.bno.pitch_deg;
    float heading_deg = snapshot.bno.heading_deg;
    bool imu_valid = snapshot.bno.valid;
    if (imu_valid) {
        imu_orientation_apply(&disp_roll, &disp_pitch);
    }

    imu_labels_update(imu_valid, disp_roll, disp_pitch, heading_deg, snapshot.bno.direction);

    // BME680 labels (außen: Pressure/Humidity/Temp)
    if (press_label || humid_label || temp_label) {
        static bool bme_has_data = false;
        static char last_press_text[32] = "";
        static char last_press_icon[8] = "";
        static char last_humid_text[40] = "";
        static char last_temp_text[40] = "";

        if (snapshot.bme680.valid) {
            if (press_label) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f hPa", snapshot.bme680.pressure_hpa);
                if (strcmp(buf, last_press_text) != 0) {
                    lv_label_set_text(press_label, buf);
                    strncpy(last_press_text, buf, sizeof(last_press_text) - 1);
                    last_press_text[sizeof(last_press_text) - 1] = '\0';
                }
                const char *trend = press_trend_arrow(snapshot.bme680.press_trend_state);
                if (press_icon_label && strcmp(trend, last_press_icon) != 0) {
                    lv_label_set_text(press_icon_label, trend);
                    strncpy(last_press_icon, trend, sizeof(last_press_icon) - 1);
                    last_press_icon[sizeof(last_press_icon) - 1] = '\0';
                }
                if (press_icon_label) {
                    lv_color_t c = press_trend_color(snapshot.bme680.press_trend_state);
                    lv_obj_set_style_text_color(press_icon_label, c, 0);
                }
            }
            if (humid_label) {
                char buf[40];
                snprintf(buf, sizeof(buf), "%.1f %%", snapshot.bme680.humidity_percent);
                if (strcmp(buf, last_humid_text) != 0) {
                    lv_label_set_text(humid_label, buf);
                    strncpy(last_humid_text, buf, sizeof(last_humid_text) - 1);
                    last_humid_text[sizeof(last_humid_text) - 1] = '\0';
                }
            }
            if (temp_label) {
                char buf[40];
                snprintf(buf, sizeof(buf), "%.1f °C", snapshot.bme680.temperature_c);
                if (strcmp(buf, last_temp_text) != 0) {
                    lv_label_set_text(temp_label, buf);
                    strncpy(last_temp_text, buf, sizeof(last_temp_text) - 1);
                    last_temp_text[sizeof(last_temp_text) - 1] = '\0';
                }
            }
            bme_has_data = true;
        } else if (!snapshot.bme680.valid && bme_has_data) {
            if (press_label) {
                if (strcmp(PLACEHOLDER_PRESSURE, last_press_text) != 0) {
                    lv_label_set_text(press_label, PLACEHOLDER_PRESSURE);
                    strncpy(last_press_text, PLACEHOLDER_PRESSURE, sizeof(last_press_text) - 1);
                    last_press_text[sizeof(last_press_text) - 1] = '\0';
                }
                const char *trend = press_trend_arrow(NULL);
                if (press_icon_label && strcmp(trend, last_press_icon) != 0) {
                    lv_label_set_text(press_icon_label, trend);
                    strncpy(last_press_icon, trend, sizeof(last_press_icon) - 1);
                    last_press_icon[sizeof(last_press_icon) - 1] = '\0';
                }
                if (press_icon_label) {
                    lv_color_t c = press_trend_color(NULL);
                    lv_obj_set_style_text_color(press_icon_label, c, 0);
                }
            }
            if (humid_label && strcmp(PLACEHOLDER_HUMIDITY, last_humid_text) != 0) {
                lv_label_set_text(humid_label, PLACEHOLDER_HUMIDITY);
                strncpy(last_humid_text, PLACEHOLDER_HUMIDITY, sizeof(last_humid_text) - 1);
                last_humid_text[sizeof(last_humid_text) - 1] = '\0';
            }
            if (temp_label && strcmp(PLACEHOLDER_TEMPERATURE, last_temp_text) != 0) {
                lv_label_set_text(temp_label, PLACEHOLDER_TEMPERATURE);
                strncpy(last_temp_text, PLACEHOLDER_TEMPERATURE, sizeof(last_temp_text) - 1);
                last_temp_text[sizeof(last_temp_text) - 1] = '\0';
            }
            bme_has_data = false;
        }
    }

    // BME680 indoor labels (0x76) mit Farb-Ampel
    if (gas_label_in || press_label_in || voc_label_in || humid_label_in || temp_label_in) {
        static bool bme_in_has_data = false;
        static char last_iaq_text_in[40] = "";
        static char last_co2_text_in[40] = "";
        static char last_voc_text_in[40] = "";
        static char last_humid_text_in[40] = "";
        static char last_temp_text_in[40] = "";

        if (snapshot.bme680_indoor.valid) {
            if (gas_label_in) {
                char buf[40];
                snprintf(buf, sizeof(buf), "IAQ %u", (unsigned)snapshot.bme680_indoor.iaq);
                if (strcmp(buf, last_iaq_text_in) != 0) {
                    lv_label_set_text(gas_label_in, buf);
                    strncpy(last_iaq_text_in, buf, sizeof(last_iaq_text_in) - 1);
                    last_iaq_text_in[sizeof(last_iaq_text_in) - 1] = '\0';
                }
                uint16_t iaq = snapshot.bme680_indoor.iaq;
                lv_color_t c = iaq <= 100 ? lv_color_make(0, 180, 0) : (iaq <= 200 ? lv_color_make(255, 165, 0) : lv_color_make(192, 0, 0));
                lv_obj_set_style_text_color(gas_label_in, c, 0);
            }
            if (press_label_in) {
                char buf[40];
                int co2_value = (int)roundf(snapshot.bme680_indoor.eco2_ppm);
                snprintf(buf, sizeof(buf), "CO2 %d ppm", co2_value);
                if (strcmp(buf, last_co2_text_in) != 0) {
                    lv_label_set_text(press_label_in, buf);
                    strncpy(last_co2_text_in, buf, sizeof(last_co2_text_in) - 1);
                    last_co2_text_in[sizeof(last_co2_text_in) - 1] = '\0';
                }
                lv_color_t c = co2_value <= 800 ? lv_color_make(0, 180, 0) : (co2_value <= 1200 ? lv_color_make(255, 165, 0) : lv_color_make(192, 0, 0));
                lv_obj_set_style_text_color(press_label_in, c, 0);
            }
            if (voc_label_in) {
                char buf[40];
                snprintf(buf, sizeof(buf), "bVOC %.2f ppm", snapshot.bme680_indoor.bvoc_ppm);
                if (strcmp(buf, last_voc_text_in) != 0) {
                    lv_label_set_text(voc_label_in, buf);
                    strncpy(last_voc_text_in, buf, sizeof(last_voc_text_in) - 1);
                    last_voc_text_in[sizeof(last_voc_text_in) - 1] = '\0';
                }
                float voc = snapshot.bme680_indoor.bvoc_ppm;
                lv_color_t c = (voc <= 0.5f) ? lv_color_make(0, 180, 0) : ((voc <= 1.5f) ? lv_color_make(255, 165, 0) : lv_color_make(192, 0, 0));
                lv_obj_set_style_text_color(voc_label_in, c, 0);
            }
            if (humid_label_in) {
                char buf[40];
                snprintf(buf, sizeof(buf), "%.1f %%", snapshot.bme680_indoor.humidity_percent);
                if (strcmp(buf, last_humid_text_in) != 0) {
                    lv_label_set_text(humid_label_in, buf);
                    strncpy(last_humid_text_in, buf, sizeof(last_humid_text_in) - 1);
                    last_humid_text_in[sizeof(last_humid_text_in) - 1] = '\0';
                }
            }
            if (temp_label_in) {
                char buf[40];
                snprintf(buf, sizeof(buf), "%.1f °C", snapshot.bme680_indoor.temperature_c);
                if (strcmp(buf, last_temp_text_in) != 0) {
                    lv_label_set_text(temp_label_in, buf);
                    strncpy(last_temp_text_in, buf, sizeof(last_temp_text_in) - 1);
                    last_temp_text_in[sizeof(last_temp_text_in) - 1] = '\0';
                }
            }
            bme_in_has_data = true;
        } else if (!snapshot.bme680_indoor.valid && bme_in_has_data) {
            if (gas_label_in && strcmp(PLACEHOLDER_IAQ, last_iaq_text_in) != 0) {
                lv_label_set_text(gas_label_in, PLACEHOLDER_IAQ);
                strncpy(last_iaq_text_in, PLACEHOLDER_IAQ, sizeof(last_iaq_text_in) - 1);
                last_iaq_text_in[sizeof(last_iaq_text_in) - 1] = '\0';
                lv_obj_set_style_text_color(gas_label_in, lv_color_black(), 0);
            }
            if (press_label_in && strcmp(PLACEHOLDER_CO2, last_co2_text_in) != 0) {
                lv_label_set_text(press_label_in, PLACEHOLDER_CO2);
                strncpy(last_co2_text_in, PLACEHOLDER_CO2, sizeof(last_co2_text_in) - 1);
                last_co2_text_in[sizeof(last_co2_text_in) - 1] = '\0';
                lv_obj_set_style_text_color(press_label_in, lv_color_black(), 0);
            }
            if (voc_label_in && strcmp(PLACEHOLDER_BVOC, last_voc_text_in) != 0) {
                lv_label_set_text(voc_label_in, PLACEHOLDER_BVOC);
                strncpy(last_voc_text_in, PLACEHOLDER_BVOC, sizeof(last_voc_text_in) - 1);
                last_voc_text_in[sizeof(last_voc_text_in) - 1] = '\0';
                lv_obj_set_style_text_color(voc_label_in, lv_color_black(), 0);
            }
            if (humid_label_in && strcmp(PLACEHOLDER_HUMIDITY, last_humid_text_in) != 0) {
                lv_label_set_text(humid_label_in, PLACEHOLDER_HUMIDITY);
                strncpy(last_humid_text_in, PLACEHOLDER_HUMIDITY, sizeof(last_humid_text_in) - 1);
                last_humid_text_in[sizeof(last_humid_text_in) - 1] = '\0';
            }
            if (temp_label_in && strcmp(PLACEHOLDER_TEMPERATURE, last_temp_text_in) != 0) {
                lv_label_set_text(temp_label_in, PLACEHOLDER_TEMPERATURE);
                strncpy(last_temp_text_in, PLACEHOLDER_TEMPERATURE, sizeof(last_temp_text_in) - 1);
                last_temp_text_in[sizeof(last_temp_text_in) - 1] = '\0';
            }
            bme_in_has_data = false;
        }
    }

    // Gas bottle widgets
    if (gas_bottle_a || gas_bottle_b) {
        static bool gas_has_data_a = false;
        static bool gas_has_data_b = false;
        static float last_level_a = NAN;
        static float last_level_b = NAN;
        static int8_t last_active_idx = -2;
        static float last_net_kg = NAN;
        static float last_rate = NAN;
        static float last_rest = NAN;

        bool hx_nc = snapshot.hx711.valid && snapshot.hx711.nc;
        bool hx_available = snapshot.hx711.valid && !hx_nc;
        bool pct_a_valid = snapshot.gas.valid && isfinite(snapshot.gas.pct_a);
        bool pct_b_valid = snapshot.gas.valid && isfinite(snapshot.gas.pct_b);

        if (hx_nc) {
            gas_nc_active = true;
            if (gas_bottle_a) {
                womo_gas_bottle_set_nc(gas_bottle_a);
                womo_gas_bottle_set_status(gas_bottle_a, WOMO_STATUS_OK);
            }
            if (gas_bottle_b) {
                womo_gas_bottle_set_nc(gas_bottle_b);
                womo_gas_bottle_set_status(gas_bottle_b, WOMO_STATUS_OK);
            }
            if (gas_info_label) {
                lv_label_set_text(gas_info_label, "Gas (nc):\n--.-- kg\n--.-- kg/h\n--.- h");
            }
            gas_has_data_a = false;
            gas_has_data_b = false;
            last_level_a = NAN;
            last_level_b = NAN;
            last_active_idx = -2;
            last_net_kg = NAN;
            last_rate = NAN;
            last_rest = NAN;
            goto gas_done;
        } else {
            gas_nc_active = false;
        }

        if (gas_bottle_a) {
            if (pct_a_valid) {
                float pct = snapshot.gas.pct_a;
                if (!gas_has_data_a || isnan(last_level_a) || fabsf(pct - last_level_a) > 0.5f) {
                    womo_gas_bottle_set_percent(gas_bottle_a, pct);
                    uint8_t fill = womo_gas_bottle_get_fill_percent(gas_bottle_a);
                    womo_status_level_t status = evaluate_low_is_bad(fill, GAS_WARN_PERCENT, GAS_CRIT_PERCENT);
                    womo_gas_bottle_set_status(gas_bottle_a, status);
                    if (status > sensor_level) {
                        sensor_level = status;
                        sensor_cause = "Gas";
                    }
                }
                last_level_a = pct;
                gas_has_data_a = true;
            } else if (hx_available) {
                if (!gas_has_data_a || isnan(last_level_a) || fabsf(snapshot.hx711.weight_a_kg - last_level_a) > 0.05f) {
                    womo_gas_bottle_update_weight(gas_bottle_a, snapshot.hx711.weight_a_kg);
                    uint8_t fill = womo_gas_bottle_get_fill_percent(gas_bottle_a);
                    womo_status_level_t status = evaluate_low_is_bad(fill, GAS_WARN_PERCENT, GAS_CRIT_PERCENT);
                    womo_gas_bottle_set_status(gas_bottle_a, status);
                    if (status > sensor_level) {
                        sensor_level = status;
                        sensor_cause = "Gas";
                    }
                }
                last_level_a = snapshot.hx711.weight_a_kg;
                gas_has_data_a = true;
            } else if (gas_has_data_a) {
                womo_gas_bottle_set_no_data(gas_bottle_a);
                womo_gas_bottle_set_status(gas_bottle_a, WOMO_STATUS_OK);
                gas_has_data_a = false;
                last_level_a = NAN;
            }
        }

        if (gas_bottle_b) {
            if (pct_b_valid) {
                float pct = snapshot.gas.pct_b;
                if (!gas_has_data_b || isnan(last_level_b) || fabsf(pct - last_level_b) > 0.5f) {
                    womo_gas_bottle_set_percent(gas_bottle_b, pct);
                    uint8_t fill = womo_gas_bottle_get_fill_percent(gas_bottle_b);
                    womo_status_level_t status = evaluate_low_is_bad(fill, GAS_WARN_PERCENT, GAS_CRIT_PERCENT);
                    womo_gas_bottle_set_status(gas_bottle_b, status);
                    if (status > sensor_level) {
                        sensor_level = status;
                        sensor_cause = "Gas";
                    }
                }
                last_level_b = pct;
                gas_has_data_b = true;
            } else if (hx_available) {
                if (!gas_has_data_b || isnan(last_level_b) || fabsf(snapshot.hx711.weight_b_kg - last_level_b) > 0.05f) {
                    womo_gas_bottle_update_weight(gas_bottle_b, snapshot.hx711.weight_b_kg);
                    uint8_t fill = womo_gas_bottle_get_fill_percent(gas_bottle_b);
                    womo_status_level_t status = evaluate_low_is_bad(fill, GAS_WARN_PERCENT, GAS_CRIT_PERCENT);
                    womo_gas_bottle_set_status(gas_bottle_b, status);
                    if (status > sensor_level) {
                        sensor_level = status;
                        sensor_cause = "Gas";
                    }
                }
                last_level_b = snapshot.hx711.weight_b_kg;
                gas_has_data_b = true;
            } else if (gas_has_data_b) {
                womo_gas_bottle_set_no_data(gas_bottle_b);
                womo_gas_bottle_set_status(gas_bottle_b, WOMO_STATUS_OK);
                gas_has_data_b = false;
                last_level_b = NAN;
            }
        }

        // Gas summary (aktuelle Flasche, Verbrauch, Restzeit)
        if (snapshot.gas.valid && gas_info_label) {
            bool active_a = (snapshot.gas.active_idx == 0);
            bool active_b = (snapshot.gas.active_idx == 1);
            float net = snapshot.gas.net_kg;
            float rate = snapshot.gas.rate_kgph_1h;
            float rest = snapshot.gas.rest_hours;

            // Fallback: falls active_idx nicht geliefert, aktive Flasche anhand net_a/net_b schätzen
            if (snapshot.gas.active_idx < 0) {
                if (isfinite(snapshot.gas.net_a_kg) && snapshot.gas.net_a_kg > 0.1f &&
                    (!isfinite(snapshot.gas.net_b_kg) || snapshot.gas.net_a_kg > snapshot.gas.net_b_kg)) {
                    active_a = true;
                    active_b = false;
                    net = snapshot.gas.net_a_kg;
                } else if (isfinite(snapshot.gas.net_b_kg) && snapshot.gas.net_b_kg > 0.1f) {
                    active_a = false;
                    active_b = true;
                    net = snapshot.gas.net_b_kg;
                }
            }

            char buf[96];
            const char *flasche = active_a ? "V" : (active_b ? "H" : "--");
            if (!isfinite(net)) net = NAN;
            if (!isfinite(rate)) rate = NAN;
            if (!isfinite(rest)) rest = NAN;

            // Refresh nur bei Änderungen, um LVGL-Last klein zu halten
            bool changed = (snapshot.gas.active_idx != last_active_idx) ||
                           (!isnan(net) && (!isfinite(last_net_kg) || fabsf(net - last_net_kg) > 0.05f)) ||
                           (!isnan(rate) && (!isfinite(last_rate) || fabsf(rate - last_rate) > 0.01f)) ||
                           (!isnan(rest) && (!isfinite(last_rest) || fabsf(rest - last_rest) > 0.05f));

            if (changed) {
                float net_disp = isnan(net) ? 0.0f : net;
                float rate_disp = isnan(rate) ? 0.0f : rate;
                float rest_disp = isnan(rest) ? 0.0f : rest;

                snprintf(buf, sizeof(buf),
                         "Gas (%s):\n%.2f kg\n%.2f kg/h\n%.1f h",
                         flasche,
                         isnan(net) ? 0.0f : net_disp,
                         isnan(rate) ? 0.0f : rate_disp,
                         isnan(rest) ? 0.0f : rest_disp);
                lv_label_set_text(gas_info_label, buf);
                last_active_idx = snapshot.gas.active_idx;
                last_net_kg = net;
                last_rate = rate;
                last_rest = rest;
            }
        } else if (gas_info_label) {
            lv_label_set_text(gas_info_label, "Gas (--):\n--.-- kg\n--.-- kg/h\n--.- h");
            last_active_idx = -2;
            last_net_kg = NAN;
            last_rate = NAN;
            last_rest = NAN;
        }
    }

gas_done:

    // Battery widgets
    if (main_battery || secondary_battery) {
        static bool battery_has_data = false;
        static float last_battery1 = NAN;
        static float last_battery2 = NAN;

        const bool battery_frame_valid = snapshot.battery.valid;
        if (battery_frame_valid) {
            womo_status_level_t battery_status = WOMO_STATUS_OK;
            bool any_voltage = false;

            if (main_battery) {
                if (snapshot.battery.nc1) {
                    womo_battery_set_nc(main_battery);
                    last_battery1 = NAN;
                } else {
                    if (!battery_has_data || isnan(last_battery1) || fabsf(snapshot.battery.battery1_v - last_battery1) > 0.05f) {
                        womo_battery_set_voltage(main_battery, snapshot.battery.battery1_v);
                    }
                    womo_status_level_t st = battery_status_from_voltage(snapshot.battery.battery1_v);
                    if (st > battery_status) {
                        battery_status = st;
                    }
                    any_voltage = true;
                    last_battery1 = snapshot.battery.battery1_v;
                }
            }
            if (secondary_battery) {
                if (snapshot.battery.nc2) {
                    womo_battery_set_nc(secondary_battery);
                    last_battery2 = NAN;
                } else {
                    if (!battery_has_data || isnan(last_battery2) || fabsf(snapshot.battery.battery2_v - last_battery2) > 0.05f) {
                        womo_battery_set_voltage(secondary_battery, snapshot.battery.battery2_v);
                    }
                    womo_status_level_t st = battery_status_from_voltage(snapshot.battery.battery2_v);
                    if (st > battery_status) {
                        battery_status = st;
                    }
                    any_voltage = true;
                    last_battery2 = snapshot.battery.battery2_v;
                }
            }

            battery_has_data = true;

            if (any_voltage && battery_status > sensor_level) {
                sensor_level = battery_status;
                sensor_cause = "Bat";
            }
        } else if (!snapshot.battery.valid && battery_has_data) {
            if (main_battery) {
                womo_battery_set_no_data(main_battery);
            }
            if (secondary_battery) {
                womo_battery_set_no_data(secondary_battery);
            }
            battery_has_data = false;
            last_battery1 = NAN;
            last_battery2 = NAN;
        }
    }

    // Tank widgets
    if (fresh_water_tank || grey_water_tank) {
        static bool tank_has_data = false;
        static uint8_t last_tank1 = 0;
        static uint8_t last_tank2 = 0;

        if (snapshot.tank.valid) {
            if (fresh_water_tank && (!tank_has_data || last_tank1 != snapshot.tank.tank1_percent)) {
                womo_tank_set_level(fresh_water_tank, snapshot.tank.tank1_percent);
                womo_status_level_t status = evaluate_low_is_bad(snapshot.tank.tank1_percent,
                                                                 FRESH_WARN_PERCENT,
                                                                 FRESH_CRIT_PERCENT);
                womo_tank_set_status(fresh_water_tank, status);
                if (status > sensor_level) {
                    sensor_level = status;
                    sensor_cause = "Was";
                }
            }
            if (grey_water_tank && (!tank_has_data || last_tank2 != snapshot.tank.tank2_percent)) {
                womo_tank_set_level(grey_water_tank, snapshot.tank.tank2_percent);
                womo_status_level_t status = evaluate_high_is_bad(snapshot.tank.tank2_percent,
                                                                  GREY_WARN_PERCENT,
                                                                  GREY_CRIT_PERCENT);
                womo_tank_set_status(grey_water_tank, status);
                if (status > sensor_level) {
                    sensor_level = status;
                    sensor_cause = "Was";
                }
            }
            last_tank1 = snapshot.tank.tank1_percent;
            last_tank2 = snapshot.tank.tank2_percent;
            tank_has_data = true;
        } else if (!snapshot.tank.valid && tank_has_data) {
            if (fresh_water_tank) {
                womo_tank_set_no_data(fresh_water_tank);
                womo_tank_set_status(fresh_water_tank, WOMO_STATUS_OK);
            }
            if (grey_water_tank) {
                womo_tank_set_no_data(grey_water_tank);
                womo_tank_set_status(grey_water_tank, WOMO_STATUS_OK);
            }
            tank_has_data = false;
        }
    }

    // Power / Radio / Landstrom aus RS485 ctrl-Block synchronisieren
    if (snapshot.power.valid) {
        int64_t now_us = esp_timer_get_time();
        bool pwr_grace = (s_pwr_cmd_sent_us > 0 &&
                          (now_us - s_pwr_cmd_sent_us) < CTRL_GRACE_PERIOD_US);
        bool radio_grace = (s_radio_cmd_sent_us > 0 &&
                            (now_us - s_radio_cmd_sent_us) < CTRL_GRACE_PERIOD_US);

        // Walter bestätigt neuen Zustand → Grace-Period sofort beenden
        if (pwr_grace && classic_on == snapshot.power.pwr_12v_on) {
            s_pwr_cmd_sent_us = 0;
            pwr_grace = false;
        }
        if (radio_grace && radio_on == snapshot.power.radio_on) {
            s_radio_cmd_sent_us = 0;
            radio_grace = false;
        }

        bool pwr_changed = (!pwr_grace && classic_on != snapshot.power.pwr_12v_on);
        bool radio_changed = (!radio_grace && radio_on != snapshot.power.radio_on);
        bool shore_changed = (shore_power_present != snapshot.power.ac_present);

        if (pwr_changed) {
            classic_on = snapshot.power.pwr_12v_on;
            if (classic_btn && classic_label) {
                lv_color_t c = lv_color_hex(0x2E7D32);
                simple_toggle_button_update(classic_btn, classic_label, classic_on, "", c);
                update_classic_icon(c, classic_on);
            }
        }
        if (radio_changed) {
            radio_on = snapshot.power.radio_on;
            if (radio_btn && radio_label) {
                lv_color_t c = lv_color_hex(0x1565C0);
                simple_toggle_button_update(radio_btn, radio_label, radio_on, "Radio", c);
                update_radio_icon(c, radio_on);
            }
        }
        if (shore_changed) {
            shore_power_present = snapshot.power.ac_present;
            shore_power_update_label();
            if (shore_label) {
                update_shore_icon(lv_color_hex(0xF9A825), shore_power_present);
            }
        }
    }

    sensor_level_raw_last = sensor_level;

    if (sensor_level == WOMO_STATUS_OK) {
        sensor_level_prev = WOMO_STATUS_OK;
        sensor_latched_level = WOMO_STATUS_OK;
        sensor_detail_text[0] = '\0';
        sensor_ack_active = false;
        sensor_ack_level = WOMO_STATUS_OK;
        if (system_status_sensor_level != WOMO_STATUS_OK) {
            system_status_apply_sensor_level(WOMO_STATUS_OK);
        }
    } else {
        bool severity_up_vs_ack = (sensor_level > sensor_ack_level);
        bool blocked_by_ack = (sensor_ack_active && !severity_up_vs_ack);

        if (!blocked_by_ack) {
            sensor_latched_level = sensor_level;
            if (sensor_cause) {
                strncpy(sensor_detail_text, sensor_cause, sizeof(sensor_detail_text) - 1);
                sensor_detail_text[sizeof(sensor_detail_text) - 1] = '\0';
            }
            system_status_apply_sensor_level(sensor_latched_level);
            sensor_ack_active = false;
            sensor_ack_level = WOMO_STATUS_OK;
        }
        sensor_level_prev = sensor_level;
    }

    if (gps_label) {
        bool gps_ready = false;
        if (snapshot.gps.valid) {
            // Nur anzeigen, wenn Koordinaten plausibel und genug Satelliten vorhanden sind
            const bool coords_finite = isfinite(snapshot.gps.latitude) && isfinite(snapshot.gps.longitude);
            const bool coords_nonzero = fabs(snapshot.gps.latitude) + fabs(snapshot.gps.longitude) > 0.0001;
            const bool sats_ok = snapshot.gps.satellites > 0;
            const bool conf_ok = !isfinite(snapshot.gps.confidence_m) || snapshot.gps.confidence_m > 0.0f;
            gps_ready = coords_finite && coords_nonzero && sats_ok && conf_ok;
        }

        if (gps_ready) {
            char lat_buf[24];
            char lon_buf[24];
            gps_format_coordinate(snapshot.gps.latitude, true, lat_buf, sizeof(lat_buf));
            gps_format_coordinate(snapshot.gps.longitude, false, lon_buf, sizeof(lon_buf));

            geocode_trigger_if_needed(&snapshot);

            long altitude_m = lrint(snapshot.gps.altitude_m);
            float speed_kmh = snapshot.gps.speed_kmh;
            float heading = snapshot.gps.heading_deg;
            uint8_t satellites = snapshot.gps.satellites;

            if (!isfinite(snapshot.gps.altitude_m)) {
                altitude_m = 0;
            }
            if (!isfinite(speed_kmh)) {
                speed_kmh = 0.0f;
            }
            if (!isfinite(heading)) {
                heading = 0.0f;
            }

            char accuracy_buf[24] = "+/- --.- m";
            if (isfinite(snapshot.gps.confidence_m) && snapshot.gps.confidence_m > 0.0f) {
                snprintf(accuracy_buf, sizeof(accuracy_buf), "+/-%0.1f m", snapshot.gps.confidence_m);
            }

            char fix_time_buf[16] = "--:--";
            int64_t fix_ts = snapshot.gps.ts;  // Sekunden seit Unix-Epoch (Modem, UTC)
            if (fix_ts > 0) {
                time_t fix_ts_time = (time_t)fix_ts;
                struct tm fix_tm;
                if (localtime_r(&fix_ts_time, &fix_tm) != NULL) {
                    strftime(fix_time_buf, sizeof(fix_time_buf), "%H:%M", &fix_tm);
                }
            }

            char info_line1[96];
            snprintf(info_line1,
                     sizeof(info_line1),
                     "Alt %ld m | %.0f km/h | %.0f°",
                     altitude_m,
                     speed_kmh,
                     heading);

            char info_line2[96];
            snprintf(info_line2,
                     sizeof(info_line2),
                     "SV %u | %s | %s",
                     satellites,
                     accuracy_buf,
                     fix_time_buf);

            char text[256];
            snprintf(text, sizeof(text), "GPS %s  %s\n%s\n%s", lat_buf, lon_buf, info_line1, info_line2);
            if (strcmp(text, last_gps_text) != 0) {
                strncpy(last_gps_text, text, sizeof(last_gps_text) - 1);
                last_gps_text[sizeof(last_gps_text) - 1] = '\0';
                if (gps_details_visible) {
                    lv_label_set_text(gps_label, last_gps_text);
                }
            }
        } else if (!snapshot.gps.valid) {
            if (strcmp(PLACEHOLDER_GPS, last_gps_text) != 0) {
                strncpy(last_gps_text, PLACEHOLDER_GPS, sizeof(last_gps_text) - 1);
                last_gps_text[sizeof(last_gps_text) - 1] = '\0';
                if (gps_details_visible) {
                    lv_label_set_text(gps_label, last_gps_text);
                }
            }
            if (location_label && location_last_text[0]) {
                lv_label_set_text(location_label, "");
                location_last_text[0] = '\0';
            }
        }
    }
}

// Timer callback for updating time display
static bool theme_mode_is_daylike(womo_theme_mode_t mode)
{
    return (mode == WOMO_THEME_DAY || mode == WOMO_THEME_SUNRISE);
}

static void time_update_timer_cb(lv_timer_t *timer)
{
    char time_str[32];
    char date_str[32];
    static bool theme_after_time_sync_applied = false;  // sorgt dafür, dass nach erster gültiger Zeit sofort Theme+BG neu gesetzt werden
    static bool initial_bg_loaded = false; // stellt sicher, dass bei erstem Tag/Nacht direkt der passende Hintergrund geladen wird
    static bool weather_started = false; // startet Online-Wetter erst nach WiFi-Verbindung
    struct tm timeinfo;
    bool time_valid_now = (womo_time_get(&timeinfo) == ESP_OK) && (timeinfo.tm_year >= (2024 - 1900));
    
    if (time_valid_now) {
        // Zeit HH:MM:SS
        if (strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo) > 0) {
            lv_label_set_text(time_label, time_str);
        }

        // Datum mit lokalisiertem Wochentag (Mo 04.11.2025)
        char weekday_en[16];
        char full_date[128];
        if (strftime(weekday_en, sizeof(weekday_en), "%w", &timeinfo) > 0 &&
            strftime(date_str, sizeof(date_str), "%d.%m.%Y", &timeinfo) > 0) {
            int day_index = atoi(weekday_en);  // 0=Sunday, 1=Monday, ...
            const char* weekday_str = womo_locale_get_weekday(day_index);
            snprintf(full_date, sizeof(full_date), "%s  %s", weekday_str, date_str);
            lv_label_set_text(date_label, full_date);
        }
    } else {
        lv_label_set_text(time_label, "--:--:--");
        lv_label_set_text(date_label, "--.--.----");
    }

    // Wettercode erst abrufen, wenn WiFi steht UND Zeit synchronisiert ist (TLS braucht gültige Uhrzeit)
    if (!weather_started && womo_wifi_is_connected() && womo_time_is_synced()) {
        esp_err_t weather_err = womo_weather_http_start(openweather_update_cb, NULL);
        if (weather_err != ESP_OK) {
            ESP_LOGW(TAG, "Online weather updates disabled: %s", esp_err_to_name(weather_err));
        } else {
            weather_started = true;
        }
    }

    // Sobald die RTC/NTP-Zeit erstmals valide ist, Theme und Hintergrund sofort aktualisieren
    if (!theme_after_time_sync_applied && time_valid_now) {
        theme_after_time_sync_applied = true;

        womo_theme_mode_t mode = womo_theme_update(womo_theme_get_status());
        bool is_day_now = theme_mode_is_daylike(mode);
        load_background_image(lv_scr_act(), is_day_now);

        apply_text_theme_colors();
        womo_theme_apply_to_screen(NULL);
        initial_bg_loaded = true;
    }

    // Quiet hours: 22:00-08:00 Backlight aus, außerhalb einschalten; Touch in Ruhezeit bekommt 5min Timeout
    if (time_valid_now) {
        static bool last_quiet_state = false;
        bool quiet_now = is_quiet_hours(&timeinfo);
        quiet_hours_active = quiet_now;

        if (quiet_now && !last_quiet_state) {
            ESP_LOGI(TAG, "Quiet hours start %02d:%02d (backlight_on=%d)", timeinfo.tm_hour, timeinfo.tm_min, backlight_on);
            backlight_stop_quiet_timer();
            if (backlight_on) {
                backlight_set(false);
            }
        } else if (!quiet_now && last_quiet_state) {
            ESP_LOGI(TAG, "Quiet hours end %02d:%02d (backlight_on=%d)", timeinfo.tm_hour, timeinfo.tm_min, backlight_on);
            backlight_stop_quiet_timer();
            if (!backlight_on) {
                backlight_set(true);
            }
        }

        last_quiet_state = quiet_now;
    } else {
        quiet_hours_active = false;
    }
    
    // Update WiFi status (2 lines: RSSI first, then SSID)
    update_connectivity_label();
    
    // Update sensor data every 5 seconds (counter % 5 == 0)
    static uint32_t sensor_counter = 0;
    sensor_counter++;
    
    // Sensor data now comes via RS485 from Walter - no local sensor reading needed
    
    // Nur bei Auto-Mode prüfen; Hintergrund nur beim tatsächlichen Day/Night-Wechsel neu laden.
    // Poll seltener (60 s), da wir nur auf Zustandswechsel reagieren.
    if (womo_theme_is_auto_mode() && (sensor_counter % 60 == 0)) {
        womo_theme_mode_t mode = womo_theme_update(womo_theme_get_status());
        bool is_day_now = theme_mode_is_daylike(mode);

        // Reload background PNG when theme switches day/night so the correct Ducato variant is shown
        if (!initial_bg_loaded || bg_last_day_state != (is_day_now ? 1 : 0)) {
            load_background_image(lv_scr_act(), is_day_now);
            initial_bg_loaded = true;
        }

        apply_text_theme_colors();
        womo_theme_apply_to_screen(NULL);
    }
}

static womo_weather_condition_t map_day_condition_to_night(womo_weather_condition_t condition)
{
    switch (condition) {
        case WEATHER_CLEAR:
        case WEATHER_SUNNY:
            return WEATHER_NT_CLEAR;
        case WEATHER_PARTLYSUNNY:
        case WEATHER_PARTLYCLOUDY:
            return WEATHER_NT_PARTLYCLOUDY;
        case WEATHER_MOSTLYSUNNY:
            return WEATHER_NT_MOSTLYSUNNY;
        case WEATHER_MOSTLYCLOUDY:
        case WEATHER_CLOUDY:
            return WEATHER_NT_CLOUDY;
        case WEATHER_CHANCERAIN:
            return WEATHER_NT_CHANCERAIN;
        case WEATHER_RAIN:
            return WEATHER_NT_RAIN;
        case WEATHER_CHANCESNOW:
            return WEATHER_NT_CHANCESNOW;
        case WEATHER_SNOW:
            return WEATHER_NT_SNOW;
        case WEATHER_CHANCESLEET:
            return WEATHER_NT_CHANCESLEET;
        case WEATHER_SLEET:
            return WEATHER_NT_SLEET;
        case WEATHER_CHANCETSTORMS:
            return WEATHER_NT_CHANCETSTORMS;
        case WEATHER_TSTORMS:
            return WEATHER_NT_TSTORMS;
        case WEATHER_FLURRIES:
            return WEATHER_NT_FLURRIES;
        case WEATHER_FOG:
            return WEATHER_NT_FOG;
        case WEATHER_HAZY:
            return WEATHER_NT_HAZY;
        default:
            return WEATHER_NT_UNKNOWN;
    }
}

static womo_weather_condition_t map_openweather_condition(int weather_id, bool is_night)
{
    // Re-used name, but mapping now targets Open-Meteo WMO weather codes
    womo_weather_condition_t condition = WEATHER_UNKNOWN;

    switch (weather_id) {
        case 0:
            condition = WEATHER_SUNNY;
            break;
        case 1:
        case 2:
            condition = WEATHER_PARTLYCLOUDY;
            break;
        case 3:
            condition = WEATHER_CLOUDY;
            break;
        case 45:
        case 48:
            condition = WEATHER_FOG;
            break;
        case 51:
        case 53:
        case 55:
            condition = WEATHER_CHANCERAIN;
            break;
        case 56:
        case 57:
            condition = WEATHER_CHANCESLEET;
            break;
        case 61:
            condition = WEATHER_CHANCERAIN;
            break;
        case 63:
        case 65:
            condition = WEATHER_RAIN;
            break;
        case 66:
        case 67:
            condition = WEATHER_SLEET;
            break;
        case 71:
            condition = WEATHER_CHANCESNOW;
            break;
        case 73:
        case 75:
        case 77:
            condition = WEATHER_SNOW;
            break;
        case 80:
            condition = WEATHER_CHANCERAIN;
            break;
        case 81:
        case 82:
            condition = WEATHER_RAIN;
            break;
        case 85:
            condition = WEATHER_CHANCESNOW;
            break;
        case 86:
            condition = WEATHER_SNOW;
            break;
        case 95:
        case 96:
        case 99:
            condition = WEATHER_TSTORMS;
            break;
        default:
            condition = WEATHER_UNKNOWN;
            break;
    }

    if (is_night) {
        return map_day_condition_to_night(condition);
    }
    return condition;
}

static void openweather_update_cb(const womo_weather_http_data_t *data, void *user_data)
{
    if (!data || !data->valid) {
        return;
    }

    womo_weather_condition_t condition = map_openweather_condition(data->weather_id, data->is_night);
    int16_t temperature = (int16_t)lrintf(data->temperature_c);

    ESP_LOGI(TAG, "Open-Meteo update: code=%d (%s), %.1f°C, night=%s",
             data->weather_id,
             data->description,
             data->temperature_c,
             data->is_night ? "yes" : "no");

    if (weather_widget && lvgl_port_lock(-1)) {
        womo_weather_set_condition(weather_widget, condition, data->is_night);
        if (weather_widget->temp_label) {
            womo_weather_set_temperature(weather_widget, temperature);
        }
        lvgl_port_unlock();
    }
}

static void wifi_autoretry_task(void *arg)
{
    (void)arg;
    const TickType_t delay_ticks = pdMS_TO_TICKS(180000); // 180 s

    for (;;) {
        vTaskDelay(delay_ticks);

        if (womo_wifi_is_connected()) {
            continue;
        }

        if (womo_wifi_get_status() == WOMO_WIFI_CONNECTING) {
            continue;
        }

        ESP_LOGI(TAG, "WiFi auto-reconnect to Router-AP: %s", WIFI_SSID);
        esp_err_t err = womo_wifi_connect(WIFI_SSID, WIFI_PASSWORD, WIFI_MAX_RETRY);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "WiFi reconnect to Router-AP failed: %s", esp_err_to_name(err));
        }
    }
}

/* ── Router-Status-Polling (WiFi-Client + LTE vom RUTX11) ── */
static void router_poll_task(void *arg)
{
    (void)arg;
    const TickType_t interval = pdMS_TO_TICKS(ROUTER_POLL_INTERVAL_MS);
    static int poll_count = 0;

    /* Kurz warten bis WiFi zum Router steht */
    vTaskDelay(pdMS_TO_TICKS(5000));

    for (;;) {
        if (!womo_wifi_is_connected()) {
            vTaskDelay(interval);
            continue;
        }

        poll_count++;
        womo_router_wifi_status_t wifi_tmp = {0};
        womo_router_lte_status_t  lte_tmp  = {0};

        esp_err_t w_err = womo_router_get_wifi_status(&wifi_tmp);
        if (w_err == ESP_OK) {
            if (poll_count <= 3 || (poll_count % 20) == 0) {
                ESP_LOGI(TAG, "Router WiFi: '%s' %d%% (ch%d, %s)",
                         wifi_tmp.ssid, wifi_tmp.signal_percent,
                         wifi_tmp.channel, wifi_tmp.mode);
            }
        } else {
            ESP_LOGW(TAG, "Router WiFi-Status fehlgeschlagen: %s", esp_err_to_name(w_err));
        }

        esp_err_t l_err = womo_router_get_lte_status(&lte_tmp);
        if (l_err == ESP_OK) {
            if (poll_count <= 3 || (poll_count % 20) == 0) {
                ESP_LOGI(TAG, "Router LTE: '%s' %d dBm (%u%%) %s",
                         lte_tmp.operator_name, lte_tmp.rssi_dbm,
                         lte_tmp.signal_percent, lte_tmp.conn_type);
            }
        } else {
            ESP_LOGW(TAG, "Router LTE-Status fehlgeschlagen: %s", esp_err_to_name(l_err));
        }

        /* AP-Status (HotSpot) */
        womo_router_ap_status_t ap_tmp = {0};
        esp_err_t a_err = womo_router_get_ap_status(&ap_tmp);
        if (a_err == ESP_OK && (poll_count <= 3 || (poll_count % 20) == 0)) {
            ESP_LOGI(TAG, "Router AP: '%s' %s ch%d",
                     ap_tmp.ssid, ap_tmp.enabled ? "aktiv" : "aus", ap_tmp.channel);
        }

        /* Auch bei Teilfehlern aktualisieren – was da ist wird angezeigt */
        if (s_router_mutex) {
            xSemaphoreTake(s_router_mutex, portMAX_DELAY);
            if (w_err == ESP_OK) s_router_wifi = wifi_tmp;
            if (l_err == ESP_OK) s_router_lte  = lte_tmp;
            if (a_err == ESP_OK) s_router_ap   = ap_tmp;
            xSemaphoreGive(s_router_mutex);
        }

        /* ── GPS vom Router (GNSS-Antenne am RUTX11) ──────────── */
        womo_router_gps_t gps_tmp = {0};
        esp_err_t g_err = womo_router_get_gps(&gps_tmp);
        if (g_err == ESP_OK && gps_tmp.valid) {
            if (poll_count <= 3 || (poll_count % 20) == 0) {
                ESP_LOGI(TAG, "Router GPS: %.6f, %.6f alt=%.0fm sat=%d spd=%.1fkm/h",
                         gps_tmp.latitude, gps_tmp.longitude,
                         gps_tmp.altitude_m, gps_tmp.satellites,
                         gps_tmp.speed_kmh);
            }

            /* UTC-Zeit VOR der Critical Section parsen (mktime braucht Mutex) */
            int64_t gps_epoch = 0;
            if (gps_tmp.utc_time[0]) {
                struct tm tm_gps = {0};
                if (strptime(gps_tmp.utc_time, "%Y-%m-%d %H:%M:%S", &tm_gps)) {
                    gps_epoch = (int64_t)mktime(&tm_gps);
                }
            }

            /* GPS-Daten in latest_sensor_data.gps eintragen,
             * damit die bestehende GPS-Anzeige + Geocoding funktioniert. */
            taskENTER_CRITICAL(&display_data_spinlock);
            latest_sensor_data.gps.valid       = true;
            latest_sensor_data.gps.latitude    = gps_tmp.latitude;
            latest_sensor_data.gps.longitude   = gps_tmp.longitude;
            latest_sensor_data.gps.altitude_m  = gps_tmp.altitude_m;
            latest_sensor_data.gps.speed_kmh   = (float)gps_tmp.speed_kmh;
            latest_sensor_data.gps.heading_deg = (float)gps_tmp.heading;
            latest_sensor_data.gps.satellites  = (uint8_t)gps_tmp.satellites;
            latest_sensor_data.gps.confidence_m = (float)gps_tmp.accuracy_m;
            latest_sensor_data.gps.last_fix_us  = esp_timer_get_time();
            latest_sensor_data.gps.ts           = gps_epoch;

            latest_data_valid = true;
            taskEXIT_CRITICAL(&display_data_spinlock);
        } else if (g_err != ESP_OK && poll_count <= 3) {
            ESP_LOGW(TAG, "Router GPS fehlgeschlagen: %s", esp_err_to_name(g_err));
        }

        vTaskDelay(interval);
    }
}

static void gas_replace_close_modal(void)
{
    if (gas_replace_modal) {
        lv_obj_t *to_close = gas_replace_modal;
        gas_replace_modal = NULL;
        lv_msgbox_close(to_close);
    }
}

static void gas_replace_msgbox_event_cb(lv_event_t *event)
{
    if (!event) {
        return;
    }

    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    lv_obj_t *msgbox = lv_event_get_current_target(event);
    if (!msgbox || msgbox != gas_replace_modal) {
        return;
    }

    uint8_t slot = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    uint16_t btn_id = lv_msgbox_get_active_btn(msgbox);
    if (btn_id == LV_BTNMATRIX_BTN_NONE) {
        return;
    }

    gas_replace_close_modal();

    if (btn_id == 0) {
        lv_timer_t *t = lv_timer_create(gas_replace_send_timer_cb, 0, (void *)(uintptr_t)slot);
        if (t) {
            lv_timer_set_repeat_count(t, 1);
        } else {
            ESP_LOGW(TAG, "Timer create failed, sending directly");
            const char *channel = (slot == 0) ? "front" : "back";
            esp_err_t err = womo_rs485_send_gas_bottle_replace(slot, channel);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "RS485 gas_bottle_replace (slot=%u) failed: %s", (unsigned)slot, esp_err_to_name(err));
            }
        }
    }
}

static void gas_replace_show_modal(uint8_t slot)
{
    if (slot > 1) {
        return;
    }

    gas_replace_close_modal();

    static const char *btns[3] = {0};
    btns[0] = womo_locale_get_string(STR_GAS_MODAL_BTN_YES);
    btns[1] = womo_locale_get_string(STR_GAS_MODAL_BTN_NO);
    btns[2] = "";

    const char *title = (slot == 0)
                            ? womo_locale_get_string(STR_GAS_MODAL_TITLE_FRONT)
                            : womo_locale_get_string(STR_GAS_MODAL_TITLE_REAR);
    const char *question = womo_locale_get_string(STR_GAS_MODAL_QUESTION);

    gas_replace_modal = lv_msgbox_create(NULL, title, question, btns, false);
    lv_obj_center(gas_replace_modal);
    lv_obj_add_event_cb(gas_replace_modal, gas_replace_msgbox_event_cb, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)slot);
}

static void gas_bottle_clicked_cb(lv_event_t *event)
{
    if (!event) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_LONG_PRESSED) {
        return;
    }

    if (gas_nc_active) {
        ESP_LOGW(TAG, "Gas bottle click ignored: HX711 nc active");
        return;
    }

    uint8_t slot = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    gas_replace_show_modal(slot);
}

static void gas_replace_send_timer_cb(lv_timer_t *timer)
{
    if (!timer) {
        return;
    }

    uint8_t slot = (uint8_t)(uintptr_t)timer->user_data;
    const char *channel = (slot == 0) ? "front" : "back";

    esp_err_t err = womo_rs485_send_gas_bottle_replace(slot, channel);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RS485 gas_bottle_replace (slot=%u) failed: %s", (unsigned)slot, esp_err_to_name(err));
    }
}

static void wifi_label_event_cb(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    womo_connectivity_snapshot_t snapshot;
    connectivity_snapshot_fill(&snapshot);
    womo_connectivity_modal_show(lv_scr_act(), &snapshot);
}

static void backlight_button_event_cb(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    ESP_LOGI(TAG, "Backlight button clicked (before: on=%d, quiet=%d)", backlight_on, quiet_hours_active);

    if (quiet_hours_active) {
        if (backlight_on) {
            /* In Ruhezeit per Button ausschalten + Timer stoppen */
            backlight_set(false);
            backlight_stop_quiet_timer();
        } else {
            /* Display war aus → aufwecken + Timer starten */
            backlight_set(true);
            backlight_start_quiet_timer();
        }
        ESP_LOGI(TAG, "Backlight button in quiet hours (after: on=%d)", backlight_on);
        return;
    }

    backlight_set(!backlight_on);
    ESP_LOGI(TAG, "Backlight button handled (after: on=%d)", backlight_on);
    backlight_stop_quiet_timer();
}

static void status_label_event_cb(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    sensor_latched_level = WOMO_STATUS_OK;
    system_status_apply_sensor_level(WOMO_STATUS_OK);
    ESP_LOGI(TAG, "Sensorstatus per Touch quittiert");

    // Keine erneute Auslösung, solange der aktuelle Rohzustand unverändert bleibt
    sensor_level_prev = sensor_level_raw_last;
    sensor_ack_active = true;
    sensor_ack_level = sensor_level_raw_last;
    sensor_detail_text[0] = '\0';

    // Theme/Label sofort aktualisieren
    system_status_apply(true);
}

static void backlight_update_label(void)
{
    if (!backlight_btn) {
        return;
    }

    lv_color_t icon_color = lv_color_hex(0x1E88E5); // kräftiges Blau für Symbol und Rand
    lv_color_t border_color = icon_color;

    lv_obj_set_style_border_width(backlight_btn, 0, 0);
    lv_obj_set_style_border_color(backlight_btn, lv_color_hex(0x7A7A7A), 0);

    if (!backlight_ring) {
        backlight_ring = lv_obj_create(backlight_btn);
        lv_obj_set_size(backlight_ring, 42, 42);
        lv_obj_set_style_radius(backlight_ring, LV_RADIUS_CIRCLE, 0); // Ring bleibt kreisrund
        lv_obj_set_style_bg_opa(backlight_ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(backlight_ring, 3, 0); // 1px dicker
        lv_obj_set_style_border_color(backlight_ring, border_color, 0);
        lv_obj_set_style_pad_all(backlight_ring, 0, 0);
        lv_obj_clear_flag(backlight_ring, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(backlight_ring);
    } else {
        lv_obj_set_style_border_color(backlight_ring, border_color, 0);
        lv_obj_set_style_border_width(backlight_ring, 3, 0);
        lv_obj_set_size(backlight_ring, 42, 42);
        lv_obj_set_style_radius(backlight_ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(backlight_ring, LV_OBJ_FLAG_CLICKABLE);
    }

    if (!backlight_bulb_outline) {
        backlight_bulb_outline = lv_obj_create(backlight_btn);
        lv_obj_set_size(backlight_bulb_outline, 18, 18);
        lv_obj_set_style_radius(backlight_bulb_outline, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(backlight_bulb_outline, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(backlight_bulb_outline, 1, 0);
        lv_obj_set_style_border_color(backlight_bulb_outline, icon_color, 0);
        lv_obj_set_style_pad_all(backlight_bulb_outline, 0, 0);
        lv_obj_clear_flag(backlight_bulb_outline, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(backlight_bulb_outline);
    } else {
        lv_obj_set_style_border_color(backlight_bulb_outline, icon_color, 0);
        lv_obj_clear_flag(backlight_bulb_outline, LV_OBJ_FLAG_CLICKABLE);
    }

    if (!backlight_bulb_base) {
        backlight_bulb_base = lv_obj_create(backlight_btn);
        lv_obj_set_size(backlight_bulb_base, 11, 6);
        lv_obj_set_style_radius(backlight_bulb_base, 2, 0);
        lv_obj_set_style_bg_color(backlight_bulb_base, icon_color, 0);
        lv_obj_set_style_bg_opa(backlight_bulb_base, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(backlight_bulb_base, 0, 0);
        lv_obj_set_style_pad_all(backlight_bulb_base, 0, 0);
        lv_obj_clear_flag(backlight_bulb_base, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(backlight_bulb_base, LV_ALIGN_CENTER, 0, 10);
    } else {
        lv_obj_set_style_bg_color(backlight_bulb_base, icon_color, 0);
        lv_obj_clear_flag(backlight_bulb_base, LV_OBJ_FLAG_CLICKABLE);
    }

    size_t ray_count = sizeof(BACKLIGHT_RAY_POINTS) / sizeof(BACKLIGHT_RAY_POINTS[0]);
    for (size_t i = 0; i < ray_count; ++i) {
        if (backlight_icon_rays[i]) {
            lv_obj_del(backlight_icon_rays[i]);
            backlight_icon_rays[i] = NULL;
        }
    }

    if (!backlight_strike) {
        static const lv_point_t strike_points[2] = {{10, 38}, {38, 10}}; // endet innerhalb des blauen Rings
        backlight_strike = lv_line_create(backlight_btn);
        lv_line_set_points(backlight_strike, strike_points, 2);
        lv_obj_set_size(backlight_strike, 48, 48);
        lv_obj_center(backlight_strike);
        lv_obj_set_style_line_width(backlight_strike, 3, 0);
        lv_obj_set_style_line_color(backlight_strike, border_color, 0);
        lv_obj_set_style_line_rounded(backlight_strike, true, 0);
        lv_obj_clear_flag(backlight_strike, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_set_style_line_width(backlight_strike, 3, 0);
        lv_obj_set_style_line_color(backlight_strike, border_color, 0);
        lv_obj_clear_flag(backlight_strike, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void simple_toggle_button_update(lv_obj_t *btn, lv_obj_t *label, bool active, const char *text, lv_color_t active_color)
{
    if (!btn || !label) {
        return;
    }

    lv_color_t off_bg = lv_color_hex(0xC0C0C0);
    lv_color_t bg = active ? active_color : off_bg;
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x7A7A7A), 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_label_set_text(label, (text && text[0]) ? text : "");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, active ? lv_color_white() : lv_color_black(), 0);
    lv_obj_center(label);
}

static lv_obj_t *ensure_arc(lv_obj_t *arc, lv_obj_t *parent, lv_color_t color, bool open_top)
{
    if (!parent) {
        return arc;
    }

    if (!arc) {
        arc = lv_arc_create(parent);
        lv_obj_set_size(arc, 42, 42); // wie Backlight: Ring innerhalb des Buttons
        lv_obj_center(arc);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(arc, 0, 0);
        lv_obj_set_style_border_width(arc, 0, 0);
        // Drehe Null-Position nach oben, damit open_top die Lücke oben platziert
        lv_arc_set_rotation(arc, 270);
        // Knob ausblenden, sonst erscheint ein blauer Punkt
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_border_width(arc, 0, LV_PART_KNOB);
        lv_obj_set_style_arc_width(arc, 0, LV_PART_KNOB);
        lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    }

    uint16_t start = open_top ? 30 : 0;
    uint16_t end = open_top ? 330 : 360;
    lv_arc_set_bg_angles(arc, start, end);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_width(arc, 3, 0);
    lv_obj_set_style_arc_color(arc, color, 0);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(arc, 0, 0);
    return arc;
}

static void update_classic_icon(lv_color_t color, bool active)
{
    classic_arc = ensure_arc(classic_arc, classic_btn, lv_color_black(), true);
    if (classic_tick == NULL && classic_btn) {
        static const lv_point_t tick_pts[2] = {{24, 6}, {24, 16}};
        classic_tick = lv_line_create(classic_btn);
        lv_line_set_points(classic_tick, tick_pts, 2);
        lv_obj_set_size(classic_tick, 48, 48);
        lv_obj_center(classic_tick);
        lv_obj_clear_flag(classic_tick, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_line_width(classic_tick, 3, 0);
        lv_obj_set_style_line_rounded(classic_tick, true, 0);
    }
    if (classic_tick) {
        lv_obj_set_style_line_color(classic_tick, lv_color_black(), 0);
        lv_obj_set_style_line_opa(classic_tick, active ? LV_OPA_COVER : LV_OPA_60, 0);
    }
}

static void update_radio_icon(lv_color_t color, bool active)
{
    radio_arc = ensure_arc(radio_arc, radio_btn, lv_color_black(), false);
    if (radio_arc) {
        lv_obj_set_style_arc_color(radio_arc, lv_color_black(), 0);
        lv_obj_set_style_arc_opa(radio_arc, LV_OPA_COVER, 0);
    }
}

static void update_shore_icon(lv_color_t color, bool active)
{
    shore_arc = ensure_arc(shore_arc, shore_label, lv_color_black(), false);
    if (shore_arc) {
        lv_obj_set_style_arc_color(shore_arc, lv_color_black(), 0);
        lv_obj_set_style_arc_opa(shore_arc, LV_OPA_COVER, 0);
    }

    // Aufräumen alter Segmente, falls vorhanden
    if (shore_bolt_a) { lv_obj_del(shore_bolt_a); shore_bolt_a = NULL; }
    if (shore_bolt_b) { lv_obj_del(shore_bolt_b); shore_bolt_b = NULL; }
    if (shore_bolt_c) { lv_obj_del(shore_bolt_c); shore_bolt_c = NULL; }

    if (!shore_bolt_poly && shore_label) {
        static const lv_point_t bolt_pts[] = {
            {26, 10}, {18, 24}, {26, 24}, {20, 36}, {30, 22}, {22, 22}, {26, 10}
        };
        shore_bolt_poly = lv_line_create(shore_label);
        lv_line_set_points(shore_bolt_poly, bolt_pts, sizeof(bolt_pts) / sizeof(bolt_pts[0]));
        lv_obj_set_size(shore_bolt_poly, 48, 48);
        lv_obj_center(shore_bolt_poly);
        lv_obj_clear_flag(shore_bolt_poly, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_line_width(shore_bolt_poly, 3, 0);
        lv_obj_set_style_line_rounded(shore_bolt_poly, true, 0);
    }

    lv_opa_t bolt_opa = LV_OPA_COVER; // immer voll deckend
    lv_color_t bolt_color = lv_color_black();
    if (shore_bolt_poly) {
        lv_obj_set_style_line_color(shore_bolt_poly, bolt_color, 0);
        lv_obj_set_style_line_opa(shore_bolt_poly, bolt_opa, 0);
    }
}

// Timer-Callbacks für asynchronen RS485-Send (blockiert nicht den LVGL-Thread)
// Statische Handles für Debounce – verhindert Doppel-Send bei schnellem Tippen
static lv_timer_t *s_pwr_send_timer = NULL;
static lv_timer_t *s_radio_send_timer = NULL;

static void pwr_12v_send_timer_cb(lv_timer_t *timer)
{
    s_pwr_send_timer = NULL;
    bool enable = (bool)(uintptr_t)timer->user_data;
    esp_err_t err = womo_rs485_send_pwr_12v(enable);
    if (err == ESP_OK) {
        s_pwr_cmd_sent_us = esp_timer_get_time();
    } else {
        ESP_LOGW(TAG, "12V RS485-Send fehlgeschlagen: %s – warte auf ctrl-Korrektur", esp_err_to_name(err));
    }
    lv_timer_del(timer);
}

static void radio_send_timer_cb(lv_timer_t *timer)
{
    s_radio_send_timer = NULL;
    bool enable = (bool)(uintptr_t)timer->user_data;
    esp_err_t err = womo_rs485_send_radio(enable);
    if (err == ESP_OK) {
        s_radio_cmd_sent_us = esp_timer_get_time();
    } else {
        ESP_LOGW(TAG, "Radio RS485-Send fehlgeschlagen: %s – warte auf ctrl-Korrektur", esp_err_to_name(err));
    }
    lv_timer_del(timer);
}

static void classic_button_event_cb(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    // Debounce: vorherigen noch nicht gesendeten Befehl abbrechen
    if (s_pwr_send_timer) {
        lv_timer_del(s_pwr_send_timer);
        s_pwr_send_timer = NULL;
    }
    // Optimistisches UI: sofort umschalten
    classic_on = !classic_on;
    lv_color_t color = lv_color_hex(0x2E7D32);
    simple_toggle_button_update(classic_btn, classic_label, classic_on, "", color);
    update_classic_icon(color, classic_on);
    // Radio geht aus wenn 12V aus
    if (!classic_on && radio_on) {
        radio_on = false;
        lv_color_t rc = lv_color_hex(0x1565C0);
        simple_toggle_button_update(radio_btn, radio_label, radio_on, "Radio", rc);
        update_radio_icon(rc, radio_on);
        // Radio-Aus: alten Timer canceln falls vorhanden, neuen starten
        if (s_radio_send_timer) {
            lv_timer_del(s_radio_send_timer);
            s_radio_send_timer = NULL;
        }
        s_radio_send_timer = lv_timer_create(radio_send_timer_cb, 10, (void *)(uintptr_t)false);
        if (s_radio_send_timer) lv_timer_set_repeat_count(s_radio_send_timer, 1);
    }
    // 12V-Befehl asynchron senden
    s_pwr_send_timer = lv_timer_create(pwr_12v_send_timer_cb, 10, (void *)(uintptr_t)classic_on);
    if (s_pwr_send_timer) lv_timer_set_repeat_count(s_pwr_send_timer, 1);
}

static void radio_button_event_cb(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    bool new_state = !radio_on;
    // Radio nur einschalten wenn 12V aktiv
    if (new_state && !classic_on) {
        ESP_LOGW(TAG, "Radio kann nicht eingeschaltet werden: 12V nicht aktiv");
        return;
    }
    // Debounce: vorherigen noch nicht gesendeten Befehl abbrechen
    if (s_radio_send_timer) {
        lv_timer_del(s_radio_send_timer);
        s_radio_send_timer = NULL;
    }
    // Optimistisches UI: sofort umschalten
    radio_on = new_state;
    lv_color_t color = lv_color_hex(0x1565C0);
    simple_toggle_button_update(radio_btn, radio_label, radio_on, "Radio", color);
    update_radio_icon(color, radio_on);
    // RS485-Befehl asynchron senden
    s_radio_send_timer = lv_timer_create(radio_send_timer_cb, 10, (void *)(uintptr_t)radio_on);
    if (s_radio_send_timer) lv_timer_set_repeat_count(s_radio_send_timer, 1);
}

static void shore_power_update_label(void)
{
    if (!shore_label) {
        return;
    }

    lv_color_t bg = shore_power_present ? lv_color_hex(0xF9A825) : lv_color_hex(0xE0E0E0);
    lv_opa_t opa = shore_power_present ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_obj_set_style_bg_color(shore_label, bg, 0);
    lv_obj_set_style_bg_opa(shore_label, opa, 0);
    lv_label_set_text(shore_label, "");
}

static void backlight_set(bool on)
{
    esp_err_t err = ESP_OK;
    if (on) {
        err = wavesahre_rgb_lcd_bl_on();
    } else {
        err = wavesahre_rgb_lcd_bl_off();
    }

    if (err == ESP_OK) {
        backlight_on = on;
        ESP_LOGI(TAG, "Backlight set %s", on ? "ON" : "OFF");
        backlight_update_label();
    } else {
        ESP_LOGW(TAG, "Backlight %s fehlgeschlagen: %s", on ? "ein" : "aus", esp_err_to_name(err));
    }
}

void app_main()
{
    // Initialize i2cdev FIRST (needed by display for CH422G GPIO expander)
    ESP_LOGI(TAG, "Initializing i2cdev...");
    ESP_ERROR_CHECK(i2cdev_init());
    
    // Initialize time management
    womo_time_init();
    rs485_watchdog_start_us = esp_timer_get_time();
    rs485_last_packet_time_us = rs485_watchdog_start_us;
    rs485_timeout_active = false;
    
    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    womo_wifi_init();
    imu_orientation_load_from_nvs();
    if (imu_orientation != WOMO_IMU_ORIENTATION_RIGHT) {
        ESP_LOGI(TAG, "Forcing IMU orientation to RIGHT for lateral display mounting");
        imu_orientation_set(WOMO_IMU_ORIENTATION_RIGHT, true);
    }
    
    // Initialize theme (default location: Central Europe)
    // TODO: Get location from GPS (Walter Modem)
    womo_theme_init(50.0, 10.0);  // Approximate Germany
    
    // Set sunrise/sunset for Central Europe winter
    womo_theme_set_sun_times(7, 30, 17, 0);
    
    // Initialize display (uses I2C for touch controller)
    waveshare_esp32_s3_rgb_lcd_init();

    // Ensure backlight is turned on after panel init (CH422G-controlled)
    backlight_set(true);
    
    // NOW initialize SD card (will use existing I2C bus for CH422G GPIO expander)
    ESP_LOGI(TAG, "Initializing SD card...");
    if (womo_sd_init() == ESP_OK) {
        ESP_LOGI(TAG, "SD card mounted successfully");
    } else {
        ESP_LOGW(TAG, "SD card mount failed - continuing without SD");
    }
    
    // Initialize RS485 communication (receives data from Walter)
    ESP_LOGI(TAG, "Initializing RS485 display receiver...");
    if (womo_rs485_display_init() == ESP_OK) {
        ESP_LOGI(TAG, "RS485 initialized - will receive data from Walter");
        womo_rs485_set_data_callback(rs485_data_received, NULL);
        womo_rs485_set_event_callback(rs485_event_handler, NULL);
        rs485_waiting_for_handshake = true;
    } else {
        ESP_LOGW(TAG, "RS485 init failed - continuing without external sensors");
    }
    
    ESP_LOGI(TAG, "Display WoMo Home Control with Dynamic Theme");
    
    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (lvgl_port_lock(-1)) {
        
        // Get main screen and enable touch events
        lv_obj_t *screen = lv_scr_act();
        lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(screen, screen_event_handler, LV_EVENT_CLICKED, NULL);

        // Touch-Wake-Callback: feuert bei JEDEM Touch, noch vor LVGL-Events
        lvgl_touch_set_wake_cb(touch_wake_cb);
        
        // Apply initial theme and load matching background (day/night)
        womo_theme_mode_t initial_mode = womo_theme_update(WOMO_STATUS_OK);
        load_background_image(screen, theme_mode_is_daylike(initial_mode));
        if (bg_img != NULL) {
            // Screen background stays SOLID (LV_OPA_COVER) to show theme colors
            // Ducato image is semi-transparent (LV_OPA_60) to let colors through
            lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        }
        womo_theme_apply_to_screen(screen);
        
        // Initialize locale system
        womo_locale_init();
        
        // Test deutsche Schriftarten
        womo_test_german_fonts();
        
    // Create title - höher positioniert
    title_label = lv_label_create(screen);
    lv_label_set_text(title_label, womo_locale_get_string(STR_TITLE));
    lv_obj_set_style_text_font(title_label, WOMO_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title_label, lv_color_black(), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 5);
        
    // Create time display - direkt unter Titel, feste Breite damit stabil
    // Create license plate style frame for time display (smaller for 20px font)
    // Create time label below the title (no frame)
    time_label = lv_label_create(screen);
    lv_label_set_text(time_label, "--:--:--");
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_20_german, 0);
    lv_obj_set_style_text_color(time_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(time_label, LV_ALIGN_TOP_MID, 0, 35);  // Direkt unter dem Titel
    lv_label_set_long_mode(time_label, LV_LABEL_LONG_CLIP);
        
        // Create date display - links unten (statt Mode)
        date_label = lv_label_create(screen);
        lv_label_set_text(date_label, "--.--.----");
        lv_obj_set_style_text_font(date_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(date_label, lv_color_black(), 0);
        lv_obj_align(date_label, LV_ALIGN_BOTTOM_LEFT, 20, -20);
        
        // Create WiFi status (top left) - moved from right
        wifi_label = lv_label_create(screen);
        lv_label_set_text(wifi_label, "WiFi: offline 0%\nLTE : -- 0%");
        lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(wifi_label, lv_color_black(), 0);
        lv_obj_set_style_border_width(wifi_label, 2, 0);
        lv_obj_set_style_border_color(wifi_label, lv_color_black(), 0);
        lv_obj_set_style_radius(wifi_label, 6, 0);
        lv_obj_set_style_pad_all(wifi_label, 6, 0);
        lv_obj_set_style_bg_color(wifi_label, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_bg_opa(wifi_label, LV_OPA_30, 0); // 70% Durchscheinen
        lv_obj_set_style_text_align(wifi_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(wifi_label, LV_ALIGN_TOP_LEFT, 10, 10);
        lv_obj_add_flag(wifi_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(wifi_label, wifi_label_event_cb, LV_EVENT_CLICKED, NULL);
        update_connectivity_label();

        // Zusätzliche Schalter links (klassisch, Radio) und Netzstrom-Anzeige
        classic_btn = lv_btn_create(screen);
        lv_obj_set_size(classic_btn, 48, 48);
        lv_obj_align_to(classic_btn, date_label, LV_ALIGN_OUT_TOP_LEFT, 0, -10);
        lv_obj_set_style_radius(classic_btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(classic_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(classic_btn, 0, 0);
        lv_obj_set_style_border_color(classic_btn, lv_color_hex(0x7A7A7A), 0);
        lv_obj_add_flag(classic_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(classic_btn, classic_button_event_cb, LV_EVENT_CLICKED, NULL);
        classic_label = lv_label_create(classic_btn);
        simple_toggle_button_update(classic_btn, classic_label, classic_on, "", lv_color_hex(0x2E7D32));
        update_classic_icon(lv_color_hex(0x2E7D32), classic_on);

        radio_btn = lv_btn_create(screen);
        lv_obj_set_size(radio_btn, 48, 48);
        lv_obj_align_to(radio_btn, wifi_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
        lv_obj_set_style_radius(radio_btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(radio_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(radio_btn, 0, 0);
        lv_obj_set_style_border_color(radio_btn, lv_color_hex(0x7A7A7A), 0);
        lv_obj_add_flag(radio_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(radio_btn, radio_button_event_cb, LV_EVENT_CLICKED, NULL);
        radio_label = lv_label_create(radio_btn);
        simple_toggle_button_update(radio_btn, radio_label, radio_on, "Radio", lv_color_hex(0x1565C0));
        update_radio_icon(lv_color_hex(0x1565C0), radio_on);

        shore_label = lv_label_create(screen);
        lv_obj_set_size(shore_label, 48, 48);
        lv_obj_align(shore_label, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_radius(shore_label, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(shore_label, 0, 0);
        lv_obj_set_style_pad_all(shore_label, 0, 0);
        lv_obj_set_style_bg_opa(shore_label, LV_OPA_COVER, 0);
        lv_label_set_text(shore_label, "");
        shore_power_update_label();
        update_shore_icon(lv_color_hex(0xF9A825), shore_power_present);

        shore_caption_label = lv_label_create(screen);
        lv_label_set_text(shore_caption_label, "220 V");
        lv_obj_set_style_text_font(shore_caption_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(shore_caption_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(shore_caption_label, lv_color_black(), 0);
        lv_obj_align_to(shore_caption_label, shore_label, LV_ALIGN_OUT_BOTTOM_MID, 0, -24);

            // Backlight Toggle (Lampe) unten mittig
            backlight_btn = lv_btn_create(screen);
            lv_obj_set_size(backlight_btn, 48, 48);
            lv_obj_align(backlight_btn, LV_ALIGN_RIGHT_MID, -10, 0); // rechtsbündig mit einheitlichem Rand
            lv_obj_set_style_radius(backlight_btn, LV_RADIUS_CIRCLE, 0); // Button bleibt kreisrund
            lv_obj_set_style_bg_color(backlight_btn, lv_color_hex(0xC0C0C0), 0); // silberner Hintergrund
            lv_obj_set_style_bg_opa(backlight_btn, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(backlight_btn, 0, 0);
            lv_obj_set_style_border_color(backlight_btn, lv_color_hex(0x7A7A7A), 0);
            lv_obj_add_flag(backlight_btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(backlight_btn, backlight_button_event_cb, LV_EVENT_CLICKED, NULL);

            backlight_update_label();
        
        // Weather data (top right) - Gas first, all one font size larger
    char init_buf[40];

    air_title_label = lv_label_create(screen);
    lv_label_set_text(air_title_label, "Luftwerte aussen");
    lv_obj_set_style_text_font(air_title_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(air_title_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(air_title_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(air_title_label, LV_ALIGN_TOP_RIGHT, -10, 10);

    press_container = lv_obj_create(screen);
    lv_obj_clear_flag(press_container, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(press_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_size(press_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(press_container, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_width(press_container, 150);
    lv_obj_set_style_bg_opa(press_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(press_container, 0, 0);
    lv_obj_set_style_pad_all(press_container, 0, 0);
    lv_obj_set_style_pad_column(press_container, 4, 0);
    lv_obj_align(press_container, LV_ALIGN_TOP_RIGHT, -10, 35);

    press_icon_label = lv_label_create(press_container);
    lv_label_set_text(press_icon_label, press_trend_arrow(NULL));
    lv_obj_set_style_text_font(press_icon_label, WOMO_FONT_ICONS, 0);
    lv_obj_set_style_text_color(press_icon_label, lv_color_black(), 0);

    press_label = lv_label_create(press_container);
    lv_label_set_text(press_label, PLACEHOLDER_PRESSURE);
    lv_obj_set_style_text_font(press_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(press_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(press_label, LV_TEXT_ALIGN_RIGHT, 0);

    humid_label = lv_label_create(screen);
    snprintf(init_buf, sizeof(init_buf), "--.-- %%");
    lv_label_set_text(humid_label, init_buf);
    lv_obj_set_style_text_font(humid_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(humid_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(humid_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(humid_label, LV_ALIGN_TOP_RIGHT, -10, 60);

    temp_label = lv_label_create(screen);
    snprintf(init_buf, sizeof(init_buf), "--.- °C");
    lv_label_set_text(temp_label, init_buf);
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(temp_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(temp_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(temp_label, LV_ALIGN_TOP_RIGHT, -10, 85);

    // Ortsname neben dem GPS-Button platzieren
    location_label = lv_label_create(screen);
    lv_label_set_text(location_label, "");
    lv_obj_set_style_text_font(location_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(location_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(location_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(location_label, 240);
    lv_label_set_long_mode(location_label, LV_LABEL_LONG_WRAP);

    // IMU-Werte als freistehende Labels (ca. 40% von oben, 30% vom rechten Rand)
    lv_coord_t disp_w = lv_disp_get_hor_res(NULL);
    lv_coord_t disp_h = lv_disp_get_ver_res(NULL);
    if (disp_w <= 0) {
        disp_w = 800;
    }
    if (disp_h <= 0) {
        disp_h = 480;
    }

    // Innenraum-BME680 (addr 0x76) mittig platzieren, etwas tiefer (ca. +1/4 Displayhöhe)
    lv_coord_t indoor_base_y = 170 + (disp_h / 4) - 35; // 15px höher
    // Block so ausrichten, dass das rechte Ende (rechtsbündig) mittig im Display liegt
    lv_coord_t indoor_block_x = (disp_w / 2) - 290;
    air_title_label_in = lv_label_create(screen);
    lv_label_set_text(air_title_label_in, "Luftwerte innen");
    lv_obj_set_style_text_font(air_title_label_in, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(air_title_label_in, lv_color_black(), 0);
    lv_obj_set_width(air_title_label_in, 320);
    lv_obj_set_style_text_align(air_title_label_in, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(air_title_label_in, indoor_block_x, indoor_base_y);

    humid_label_in = lv_label_create(screen);
    lv_label_set_text(humid_label_in, "--.-- %");
    lv_obj_set_style_text_font(humid_label_in, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(humid_label_in, lv_color_black(), 0);
    lv_obj_set_width(humid_label_in, 150);
    lv_obj_set_style_text_align(humid_label_in, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(humid_label_in, indoor_block_x + 100, indoor_base_y + 25);

    temp_label_in = lv_label_create(screen);
    lv_label_set_text(temp_label_in, "--.- °C");
    lv_obj_set_style_text_font(temp_label_in, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(temp_label_in, lv_color_black(), 0);
    lv_obj_set_width(temp_label_in, 150);
    lv_obj_set_style_text_align(temp_label_in, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(temp_label_in, indoor_block_x + 170, indoor_base_y + 25);

    gas_label_in = lv_label_create(screen);
    lv_label_set_text(gas_label_in, PLACEHOLDER_IAQ);
    lv_obj_set_style_text_font(gas_label_in, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(gas_label_in, lv_color_black(), 0);
    lv_obj_set_width(gas_label_in, 320);
    lv_obj_set_style_text_align(gas_label_in, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(gas_label_in, indoor_block_x, indoor_base_y + 55);

    press_label_in = lv_label_create(screen);
    lv_label_set_text(press_label_in, PLACEHOLDER_CO2);
    lv_obj_set_style_text_font(press_label_in, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(press_label_in, lv_color_black(), 0);
    lv_obj_set_width(press_label_in, 320);
    lv_obj_set_style_text_align(press_label_in, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(press_label_in, indoor_block_x, indoor_base_y + 80);

    voc_label_in = lv_label_create(screen);
    lv_label_set_text(voc_label_in, PLACEHOLDER_BVOC);
    lv_obj_set_style_text_font(voc_label_in, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(voc_label_in, lv_color_black(), 0);
    lv_obj_set_width(voc_label_in, 320);
    lv_obj_set_style_text_align(voc_label_in, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(voc_label_in, indoor_block_x, indoor_base_y + 105);

    // Farben an aktuelles Theme anpassen (Tag/Nacht)
    apply_text_theme_colors();

    lv_coord_t right_margin = (lv_coord_t)lrintf(disp_w * 0.30f) - 30;
    if (right_margin < 0) {
        right_margin = 0;
    }
    lv_coord_t top_offset = (lv_coord_t)lrintf(disp_h * 0.40f) - 40;
    if (top_offset < 0) {
        top_offset = 0;
    }
    lv_coord_t line_spacing = 24;

    imu_pitch_label = lv_label_create(screen);
    lv_label_set_text(imu_pitch_label, "Pitch: --.-°");
    lv_obj_set_style_text_font(imu_pitch_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(imu_pitch_label, lv_color_white(), 0);
    lv_obj_align(imu_pitch_label, LV_ALIGN_TOP_RIGHT, -right_margin, top_offset);

    imu_roll_label = lv_label_create(screen);
    lv_label_set_text(imu_roll_label, "Roll : --.-°");
    lv_obj_set_style_text_font(imu_roll_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(imu_roll_label, lv_color_white(), 0);
    lv_obj_align(imu_roll_label, LV_ALIGN_TOP_RIGHT, -right_margin, top_offset + line_spacing);

    imu_heading_label = lv_label_create(screen);
    lv_label_set_text(imu_heading_label, "-- (---°)");
    lv_obj_set_style_text_font(imu_heading_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(imu_heading_label, lv_color_white(), 0);
    lv_obj_align(imu_heading_label, LV_ALIGN_TOP_RIGHT, -right_margin, top_offset + (line_spacing * 2));

    gps_label = lv_label_create(screen);
    lv_label_set_text(gps_label, "GPS");
    lv_obj_set_style_text_font(gps_label, &lv_font_montserrat_14, 0); // gleiche Schriftgröße wie Status-Label
    lv_obj_set_style_text_color(gps_label, lv_color_black(), 0);
    lv_obj_set_width(gps_label, 360);
    lv_label_set_long_mode(gps_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(gps_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_coord_t gps_offset_x = disp_w / 5;
    if (gps_offset_x < 0) {
        gps_offset_x = 0;
    }
    lv_obj_align(gps_label, LV_ALIGN_BOTTOM_LEFT, gps_offset_x, -20); // gleiche Höhe wie Datum

    // Button deckungsgleich mit dem Label-Bereich, Größe wie Status-Button (Font 14 + Padding)
    lv_point_t gps_text_size;
    lv_txt_get_size(&gps_text_size, "GPS", &lv_font_montserrat_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_coord_t gps_btn_w = gps_text_size.x + 12; // Padding analog Status (6px pro Seite)
    lv_coord_t gps_btn_h = gps_text_size.y + 12; // Höhe analog Status (Pad 6 oben/unten)
    if (gps_btn_w < 48) gps_btn_w = 48;
    if (gps_btn_h < 24) gps_btn_h = 24;
    gps_button = lv_btn_create(screen);
    lv_obj_set_size(gps_button, gps_btn_w, gps_btn_h);
    lv_obj_set_style_radius(gps_button, 6, 0);
    lv_obj_set_style_border_width(gps_button, 2, 0);
    lv_obj_set_style_bg_opa(gps_button, LV_OPA_30, 0); // leicht durchscheinend wie Status
    // Button links-mittig am Label ausrichten, damit Text mittig im Rahmen liegt
    lv_obj_align_to(gps_button, gps_label, LV_ALIGN_LEFT_MID, -6, 0);
    lv_obj_add_event_cb(gps_button, gps_label_event_cb, LV_EVENT_CLICKED, NULL);

    // Ortsname rechts neben dem GPS-Button anordnen
    if (location_label) {
        lv_obj_align_to(location_label, gps_button, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    }

    // cm → Pixel Umrechnung (anpassbar für physische Displaymaße)
    const float DISP_WIDTH_CM = 15.5f;
    const float DISP_HEIGHT_CM = 9.3f;
    const float px_per_cm_x = disp_w / DISP_WIDTH_CM;
    const float px_per_cm_y = disp_h / DISP_HEIGHT_CM;

    // Create water tank widgets (positioned above the gas bottles)
    fresh_water_tank = womo_tank_create(screen, 115, 110, WOMO_TANK_FRESH);
    if (fresh_water_tank) {
        womo_tank_set_caption(fresh_water_tank, "");
        womo_tank_set_no_data(fresh_water_tank);
        fresh_water_caption_label = lv_label_create(screen);
        lv_label_set_text(fresh_water_caption_label, "Frisch");
        lv_obj_set_style_text_font(fresh_water_caption_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(fresh_water_caption_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(fresh_water_caption_label, lv_color_black(), 0);
        lv_obj_align_to(fresh_water_caption_label, fresh_water_tank->container, LV_ALIGN_OUT_BOTTOM_MID, 0, -24);
    } else {
        ESP_LOGW(TAG, "Failed to create fresh water tank widget");
    }

    grey_water_tank = womo_tank_create(screen, 185, 110, WOMO_TANK_GREY);
    if (grey_water_tank) {
        womo_tank_set_caption(grey_water_tank, "");
        womo_tank_set_no_data(grey_water_tank);
        grey_water_caption_label = lv_label_create(screen);
        lv_label_set_text(grey_water_caption_label, "Grau");
        lv_obj_set_style_text_font(grey_water_caption_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(grey_water_caption_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(grey_water_caption_label, lv_color_black(), 0);
        lv_obj_align_to(grey_water_caption_label, grey_water_tank->container, LV_ALIGN_OUT_BOTTOM_MID, 0, -24);
        if (shore_label) {
            lv_obj_align_to(shore_label, grey_water_tank->container, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
            if (shore_caption_label) {
                lv_obj_align_to(shore_caption_label, shore_label, LV_ALIGN_OUT_BOTTOM_MID, 0, -24);
                if (grey_water_caption_label) {
                    lv_obj_set_y(shore_caption_label, lv_obj_get_y(grey_water_caption_label));
                }
            }
        }
    } else {
        ESP_LOGW(TAG, "Failed to create grey water tank widget");
    }

    // Create gas bottle widgets (20px weiter nach links)
    // Display height is 480px, so 480 - 250 = 230px from top
    gas_bottle_a = womo_gas_bottle_create(screen, 120, 230);  // Gas bottle A (90 - 20 + 50)
    gas_bottle_b = womo_gas_bottle_create(screen, 190, 230);  // Gas bottle B (160 - 20 + 50)
    
    // Set weights for bottles: 10.1 kg = 0%, 21 kg = 100%
    if (gas_bottle_a) {
        womo_gas_bottle_set_empty_weight(gas_bottle_a, 10.1f);  // Empty bottle (0%)
        womo_gas_bottle_set_full_weight(gas_bottle_a, 21.0f);   // Full bottle (100%)
        womo_gas_bottle_set_cap_label(gas_bottle_a, "");
        lv_obj_add_flag(gas_bottle_a->container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(gas_bottle_a->container, gas_bottle_clicked_cb, LV_EVENT_ALL, (void *)(uintptr_t)0);
        if (!gas_label_front) {
            gas_label_front = lv_label_create(screen);
            lv_label_set_text(gas_label_front, "Vorne");
            lv_obj_set_style_text_font(gas_label_front, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_align(gas_label_front, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(gas_label_front, lv_color_black(), 0);
        }
        lv_obj_align_to(gas_label_front, gas_bottle_a->container, LV_ALIGN_OUT_BOTTOM_MID, -10, -33); // 5px tiefer
    }
    if (gas_bottle_b) {
        womo_gas_bottle_set_empty_weight(gas_bottle_b, 10.1f);  // Empty bottle (0%)
        womo_gas_bottle_set_full_weight(gas_bottle_b, 21.0f);   // Full bottle (100%)
        womo_gas_bottle_set_cap_label(gas_bottle_b, "");
        lv_obj_add_flag(gas_bottle_b->container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(gas_bottle_b->container, gas_bottle_clicked_cb, LV_EVENT_ALL, (void *)(uintptr_t)1);
        if (!gas_label_rear) {
            gas_label_rear = lv_label_create(screen);
            lv_label_set_text(gas_label_rear, "Hinten");
            lv_obj_set_style_text_font(gas_label_rear, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_align(gas_label_rear, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(gas_label_rear, lv_color_black(), 0);
        }
        lv_obj_align_to(gas_label_rear, gas_bottle_b->container, LV_ALIGN_OUT_BOTTOM_MID, -10, -33); // 5px tiefer
    }

    if (!gas_info_label) {
        gas_info_label = lv_label_create(screen);
        lv_label_set_text(gas_info_label, "Gas (--):\n--.-- kg\n--.-- kg/h\n--.- h");
        lv_obj_set_style_text_font(gas_info_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(gas_info_label, lv_color_black(), 0);
        lv_obj_set_style_text_align(gas_info_label, LV_TEXT_ALIGN_RIGHT, 0);
        if (gas_bottle_a && gas_bottle_a->container) {
            lv_obj_align_to(gas_info_label, gas_bottle_a->container, LV_ALIGN_OUT_LEFT_MID, -8, 0);
        } else {
            lv_obj_align(gas_info_label, LV_ALIGN_TOP_LEFT, 10, 120);
        }
    }
    
    // Create weather widget in top-right corner (icon sits beneath the weather values)
    weather_widget = womo_weather_create(screen);
    if (weather_widget) {
        // Initial: unbekanntes Wetter, Nacht-Icon, kein Demo-Temperatur-Update (Temp-Label entfernt)
        womo_weather_set_condition(weather_widget, WEATHER_NT_UNKNOWN, true);
        ESP_LOGI(TAG, "Weather widget created successfully");
    } else {
        ESP_LOGW(TAG, "Failed to create weather widget");
    }
    
    const lv_coord_t battery_bottom_margin = 4;

    // Create first battery widget (Battery 1) - KFZ Batterie
    main_battery = womo_battery_create(screen, 0, 0);
    if (main_battery) {
        // Set 12V battery voltage range: 10.5V (0%) to 14.4V (100%)
        womo_battery_set_voltage_range(main_battery, 10.5f, 14.4f);
        womo_battery_set_no_data(main_battery);
        womo_battery_set_show_percent(main_battery, false); // Hide percentage for clean look

        // Position: gemessen von links/unten (Mitte des Batterieblocks)
        lv_coord_t car_width = lv_obj_get_width(main_battery->container);
        lv_coord_t car_height = lv_obj_get_height(main_battery->container);
        float car_center_x = (13.5f * px_per_cm_x);
        float car_center_y = disp_h - (1.5f * px_per_cm_y);
        lv_coord_t car_x = (lv_coord_t)lrintf(car_center_x - (car_width / 2.0f)) - 35; // weitere 5px nach links (insgesamt 35px)
        lv_coord_t car_y = (lv_coord_t)lrintf(car_center_y - (car_height / 2.0f)) - 30; // weitere 5px nach oben (insgesamt 30px)
        if (car_x < 0) car_x = 0;
        if ((car_x + car_width) > disp_w) car_x = disp_w - car_width;
        if (car_y < 0) car_y = 0;
        if ((car_y + car_height) > disp_h) car_y = disp_h - car_height;
        womo_battery_set_pos(main_battery, car_x, car_y);
        if (!battery_kfz_label) {
            battery_kfz_label = lv_label_create(screen);
            lv_label_set_text(battery_kfz_label, "Kfz");
            lv_obj_set_style_text_font(battery_kfz_label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(battery_kfz_label, lv_color_black(), 0);
            lv_obj_align_to(battery_kfz_label, main_battery->container, LV_ALIGN_OUT_BOTTOM_MID, 0, -4);
        } else {
            lv_obj_align_to(battery_kfz_label, main_battery->container, LV_ALIGN_OUT_BOTTOM_MID, 0, -4);
        }
        ESP_LOGI(TAG, "Battery 1 widget created successfully");
    } else {
        ESP_LOGW(TAG, "Failed to create battery 1 widget");
    }
    
    // Create second battery widget (Battery 2) - OMO Batterie
    secondary_battery = womo_battery_create(screen, 0, 0);
    if (secondary_battery) {
        // Same voltage range as first battery
        womo_battery_set_voltage_range(secondary_battery, 10.5f, 14.4f);
        womo_battery_set_no_data(secondary_battery);
        womo_battery_set_show_percent(secondary_battery, false); // Hide percentage for clean look

        // Position: gemessen von links/unten (Mitte des Batterieblocks)
        lv_coord_t omo_width = lv_obj_get_width(secondary_battery->container);
        lv_coord_t omo_height = lv_obj_get_height(secondary_battery->container);
        float omo_center_x = (9.0f * px_per_cm_x);
        float omo_center_y = disp_h - (1.5f * px_per_cm_y);
        lv_coord_t omo_x = (lv_coord_t)lrintf(omo_center_x - (omo_width / 2.0f)) - 15; // 10px weiter nach links (insgesamt 15px)
        lv_coord_t omo_y = (lv_coord_t)lrintf(omo_center_y - (omo_height / 2.0f)) - 30; // auf gleiche Höhe wie KFZ-Batterie
        if (omo_x < 0) omo_x = 0;
        if (omo_x + omo_width > disp_w) omo_x = disp_w - omo_width;
        if (omo_y < 0) omo_y = 0;
        if (omo_y + omo_height > disp_h) omo_y = disp_h - omo_height - battery_bottom_margin;

        womo_battery_set_pos(secondary_battery, omo_x, omo_y);
        if (!battery_board_label) {
            battery_board_label = lv_label_create(screen);
            lv_label_set_text(battery_board_label, "Board");
            lv_obj_set_style_text_font(battery_board_label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(battery_board_label, lv_color_black(), 0);
            lv_obj_align_to(battery_board_label, secondary_battery->container, LV_ALIGN_OUT_BOTTOM_MID, 0, -4);
        } else {
            lv_obj_align_to(battery_board_label, secondary_battery->container, LV_ALIGN_OUT_BOTTOM_MID, 0, -4);
        }
        ESP_LOGI(TAG, "Battery 2 widget created successfully");
    } else {
        ESP_LOGW(TAG, "Failed to create battery 2 widget");
    }
    
    // RS485 Debug label (über KFZ-Batterie, fallback unten links)
    rs485_debug_label = lv_label_create(screen);
    lv_label_set_text(rs485_debug_label, womo_locale_get_string(STR_RS485_WAITING));
    lv_obj_set_style_text_font(rs485_debug_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rs485_debug_label, lv_color_make(255, 0, 0), 0); // Red for visibility
    if (main_battery && main_battery->container) {
        lv_obj_align_to(rs485_debug_label, main_battery->container, LV_ALIGN_OUT_TOP_MID, 0, -6);
    } else {
        lv_obj_align(rs485_debug_label, LV_ALIGN_BOTTOM_LEFT, 20, -60);
    }
        
        // Mode label removed - theme now fully automatic based on real time
        
        // Create status label (right bottom - higher to avoid FPS overlay)
        status_label = lv_label_create(screen);
        status_label_last_text[0] = '\0';
        system_status_apply(true);
        lv_obj_set_style_text_color(status_label, lv_color_black(), 0);
        lv_obj_set_style_border_width(status_label, 2, 0);
        lv_obj_set_style_border_color(status_label, lv_color_black(), 0);
        lv_obj_set_style_radius(status_label, 6, 0);
        lv_obj_set_style_pad_all(status_label, 6, 0);
        lv_obj_set_style_bg_color(status_label, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_bg_opa(status_label, LV_OPA_30, 0); // 70% Durchscheinen
        lv_obj_add_flag(status_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(status_label, status_label_event_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_align(status_label, LV_ALIGN_BOTTOM_LEFT, 543, -10);  // 10px weiter nach links
        
    // Create LVGL timer to update time every second
    lv_timer_create(time_update_timer_cb, 1000, NULL);
    ui_update_timer = lv_timer_create(ui_update_timer_cb, UI_UPDATE_INTERVAL_DEFAULT_MS, NULL);
    if (!ui_update_timer) {
        ESP_LOGW(TAG, "Failed to create UI update timer");
    }

    // Ensure text colors match the initial day/night theme
    apply_text_theme_colors();
        
        // Release the mutex
        lvgl_port_unlock();
    }

    // Connect to fixed Router-AP (RUTX11 "Malibu-622")
    ESP_LOGI(TAG, "Connecting to Router-AP: %s", WIFI_SSID);
    esp_err_t wifi_err = womo_wifi_connect(WIFI_SSID, WIFI_PASSWORD, WIFI_MAX_RETRY);

    if (wifi_err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected to Router-AP: %s", WIFI_SSID);

        // Backlight initial einschalten
        backlight_set(true);

        // Reverse-Geocoding wird erst nach dem ersten GPS-Fix on-demand ausgelöst

        // Sync time via NTP (non-blocking)
        if (womo_time_sync_ntp(true) == ESP_OK) {
            ESP_LOGI(TAG, "Time synchronized via NTP");
        } else {
            ESP_LOGW(TAG, "NTP sync failed, using internal RTC");
        }
    } else {
        ESP_LOGW(TAG, "WiFi connection to Router-AP failed (%s) - RUTX11 eingeschaltet?",
                 esp_err_to_name(wifi_err));
    }

    // Router UCI Client initialisieren + Poll-Task starten
    womo_router_uci_init();
    s_router_mutex = xSemaphoreCreateMutex();
    if (s_router_mutex) {
        BaseType_t rc = xTaskCreate(router_poll_task,
                                     "router_poll",
                                     8192,
                                     NULL,
                                     4,
                                     &s_router_poll_handle);
        if (rc != pdPASS) {
            s_router_poll_handle = NULL;
            ESP_LOGW(TAG, "Router-Poll-Task konnte nicht gestartet werden");
        }
    }

    if (wifi_autoretry_handle == NULL) {
        BaseType_t created = xTaskCreate(wifi_autoretry_task,
                                         "wifi_autoretry",
                                         4096,
                                         NULL,
                                         4,
                                         &wifi_autoretry_handle);
        if (created != pdPASS) {
            wifi_autoretry_handle = NULL;
            ESP_LOGW(TAG, "Failed to start WiFi auto-rescan task");
        }
    }
    
    ESP_LOGI(TAG, "System running - Touch Control: Bottom-Left=Language, Right=Status, Auto-Theme=Enabled");
}

// RS485 data received callback - updates display labels
static void rs485_data_received(const womo_sensor_data_t *data, void *user_data)
{
    if (!data) {
        return;
    }

    if (rs485_waiting_for_handshake) {
        rs485_waiting_for_handshake = false;
        ESP_LOGI(TAG, "RS485 handshake abgeschlossen (erstes Datenpaket)");
    }

    if (rs485_invalid_data_active) {
        rs485_invalid_data_active = false;
    }

    rs485_packet_count++;
    int64_t now_us = esp_timer_get_time();
    int64_t previous_us = 0;
    bool timeout_was_active = false;

    taskENTER_CRITICAL(&display_data_spinlock);
    previous_us = rs485_last_packet_time_us;
    rs485_last_packet_time_us = now_us;
    timeout_was_active = rs485_timeout_active;
    rs485_timeout_active = false;
    taskEXIT_CRITICAL(&display_data_spinlock);

    int64_t delta_us = (previous_us == 0) ? 0 : (now_us - previous_us);
    if (timeout_was_active) {
        ESP_LOGI(TAG, "RS485 Timeout beendet, Empfang nach %.1f s wieder aktiv",
                 (double)delta_us / 1000000.0);
    }
    ESP_LOGD(TAG, "rs485_data_received: packet %lu (Δ%lld us)", rs485_packet_count, (long long)delta_us);

    womo_sensor_data_t snapshot = *data;

    if (snapshot.bno.valid && (rs485_packet_count % 20 == 0)) {
        const char *dir = snapshot.bno.direction[0] ? snapshot.bno.direction : "?";
        ESP_LOGD(TAG, "RS485 IMU: %s %.1f° R:%.1f° P:%.1f°",
                 dir, snapshot.bno.heading_deg, snapshot.bno.roll_deg, snapshot.bno.pitch_deg);
    }

    // HX711 weights
    // (Mit topic-basiertem RS485 v2 bleibt valid persistent im Merge-State;
    //  separate Missing-Counter sind nicht mehr nötig.)

    // GPS und LTE werden nicht mehr über RS485 empfangen
    // (Display pollt Router direkt → latest_sensor_data.gps / .lte
    //  werden im router_poll_task geschrieben und dürfen hier nicht
    //  überschrieben werden.)

    taskENTER_CRITICAL(&display_data_spinlock);
    // GPS/LTE-Felder aus dem bestehenden State übernehmen,
    // damit der Router-Poll-Task-State erhalten bleibt.
    snapshot.gps = latest_sensor_data.gps;
    snapshot.lte = latest_sensor_data.lte;
    latest_sensor_data = snapshot;
    latest_packet_count = rs485_packet_count;
    latest_data_valid = true;
    taskEXIT_CRITICAL(&display_data_spinlock);
}

