/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
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
#include "gui/womo_router_leds_modal.h"
#include "gui/womo_settings_modal.h"
#include "gui/womo_forecast_modal.h"
#include "gui/womo_tank.h"
#include "gui/womo_fonts_german.h"
#include "network/womo_wifi.h"
#include "network/womo_weather_http.h"
#include "network/womo_meteoalarm.h"
#include "network/womo_geocode.h"
#include "network/womo_router_uci.h"
#include "network/womo_http_mutex.h"
#include "network/womo_buzzer_http.h"
#include "storage/womo_sd.h"
#include "rs485/womo_rs485_display.h"
#include "hardware/buzzer.h"
#include "nvs.h"
#include "sdkconfig.h"
#include <inttypes.h>
#include <stdio.h>
#include <errno.h>
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
#include "display_config.h"

static const char *TAG = "womo_main";

static const char *PLACEHOLDER_PRESSURE = "---- hPa";
static const char *PLACEHOLDER_IAQ = "IAQ --";
static const char *PLACEHOLDER_CO2 = "CO2 ---- ppm";
static const char *PLACEHOLDER_BVOC = "bVOC --.- ppm";
static const char *PLACEHOLDER_HUMIDITY = "--.-- %";
static const char *PLACEHOLDER_TEMPERATURE = "--.- °C";
static const char *PLACEHOLDER_GPS = "GPS : ---";
#define GEOCODE_MIN_INTERVAL_US       (60LL * 1000000LL) // 60s nach Erfolg
#define GEOCODE_RETRY_INTERVAL_US      (10LL * 1000000LL) // 10s nach Fehler (TLS-Kollision)
// Boot-Delay entfernt – HTTPS-Mutex (womo_http_mutex) serialisiert TLS-Sessions
#define GEOCODE_MIN_DELTA_DEG   0.01
#define QUIET_HOUR_START 22
#define QUIET_HOUR_END   8
#define QUIET_TOUCH_TIMEOUT_MS (5 * 60 * 1000)

#include "gui/womo_thresholds.h"
/* lodepng nicht mehr benötigt – Screenshot schreibt BMP ohne Zwischenpuffer */

// Default thresholds werden jetzt über womo_thresholds.h verwaltet (veränderbar via Einstellungen)

// WiFi credentials from Kconfig
#define WIFI_SSID      CONFIG_WOMO_WIFI_SSID
#define WIFI_PASSWORD  CONFIG_WOMO_WIFI_PASSWORD
#define WIFI_MAX_RETRY CONFIG_WOMO_WIFI_MAX_RETRY

// Global LVGL objects
static lv_obj_t *title_label = NULL;
static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *status_btn = NULL;
static lv_obj_t *status_label = NULL;
static char status_label_last_text[80] = "";
static lv_obj_t *status_inlay = NULL;
static lv_obj_t *wifi_label = NULL;
static lv_obj_t *wifi_inner_border = NULL;
static lv_obj_t *wifi_icon_img = NULL;
static lv_obj_t *lte_icon_img = NULL;
static lv_obj_t *wifi_ap_section = NULL;  // Grüner AP-Bereich in der Mitte des WiFi/LTE-Pills
// wifi_0_bar..wifi_4_bar + cellular_0_bar..cellular_4_bar
static uint8_t       *wifi_icon_bufs[6]  = {0};  // wifi_1..wifi_6
static lv_image_dsc_t wifi_icon_dscs[6]  = {0};
static uint8_t       *lte_icon_bufs[6]   = {0};  // signal_1..signal_6
static lv_image_dsc_t lte_icon_dscs[6]   = {0};
static lv_obj_t *backlight_btn = NULL;
static lv_obj_t *backlight_icon_label = NULL;  // Material-Font-Icon für Helligkeit
static lv_obj_t *settings_btn  = NULL;  // Zahnrad-Taste → Einstellungs-Modal
static lv_obj_t *settings_icon_label = NULL;  // Material-Font-Icon für Einstellungen
static lv_obj_t *classic_btn = NULL;
static lv_obj_t *classic_label = NULL;  // Material-Font-Icon für 12V
static lv_obj_t *radio_btn = NULL;
static lv_obj_t *radio_label = NULL;    // Material-Font-Icon für Multimedia
static lv_obj_t *shore_label = NULL;
static lv_obj_t *shore_caption_label = NULL;
static lv_obj_t *shore_icon_label = NULL;  // Bolt-Icon auf Landstrom-Anzeige
static lv_obj_t *battery_board_label = NULL;
static lv_obj_t *battery_kfz_label = NULL;
static lv_obj_t *elec_title_label = NULL;   // "Strom"
static lv_obj_t *elec_vi_label    = NULL;   // "12.4V  8.3A"
static lv_obj_t *elec_power_label = NULL;   // "102W"
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
static bool s_indoor_simple_view = false; // true = nur Temp+Feuchte Innen
static lv_obj_t *imu_pitch_label = NULL;
static lv_obj_t *imu_roll_label = NULL;
static lv_obj_t *imu_heading_label = NULL;
static lv_obj_t *gps_button = NULL;  // Runder Icon-Button für GPS-Details
static lv_obj_t *gps_label = NULL;    // GPS position (Alias auf gps_button)
static lv_obj_t *gps_icon_label = NULL;  // Material-Font-Icon für GPS
static char last_gps_text[256] = ""; // Zuletzt berechneter GPS-Text (Detailansicht)
static bool gps_details_visible = false;
static lv_timer_t *gps_hide_timer = NULL;
static lv_obj_t *gps_popup_panel = NULL;      // Separates Detail-Panel neben GPS-Button
static lv_obj_t *gps_popup_text_label = NULL; // Text-Label im GPS-Popup-Panel
static lv_timer_t *backlight_quiet_timer = NULL;
static bool quiet_hours_active = false;
static lv_obj_t *bg_img = NULL;       // Background image widget
static int bg_last_day_state = -1;   // -1 unknown, 0 night, 1 day (aktuell angezeigt)
static lv_obj_t *logo_img = NULL;    // Fahrzeug-Logo über Hintergrundbild
static uint8_t *logo_png_data = NULL; // Geladener Logo-PNG-Puffer
static TaskHandle_t sd_init_task_handle = NULL;
static TaskHandle_t rs485_init_task_handle = NULL;
static lv_obj_t *rs485_debug_label = NULL; // RS485 debug status
static lv_obj_t *imu_zero_modal = NULL;     // IMU calibration modal
static womo_weather_t *weather_widget = NULL; // Weather widget
static womo_battery_t *main_battery = NULL;   // Battery 1 widget
static womo_battery_t *secondary_battery = NULL; // Battery 2 widget
/* Meteoalarm ─────────────────────────────────────────────────────── */
static bool  meteoalarm_started = false;        // Task einmalig gestartet
static bool  weather_started   = false;         // Task einmalig gestartet
static lv_obj_t *meteoalarm_popup = NULL;       // aktuelles Warnungs-Popup (NULL = geschlossen)
static lv_timer_t *ui_update_timer = NULL; // Periodic UI/IMU refresh timer
static int64_t last_time_sync_try_us = 0; // Throttle multi-source time sync attempts
static const uint32_t UI_UPDATE_INTERVAL_DEFAULT_MS = 500;
static bool geocode_in_progress = false;
static bool geocode_last_failed  = false; // steuert Retry- vs. Normal-Intervall
static bool perf_monitor_visible = true;    // Performance monitor visibility
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
static bool                       s_router_reachable = false;
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
static lv_obj_t *tank_info_label = NULL;  // Tank-Verbrauch (L/h) + Restlaufzeit

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

// --- Sensor-Fehler-Stapel: pro Quelle aktueller, gelatchter und quittierter Level ---
//
// Fehler-Management:
//   1. "current" wird jeden Tick neu evaluiert (Sensor-Schwellwert-Vergleich).
//   2. "latched" steigt mit current (sticky), fällt erst nach Quittierung +
//      stabiler Erholung (ok_streak >= SENSOR_ERR_OK_DEBOUNCE).
//   3. "acked" wird per Touch-Quittierung auf latched gesetzt.
//   4. Unquittiert = latched > acked → System-Alarm, Buzzer.
//   5. Widget-Farbe folgt "current" (Echtzeit), nicht latched/acked.
//
#define SENSOR_ERR_OK_DEBOUNCE  6   // 6 Ticks × 500 ms = 3 s stabile Erholung nötig

typedef enum {
    SENSOR_ERR_SRC_GAS = 0,
    SENSOR_ERR_SRC_FRESH,
    SENSOR_ERR_SRC_GREY,
    SENSOR_ERR_SRC_BAT,
    SENSOR_ERR_SRC_RS485,  // RS485 Timeout / Verbindungsverlust
    SENSOR_ERR_SRC_IAQ,    // Luftqualität (BME680 indoor IAQ)
    SENSOR_ERR_SRC_MAX
} sensor_err_src_t;

static const char * const sensor_err_src_name[SENSOR_ERR_SRC_MAX] = {
    "Gas", "Wasser", "Abw.", "Bat", "RS485", "IAQ"
};

typedef struct {
    womo_status_level_t current;   // Aktuell evaluierter Level (jeden Tick neu berechnet)
    womo_status_level_t latched;   // Gelatchter Level (steigt mit current, fällt erst nach Quittierung + Erholung)
    womo_status_level_t acked;     // Per Touch quittierter Level
    uint8_t ok_streak;             // Aufeinanderfolgende Ticks mit current == OK
} sensor_err_state_t;

static sensor_err_state_t sensor_err_states[SENSOR_ERR_SRC_MAX] = {0};
static int sensor_err_display_idx = -1;
static char sensor_detail_text[32] = "";
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
static void forecast_update_cb(const womo_weather_forecast_t *forecast, void *user_data);
static void weather_widget_click_cb(lv_event_t *e);
static void meteoalarm_update_cb(const womo_meteoalarm_result_t *result, void *user_data);
static womo_weather_condition_t map_openweather_condition(int weather_id, bool is_night);
static womo_weather_condition_t map_day_condition_to_night(womo_weather_condition_t condition);
static void update_connectivity_label(void);
static void gps_format_coordinate(double value, bool is_latitude, char *out, size_t len);
static void ui_update_timer_cb(lv_timer_t *timer);
static void wifi_label_event_cb(lv_event_t *event);
static void status_label_event_cb(lv_event_t *event);
static void status_button_update_layout(void);
static void backlight_button_event_cb(lv_event_t *event);
static void settings_button_event_cb(lv_event_t *event);
void router_leds_button_event_cb(lv_event_t *event);
static void classic_button_event_cb(lv_event_t *event);
static void radio_button_event_cb(lv_event_t *event);
static void geocode_result_cb(const womo_geocode_result_t *result, void *user_data);
static void geocode_trigger_if_needed(const womo_sensor_data_t *snapshot);
static void backlight_update_label(void);
static lv_obj_t *create_round_button(lv_obj_t *parent, int size, lv_color_t bg_color,
                                     lv_event_cb_t cb, lv_event_code_t code);
static lv_obj_t *round_button_add_icon(lv_obj_t *btn, const char *icon_utf8);
static void update_round_button_state(lv_obj_t *btn, lv_obj_t *icon_label, bool active,
                                      lv_color_t active_color);
static void update_classic_icon(lv_color_t color, bool active);
static void update_radio_icon(lv_color_t color, bool active);
static void update_shore_icon(lv_color_t color, bool active);
static void shore_power_update_label(void);
static void log_runtime_heap_stats(void);
static void backlight_start_quiet_timer(void);
static void backlight_stop_quiet_timer(void);
static void wifi_autoretry_task(void *arg);
static void router_poll_task(void *arg);
#if WOMO_ENABLE_SCREENSHOT
static void screenshot_task(void *arg);
static void take_screenshot(void);
static void screenshot_indev_cb(lv_event_t *e);
#endif /* WOMO_ENABLE_SCREENSHOT */
static bool is_quiet_hours(const struct tm *timeinfo);
static void backlight_set(bool on);
static bool theme_mode_is_daylike(womo_theme_mode_t mode);
static void full_theme_refresh(void);
static void load_logo_image(lv_obj_t *screen);
static void sd_init_task(void *arg);
static void rs485_init_task(void *arg);
static void connectivity_snapshot_fill(womo_connectivity_snapshot_t *snapshot);
static void rs485_event_handler(womo_rs485_event_t event, void *user_data);
static void gas_replace_show_modal(uint8_t slot);
static void gas_replace_close_modal(void);
static void gas_replace_msgbox_event_cb(lv_event_t *event);
static void gas_bottle_clicked_cb(lv_event_t *event);
static void gas_replace_send_timer_cb(lv_timer_t *timer);
static void imu_zero_show_modal(void);
static void imu_zero_close_modal(void);
static void imu_zero_msgbox_event_cb(lv_event_t *event);
static void perf_monitor_toggle_event_cb(lv_event_t *e);
static void imu_zero_area_cb(lv_event_t *event);
static void indoor_air_long_press_cb(lv_event_t *event);
static void on_locale_changed(void);
static void on_thresholds_changed(void);

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
    /* theme_mode_is_daylike(): DAY + SUNRISE → true (helles Hintergrundbild, dunkle Texte)
     *                          NIGHT + SUNSET → false (dunkles Hintergrundbild, helle Texte)
     * SUNRISE = Morgen, heller Himmel → schwarzer Text gut lesbar.
     * SUNSET  = Abend, dunkler Hintergrund → weißer Text nötig. */
    womo_theme_mode_t mode = womo_theme_get_mode();
    lv_color_t text_color = theme_mode_is_daylike(mode) ? lv_color_black() : lv_color_white();
    lv_color_t classic_color = lv_color_hex(0x2E7D32);
    lv_color_t radio_color = lv_color_hex(0x1565C0);
    lv_color_t shore_color = lv_color_hex(0xF9A825);

    if (title_label) lv_obj_set_style_text_color(title_label, text_color, 0);
    if (time_label) lv_obj_set_style_text_color(time_label, text_color, 0);
    if (date_label) lv_obj_set_style_text_color(date_label, text_color, 0);
    if (status_label) lv_obj_set_style_text_color(status_label, lv_color_black(), 0);  // immer schwarz (hellgrauer Hintergrund)
    if (wifi_label) lv_obj_set_style_text_color(wifi_label, text_color, 0);
    if (wifi_label) lv_obj_set_style_border_color(wifi_label, text_color, 0);
    if (air_title_label) lv_obj_set_style_text_color(air_title_label, text_color, 0);
    if (press_label) lv_obj_set_style_text_color(press_label, text_color, 0);
    if (press_icon_label) lv_obj_set_style_text_color(press_icon_label, text_color, 0);
    if (humid_label) lv_obj_set_style_text_color(humid_label, text_color, 0);
    if (temp_label) lv_obj_set_style_text_color(temp_label, text_color, 0);
    // Innenraum-Werte: keine Tag/Nacht-Färbung, IAQ/CO2/bVOC werden dynamisch gefärbt

    // GPS/Weather Popup-Panels Theme anpassen
    if (gps_popup_panel && gps_popup_text_label) {
        lv_obj_set_style_bg_color(gps_popup_panel,
                                  theme_mode_is_daylike(mode) ? lv_color_hex(0xE0E0E0) : lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_color(gps_popup_panel, text_color, 0);
        lv_obj_set_style_text_color(gps_popup_text_label, text_color, 0);
    }
    if (air_title_label_in) lv_obj_set_style_text_color(air_title_label_in, text_color, 0);
    if (humid_label_in) lv_obj_set_style_text_color(humid_label_in, text_color, 0);
    if (temp_label_in) lv_obj_set_style_text_color(temp_label_in, text_color, 0);
    // IAQ, CO2, bVOC bleiben immer schwarz (dynamisch eingefärbt je Wert)
    if (rs485_debug_label) lv_obj_set_style_text_color(rs485_debug_label, text_color, 0);
    if (imu_pitch_label) lv_obj_set_style_text_color(imu_pitch_label, lv_color_black(), 0);
    if (imu_roll_label) lv_obj_set_style_text_color(imu_roll_label, lv_color_black(), 0);
    if (imu_heading_label) lv_obj_set_style_text_color(imu_heading_label, lv_color_black(), 0);
    // gps_button ist jetzt ein Icon-Button (kein Label-Text, kein Rahmen)
    if (location_label) lv_obj_set_style_text_color(location_label, text_color, 0);
    if (gps_popup_panel && gps_popup_text_label) {
        bool day = theme_mode_is_daylike(mode);
        lv_obj_set_style_bg_color(gps_popup_panel,
                                  day ? lv_color_hex(0xE0E0E0) : lv_color_hex(0x303030), 0);
        lv_obj_set_style_border_color(gps_popup_panel, text_color, 0);
        lv_obj_set_style_text_color(gps_popup_text_label, text_color, 0);
    }
    lv_color_t tank_label_color = lv_color_black();
    if (fresh_water_tank) womo_tank_set_text_color(fresh_water_tank, tank_label_color);
    if (grey_water_tank) womo_tank_set_text_color(grey_water_tank, tank_label_color);
    if (fresh_water_caption_label) lv_obj_set_style_text_color(fresh_water_caption_label, lv_color_black(), 0);
    if (grey_water_caption_label) lv_obj_set_style_text_color(grey_water_caption_label, lv_color_black(), 0);
    if (gas_label_front) lv_obj_set_style_text_color(gas_label_front, lv_color_black(), 0);
    if (gas_label_rear) lv_obj_set_style_text_color(gas_label_rear, lv_color_black(), 0);
    if (elec_title_label) lv_obj_set_style_text_color(elec_title_label, text_color, 0);
    if (elec_vi_label)    lv_obj_set_style_text_color(elec_vi_label,    text_color, 0);
    if (elec_power_label) lv_obj_set_style_text_color(elec_power_label, text_color, 0);
    if (classic_btn) {
        update_classic_icon(classic_color, classic_on);
    }
    if (radio_btn) {
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

    if (!result || !result->valid || !location_label) {
        /* Bei Fehler: kurzes Retry-Intervall, Koordinaten zurücksetzen */
        ESP_LOGW(TAG, "Geocode fehlgeschlagen (result=%s valid=%s label=%s)",
                 result ? "ok" : "NULL",
                 (result && result->valid) ? "yes" : "no",
                 location_label ? "ok" : "NULL");
        geocode_last_failed = true;
        geocode_last_lat = NAN;
        geocode_last_lon = NAN;
        return;
    }
    ESP_LOGI(TAG, "Geocode OK: '%s'", result->short_name);
    /* Erfolg: normales Intervall */
    geocode_last_failed = false;

    if (lvgl_port_lock(0)) {
        const char *text = result->short_name[0] ? result->short_name : result->display_name;
        if (text && text[0] != '\0') {
            if (strcmp(text, location_last_text) != 0) {
                lv_label_set_text(location_label, text);
                strlcpy(location_last_text, text, sizeof(location_last_text));
            }
            /* Ortsnamen sofort im offenen Forecast-Modal aktualisieren */
            {
                const womo_sun_times_t *st = womo_theme_get_sun_times();
                womo_forecast_modal_set_location(
                    location_last_text[0] ? location_last_text : NULL,
                    st->sunrise_hour, st->sunrise_minute,
                    st->sunset_hour,  st->sunset_minute);
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
        ESP_LOGD(TAG, "Geocode: kein WiFi");
        return; // benötigt Internet
    }

    /* TLS-Zertifikate werden gegen die Systemzeit geprüft → erst nach NTP-Sync */
    if (!womo_time_is_synced()) {
        ESP_LOGD(TAG, "Geocode: NTP noch nicht sync");
        return;
    }

    if (geocode_in_progress) {
        return;
    }

    const bool gps_ok = snapshot->gps.valid;
    if (!gps_ok) {
        ESP_LOGD(TAG, "Geocode: GPS nicht valid");
        return;
    }

    const bool coords_finite = isfinite(snapshot->gps.latitude) && isfinite(snapshot->gps.longitude);
    const bool coords_nonzero = fabs(snapshot->gps.latitude) + fabs(snapshot->gps.longitude) > 0.0001;
    if (!coords_finite || !coords_nonzero) {
        ESP_LOGD(TAG, "Geocode: Koordinaten ungültig (finite=%d nonzero=%d)", coords_finite, coords_nonzero);
        return;
    }

    int64_t now_us = esp_timer_get_time();
    int64_t min_interval = geocode_last_failed ? GEOCODE_RETRY_INTERVAL_US : GEOCODE_MIN_INTERVAL_US;
    if (geocode_last_request_us > 0 && (now_us - geocode_last_request_us) < min_interval) {
        return;
    }

    if (isfinite(geocode_last_lat) && isfinite(geocode_last_lon)) {
        double dlat = fabs(snapshot->gps.latitude - geocode_last_lat);
        double dlon = fabs(snapshot->gps.longitude - geocode_last_lon);
        if (dlat < GEOCODE_MIN_DELTA_DEG && dlon < GEOCODE_MIN_DELTA_DEG) {
            return; // keine relevante Bewegung
        }
    }

    ESP_LOGI(TAG, "Geocode: starte Request (lat=%.5f lon=%.5f)", snapshot->gps.latitude, snapshot->gps.longitude);

    geocode_in_progress = true;
    /* Zeitstempel VOR dem Request setzen – verhindert parallele Requests
     * auch wenn der Callback sehr schnell zurückkommt. Bei Fehler wird
     * der Zeitstempel im Callback auf Retry-Intervall angepasst. */
    geocode_last_request_us = esp_timer_get_time();
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
        geocode_last_failed = true;
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

// Hilfsfunktion: signal_percent (0-100) → Icon-Index 0..4
// WiFi: wifi_1..wifi_5 → Index 0..4
// WiFi: wifi_1..wifi_6 → Index 0..5
static int signal_to_wifi(unsigned int pct, bool connected) {
    if (!connected || pct == 0) return 0;  // wifi_1 = kein Signal
    if (pct < 20) return 1;                // wifi_2
    if (pct < 40) return 2;                // wifi_3
    if (pct < 60) return 3;                // wifi_4
    if (pct < 80) return 4;                // wifi_5
    return 5;                              // wifi_6
}

// LTE: signal_1..signal_6 → Index 0..5
static int signal_to_lte(unsigned int pct, bool connected) {
    if (!connected || pct == 0) return 0;  // signal_1 = kein Signal
    if (pct < 20) return 1;                // signal_2
    if (pct < 40) return 2;                // signal_3
    if (pct < 60) return 3;                // signal_4
    if (pct < 80) return 4;                // signal_5
    return 5;                              // signal_6
}

// Hilfsfunktion: setzt WiFi/LTE Icon-Widgets auf den richtigen Deskriptor
static void update_connectivity_icons(unsigned int wifi_pct, bool wifi_connected,
                                       unsigned int lte_pct, bool lte_connected)
{
    if (wifi_icon_img) {
        int idx = signal_to_wifi(wifi_pct, wifi_connected);
        if (wifi_icon_dscs[idx].data)
            lv_img_set_src(wifi_icon_img, &wifi_icon_dscs[idx]);
    }
    if (lte_icon_img) {
        int idx = signal_to_lte(lte_pct, lte_connected);
        if (lte_icon_dscs[idx].data)
            lv_img_set_src(lte_icon_img, &lte_icon_dscs[idx]);
    }
}

static void update_router_btn_overlay(bool router_reachable, bool esp_wifi_connected)
{
    (void)router_reachable;
    if (!wifi_ap_section) return;
    lv_obj_set_style_bg_color(wifi_ap_section,
        esp_wifi_connected ? lv_palette_main(LV_PALETTE_GREEN)
                           : lv_color_hex(0xC0C0C0), 0);
}

static void update_connectivity_label(void)
{
    if (!wifi_label) {
        return;
    }

    womo_router_wifi_status_t rw = {0};
    womo_router_lte_status_t  rl = {0};
    if (s_router_mutex) {
        xSemaphoreTake(s_router_mutex, portMAX_DELAY);
        rw = s_router_wifi;
        rl = s_router_lte;
        xSemaphoreGive(s_router_mutex);
    }

    bool wifi_ok = womo_wifi_is_connected() && rw.connected;
    bool lte_ok  = rl.registered;
    update_connectivity_icons(rw.signal_percent, wifi_ok,
                              rl.signal_percent,  lte_ok);
}

static void connectivity_snapshot_fill(womo_connectivity_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));

    /* ESP32 eigenes WiFi (für Hotspot-Spalte) */
    snapshot->esp_wifi_connected = womo_wifi_is_connected();
    if (snapshot->esp_wifi_connected) {
        womo_wifi_get_ssid(snapshot->esp_wifi_ssid, sizeof(snapshot->esp_wifi_ssid));
        snapshot->esp_wifi_rssi = womo_wifi_get_rssi();
        /* RSSI in Prozent umrechnen: -100 dBm = 0%, -50 dBm = 100% */
        int8_t rssi = snapshot->esp_wifi_rssi;
        if (rssi <= -100) {
            snapshot->esp_wifi_signal_percent = 0;
        } else if (rssi >= -50) {
            snapshot->esp_wifi_signal_percent = 100;
        } else {
            snapshot->esp_wifi_signal_percent = (uint8_t)(2 * (rssi + 100));
        }
    }

    /* Router-Daten unter Mutex kopieren */
    womo_router_wifi_status_t rw = {0};
    womo_router_lte_status_t  rl = {0};
    snapshot->router_reachable = s_router_reachable;
    if (s_router_mutex) {
        xSemaphoreTake(s_router_mutex, portMAX_DELAY);
        rw = s_router_wifi;
        rl = s_router_lte;
        xSemaphoreGive(s_router_mutex);
    }

    /* WiFi = Router WAN Client */
    snapshot->wifi_enabled = rw.enabled;
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
    status_button_update_layout();
}

static void status_button_update_layout(void)
{
    if (!status_btn || !status_label) {
        return;
    }

    lv_obj_update_layout(status_label);
    lv_coord_t label_w = lv_obj_get_width(status_label);
    lv_coord_t label_h = lv_obj_get_height(status_label);

    lv_coord_t btn_w = label_w + 24;
    lv_coord_t btn_h = label_h + 18;
    if (btn_w < 90) {
        btn_w = 90;
    }
    if (btn_h < 34) {
        btn_h = 34;
    }

    lv_obj_set_size(status_btn, btn_w, btn_h);
    lv_obj_center(status_label);

    if (status_inlay) {
        lv_coord_t inlay_w = btn_w - 6;
        lv_coord_t inlay_h = btn_h - 6;
        if (inlay_w < 1) {
            inlay_w = 1;
        }
        if (inlay_h < 1) {
            inlay_h = 1;
        }
        lv_obj_set_size(status_inlay, inlay_w, inlay_h);
        lv_obj_center(status_inlay);
    }
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
    womo_status_level_t prev_level = system_status_current_level;  /* vor Überschreiben merken */

    system_status_current_level = resolved;
    system_status_current_detail = detail_id;

    womo_theme_set_status(resolved);
    if (level_changed) {
        /* WARNING ändert Hintergrund nicht (siehe womo_theme_get_background_color).
         * full_theme_refresh (inkl. Hintergrundbild-Reload) nur bei Übergängen, die tatsächlich
         * die Hintergrundfarbe betreffen: alles was ERROR/CRITICAL ein- oder ausschaltet.
         * Bei OK↔WARNING reicht apply_text_theme_colors(). */
        bool was_critical = (prev_level  == WOMO_STATUS_ERROR || prev_level  == WOMO_STATUS_CRITICAL);
        bool is_critical  = (resolved    == WOMO_STATUS_ERROR || resolved    == WOMO_STATUS_CRITICAL);
        if (was_critical || is_critical) {
            full_theme_refresh();
        } else {
            apply_text_theme_colors();
        }
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

static void rs485_event_handler(womo_rs485_event_t event, void *user_data)
{
    (void)user_data;

    switch (event) {
        case WOMO_RS485_EVENT_HELLO:
            ESP_LOGI(TAG, "RS485 hello empfangen - Sensorboard ist bereit");
            rs485_waiting_for_handshake = false;
            /* Immer mit display_ready antworten – auch nach Sensor-Neustart */
            womo_rs485_send_display_ready();
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


LV_IMAGE_DECLARE(ducato);

// Load embedded background image
// `is_day` steuert nur die Recolor-Farbe des eingebetteten A8-Ducato-Assets.
// load_background_image():
//   Kein SD-Lesen, kein PNG-Decode, kein zweiter Framebuffer.
//   Nur die LVGL-Widget-Manipulation wird mit einem kurzen Lock geschützt.
static bool load_background_image(lv_obj_t *screen, bool is_day)
{
    lv_color_t recolor = lv_color_hex(is_day ? WOMO_BG_IMAGE_DAY_COLOR
                                             : WOMO_BG_IMAGE_NIGHT_COLOR);

    // Passendes Bild bereits angezeigt – nichts tun
    if (bg_last_day_state == (is_day ? 1 : 0) && bg_img != NULL) {
        ESP_LOGD(TAG, "Background already shown (%s)", is_day ? "day" : "night");
        return true;
    }

    // screen == NULL → historischer Preload-Aufruf, jetzt nur noch No-op
    if (!screen && !bg_img) {
        ESP_LOGD(TAG, "Background preload skipped (embedded asset)");
        return true;
    }

    // ── LVGL-Widget-Manipulation (kurzer Lock) ───────────────────────
    if (!lvgl_port_lock(500)) {
        ESP_LOGW(TAG, "LVGL lock timeout – background not applied");
        return false;
    }
    if (!bg_img) {
        bg_img = lv_img_create(screen);
        lv_obj_move_background(bg_img);
        lv_obj_set_style_pad_all(bg_img, 0, 0);
        lv_obj_set_size(bg_img, 800, 480);
        lv_obj_align(bg_img, LV_ALIGN_CENTER, 0, 0);
    }
    lv_img_set_src(bg_img, &ducato);
    bg_last_day_state = is_day ? 1 : 0;
    lv_obj_set_style_img_opa(bg_img, LV_OPA_COVER, 0);
    lv_obj_set_style_img_recolor(bg_img, recolor, 0);
    lv_obj_set_style_img_recolor_opa(bg_img, LV_OPA_COVER, 0);
    lv_obj_move_to_index(bg_img, 0);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Embedded background applied (%s, recolor=%06" PRIx32 ")",
             is_day ? "day" : "night",
             (uint32_t)lv_color_to_u32(recolor));
    return true;
}

// Fahrzeug-Logo von SD-Karte laden und über das Hintergrundbild legen.
// Datei: WOMO_LOGO_IMAGE (siehe display_config.h), transparenter Hintergrund empfohlen.
// Position und Skalierung per Defines anpassen:
#define LOGO_X         565   // X-Position linke obere Ecke (Pixel vom linken Rand)
#define LOGO_Y         275   // Y-Position linke obere Ecke (Pixel vom oberen Rand)
#define LOGO_SCALE_PCT  50   // Skalierung in Prozent (100 = Originalgröße, 50 = halb)
static void load_logo_image(lv_obj_t *screen)
{
    if (!womo_sd_is_mounted()) {
        return;
    }
    const char *path = WOMO_LOGO_IMAGE;
    struct stat st;
    womo_ch422g_assert_sd_cs();
    if (stat(path, &st) != 0) {
        ESP_LOGW(TAG, "Logo nicht gefunden: %s", path);
        return;
    }
    if (st.st_size <= 0 || st.st_size > 1024 * 1024) {
        ESP_LOGW(TAG, "Logo: unplausible Dateigröße %ld", (long)st.st_size);
        return;
    }
    womo_ch422g_assert_sd_cs();
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Logo: fopen fehlgeschlagen");
        return;
    }
    uint8_t *buf = heap_caps_malloc(st.st_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGE(TAG, "Logo: kein Speicher (%ld bytes)", (long)st.st_size);
        fclose(fp);
        return;
    }
    if (fread(buf, 1, st.st_size, fp) != (size_t)st.st_size) {
        ESP_LOGE(TAG, "Logo: Lesefehler");
        fclose(fp);
        heap_caps_free(buf);
        return;
    }
    fclose(fp);

    // Alten Puffer freigeben
    if (logo_png_data) {
        heap_caps_free(logo_png_data);
        logo_png_data = NULL;
    }
    logo_png_data = buf;

    static lv_image_dsc_t logo_dsc;
    logo_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    logo_dsc.header.w     = 0;
    logo_dsc.header.h     = 0;
    logo_dsc.header.cf    = LV_COLOR_FORMAT_RAW_ALPHA;
    logo_dsc.data         = logo_png_data;
    logo_dsc.data_size    = (uint32_t)st.st_size;

    // ── LVGL-Widget-Manipulation (kurzer Lock) ───────────────────────
    if (!lvgl_port_lock(500)) {
        ESP_LOGW(TAG, "Logo: LVGL lock timeout");
        return;
    }
    if (!logo_img) {
        logo_img = lv_img_create(screen);
        lv_obj_clear_flag(logo_img, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
    lv_img_set_src(logo_img, &logo_dsc);
    lv_obj_set_style_img_opa(logo_img, LV_OPA_COVER, 0);

    // Skalierung: LOGO_SCALE_PCT % → LVGL-Zoom-Wert (256 = 100%)
    uint16_t zoom = (uint16_t)((LOGO_SCALE_PCT * 256) / 100);
    lv_img_set_zoom(logo_img, zoom);
    ESP_LOGI(TAG, "Logo: zoom=%u (%d%%)", zoom, LOGO_SCALE_PCT);

    lv_obj_align(logo_img, LV_ALIGN_TOP_LEFT, LOGO_X, LOGO_Y);
    // Logo über Hintergrundbild, aber unter allen Widgets
    lv_obj_move_to_index(logo_img, 1);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "Logo geladen und positioniert (x=%d, y=%d)", LOGO_X, LOGO_Y);
}

// GPS-Detailanzeige nach Timeout wieder einklappen
static void gps_hide_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    gps_details_visible = false;
    // GPS-Button bleibt "GPS" – Popup-Panel ausblenden
    if (gps_popup_panel) {
        lv_obj_add_flag(gps_popup_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (location_label) {
        lv_obj_clear_flag(location_label, LV_OBJ_FLAG_HIDDEN);
    }
}

// GPS-Label-Klick: Separates Detail-Panel neben GPS-Button einblenden
static void gps_label_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    gps_details_visible = true;
    const char *text = (last_gps_text[0] != '\0') ? last_gps_text : PLACEHOLDER_GPS;

    // Detail-Panel befüllen und anzeigen, GPS-Button bleibt unverändert als "GPS"
    if (gps_popup_panel && gps_popup_text_label) {
        lv_label_set_text(gps_popup_text_label, text);
        lv_obj_clear_flag(gps_popup_panel, LV_OBJ_FLAG_HIDDEN);
    }

    // Ortsname ausblenden, solange Popup-Panel sichtbar ist
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
        lv_indev_get_point(lv_indev_active(), &point);

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
    }
}

static void ui_update_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    womo_sensor_data_t snapshot = {0};
    womo_lte_status_t lte_snapshot = {0};
    womo_thresholds_t thr;
    womo_thresholds_get(&thr);
    uint32_t packet_count = 0;
    bool data_valid = false;
    bool rs485_timeout_snapshot = false;
    bool rs485_waiting_snapshot = false;
    bool rs485_invalid_snapshot = false;
    // Per-Sensor-Quellen-Level für Stapel zurücksetzen (globale States)
    for (int _si = 0; _si < SENSOR_ERR_SRC_MAX; _si++) {
        sensor_err_states[_si].current = WOMO_STATUS_OK;
    }
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

    // RS485-Timeout → Sensor-Stapel (WARNING, damit veraltete Daten sichtbar werden)
    sensor_err_states[SENSOR_ERR_SRC_RS485].current =
        rs485_timeout_snapshot ? WOMO_STATUS_WARNING : WOMO_STATUS_OK;

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
            strncpy(last_connectivity_text, combined, sizeof(last_connectivity_text) - 1);
            last_connectivity_text[sizeof(last_connectivity_text) - 1] = '\0';
        }
        bool wifi_ok = wifi_connected_now && rw.connected;
        bool lte_ok  = rl.registered;
        update_connectivity_icons(rw.signal_percent, wifi_ok,
                                  rl.signal_percent,  lte_ok);
        update_router_btn_overlay(s_router_reachable, womo_wifi_is_connected());

        if (modal_open) {
            womo_connectivity_snapshot_t modal_snapshot;
            connectivity_snapshot_fill(&modal_snapshot);
            static womo_connectivity_snapshot_t last_modal_snapshot = {0};
            if (memcmp(&modal_snapshot, &last_modal_snapshot, sizeof(modal_snapshot)) != 0) {
                womo_connectivity_modal_refresh(&modal_snapshot);
                last_modal_snapshot = modal_snapshot;
            }
        }

        /* Router-LEDs-Modal aktualisieren, wenn offen */
        if (womo_router_leds_modal_is_open()) {
            womo_router_leds_snapshot_t leds_snapshot = {0};
            leds_snapshot.router_online = wifi_connected_now;
            leds_snapshot.router_ap_24ghz = s_router_ap.band_2_4ghz_active;
            leds_snapshot.router_ap_5ghz = s_router_ap.band_5ghz_active;
            leds_snapshot.wifi_connected = rw.connected;
            strncpy(leds_snapshot.wifi_ssid, rw.ssid, sizeof(leds_snapshot.wifi_ssid) - 1);
            leds_snapshot.wifi_signal_percent = rw.signal_percent;
            leds_snapshot.lte_registered = rl.registered;
            strncpy(leds_snapshot.lte_operator, rl.operator_name, sizeof(leds_snapshot.lte_operator) - 1);
            leds_snapshot.lte_signal_percent = rl.signal_percent;
            strncpy(leds_snapshot.lte_conn_type, rl.conn_type, sizeof(leds_snapshot.lte_conn_type) - 1);
            strncpy(leds_snapshot.sim_state, rl.sim_state, sizeof(leds_snapshot.sim_state) - 1);
            
            static womo_router_leds_snapshot_t last_leds_snapshot = {0};
            if (memcmp(&leds_snapshot, &last_leds_snapshot, sizeof(leds_snapshot)) != 0) {
                womo_router_leds_modal_refresh(&leds_snapshot);
                last_leds_snapshot = leds_snapshot;
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
                // Entprellung: Rot erst nach 3 aufeinanderfolgenden Messungen > 200
                // (~45 s bei 15-s-BME-Intervall). Kurze Kochduft-Spitzen loesen kein Rot aus.
                static uint8_t s_iaq_alarm_count = 0;
                if (iaq > 200) {
                    if (s_iaq_alarm_count < 3) s_iaq_alarm_count++;
                } else {
                    s_iaq_alarm_count = 0;
                }
                lv_color_t c;
                if (iaq <= 100) {
                    c = lv_color_make(0, 180, 0);
                } else if (iaq <= 200 || s_iaq_alarm_count < 3) {
                    c = lv_color_make(255, 165, 0);  // orange: erhoehte Belastung
                } else {
                    c = lv_color_make(192, 0, 0);     // rot: anhaltend schlechte Luft
                }
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

    // IAQ Luftqualität → Sensor-Stapel (unabhängig von Label-Existenz, nur bei kalibriertem Sensor)
    if (snapshot.bme680_indoor.valid && snapshot.bme680_indoor.iaq_accuracy >= 2) {
        uint16_t iaq = snapshot.bme680_indoor.iaq;
        womo_status_level_t iaq_status = WOMO_STATUS_OK;
        if (iaq >= THRESH_IAQ_CRIT_DEFAULT) {
            iaq_status = WOMO_STATUS_ERROR;
        } else if (iaq >= THRESH_IAQ_WARN_DEFAULT) {
            iaq_status = WOMO_STATUS_WARNING;
        }
        if (iaq_status > sensor_err_states[SENSOR_ERR_SRC_IAQ].current) {
            sensor_err_states[SENSOR_ERR_SRC_IAQ].current = iaq_status;
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

        bool nc_a = snapshot.gas.valid && snapshot.gas.nc_a;
        bool nc_b = snapshot.gas.valid && snapshot.gas.nc_b;
        // Fallback auf hx711.nc wenn gas-Topic noch nicht empfangen
        if (!snapshot.gas.valid) {
            bool hx_nc = snapshot.hx711.valid && snapshot.hx711.nc;
            nc_a = hx_nc;
            nc_b = hx_nc;
        }
        bool hx_available = snapshot.hx711.valid && !snapshot.hx711.nc;
        bool pct_a_valid = snapshot.gas.valid && !nc_a && isfinite(snapshot.gas.pct_a);
        bool pct_b_valid = snapshot.gas.valid && !nc_b && isfinite(snapshot.gas.pct_b);

        gas_nc_active = nc_a && nc_b;

        // Pro-Kanal nc setzen
        if (gas_bottle_a && nc_a) {
            womo_gas_bottle_set_nc(gas_bottle_a);
            womo_gas_bottle_set_status(gas_bottle_a, WOMO_STATUS_OK);
            gas_has_data_a = false;
            last_level_a = NAN;
        }
        if (gas_bottle_b && nc_b) {
            womo_gas_bottle_set_nc(gas_bottle_b);
            womo_gas_bottle_set_status(gas_bottle_b, WOMO_STATUS_OK);
            gas_has_data_b = false;
            last_level_b = NAN;
        }
        if (gas_nc_active) {
            if (gas_info_label) {
                lv_label_set_text(gas_info_label, "Gas (nc):\n--.-- kg\n---- g/h\n--.- h");
            }
            last_active_idx = -2;
            last_net_kg = NAN;
            last_rate = NAN;
            last_rest = NAN;
            goto gas_done;
        }

        if (gas_bottle_a && !nc_a) {
            if (pct_a_valid) {
                float pct = snapshot.gas.pct_a;
                if (!gas_has_data_a || isnan(last_level_a) || fabsf(pct - last_level_a) > 0.5f) {
                    womo_gas_bottle_set_percent(gas_bottle_a, pct);
                    uint8_t fill = womo_gas_bottle_get_fill_percent(gas_bottle_a);
                    womo_status_level_t status = evaluate_low_is_bad(fill, thr.gas_warn, thr.gas_crit);
                    womo_gas_bottle_set_status(gas_bottle_a, status);
                    if (status > sensor_err_states[SENSOR_ERR_SRC_GAS].current) {
                        sensor_err_states[SENSOR_ERR_SRC_GAS].current = status;
                    }
                }
                last_level_a = pct;
                gas_has_data_a = true;
            } else if (hx_available) {
                if (!gas_has_data_a || isnan(last_level_a) || fabsf(snapshot.hx711.weight_a_kg - last_level_a) > 0.05f) {
                    womo_gas_bottle_update_weight(gas_bottle_a, snapshot.hx711.weight_a_kg);
                    uint8_t fill = womo_gas_bottle_get_fill_percent(gas_bottle_a);
                    womo_status_level_t status = evaluate_low_is_bad(fill, thr.gas_warn, thr.gas_crit);
                    womo_gas_bottle_set_status(gas_bottle_a, status);
                    if (status > sensor_err_states[SENSOR_ERR_SRC_GAS].current) {
                        sensor_err_states[SENSOR_ERR_SRC_GAS].current = status;
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

        if (gas_bottle_b && !nc_b) {
            if (pct_b_valid) {
                float pct = snapshot.gas.pct_b;
                if (!gas_has_data_b || isnan(last_level_b) || fabsf(pct - last_level_b) > 0.5f) {
                    womo_gas_bottle_set_percent(gas_bottle_b, pct);
                    uint8_t fill = womo_gas_bottle_get_fill_percent(gas_bottle_b);
                    womo_status_level_t status = evaluate_low_is_bad(fill, thr.gas_warn, thr.gas_crit);
                    womo_gas_bottle_set_status(gas_bottle_b, status);
                    if (status > sensor_err_states[SENSOR_ERR_SRC_GAS].current) {
                        sensor_err_states[SENSOR_ERR_SRC_GAS].current = status;
                    }
                }
                last_level_b = pct;
                gas_has_data_b = true;
            } else if (hx_available) {
                if (!gas_has_data_b || isnan(last_level_b) || fabsf(snapshot.hx711.weight_b_kg - last_level_b) > 0.05f) {
                    womo_gas_bottle_update_weight(gas_bottle_b, snapshot.hx711.weight_b_kg);
                    uint8_t fill = womo_gas_bottle_get_fill_percent(gas_bottle_b);
                    womo_status_level_t status = evaluate_low_is_bad(fill, thr.gas_warn, thr.gas_crit);
                    womo_gas_bottle_set_status(gas_bottle_b, status);
                    if (status > sensor_err_states[SENSOR_ERR_SRC_GAS].current) {
                        sensor_err_states[SENSOR_ERR_SRC_GAS].current = status;
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
                float rate_disp = isnan(rate) ? 0.0f : (rate * 1000.0f);  // kg/h → g/h
                float rest_disp = isnan(rest) ? 0.0f : rest;

                snprintf(buf, sizeof(buf),
                         "Gas (%s):\n%.2f kg\n%.0f g/h\n%.1f h",
                         flasche,
                         isnan(net) ? 0.0f : net_disp,
                         rate_disp,
                         isnan(rest) ? 0.0f : rest_disp);
                lv_label_set_text(gas_info_label, buf);
                last_active_idx = snapshot.gas.active_idx;
                last_net_kg = net;
                last_rate = rate;
                last_rest = rest;
            }
        } else if (gas_info_label) {
            lv_label_set_text(gas_info_label, "Gas (--):\n--.-- kg\n---- g/h\n--.- h");
            last_active_idx = -2;
            last_net_kg = NAN;
            last_rate = NAN;
            last_rest = NAN;
        }
    }

gas_done:

    // Tank summary (Frischwasser: Verbrauch L/h, Restlaufzeit)
    if (snapshot.tank.valid && tank_info_label) {
        static float last_tank1_liters = NAN;
        static float last_tank1_rate1h = NAN;
        static float last_tank1_rest_h = NAN;

        float liters = snapshot.tank.tank1_liters;
        float rate = snapshot.tank.tank1_rate1h;  // negativ=Verbrauch
        float rest = snapshot.tank.tank1_rest_h;

        if (!isfinite(liters)) liters = NAN;
        if (!isfinite(rate)) rate = NAN;
        if (!isfinite(rest)) rest = NAN;

        // Refresh nur bei Änderungen (oder initialer Aufruf)
        bool changed = !isfinite(last_tank1_liters) ||  // Erster Aufruf
                       (!isnan(liters) && fabsf(liters - last_tank1_liters) > 0.5f) ||
                       (!isnan(rate) && (!isfinite(last_tank1_rate1h) || fabsf(rate - last_tank1_rate1h) > 0.05f)) ||
                       (!isnan(rest) && (!isfinite(last_tank1_rest_h) || fabsf(rest - last_tank1_rest_h) > 0.5f));

        if (changed) {
            char buf[80];
            snprintf(buf, sizeof(buf),
                     "%s:\n%.1f L\n%.1f L/h\n%.1f h",
                     womo_locale_get_string(STR_TANK_FRESH),
                     isnan(liters) ? 0.0f : liters,
                     isnan(rate) ? 0.0f : rate,
                     isnan(rest) ? 0.0f : rest);
            lv_label_set_text(tank_info_label, buf);
            last_tank1_liters = liters;
            last_tank1_rate1h = rate;
            last_tank1_rest_h = rest;
        }
    } else if (tank_info_label) {
        lv_label_set_text(tank_info_label, womo_locale_get_string(STR_TANK_FRESH_PLACEHOLDER));
    }

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

            if (any_voltage && battery_status > sensor_err_states[SENSOR_ERR_SRC_BAT].current) {
                sensor_err_states[SENSOR_ERR_SRC_BAT].current = battery_status;
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

    // Elec widget (INA226)
    if (elec_vi_label && elec_power_label) {
        static float last_v = NAN, last_i = NAN, last_p = NAN;
        static bool  last_nc = false, elec_has_data = false;

        if (snapshot.elec.valid) {
            bool changed = !elec_has_data
                        || (snapshot.elec.nc != last_nc)
                        || (!snapshot.elec.nc && (
                               fabsf(snapshot.elec.v_bus_v - last_v) > 0.05f ||
                               fabsf(snapshot.elec.i_a     - last_i) > 0.05f ||
                               fabsf(snapshot.elec.p_w     - last_p) > 0.5f));
            if (changed) {
                if (snapshot.elec.nc) {
                    lv_label_set_text(elec_vi_label,    "---");
                    lv_label_set_text(elec_power_label, "---");
                } else {
                    char buf_vi[24], buf_p[12];
                    snprintf(buf_vi, sizeof(buf_vi), "%.1fV  %.1fA",
                             snapshot.elec.v_bus_v, snapshot.elec.i_a);
                    snprintf(buf_p, sizeof(buf_p), "%.0fW", snapshot.elec.p_w);
                    lv_label_set_text(elec_vi_label,    buf_vi);
                    lv_label_set_text(elec_power_label, buf_p);
                }
                last_v = snapshot.elec.v_bus_v;
                last_i = snapshot.elec.i_a;
                last_p = snapshot.elec.p_w;
                last_nc = snapshot.elec.nc;
                elec_has_data = true;
            }
        } else if (elec_has_data) {
            lv_label_set_text(elec_vi_label,    "---");
            lv_label_set_text(elec_power_label, "---");
            last_v = last_i = last_p = NAN;
            elec_has_data = false;
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
                                                                 thr.fresh_warn,
                                                                 thr.fresh_crit);
                womo_tank_set_status(fresh_water_tank, status);
                if (status > sensor_err_states[SENSOR_ERR_SRC_FRESH].current) {
                    sensor_err_states[SENSOR_ERR_SRC_FRESH].current = status;
                }
            }
            if (grey_water_tank && (!tank_has_data || last_tank2 != snapshot.tank.tank2_percent)) {
                womo_tank_set_level(grey_water_tank, snapshot.tank.tank2_percent);
                womo_status_level_t status = evaluate_high_is_bad(snapshot.tank.tank2_percent,
                                                                  thr.grey_warn,
                                                                  thr.grey_crit);
                womo_tank_set_status(grey_water_tank, status);
                if (status > sensor_err_states[SENSOR_ERR_SRC_GREY].current) {
                    sensor_err_states[SENSOR_ERR_SRC_GREY].current = status;
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

        // Sensorboard bestätigt neuen Zustand → Grace-Period sofort beenden
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
            lv_color_t c = lv_color_hex(0x2E7D32);
            update_classic_icon(c, classic_on);
        }
        if (radio_changed) {
            radio_on = snapshot.power.radio_on;
            lv_color_t c = lv_color_hex(0x1565C0);
            update_radio_icon(c, radio_on);
        }
        if (shore_changed) {
            shore_power_present = snapshot.power.ac_present;
            shore_power_update_label();
            if (shore_label) {
                update_shore_icon(lv_color_hex(0xF9A825), shore_power_present);
            }
        }
    }

    // Per-Topic Stale-Prüfung: Topic seit > 60 s nicht empfangen, aber RS485 läuft → WARNING
    {
        const int64_t STALE_TOPIC_US = 60LL * 1000000LL;
        if (!rs485_timeout_snapshot && packet_count > 0) {
            if (snapshot.bat_topic_rx_us > 0 &&
                (now_us - snapshot.bat_topic_rx_us) > STALE_TOPIC_US &&
                sensor_err_states[SENSOR_ERR_SRC_BAT].current < WOMO_STATUS_WARNING) {
                sensor_err_states[SENSOR_ERR_SRC_BAT].current = WOMO_STATUS_WARNING;
            }
            if (snapshot.tank_topic_rx_us > 0 &&
                (now_us - snapshot.tank_topic_rx_us) > STALE_TOPIC_US &&
                sensor_err_states[SENSOR_ERR_SRC_FRESH].current < WOMO_STATUS_WARNING) {
                sensor_err_states[SENSOR_ERR_SRC_FRESH].current = WOMO_STATUS_WARNING;
            }
            if (snapshot.gas_topic_rx_us > 0 &&
                (now_us - snapshot.gas_topic_rx_us) > STALE_TOPIC_US &&
                sensor_err_states[SENSOR_ERR_SRC_GAS].current < WOMO_STATUS_WARNING) {
                sensor_err_states[SENSOR_ERR_SRC_GAS].current = WOMO_STATUS_WARNING;
            }
        }
    }

    // Sensor-Fehler-Stapel: Latching + Debounce
    //
    // current (pro Tick evaluiert) steuert den Latch:
    //   - current > OK  → Latch hochziehen (sticky), ok_streak resetten
    //   - current == OK → ok_streak hochzählen; Latch darf nur fallen wenn
    //                      (a) bereits quittiert (acked >= latched) UND
    //                      (b) genug aufeinanderfolgende OK-Ticks (Entprellung)
    //
    for (int _si = 0; _si < SENSOR_ERR_SRC_MAX; _si++) {
        if (sensor_err_states[_si].current > WOMO_STATUS_OK) {
            // Fehler aktiv → Latch hochziehen, OK-Streak resetten
            sensor_err_states[_si].ok_streak = 0;
            if (sensor_err_states[_si].current > sensor_err_states[_si].latched) {
                sensor_err_states[_si].latched = sensor_err_states[_si].current;
            }
        } else {
            // Sensor meldet OK
            if (sensor_err_states[_si].ok_streak < 255) {
                sensor_err_states[_si].ok_streak++;
            }
            // Latch darf nur fallen wenn quittiert UND stabil erholt
            if (sensor_err_states[_si].acked >= sensor_err_states[_si].latched &&
                sensor_err_states[_si].ok_streak >= SENSOR_ERR_OK_DEBOUNCE) {
                sensor_err_states[_si].latched = WOMO_STATUS_OK;
                sensor_err_states[_si].acked   = WOMO_STATUS_OK;
            }
        }
    }

    // Unquittierte Fehler: latched > acked
    womo_status_level_t stack_max = WOMO_STATUS_OK;
    int stack_show_idx = -1;
    int stack_pending = 0;
    for (int _si = 0; _si < SENSOR_ERR_SRC_MAX; _si++) {
        if (sensor_err_states[_si].latched > sensor_err_states[_si].acked) {
            stack_pending++;
            if (sensor_err_states[_si].latched > stack_max) {
                stack_max = sensor_err_states[_si].latched;
                stack_show_idx = _si;
            }
        }
    }

    if (stack_pending > 0) {
        sensor_err_display_idx = stack_show_idx;
        if (stack_pending > 1) {
            snprintf(sensor_detail_text, sizeof(sensor_detail_text), "%s+%d",
                     sensor_err_src_name[stack_show_idx], stack_pending - 1);
        } else {
            snprintf(sensor_detail_text, sizeof(sensor_detail_text), "%s",
                     sensor_err_src_name[stack_show_idx]);
        }
        system_status_apply_sensor_level(stack_max);
    } else {
        sensor_err_display_idx = -1;
        sensor_detail_text[0] = '\0';
        if (system_status_sensor_level != WOMO_STATUS_OK) {
            system_status_apply_sensor_level(WOMO_STATUS_OK);
        }
    }

    // Buzzer: steigende Flanke → lokaler Ton (direkt, kein RS485)
    {
        static womo_status_level_t s_prev_stack_max = WOMO_STATUS_OK;
        if (stack_max > s_prev_stack_max) {
            if (stack_max >= WOMO_STATUS_ERROR) {
                display_buzzer_alarm();
            } else {
                display_buzzer_warn();
            }
        }
        s_prev_stack_max = stack_max;
    }

    if (gps_label) {
        bool gps_ready = false;
        if (snapshot.gps.valid) {
            // Nur anzeigen, wenn Koordinaten plausibel sind.
            // sats_ok: RUTX11 meldet manchmal 0 Satelliten trotz gültigem Fix (fix_status>=2 bereits
            // in womo_router_get_gps() geprüft) – daher keine Filterung nach Satellitenanzahl.
            // conf_ok: RUTX11 liefert accuracy 0 wenn nicht unterstützt; 0 = unbekannt, nicht ungültig
            // → >= 0.0f akzeptieren (negative Werte wären Datenfehler).
            const bool coords_finite = isfinite(snapshot.gps.latitude) && isfinite(snapshot.gps.longitude);
            const bool coords_nonzero = fabs(snapshot.gps.latitude) + fabs(snapshot.gps.longitude) > 0.0001;
            const bool conf_ok = !isfinite(snapshot.gps.confidence_m) || snapshot.gps.confidence_m >= 0.0f;
            gps_ready = coords_finite && coords_nonzero && conf_ok;
        }

        if (gps_ready) {
            /* Einmalig loggen: GPS bereit, Status der Geocode-Voraussetzungen */
            static bool geocode_status_logged = false;
            if (!geocode_status_logged) {
                ESP_LOGI(TAG, "GPS ready! WiFi=%d NTP=%d geocode_in_progress=%d last_req=%" PRId64 " loc_label=%p",
                         womo_wifi_is_connected(), womo_time_is_synced(),
                         geocode_in_progress, geocode_last_request_us, (void*)location_label);
                geocode_status_logged = true;
            }
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
                if (gps_details_visible && gps_popup_text_label) {
                    lv_label_set_text(gps_popup_text_label, last_gps_text);
                }
            }
        } else if (!snapshot.gps.valid) {
            if (strcmp(PLACEHOLDER_GPS, last_gps_text) != 0) {
                strncpy(last_gps_text, PLACEHOLDER_GPS, sizeof(last_gps_text) - 1);
                last_gps_text[sizeof(last_gps_text) - 1] = '\0';
                if (gps_details_visible && gps_popup_text_label) {
                    lv_label_set_text(gps_popup_text_label, last_gps_text);
                }
            }
            /* Location-Text absichtlich NICHT löschen: bei kurzem GPS-Ausfall
             * soll der zuletzt bekannte Ortsname stehen bleiben. Er wird erst
             * überschrieben wenn ein neuer Geocode-Treffer vorliegt. */
        }
    }
}

// Timer callback for updating time display
// Nur bei vollem Tageslicht (DAY) wird das helle Hintergrundbild geladen.
// Bei Dämmerung (SUNRISE/SUNSET) und Nacht → dunkles Hintergrundbild.
static bool theme_mode_is_daylike(womo_theme_mode_t mode)
{
    // SUNRISE = Morgen―sieht wie Tag aus: helles Bild, dunkle Texte
    // SUNSET  = Abend―sieht wie Nacht aus: dunkles Bild, helle Texte
    return (mode == WOMO_THEME_DAY || mode == WOMO_THEME_SUNRISE);
}

// Callback: Sprache geändert → statische UI-Labels aktualisieren.
static void on_locale_changed(void)
{
    if (air_title_label)          lv_label_set_text(air_title_label,          womo_locale_get_string(STR_AIR_OUTDOOR));
    if (air_title_label_in)       lv_label_set_text(air_title_label_in,       womo_locale_get_string(STR_AIR_INDOOR));
    if (fresh_water_caption_label)lv_label_set_text(fresh_water_caption_label, womo_locale_get_string(STR_TANK_FRESH));
    if (grey_water_caption_label) lv_label_set_text(grey_water_caption_label,  womo_locale_get_string(STR_TANK_GREY));
    if (gas_label_front)          lv_label_set_text(gas_label_front,           womo_locale_get_string(STR_GAS_FRONT));
    if (gas_label_rear)           lv_label_set_text(gas_label_rear,            womo_locale_get_string(STR_GAS_REAR));
    if (battery_kfz_label)        lv_label_set_text(battery_kfz_label,         womo_locale_get_string(STR_BATTERY_KFZ));
    if (tank_info_label) {
        const char *cur = lv_label_get_text(tank_info_label);
        const char *de_ph = "Frisch:\n--- L\n--.-- L/h\n--.- h";
        const char *en_ph = "Fresh:\n--- L\n--.-- L/h\n--.- h";
        if (cur && (strcmp(cur, de_ph) == 0 || strcmp(cur, en_ph) == 0)) {
            lv_label_set_text(tank_info_label, womo_locale_get_string(STR_TANK_FRESH_PLACEHOLDER));
        }
    }
}

// Callback: Grenzwerte geändert → keine UI-Sofortaktualisierung nötig,
// da der ui_update_timer_cb die Werte beim nächsten Tick neu liest.
static void on_thresholds_changed(void)
{
    /* intentionally empty – ui_update_timer_cb liest womo_thresholds_get() */
}

// Vollständiges Theme-Update: Mode neu berechnen, Hintergrundbild + Textfarben + BG-Farbe aktualisieren.
// Guard in load_background_image() prüft bg_last_day_state intern → kein unnötiger SD-Zugriff.
static void full_theme_refresh(void)
{
    womo_theme_mode_t mode = womo_theme_update(womo_theme_get_status());
    bool is_day_now = theme_mode_is_daylike(mode);
    load_background_image(lv_scr_act(), is_day_now);
    apply_text_theme_colors();
    womo_theme_apply_to_screen(NULL);
}

static void time_update_timer_cb(lv_timer_t *timer)
{
    char time_str[32];
    char date_str[32];
    static bool last_wifi_was_connected = false;
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

    // Bei WiFi-Disconnect: laufende Tasks stoppen und Flags zurücksetzen, damit sie beim nächsten Connect neu starten
    bool wifi_now = womo_wifi_is_connected();
    if (last_wifi_was_connected && !wifi_now) {
        if (weather_started) {
            womo_weather_http_stop();
            weather_started = false;
            ESP_LOGI(TAG, "WiFi getrennt – Wetter-Task gestoppt, Neustart beim Reconnect");
        }
        if (meteoalarm_started) {
            womo_meteoalarm_stop();
            meteoalarm_started = false;
            ESP_LOGI(TAG, "WiFi getrennt – Meteoalarm-Task gestoppt, Neustart beim Reconnect");
        }
    }
    last_wifi_was_connected = wifi_now;

    // Wettercode erst abrufen, wenn WiFi steht UND Zeit synchronisiert ist (TLS braucht gültige Uhrzeit)
    if (!weather_started && wifi_now && womo_time_is_synced()) {
        esp_err_t weather_err = womo_weather_http_start(openweather_update_cb, NULL);
        if (weather_err != ESP_OK) {
            ESP_LOGW(TAG, "Online weather updates disabled: %s", esp_err_to_name(weather_err));
        } else {
            womo_weather_http_set_forecast_callback(forecast_update_cb, NULL);
            weather_started = true;
        }
    }

    // Meteoalarm: nach WiFi + Zeitsync starten (wie Wetter)
    if (!meteoalarm_started && wifi_now && womo_time_is_synced()) {
        esp_err_t ma_err = womo_meteoalarm_start(meteoalarm_update_cb, NULL);
        if (ma_err != ESP_OK) {
            ESP_LOGW(TAG, "Meteoalarm-Task nicht gestartet: %s", esp_err_to_name(ma_err));
        } else {
            meteoalarm_started = true;
        }
    }

    // Quiet hours: 22:00-08:00 Backlight aus, außerhalb einschalten; Touch in Ruhezeit bekommt 5min Timeout
    if (time_valid_now) {
        static bool last_quiet_state = false;
        bool quiet_now = is_quiet_hours(&timeinfo);
        quiet_hours_active = quiet_now;

        if (quiet_now && !last_quiet_state) {
            ESP_LOGI(TAG, "Quiet hours start %02d:%02d (backlight_on=%d)", timeinfo.tm_hour, timeinfo.tm_min, backlight_on);
            if (backlight_on) {
                // Backlight für QUIET_TOUCH_TIMEOUT_MS anlassen, dann aus.
                // Gilt sowohl beim Übergang 21:59→22:00 als auch beim
                // Boot während der Quiet Hours (last_quiet_state startet
                // als false, daher greift dieser Zweig beim ersten Mal).
                backlight_start_quiet_timer();
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

    // ── Theme-Mode jede Sekunde prüfen ──────────────────────────────────
    // womo_theme_update() ist billig (nur Zeit-Vergleich). Bei Mode-Wechsel
    // (z.B. DAY→SUNSET, NIGHT→SUNRISE, SUNSET→NIGHT) sofort Hintergrundbild +
    // Textfarben + BG-Farbe aktualisieren. Reagiert innerhalb 1 s auf
    // NTP-Korrektur oder Dämmerungsübergang – statt bisher 60 s.
    if (womo_theme_is_auto_mode() && time_valid_now) {
        /* Letzter angewandter Mode – mit -1 initialisiert, damit der erste
           Timer-Tick ein Update erzwingt (Boot-Sync). */
        static womo_theme_mode_t last_applied_mode = (womo_theme_mode_t)-1;
        womo_theme_mode_t new_mode = womo_theme_update(womo_theme_get_status());

        if (new_mode != last_applied_mode) {
            bool is_day_now = theme_mode_is_daylike(new_mode);
            ESP_LOGI(TAG, "Theme mode %d → %d (%s)",
                     last_applied_mode, new_mode,
                     is_day_now ? "day" : "night");
            last_applied_mode = new_mode;
            load_background_image(lv_scr_act(), is_day_now);
            apply_text_theme_colors();
            womo_theme_apply_to_screen(NULL);
        }
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

    if (weather_widget && lvgl_port_lock(0)) {
        womo_weather_set_condition(weather_widget, condition, data->is_night);
        if (weather_widget->temp_label) {
            womo_weather_set_temperature(weather_widget, temperature);
        }
        /* Click-Handler einmalig beim ersten Wetter-Update registrieren */
        static bool click_registered = false;
        if (!click_registered && weather_widget->container) {
            lv_obj_add_flag(weather_widget->container, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(weather_widget->container,
                                weather_widget_click_cb,
                                LV_EVENT_CLICKED,
                                NULL);
            click_registered = true;
        }
        lvgl_port_unlock();
    }
}

/* ── 5-Tage Forecast Callback ─────────────────────────────────── */
static womo_weather_forecast_t s_forecast_latest = {0};

static void forecast_update_cb(const womo_weather_forecast_t *forecast, void *user_data)
{
    (void)user_data;
    if (!forecast || !forecast->valid) return;

    s_forecast_latest = *forecast;

    /* Wenn das Modal gerade offen ist → live aktualisieren */
    if (womo_forecast_modal_is_open() && lvgl_port_lock(0)) {
        womo_forecast_modal_update(&s_forecast_latest);
        lvgl_port_unlock();
    }
}

/* ── Meteoalarm: gespeicherte Warnungen für das Popup ─────────── */
static womo_meteoalarm_result_t s_meteoalarm_latest = {0};

/* Event-Callback zum Schließen des Popups (OK-Button) */
static void meteoalarm_popup_close_cb(lv_event_t *e)
{
    (void)e;
    if (meteoalarm_popup) {
        lv_msgbox_close(meteoalarm_popup);
        meteoalarm_popup = NULL;
    }
}

/* Schließt das offene Meteoalarm-Popup (falls vorhanden) */
static void meteoalarm_popup_close(void)
{
    if (meteoalarm_popup) {
        lv_msgbox_close(meteoalarm_popup);
        meteoalarm_popup = NULL;
    }
}

/* Öffnet ein Popup mit den aktuellen Warnungen */
static void meteoalarm_popup_open(void)
{
    if (meteoalarm_popup) {
        /* Bereits offen – schließen und neu aufbauen */
        meteoalarm_popup_close();
    }

    /* Nachrichten-Text aufbauen */
    static char msg_buf[512];
    msg_buf[0] = '\0';

    /* Standort/Region als erste Zeile:
     * 1) Meteoalarm-Region aus API (Warngebiet z.B. "Chiemgauer Alpen")
     * 2) Fallback: Geocode-Ortsname (z.B. "Schleching, Bayern") */
    const char *region_src = NULL;
    if (s_meteoalarm_latest.region[0]) {
        region_src = s_meteoalarm_latest.region;
    } else if (location_last_text[0]) {
        region_src = location_last_text;
    }
    if (region_src) {
        char loc_line[144];
        snprintf(loc_line, sizeof(loc_line), "Standort: %s\n\n", region_src);
        strlcat(msg_buf, loc_line, sizeof(msg_buf));
    }

    if (s_meteoalarm_latest.count == 0) {
        strlcat(msg_buf, "Keine aktiven Warnungen.", sizeof(msg_buf));
    } else {
        for (uint8_t i = 0; i < s_meteoalarm_latest.count; i++) {
            const womo_meteoalarm_warning_t *w = &s_meteoalarm_latest.warnings[i];
            const char *sev_mark;
            if (w->severity >= WOMO_WARN_SEV_SEVERE) {
                sev_mark = "! ";     // Rot / Extreme+Severe
            } else {
                sev_mark = "* ";     // Orange / Moderate
            }
            char line[200];
            if (w->region[0] && w->expires[0]) {
                snprintf(line, sizeof(line), "%s%s\n%s | bis %s\n\n",
                         sev_mark, w->headline, w->region, w->expires);
            } else if (w->region[0]) {
                snprintf(line, sizeof(line), "%s%s\n%s\n\n",
                         sev_mark, w->headline, w->region);
            } else if (w->expires[0]) {
                snprintf(line, sizeof(line), "%s%s\nbis %s\n\n",
                         sev_mark, w->headline, w->expires);
            } else {
                snprintf(line, sizeof(line), "%s%s\n\n", sev_mark, w->headline);
            }
            strlcat(msg_buf, line, sizeof(msg_buf));
        }
    }

    /* LVGL v9: lv_msgbox_create(parent) – Titel, Text und Buttons separat setzen */
    meteoalarm_popup = lv_msgbox_create(NULL);
    lv_msgbox_add_title(meteoalarm_popup, "Unwetterwarnungen (www.meteoalarm.org)");
    lv_msgbox_add_text(meteoalarm_popup, msg_buf);
    lv_obj_t *ok_btn = lv_msgbox_add_footer_button(meteoalarm_popup, "OK");
    lv_obj_set_width(meteoalarm_popup, 520);
    lv_obj_center(meteoalarm_popup);

    /* Schließen per OK-Button */
    lv_obj_add_event_cb(ok_btn, meteoalarm_popup_close_cb, LV_EVENT_CLICKED, NULL);
}

/* Click-Handler auf dem Wetter-Widget */
static void weather_widget_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (womo_forecast_modal_is_open()) {
        womo_forecast_modal_close();
    } else {
        const womo_weather_forecast_t *fc = s_forecast_latest.valid ? &s_forecast_latest : NULL;
        womo_forecast_modal_show(lv_screen_active(), fc);
        /* Ort + Sonnenzeiten eintragen */
        {
            const womo_sun_times_t *st = womo_theme_get_sun_times();
            womo_forecast_modal_set_location(
                location_last_text[0] ? location_last_text : NULL,
                st->sunrise_hour, st->sunrise_minute,
                st->sunset_hour,  st->sunset_minute);
        }
        /* Letzte bekannte Meteoalarm-Warnungen eintragen */
        womo_forecast_modal_set_warnings(
            s_meteoalarm_latest.valid ? &s_meteoalarm_latest : NULL);
    }
}

/* Meteoalarm-Callback (kommt aus FreeRTOS-Task → braucht LVGL-Lock) */
static void meteoalarm_update_cb(const womo_meteoalarm_result_t *result, void *user_data)
{
    (void)user_data;
    if (!result) return;

    ESP_LOGI(TAG, "Meteoalarm: valid=%d count=%u max_sev=%u",
             result->valid, result->count, result->max_severity);

    uint8_t prev_max_sev = s_meteoalarm_latest.max_severity;
    uint8_t prev_count   = s_meteoalarm_latest.count;

    /* Snapshot speichern (für späteres Popup-Öffnen).
     * Keine Region aus alten Fetches behalten – das Wohnmobil bewegt sich,
     * der Fallback auf location_last_text (Geocode, GPS-aktuell) ist korrekt. */
    s_meteoalarm_latest = *result;

    /* Auto-Popup: nur wenn neue Warnungen aufgetaucht sind oder Schwere
     * gestiegen ist (nicht bei bloßem Verschwinden oder unverand. Stand) */
    bool auto_open = result->count > 0 &&
                     result->max_severity >= WOMO_WARN_SEV_MODERATE &&
                     (prev_count == 0 || result->max_severity > prev_max_sev);

    if (!weather_widget) return;

    if (lvgl_port_lock(0)) {
        womo_weather_set_warnings(weather_widget, result->count, result->max_severity);
        /* Forecast-Modal live aktualisieren wenn es gerade offen ist */
        if (womo_forecast_modal_is_open()) {
            womo_forecast_modal_set_warnings(result);
        }
        if (auto_open) {
            ESP_LOGI(TAG, "Meteoalarm Auto-Popup (count=%u sev=%u)",
                     result->count, result->max_severity);
            meteoalarm_popup_open();
        }
        lvgl_port_unlock();
    }
}

static void wifi_autoretry_task(void *arg)
{
    (void)arg;
    const TickType_t delay_ticks = pdMS_TO_TICKS(30000); // 30 s

    for (;;) {
        vTaskDelay(delay_ticks);

        if (womo_wifi_is_connected()) {
            continue;
        }

        if (womo_wifi_get_status() == WOMO_WIFI_CONNECTING) {
            continue;
        }

        /* Priorität: 1. AP-Konfiguration (womo_ap_cfg), 2. NVS-Pool, 3. Kconfig-Fallback */
        char ap_ssid[33] = {0}, ap_pass[65] = {0};
        bool has_ap_cfg = (womo_ap_cfg_load(ap_ssid, sizeof(ap_ssid),
                                             ap_pass, sizeof(ap_pass)) == ESP_OK
                           && ap_ssid[0] != '\0');
        esp_err_t err;
        if (has_ap_cfg) {
            if (womo_wifi_get_status() != WOMO_WIFI_CONNECTING) {
                ESP_LOGI(TAG, "WiFi auto-reconnect: AP-Konfiguration '%s'", ap_ssid);
                err = womo_wifi_connect(ap_ssid, ap_pass, 1);
            } else {
                ESP_LOGI(TAG, "WiFi auto-reconnect: Watchdog verbindet bereits, überspringe");
                err = ESP_OK;
            }
        } else {
            ESP_LOGI(TAG, "WiFi auto-reconnect: versuche gespeicherte Netzwerke …");
            err = womo_wifi_connect_best_known(1);
            if (err == ESP_ERR_NOT_FOUND) {
                if (womo_wifi_get_status() != WOMO_WIFI_CONNECTING) {
                    ESP_LOGI(TAG, "WiFi auto-reconnect Fallback: %s", WIFI_SSID);
                    err = womo_wifi_connect(WIFI_SSID, WIFI_PASSWORD, 1);
                } else {
                    err = ESP_OK;
                }
            }
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "WiFi reconnect failed: %s", esp_err_to_name(err));
        }
    }
}

/* ── Router-Status-Polling (WiFi-Client + LTE vom RUTX11) ── */
static void router_poll_task(void *arg)
{
    (void)arg;
    const TickType_t interval = pdMS_TO_TICKS(ROUTER_POLL_INTERVAL_MS);
    static int poll_count = 0;
    static int heap_log_countdown = 2;
    static bool was_connected = false;

    /* Erste Poll-Abfrage sofort wenn WiFi connected */

    for (;;) {
        /* WiFi-Watchdog: Versucht automatisch zu reconnecten wenn Verbindung verloren */
        womo_wifi_watchdog();

        bool now_connected = womo_wifi_is_connected();

        if (!now_connected) {
            if (was_connected) {
                /* Verbindung verloren: gecachte Router-Daten löschen damit die
                 * Anzeige nicht "hängt" und nach Reconnect sofort neu pollt. */
                if (s_router_mutex) {
                    xSemaphoreTake(s_router_mutex, portMAX_DELAY);
                    memset(&s_router_wifi, 0, sizeof(s_router_wifi));
                    memset(&s_router_lte,  0, sizeof(s_router_lte));
                    xSemaphoreGive(s_router_mutex);
                }
                s_router_reachable = false;
                poll_count = 0;  /* Nach Reconnect wieder mit kurzen Intervallen pollen */
                was_connected = false;
            }
            vTaskDelay(interval);
            continue;
        }

        if (!was_connected) {
            was_connected = true;
            poll_count = 0;  /* Ersten Poll nach Reconnect sofort und mit dichten Intervallen */
            heap_log_countdown = 1;  /* Nach Reconnect frueh einmal den Heap sehen */
        }

        poll_count++;
        if (--heap_log_countdown <= 0) {
            log_runtime_heap_stats();
            heap_log_countdown = 2;  /* Bei 30s Pollintervall etwa jede Minute */
        }
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
            ESP_LOGI(TAG, "Router AP: '%s' %s ch%d, 2.4GHz=%d 5GHz=%d",
                     ap_tmp.ssid, ap_tmp.enabled ? "aktiv" : "aus", ap_tmp.channel,
                     ap_tmp.band_2_4ghz_active, ap_tmp.band_5ghz_active);
        }

        /* Auch bei Teilfehlern aktualisieren – was da ist wird angezeigt */
        if (s_router_mutex) {
            xSemaphoreTake(s_router_mutex, portMAX_DELAY);
            if (w_err == ESP_OK) s_router_wifi = wifi_tmp;
            if (l_err == ESP_OK) {
                s_router_lte = lte_tmp;
            } else {
                memset(&s_router_lte, 0, sizeof(s_router_lte));
            }
            if (a_err == ESP_OK) s_router_ap   = ap_tmp;
            xSemaphoreGive(s_router_mutex);
        }
        /* Router gilt als erreichbar nur wenn HTTP/UCI WiFi-Abfrage erfolgreich war.
         * LTE- und AP-Abfragen geben auch ohne RUTX11 ESP_OK zurück (leere Daten). */
        s_router_reachable = (w_err == ESP_OK);

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

            /* Lokalzeit vom Router parsen (gpsctl -e gibt Lokalzeit zurück).
             * tm_isdst = -1 → mktime() ermittelt DST automatisch aus dem Datum,
             * sonst würde im Sommer (CEST) fälschlicherweise CET angenommen. */
            int64_t gps_epoch = 0;
            if (gps_tmp.utc_time[0]) {
                struct tm tm_gps = {0};
                if (strptime(gps_tmp.utc_time, "%Y-%m-%d %H:%M:%S", &tm_gps)) {
                    tm_gps.tm_isdst = -1;  // DST auto-detect (nicht explizit 0=Winterzeit)
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

            /* Wetter-Modul mit aktuellen GPS-Koordinaten versorgen */
            womo_weather_http_set_location(gps_tmp.latitude, gps_tmp.longitude);
            /* Meteoalarm mit gleichen Koordinaten versorgen */
            womo_meteoalarm_set_location(gps_tmp.latitude, gps_tmp.longitude);

            /* Sonnenzeiten berechnen + Theme und Forecast-Modal aktualisieren */
            if (womo_time_update_location(gps_tmp.latitude, gps_tmp.longitude)) {
                uint8_t sr_h, sr_m, ss_h, ss_m;
                womo_time_get_sun_times(&sr_h, &sr_m, &ss_h, &ss_m);
                if (lvgl_port_lock(0)) {
                    womo_forecast_modal_set_location(
                        location_last_text[0] ? location_last_text : NULL,
                        sr_h, sr_m, ss_h, ss_m);
                    lvgl_port_unlock();
                }
            }
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
    if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    uint8_t slot = (uint8_t)(uintptr_t)lv_event_get_user_data(event);

    gas_replace_close_modal();

    if (slot == 255) return; /* Nein-Button: nur schließen */

    /* btn_id: 0=Ja – Timer senden */
    {
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

    /* LVGL v9 msgbox API */
    gas_replace_modal = lv_msgbox_create(NULL);
    lv_msgbox_add_title(gas_replace_modal, title);
    lv_msgbox_add_text(gas_replace_modal, question);
    lv_obj_t *yes_btn = lv_msgbox_add_footer_button(gas_replace_modal, btns[0]);
    lv_obj_t *no_btn  = lv_msgbox_add_footer_button(gas_replace_modal, btns[1]);
    lv_obj_center(gas_replace_modal);
    /* X-Button oben rechts im Header */
    lv_obj_t *gas_hdr = lv_msgbox_get_header(gas_replace_modal);
    if (gas_hdr) {
        lv_obj_t *x_btn = lv_btn_create(gas_hdr);
        lv_obj_set_size(x_btn, 36, 32);
        lv_obj_align(x_btn, LV_ALIGN_RIGHT_MID, -4, 0);
        lv_obj_set_style_bg_color(x_btn, lv_color_hex(0xC62828), 0);
        lv_obj_set_style_radius(x_btn, 4, 0);
        lv_obj_set_style_border_width(x_btn, 0, 0);
        lv_obj_t *x_lbl = lv_label_create(x_btn);
        lv_label_set_text(x_lbl, LV_SYMBOL_CLOSE);
        lv_obj_center(x_lbl);
        lv_obj_add_event_cb(x_btn, gas_replace_msgbox_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)255);
    }
    lv_obj_add_event_cb(yes_btn, gas_replace_msgbox_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)slot);
    lv_obj_add_event_cb(no_btn,  gas_replace_msgbox_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)255);
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

    uint8_t slot = (uint8_t)(uintptr_t)lv_timer_get_user_data(timer);
    const char *channel = (slot == 0) ? "front" : "back";

    esp_err_t err = womo_rs485_send_gas_bottle_replace(slot, channel);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RS485 gas_bottle_replace (slot=%u) failed: %s", (unsigned)slot, esp_err_to_name(err));
    }
}

// ── IMU Zero (Pitch/Roll Kalibrierung) ──────────────────────────────

static void imu_zero_close_modal(void)
{
    if (imu_zero_modal) {
        lv_obj_t *to_close = imu_zero_modal;
        imu_zero_modal = NULL;
        lv_msgbox_close(to_close);
    }
}

static void imu_zero_send_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    esp_err_t err = womo_rs485_send_imu_zero();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RS485 imu_zero failed: %s", esp_err_to_name(err));
    }
}

static void imu_zero_msgbox_event_cb(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    bool is_ok = (lv_event_get_user_data(event) != NULL);
    imu_zero_close_modal();
    if (!is_ok) return; /* Abbrechen */
    {
        // "OK" geklickt → Command senden (aus Timer-Kontext, nicht aus Event)
        lv_timer_t *t = lv_timer_create(imu_zero_send_timer_cb, 0, NULL);
        if (t) {
            lv_timer_set_repeat_count(t, 1);
        } else {
            womo_rs485_send_imu_zero();
        }
    }
}

static void imu_zero_show_modal(void)
{
    imu_zero_close_modal();

    /* LVGL v9 msgbox API */
    imu_zero_modal = lv_msgbox_create(NULL);
    lv_msgbox_add_title(imu_zero_modal, "Neigung kalibrieren");
    lv_msgbox_add_text(imu_zero_modal,
                       "Pitch und Roll auf Null setzen?\n"
                       "Das Fahrzeug sollte waagerecht stehen.");
    lv_obj_t *ok_imu_btn     = lv_msgbox_add_footer_button(imu_zero_modal, "OK");
    lv_obj_t *cancel_imu_btn = lv_msgbox_add_footer_button(imu_zero_modal, "Abbrechen");
    lv_obj_center(imu_zero_modal);
    /* X-Button oben rechts im Header */
    lv_obj_t *imu_hdr = lv_msgbox_get_header(imu_zero_modal);
    if (imu_hdr) {
        lv_obj_t *x_btn = lv_btn_create(imu_hdr);
        lv_obj_set_size(x_btn, 36, 32);
        lv_obj_align(x_btn, LV_ALIGN_RIGHT_MID, -4, 0);
        lv_obj_set_style_bg_color(x_btn, lv_color_hex(0xC62828), 0);
        lv_obj_set_style_radius(x_btn, 4, 0);
        lv_obj_set_style_border_width(x_btn, 0, 0);
        lv_obj_t *x_lbl = lv_label_create(x_btn);
        lv_label_set_text(x_lbl, LV_SYMBOL_CLOSE);
        lv_obj_center(x_lbl);
        lv_obj_add_event_cb(x_btn, imu_zero_msgbox_event_cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_add_event_cb(ok_imu_btn,     imu_zero_msgbox_event_cb, LV_EVENT_CLICKED, (void *)1);
    lv_obj_add_event_cb(cancel_imu_btn, imu_zero_msgbox_event_cb, LV_EVENT_CLICKED, NULL);
}

static void imu_zero_area_cb(lv_event_t *event)
{
    if (!event) return;
    if (lv_event_get_code(event) != LV_EVENT_LONG_PRESSED) return;
    imu_zero_show_modal();
}

static void indoor_air_long_press_cb(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_LONG_PRESSED) return;
    s_indoor_simple_view = !s_indoor_simple_view;
    if (gas_label_in)   { if (s_indoor_simple_view) lv_obj_add_flag(gas_label_in,   LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(gas_label_in,   LV_OBJ_FLAG_HIDDEN); }
    if (press_label_in) { if (s_indoor_simple_view) lv_obj_add_flag(press_label_in, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(press_label_in, LV_OBJ_FLAG_HIDDEN); }
    if (voc_label_in)   { if (s_indoor_simple_view) lv_obj_add_flag(voc_label_in,   LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(voc_label_in,   LV_OBJ_FLAG_HIDDEN); }
    /* Feuchte + Temp: nebeneinander (Vollansicht) oder untereinander (Einfachansicht) */
    if (humid_label_in && temp_label_in && air_title_label_in) {
        lv_coord_t bx = lv_obj_get_x(air_title_label_in);
        lv_coord_t by = lv_obj_get_y(air_title_label_in);
        if (s_indoor_simple_view) {
            lv_obj_set_style_text_align(humid_label_in, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_width(humid_label_in, 160);
            lv_obj_set_pos(humid_label_in, bx, by + 22);
            lv_obj_set_style_text_align(temp_label_in, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_width(temp_label_in, 160);
            lv_obj_set_pos(temp_label_in,  bx, by + 44);
        } else {
            lv_obj_set_width(humid_label_in, 75);
            lv_obj_set_pos(humid_label_in, bx + 11, by + 25);
            lv_obj_set_width(temp_label_in, 75);
            lv_obj_set_pos(temp_label_in,  bx + 85, by + 25);
        }
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

static void settings_button_event_cb(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    womo_settings_modal_show(lv_scr_act());
}

void router_leds_button_event_cb(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    /* Router-Status-Snapshot zusammenstellen */
    womo_router_leds_snapshot_t snapshot = {0};
    
    snapshot.router_online = womo_wifi_is_connected();
    
    if (s_router_mutex) {
        xSemaphoreTake(s_router_mutex, portMAX_DELAY);
        snapshot.wifi_connected = s_router_wifi.connected;
        strncpy(snapshot.wifi_ssid, s_router_wifi.ssid, sizeof(snapshot.wifi_ssid) - 1);
        snapshot.wifi_signal_percent = s_router_wifi.signal_percent;
        snapshot.wifi_channel = s_router_wifi.channel;
        snapshot.router_ap_24ghz = s_router_ap.band_2_4ghz_active;
        snapshot.router_ap_5ghz = s_router_ap.band_5ghz_active;
        snapshot.lte_registered = s_router_lte.registered;
        strncpy(snapshot.lte_operator, s_router_lte.operator_name, sizeof(snapshot.lte_operator) - 1);
        snapshot.lte_signal_percent = s_router_lte.signal_percent;
        strncpy(snapshot.lte_conn_type, s_router_lte.conn_type, sizeof(snapshot.lte_conn_type) - 1);
        strncpy(snapshot.sim_state, s_router_lte.sim_state, sizeof(snapshot.sim_state) - 1);
        xSemaphoreGive(s_router_mutex);
    }

    womo_router_leds_modal_show(lv_scr_act(), &snapshot);
}

/* ── Screenshot ─────────────────────────────────────────────────────────── */
#if WOMO_ENABLE_SCREENSHOT

/* Screenshot-Implementierung: liest Frame-Buffer direkt (RGB565, bereits im PSRAM).
 * Schreibt BMP 24-Bit ohne Zwischenpuffer – jede Zeile (~2400 B Stack-Puffer).
 * Peak-RAM-Verbrauch: < 4 KB. Keine große Allokation, kein OOM-Risiko.
 */

/* RGB565 → RGB888 BGR-Reihenfolge für BMP für eine einzelne Zeile */
static void rgb565_row_to_bgr888(const uint16_t *src, uint8_t *dst, uint32_t width)
{
    for (uint32_t x = 0; x < width; x++) {
        uint16_t px = src[x];
        dst[x * 3 + 0] = (uint8_t)(((px >>  0) & 0x1F) * 255 / 31);  /* B */
        dst[x * 3 + 1] = (uint8_t)(((px >>  5) & 0x3F) * 255 / 63);  /* G */
        dst[x * 3 + 2] = (uint8_t)(((px >> 11) & 0x1F) * 255 / 31);  /* R */
    }
}

static void screenshot_task(void *arg)
{
    volatile bool *running = (volatile bool *)arg;

    if (!womo_sd_is_mounted()) {
        ESP_LOGW(TAG, "Screenshot: SD-Card nicht eingehängt");
        if (running) *running = false;
        vTaskDelete(NULL);
        return;
    }

    int mk = mkdir(WOMO_SCREENSHOT_DIR, 0777);
    if (mk != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "Screenshot: mkdir fehlgeschlagen errno=%d (%s)", errno, strerror(errno));
        if (running) *running = false;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Screenshot: Verzeichnis %s (%s)", WOMO_SCREENSHOT_DIR,
             mk == 0 ? "angelegt" : "existiert bereits");

    /* Schreibbarkeit der SD-Karte vorab testen */
    {
        char test_path[64];
        snprintf(test_path, sizeof(test_path), WOMO_SCREENSHOT_DIR "/.wtest");
        FILE *tf = fopen(test_path, "wb");
        if (!tf) {
            ESP_LOGE(TAG, "Screenshot: SD-Karte nicht beschreibbar (errno=%d: %s) "
                     "– Schreibschutz-Tab oder defektes Dateisystem?", errno, strerror(errno));
            if (running) *running = false;
            vTaskDelete(NULL);
            return;
        }
        fclose(tf);
        remove(test_path);
        ESP_LOGI(TAG, "Screenshot: SD-Karte beschreibbar");
    }

    /* Frame-Buffer holen – existiert bereits im PSRAM, 0 Bytes extra */
    void *fb0 = NULL, *fb1 = NULL;
    esp_err_t fb_err = womo_lcd_get_frame_buffer(&fb0, &fb1);
    if (fb_err != ESP_OK || !fb0) {
        ESP_LOGE(TAG, "Screenshot: Frame-Buffer nicht verfügbar (%s)", esp_err_to_name(fb_err));
        if (running) *running = false;
        vTaskDelete(NULL);
        return;
    }
    const uint16_t *fb = (const uint16_t *)fb0;

    const uint32_t W = EXAMPLE_LCD_H_RES; /* 800 */
    const uint32_t H = EXAMPLE_LCD_V_RES; /* 480 */

    /* Dateiname (.bmp) */
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char path[64];
    snprintf(path, sizeof(path), WOMO_SCREENSHOT_DIR "/screen_%02d%02d%02d.bmp",
             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);

    /* BMP-Header aufbauen (54 Bytes: BITMAPFILEHEADER + BITMAPINFOHEADER).
     * 800×3 = 2400 B pro Zeile, bereits 4-Byte-aligniert – kein Padding nötig. */
    const uint32_t row_bytes   = W * 3u;          /* 2400 */
    const uint32_t pixel_bytes = row_bytes * H;    /* 1 152 000 */
    const uint32_t file_size   = 54u + pixel_bytes;
    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    /* BITMAPFILEHEADER */
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2]  = (uint8_t)(file_size);         hdr[3]  = (uint8_t)(file_size >> 8);
    hdr[4]  = (uint8_t)(file_size >> 16);   hdr[5]  = (uint8_t)(file_size >> 24);
    hdr[10] = 54; /* Pixel-Daten-Offset */
    /* BITMAPINFOHEADER */
    hdr[14] = 40; /* Header-Größe */
    hdr[18] = (uint8_t)(W);        hdr[19] = (uint8_t)(W >> 8);
    hdr[20] = (uint8_t)(W >> 16);  hdr[21] = (uint8_t)(W >> 24);
    hdr[22] = (uint8_t)(H);        hdr[23] = (uint8_t)(H >> 8);
    hdr[24] = (uint8_t)(H >> 16);  hdr[25] = (uint8_t)(H >> 24);
    hdr[26] = 1;  /* Farbebenen */
    hdr[28] = 24; /* Bits pro Pixel */
    /* Kompression=0, imageSize=0, xPPM=0, yPPM=0, Farbtabelle=0 */

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        ESP_LOGE(TAG, "Screenshot: fopen fehlgeschlagen: %s (errno=%d)", path, errno);
        if (running) *running = false;
        vTaskDelete(NULL);
        return;
    }
    if (fwrite(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        int err = errno;
        ESP_LOGE(TAG, "Screenshot: Header-Schreibfehler errno=%d (%s)", err, strerror(err));
        fclose(fp);
        remove(path);
        if (running) *running = false;
        vTaskDelete(NULL);
        return;
    }

    /* BMP speichert Zeilen von unten nach oben → reverse row order.
     * row_buf ist einzige Allokation: 2400 Bytes auf dem Task-Stack. */
    uint8_t row_buf[EXAMPLE_LCD_H_RES * 3u];
    bool write_ok = true;
    int write_errno = 0;
    for (int y = (int)H - 1; y >= 0; y--) {
        rgb565_row_to_bgr888(fb + (uint32_t)y * W, row_buf, W);
        if (fwrite(row_buf, 1, row_bytes, fp) != row_bytes) {
            write_errno = errno;
            write_ok = false;
            break;
        }
    }
    int close_err = fclose(fp);
    if (close_err != 0 && write_ok) {
        write_errno = errno;
        write_ok = false;
    }

    if (write_ok) {
        ESP_LOGI(TAG, "Screenshot gespeichert: %s (%"PRIu32" KB, %"PRIu32"x%"PRIu32")",
                 path, file_size / 1024u, W, H);
    } else {
        ESP_LOGE(TAG, "Screenshot: Schreibfehler errno=%d (%s)", write_errno, strerror(write_errno));
        remove(path);
    }
    if (running) *running = false;
    vTaskDelete(NULL);
}

static void take_screenshot(void)
{
    static volatile bool s_screenshot_running = false;
    if (s_screenshot_running) {
        ESP_LOGW(TAG, "Screenshot läuft bereits");
        return;
    }
    s_screenshot_running = true;
    BaseType_t rc = xTaskCreateWithCaps(screenshot_task, "screenshot",
                                         WOMO_SCREENSHOT_STACK_SIZE,
                                         (void *)&s_screenshot_running,
                                         WOMO_SCREENSHOT_TASK_PRIORITY, NULL,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rc != pdPASS) {
        s_screenshot_running = false;
        ESP_LOGE(TAG, "Screenshot-Task konnte nicht gestartet werden");
    }
}

#endif /* WOMO_ENABLE_SCREENSHOT */

/* Gesetzt beim Long-Press, konsumiert beim folgenden CLICKED-Event.
 * Verhindert Backlight-Toggle nach einem Long-Press-Screenshot. */
static volatile bool s_long_press_consumed = false;

#if WOMO_ENABLE_SCREENSHOT
/* Globaler Indev-Long-Press-Handler: Screenshot, unabhängig von offenem Modal.
 * Läuft auf dem Indev direkt – kein Widget muss den Touch erhalten.
 * Kein Backlight-Toggle hier: nur Screenshot, s_long_press_consumed setzen. */
static void screenshot_indev_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_LONG_PRESSED) return;
    if (!backlight_on) return;   /* Display aus – nichts tun */

    ESP_LOGI(TAG, "Long-Press → Screenshot");
    take_screenshot();

    /* Nachfolgenden CLICKED-Event auf allen Widgets blockieren */
    s_long_press_consumed = true;
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev) lv_indev_reset(indev, NULL);
}
#endif /* WOMO_ENABLE_SCREENSHOT */


static void backlight_button_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (!event) return;

    if (code != LV_EVENT_CLICKED) {
        return;
    }

    /* Einen Long-Press-Release absorbieren und ignorieren */
    if (s_long_press_consumed) {
        s_long_press_consumed = false;
        ESP_LOGD(TAG, "Backlight button: Long-Press-Release ignoriert");
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

    // Aktuell angezeigten Fehler quittieren (acked = latched)
    if (sensor_err_display_idx >= 0 && sensor_err_display_idx < SENSOR_ERR_SRC_MAX) {
        sensor_err_states[sensor_err_display_idx].acked =
            sensor_err_states[sensor_err_display_idx].latched;
        ESP_LOGI(TAG, "Sensorstatus '%s' (Level=%d) quittiert",
                 sensor_err_src_name[sensor_err_display_idx],
                 sensor_err_states[sensor_err_display_idx].acked);
    }

    // Verbleibende unquittierte Fehler prüfen
    womo_status_level_t stack_max = WOMO_STATUS_OK;
    int next_idx = -1;
    int pending_count = 0;
    for (int i = 0; i < SENSOR_ERR_SRC_MAX; i++) {
        if (sensor_err_states[i].latched > sensor_err_states[i].acked) {
            pending_count++;
            if (sensor_err_states[i].latched > stack_max) {
                stack_max = sensor_err_states[i].latched;
                next_idx = i;
            }
        }
    }

    if (pending_count > 0) {
        sensor_err_display_idx = next_idx;
        if (pending_count > 1) {
            snprintf(sensor_detail_text, sizeof(sensor_detail_text), "%s+%d",
                     sensor_err_src_name[next_idx], pending_count - 1);
        } else {
            snprintf(sensor_detail_text, sizeof(sensor_detail_text), "%s",
                     sensor_err_src_name[next_idx]);
        }
        system_status_apply_sensor_level(stack_max);
    } else {
        sensor_err_display_idx = -1;
        sensor_detail_text[0] = '\0';
        system_status_apply_sensor_level(WOMO_STATUS_OK);
    }

    // Theme/Label sofort aktualisieren
    system_status_apply(true);
}

static void perf_monitor_toggle_event_cb(lv_event_t *e)
{
    if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    perf_monitor_visible = !perf_monitor_visible;

#if LV_USE_PERF_MONITOR
    /* LVGL v9: lv_sysmon API statt Label-Suche im sys_layer */
    lv_display_t *disp = lv_display_get_default();
    if (disp) {
        if (perf_monitor_visible) {
            lv_sysmon_show_performance(disp);
        } else {
            lv_sysmon_hide_performance(disp);
        }
    }
#endif /* LV_USE_PERF_MONITOR */
    ESP_LOGI(TAG, "Performance Monitor %s", perf_monitor_visible ? "eingeblendet" : "ausgeblendet");

    // RS485 Debug Label mittogglen
    if (rs485_debug_label) {
        if (perf_monitor_visible) {
            lv_obj_clear_flag(rs485_debug_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(rs485_debug_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * Hilfsfunktion: PNG von SD-Karte in lv_image_dsc_t laden und lv_img
 * Widget setzen.  Bestehenden Puffer freigeben falls bereits belegt.
 * Rückgabe: img-Widget (neues lv_img_create falls *img_ptr == NULL)
 * ───────────────────────────────────────────────────────────────────────── */
static lv_obj_t *load_icon_png(lv_obj_t *parent, const char *path,
                                uint8_t **buf_ptr, lv_image_dsc_t *dsc,
                                lv_obj_t *img_obj)
{
    if (!path || !buf_ptr || !dsc) return img_obj;

    if (womo_sd_is_mounted()) {
        womo_ch422g_assert_sd_cs();
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        ESP_LOGW(TAG, "Icon nicht gefunden: %s", path);
        return img_obj;
    }
    fseek(fp, 0, SEEK_END);
    long fsz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsz <= 0 || fsz > 256 * 1024) {
        fclose(fp);
        return img_obj;
    }
    if (*buf_ptr) {
        heap_caps_free(*buf_ptr);
        *buf_ptr = NULL;
    }
    uint8_t *buf = heap_caps_malloc((size_t)fsz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf || fread(buf, 1, (size_t)fsz, fp) != (size_t)fsz) {
        if (buf) heap_caps_free(buf);
        fclose(fp);
        return img_obj;
    }
    fclose(fp);

    *buf_ptr = buf;
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.w     = 0;
    dsc->header.h     = 0;
    dsc->header.cf    = LV_COLOR_FORMAT_RAW_ALPHA;
    dsc->data         = buf;
    dsc->data_size    = (uint32_t)fsz;

    if (parent) {
        if (!img_obj) {
            img_obj = lv_img_create(parent);
            lv_obj_clear_flag(img_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_img_recolor_opa(img_obj, LV_OPA_TRANSP, 0);
            lv_obj_center(img_obj);
        }
        lv_img_set_src(img_obj, dsc);
        lv_obj_center(img_obj);
    }
    return img_obj;
}

static void preload_icons(void)
{
    if (!womo_sd_is_mounted()) {
        ESP_LOGI(TAG, "SD card not mounted, skipping icon preload");
        return;
    }

    // WiFi + LTE Signalstärken-Icons vorladen (32×32px, Ordner icons-48(32))
    static const char * const wifi_paths[6] = {
        "/sdcard/images/icons-48(32)/wifi_1.png",
        "/sdcard/images/icons-48(32)/wifi_2.png",
        "/sdcard/images/icons-48(32)/wifi_3.png",
        "/sdcard/images/icons-48(32)/wifi_4.png",
        "/sdcard/images/icons-48(32)/wifi_5.png",
        "/sdcard/images/icons-48(32)/wifi_6.png",
    };
    static const char * const lte_paths[6] = {
        "/sdcard/images/icons-48(32)/signal_1.png",
        "/sdcard/images/icons-48(32)/signal_2.png",
        "/sdcard/images/icons-48(32)/signal_3.png",
        "/sdcard/images/icons-48(32)/signal_4.png",
        "/sdcard/images/icons-48(32)/signal_5.png",
        "/sdcard/images/icons-48(32)/signal_6.png",
    };
    for (int i = 0; i < 6; i++)
        load_icon_png(NULL, wifi_paths[i], &wifi_icon_bufs[i], &wifi_icon_dscs[i], NULL);
    for (int i = 0; i < 6; i++)
        load_icon_png(NULL, lte_paths[i],  &lte_icon_bufs[i],  &lte_icon_dscs[i],  NULL);
}

static void sd_init_task(void *arg)
{
    (void)arg;

    int64_t start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Async SD init started");
    esp_err_t err = ESP_FAIL;
    int32_t elapsed_ms = 0;

    /* SD ist fuer Boot nicht kritisch. Nach WiFi/LCD-Start kurz warten und bei
     * internem Heap-Druck (ESP_ERR_NO_MEM) mit Backoff erneut probieren. */
    vTaskDelay(pdMS_TO_TICKS(1500));

    for (int attempt = 1; attempt <= 3; attempt++) {
        err = womo_sd_init();
        elapsed_ms = (int32_t)((esp_timer_get_time() - start_us) / 1000);

        if (err == ESP_OK) {
            break;
        }

        if (err != ESP_ERR_NO_MEM || attempt == 3) {
            break;
        }

        int retry_delay_ms = attempt * 1500;
        ESP_LOGW(TAG,
                 "Async SD init attempt %d/3 failed after %d ms: %s - retry in %d ms",
                 attempt,
                 elapsed_ms,
                 esp_err_to_name(err),
                 retry_delay_ms);
        vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Async SD init finished after %d ms", elapsed_ms);
        preload_icons();
        load_logo_image(lv_scr_act());

        if (weather_widget && lvgl_port_lock(500)) {
            womo_weather_set_condition(weather_widget,
                                       weather_widget->condition,
                                       weather_widget->is_night);
            lvgl_port_unlock();
        }
    } else {
        ESP_LOGW(TAG, "Async SD init failed after %d ms: %s",
                 elapsed_ms,
                 esp_err_to_name(err));
    }

    sd_init_task_handle = NULL;
    vTaskDelete(NULL);
}

static void rs485_init_task(void *arg)
{
    (void)arg;

    int64_t start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Async RS485 init started");

    esp_err_t rs485_err = womo_rs485_display_init();
    int32_t elapsed_ms = (int32_t)((esp_timer_get_time() - start_us) / 1000);

    if (rs485_err == ESP_OK) {
        ESP_LOGI(TAG, "Async RS485 init finished after %d ms", elapsed_ms);
        ESP_LOGI(TAG, "RS485 initialized - receiving data from Sensorboard");
        womo_rs485_set_data_callback(rs485_data_received, NULL);
        womo_rs485_set_event_callback(rs485_event_handler, NULL);
        rs485_waiting_for_handshake = true;
        vTaskDelay(pdMS_TO_TICKS(100));  // Kurz warten damit UART bereit
        womo_rs485_send_display_ready();
        ESP_LOGI(TAG, "Sent display_ready to trigger sensor data");
    } else {
        ESP_LOGW(TAG, "Async RS485 init failed after %d ms: %s",
                 elapsed_ms, esp_err_to_name(rs485_err));
    }

    rs485_init_task_handle = NULL;
    vTaskDelete(NULL);
}

static void backlight_update_label(void)
{
    if (!backlight_btn) return;
    lv_color_t bg = backlight_on ? lv_color_hex(0xF9A825) : lv_color_hex(0xC0C0C0);
    lv_obj_set_style_bg_color(backlight_btn, bg, 0);
    lv_obj_set_style_bg_opa(backlight_btn, LV_OPA_COVER, 0);
}

/* ─── Einheitliche Hilfsfunktionen für runde Icon-Buttons ─────────────────── */

static lv_obj_t *create_round_button(lv_obj_t *parent, int size, lv_color_t bg_color,
                                     lv_event_cb_t cb, lv_event_code_t code)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, bg_color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, code, NULL);
    }
    // Innenliegender schwarzer Rand: 4 px eingerückt, 2 px breit
    lv_obj_t *inner = lv_obj_create(btn);
    lv_obj_set_size(inner, size - 8, size - 8);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(inner, 2, 0);
    lv_obj_set_style_border_color(inner, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_opa(inner, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(inner, 0, 0);
    lv_obj_center(inner);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return btn;
}

static lv_obj_t *round_button_add_icon(lv_obj_t *btn, const char *icon_utf8)
{
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, icon_utf8);
    lv_obj_set_style_text_font(lbl, WOMO_FONT_ICONS_LARGE, 0);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(lbl);
    return lbl;
}

static void update_round_button_state(lv_obj_t *btn, lv_obj_t *icon_label, bool active,
                                      lv_color_t active_color)
{
    if (!btn) return;
    lv_color_t bg = active ? active_color : lv_color_hex(0xC0C0C0);
    lv_obj_set_style_bg_color(btn, bg, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    if (icon_label) lv_obj_center(icon_label);
}

static void update_classic_icon(lv_color_t color, bool active)
{
    update_round_button_state(classic_btn, classic_label, active, lv_color_hex(0x2E7D32));
}

static void update_radio_icon(lv_color_t color, bool active)
{
    update_round_button_state(radio_btn, radio_label, active, lv_color_hex(0x1565C0));
}

static void update_shore_icon(lv_color_t color, bool active)
{
    (void)color; (void)active; // Icon per PNG von SD geladen
}

static void log_runtime_heap_stats(void)
{
    /* Nur internen SRAM abfragen – PSRAM-Traversierung (MALLOC_CAP_SPIRAM/8BIT)
     * blockiert den QSPI-Bus und stört den RGB-Bounce-Buffer-DMA → Display-Flackern. */
    size_t internal_free    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t dma_free         = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t dma_largest      = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);

    ESP_LOGI(TAG,
             "Heap: internal_free=%u internal_largest=%u dma_free=%u dma_largest=%u",
             (unsigned)internal_free,
             (unsigned)internal_largest,
             (unsigned)dma_free,
             (unsigned)dma_largest);
}

// Timer-Callbacks für asynchronen RS485-Send (blockiert nicht den LVGL-Thread)
// Statische Handles für Debounce – verhindert Doppel-Send bei schnellem Tippen
static lv_timer_t *s_pwr_send_timer = NULL;
static lv_timer_t *s_radio_send_timer = NULL;

static void pwr_12v_send_timer_cb(lv_timer_t *timer)
{
    s_pwr_send_timer = NULL;
    bool enable = (bool)(uintptr_t)lv_timer_get_user_data(timer);
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
    bool enable = (bool)(uintptr_t)lv_timer_get_user_data(timer);
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
    if (!event || lv_event_get_code(event) != LV_EVENT_LONG_PRESSED) {
        return;
    }
    // Debounce: vorherigen noch nicht gesendeten Befehl abbrechen
    if (s_pwr_send_timer) {
        lv_timer_del(s_pwr_send_timer);
        s_pwr_send_timer = NULL;
    }
    // Optimistisches UI: sofort umschalten
    classic_on = !classic_on;
    update_classic_icon(lv_color_hex(0x2E7D32), classic_on);
    // Radio geht aus wenn 12V aus
    if (!classic_on && radio_on) {
        radio_on = false;
        update_radio_icon(lv_color_hex(0x1565C0), radio_on);
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
    update_radio_icon(lv_color_hex(0x1565C0), radio_on);
    // RS485-Befehl asynchron senden
    s_radio_send_timer = lv_timer_create(radio_send_timer_cb, 10, (void *)(uintptr_t)radio_on);
    if (s_radio_send_timer) lv_timer_set_repeat_count(s_radio_send_timer, 1);
}

static void shore_power_update_label(void)
{
    if (!shore_label) {
        return;
    }

    lv_color_t bg = shore_power_present ? lv_color_hex(0xF9A825) : lv_color_hex(0xC0C0C0);
    lv_obj_set_style_bg_color(shore_label, bg, 0);
    lv_obj_set_style_bg_opa(shore_label, LV_OPA_COVER, 0);
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

/* ────────────────────────────────────────────────────────────
 * Hilfsfunktion für Settings-Modal: RTC-Batterie-Status holen
 * ──────────────────────────────────────────────────────────── */
void womo_get_rtc_battery_status(bool *valid, bool *low, bool *switched)
{
    if (!valid || !low || !switched) return;

    taskENTER_CRITICAL(&display_data_spinlock);
    *valid = latest_sensor_data.power.valid;
    *low   = latest_sensor_data.power.rtc_bat_low;
    *switched = latest_sensor_data.power.rtc_bat_switched;
    taskEXIT_CRITICAL(&display_data_spinlock);
}

void app_main()
{
    // ── Log-Level zentral konfigurieren (siehe display_config.h) ──────────
    esp_log_level_set("womo_main",        LOG_LEVEL_MAIN);
    esp_log_level_set("WaveShare_7_UI",   LOG_LEVEL_UI);
    esp_log_level_set("lv_port",          LOG_LEVEL_LVGL_PORT);
    esp_log_level_set("womo_display",     LOG_LEVEL_DISPLAY_HW);
    esp_log_level_set("rs485_display",    LOG_LEVEL_RS485);
    esp_log_level_set("womo_wifi",        LOG_LEVEL_WIFI);
    esp_log_level_set("esp_netif_lwip",   LOG_LEVEL_WIFI);
    esp_log_level_set("esp_netif_handlers", LOG_LEVEL_WIFI);
    esp_log_level_set("weather_http",     LOG_LEVEL_WEATHER_HTTP);
    esp_log_level_set("meteoalarm",       LOG_LEVEL_METEOALARM);
    esp_log_level_set("geocode",          LOG_LEVEL_GEOCODE);
    esp_log_level_set("router_uci",       LOG_LEVEL_ROUTER_UCI);
    esp_log_level_set("http_mutex",       LOG_LEVEL_HTTP_MUTEX);
    esp_log_level_set("womo_buzz_http",   LOG_LEVEL_BUZZER_HTTP);
    esp_log_level_set("womo_sd",          LOG_LEVEL_SD);
    esp_log_level_set("womo_time",        LOG_LEVEL_TIME);
    esp_log_level_set("womo_sun_calc",    LOG_LEVEL_SUN_CALC);
    esp_log_level_set("womo_theme",       LOG_LEVEL_THEME);
    esp_log_level_set("weather",          LOG_LEVEL_WEATHER_WIDGET);
    esp_log_level_set("battery",          LOG_LEVEL_BATTERY);
    esp_log_level_set("tank_widget",      LOG_LEVEL_TANK);
    esp_log_level_set("gas_bottle",       LOG_LEVEL_GAS);
    esp_log_level_set("thresholds",       LOG_LEVEL_THRESHOLDS);
    esp_log_level_set("womo_fonts_german",LOG_LEVEL_FONTS);
    esp_log_level_set("forecast_modal",   LOG_LEVEL_FORECAST_MODAL);
    esp_log_level_set("womo_modal",       LOG_LEVEL_CONN_MODAL);
    esp_log_level_set("settings_modal",   LOG_LEVEL_SETTINGS_MODAL);
    esp_log_level_set("ROUTER_LEDS",      LOG_LEVEL_ROUTER_LEDS);

    // Backlight sofort AUS – CH422G könnte nach Reset in undefiniertem
    // Zustand sein.  Das Backlight wird erst nach vollständiger UI-
    // Initialisierung + Theme + Hintergrundbild explizit eingeschaltet.
    wavesahre_rgb_lcd_bl_off();

    // Buzzer so früh wie möglich – braucht nur LEDC + GPIO, kein Display/WiFi
    display_buzzer_init();
    display_buzzer_startup();

    // Initialize time management
    womo_time_init();
    rs485_watchdog_start_us = esp_timer_get_time();
    rs485_last_packet_time_us = rs485_watchdog_start_us;
    rs485_timeout_active = false;
    
    // Initialize WiFi + sofort async verbinden (läuft parallel zu Display-HW + UI-Konstruktion)
    ESP_LOGI(TAG, "Initializing WiFi...");
    womo_wifi_init();
    {
        char ap_ssid[33] = {0}, ap_pass[65] = {0};
        if (womo_ap_cfg_load(ap_ssid, sizeof(ap_ssid), ap_pass, sizeof(ap_pass)) == ESP_OK
            && ap_ssid[0] != '\0') {
            ESP_LOGI(TAG, "WiFi boot: AP-Konfiguration '%s'", ap_ssid);
            womo_wifi_connect_async(ap_ssid, ap_pass, 1);
        } else {
            ESP_LOGI(TAG, "WiFi boot async (Kconfig): %s", WIFI_SSID);
            womo_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD, 1);
        }
    }

#if WOMO_ENABLE_BUZZER_STUDIO_HTTP
    esp_err_t buzzer_http_err = womo_buzzer_http_start();
    if (buzzer_http_err == ESP_OK) {
        ESP_LOGI(TAG, "Buzzer-Studio HTTP aktiv: /buzzer_studio.html");
    } else {
        ESP_LOGW(TAG, "Buzzer-Studio HTTP Start fehlgeschlagen: %s", esp_err_to_name(buzzer_http_err));
    }
#else
    ESP_LOGI(TAG, "Buzzer-Studio HTTP deaktiviert (WOMO_ENABLE_BUZZER_STUDIO_HTTP=0)");
#endif

    // Initialize theme (default location: Central Europe)
    // Sonnenzeiten werden automatisch via GPS berechnet (router_poll_task)
    womo_theme_init(50.0, 10.0);  // Approximate Germany
    womo_theme_reset();  // Reset cached state (wichtig bei Power-Cycle!)
    
    // Fallback sunrise/sunset – MESZ-Durchschnitt für Zentraleuropa.
    // Wird bei erstem GPS-Fix oder nach NTP-Sync via sun_calc überschrieben.
    womo_theme_set_sun_times(6, 30, 20, 0);
    
    // Initialize display (uses I2C for touch controller)
    {
        int64_t t0 = esp_timer_get_time();
        waveshare_esp32_s3_rgb_lcd_init();
        ESP_LOGI(TAG, "Display init finished after %d ms",
                 (int)((esp_timer_get_time() - t0) / 1000));
    }

    /* Sichtbarer Sofort-Start: das RGB-Init hat die Framebuffer bereits mit
     * der Theme-Hintergrundfarbe vorgefüllt. Backlight hier schon einschalten,
     * damit lange Folge-Init-Schritte nicht wie ein totes Display wirken. */
    if (wavesahre_rgb_lcd_bl_on() == ESP_OK) {
        backlight_on = true;
        ESP_LOGI(TAG, "Backlight enabled early after display init");
    } else {
        ESP_LOGW(TAG, "Early backlight enable failed");
    }

    ESP_LOGI(TAG, "Display WoMo Home Control with Dynamic Theme");

    // ── LVGL sperren → UI aufbauen ──────────────────────────────────
    // Nach lvgl_port_init() läuft der LVGL-Task bereits.  Mutex nehmen,
    // damit kein Frame gerendert wird, bevor Theme + Widgets stehen.
    
    // Cache zurücksetzen beim Boot, damit Theme+Hintergrundbild korrekt geladen werden
    bg_last_day_state  = -1;
    bg_img             = NULL;
    
    ESP_LOGI(TAG, "Waiting for LVGL lock to build UI...");
    if (lvgl_port_lock(5000)) {
        ESP_LOGI(TAG, "LVGL lock acquired, building UI");
        lv_obj_t *screen = lv_scr_act();
        lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(screen, screen_event_handler, LV_EVENT_CLICKED, NULL);
        lvgl_touch_set_wake_cb(touch_wake_cb);

        // Initiales Theme: Dunkelblau (Nacht) als Fallback.
        // Besser zu dunkel als zu hell – blendet nicht und ist nachts korrekt.
        // Das echte Theme+Hintergrundbild wird nach Zeitvalidierung geladen.
        lv_obj_set_style_bg_color(screen, WOMO_COLOR_NIGHT_NORMAL, 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        
        // Initialize locale system
        womo_locale_init();
        womo_locale_register_change_cb(on_locale_changed);
        womo_thresholds_init();
        womo_thresholds_register_change_cb(on_thresholds_changed);

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
        lv_obj_align(date_label, LV_ALIGN_BOTTOM_LEFT, 310, -15);
        
        // Create WiFi/LTE status button (top left) – Pill-Form, links WiFi, Mitte AP, rechts LTE
        wifi_label = lv_btn_create(screen);
        lv_obj_set_size(wifi_label, 118, 44);
        lv_obj_set_style_radius(wifi_label, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(wifi_label, lv_color_hex(0xC0C0C0), 0);
        lv_obj_set_style_bg_opa(wifi_label, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(wifi_label, 0, 0);
        lv_obj_set_style_pad_all(wifi_label, 0, 0);
        lv_obj_align(wifi_label, LV_ALIGN_TOP_LEFT, 78, 12);
        lv_obj_add_flag(wifi_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(wifi_label, wifi_label_event_cb, LV_EVENT_CLICKED, NULL);
        // AP-Sektion (grün/grau) – vor inner_border erzeugt → liegt dahinter
        wifi_ap_section = lv_obj_create(wifi_label);
        lv_obj_set_size(wifi_ap_section, 16, 36);
        lv_obj_set_style_bg_color(wifi_ap_section, lv_color_hex(0xC0C0C0), 0);
        lv_obj_set_style_bg_opa(wifi_ap_section, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(wifi_ap_section, 0, 0);
        lv_obj_set_style_pad_all(wifi_ap_section, 0, 0);
        lv_obj_set_style_radius(wifi_ap_section, 0, 0);
        lv_obj_align(wifi_ap_section, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(wifi_ap_section, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        // Innerer Rahmen
        wifi_inner_border = lv_obj_create(wifi_label);
        lv_obj_set_size(wifi_inner_border, 110, 36);
        lv_obj_set_style_radius(wifi_inner_border, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(wifi_inner_border, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(wifi_inner_border, 2, 0);
        lv_obj_set_style_border_color(wifi_inner_border, lv_color_hex(0x000000), 0);
        lv_obj_set_style_pad_all(wifi_inner_border, 0, 0);
        lv_obj_center(wifi_inner_border);
        lv_obj_clear_flag(wifi_inner_border, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        // Zwei Trennlinien: links und rechts der AP-Sektion
        lv_obj_t *wifi_divider_l = lv_obj_create(wifi_label);
        lv_obj_set_size(wifi_divider_l, 2, 36);
        lv_obj_set_style_bg_color(wifi_divider_l, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(wifi_divider_l, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(wifi_divider_l, 0, 0);
        lv_obj_set_style_pad_all(wifi_divider_l, 0, 0);
        lv_obj_set_style_radius(wifi_divider_l, 1, 0);
        lv_obj_align(wifi_divider_l, LV_ALIGN_CENTER, -9, 0);
        lv_obj_clear_flag(wifi_divider_l, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *wifi_divider_r = lv_obj_create(wifi_label);
        lv_obj_set_size(wifi_divider_r, 2, 36);
        lv_obj_set_style_bg_color(wifi_divider_r, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(wifi_divider_r, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(wifi_divider_r, 0, 0);
        lv_obj_set_style_pad_all(wifi_divider_r, 0, 0);
        lv_obj_set_style_radius(wifi_divider_r, 1, 0);
        lv_obj_align(wifi_divider_r, LV_ALIGN_CENTER, +9, 0);
        lv_obj_clear_flag(wifi_divider_r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        // WiFi-Icon links, LTE-Icon rechts
        wifi_icon_img = lv_img_create(wifi_label);
        lv_obj_clear_flag(wifi_icon_img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_img_recolor_opa(wifi_icon_img, LV_OPA_TRANSP, 0);
        lv_obj_align(wifi_icon_img, LV_ALIGN_LEFT_MID, 10, 0);
        lte_icon_img = lv_img_create(wifi_label);
        lv_obj_clear_flag(lte_icon_img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_img_recolor_opa(lte_icon_img, LV_OPA_TRANSP, 0);
        lv_obj_align(lte_icon_img, LV_ALIGN_RIGHT_MID, -12, 0);
        // „A\nP"-Label zentriert in der AP-Sektion
        lv_obj_t *ap_section_label = lv_label_create(wifi_label);
        lv_label_set_text(ap_section_label, "A\nP");
        lv_obj_set_style_text_font(ap_section_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(ap_section_label, lv_color_black(), 0);
        lv_obj_set_style_text_align(ap_section_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(ap_section_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(ap_section_label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        update_connectivity_label();

        // Zusätzliche Schalter links (klassisch, Radio) und Netzstrom-Anzeige
        // Einheitliches System: create_round_button() + round_button_add_icon()
        // 12V-Button (unten links, Long-Press) – bolt-Icon
        classic_btn = create_round_button(screen, 48,
                                          classic_on ? lv_color_hex(0x2E7D32) : lv_color_hex(0xC0C0C0),
                                          classic_button_event_cb, LV_EVENT_LONG_PRESSED);
        lv_obj_align(classic_btn, LV_ALIGN_BOTTOM_LEFT, 10, -10);
        classic_label = round_button_add_icon(classic_btn, ICON_POWER_SETTINGS_NEW);

        // Multimedia-Button (oben links, Click) – music_note-Icon
        radio_btn = create_round_button(screen, 48,
                                        radio_on ? lv_color_hex(0x1565C0) : lv_color_hex(0xC0C0C0),
                                        radio_button_event_cb, LV_EVENT_CLICKED);
        lv_obj_align(radio_btn, LV_ALIGN_TOP_LEFT, 10, 10);
        radio_label = round_button_add_icon(radio_btn, ICON_MUSIC_NOTE);

        // Einstellungen-Button (über Multimedia-Button) – settings-Icon
        settings_btn = create_round_button(screen, 40,
                                           lv_color_hex(0xC0C0C0),
                                           settings_button_event_cb, LV_EVENT_CLICKED);
        lv_obj_align(settings_btn, LV_ALIGN_BOTTOM_LEFT, 10, -80);  // bündig links wie Radio-Button
        settings_icon_label = round_button_add_icon(settings_btn, ICON_SETTINGS);

        // Landstrom-Anzeige (Mitte unten, kein Click) – lv_obj als Container (nicht lv_label,
        // da Kind-lv_obj auf lv_label Touch-Events blockiert)
        shore_label = lv_obj_create(screen);
        lv_obj_set_size(shore_label, 44, 44);
        lv_obj_align(shore_label, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_radius(shore_label, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(shore_label, 0, 0);
        lv_obj_set_style_shadow_width(shore_label, 0, 0);
        lv_obj_set_style_pad_all(shore_label, 0, 0);
        lv_obj_set_style_bg_opa(shore_label, LV_OPA_COVER, 0);
        lv_obj_clear_flag(shore_label, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        shore_power_update_label();
        update_shore_icon(lv_color_hex(0xF9A825), shore_power_present);
        // Bolt-Icon als Kind-Label (wird von lv_label_set_text auf shore_label nicht beeinflusst)
        shore_icon_label = lv_label_create(shore_label);
        lv_label_set_text(shore_icon_label, ICON_CHARGER);
        lv_obj_set_style_text_font(shore_icon_label, WOMO_FONT_ICONS_LARGE, 0);
        lv_obj_set_style_text_color(shore_icon_label, lv_color_black(), 0);
        lv_obj_clear_flag(shore_icon_label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_center(shore_icon_label);
        // Innerer schwarzer Rand (identisch zu create_round_button)
        lv_obj_t *shore_inner = lv_obj_create(shore_label);
        lv_obj_set_size(shore_inner, 44 - 8, 44 - 8);
        lv_obj_set_style_radius(shore_inner, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(shore_inner, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(shore_inner, 2, 0);
        lv_obj_set_style_border_color(shore_inner, lv_color_hex(0x000000), 0);
        lv_obj_set_style_border_opa(shore_inner, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(shore_inner, 0, 0);
        lv_obj_center(shore_inner);
        lv_obj_clear_flag(shore_inner, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        shore_caption_label = lv_label_create(screen);
        lv_label_set_text(shore_caption_label, "220 V");
        lv_obj_set_style_text_font(shore_caption_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(shore_caption_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(shore_caption_label, lv_color_black(), 0);
        lv_obj_align_to(shore_caption_label, shore_label, LV_ALIGN_OUT_BOTTOM_MID, 0, -24);

        // Helligkeit-Button (unten rechts, Click + Long-Press) – lightbulb_circle-Icon
        backlight_btn = create_round_button(screen, 40,
                                            lv_color_hex(0xC0C0C0),
                                            backlight_button_event_cb, LV_EVENT_CLICKED);
        lv_obj_add_event_cb(backlight_btn, backlight_button_event_cb, LV_EVENT_LONG_PRESSED, NULL);
        lv_obj_align(backlight_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
        backlight_icon_label = round_button_add_icon(backlight_btn, ICON_LIGHT_OFF);
        backlight_update_label();
        // HINWEIS: preload_icons() wird NACH lvgl_port_unlock() aufgerufen,
        // da SD-Karten-DMA internen RAM benötigt der unter dem LVGL-Lock nicht frei ist.
        
        // Weather data (top right) - Gas first, all one font size larger
    char init_buf[40];

    air_title_label = lv_label_create(screen);
    lv_label_set_text(air_title_label, womo_locale_get_string(STR_AIR_OUTDOOR));
    lv_obj_set_style_text_font(air_title_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(air_title_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(air_title_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(air_title_label, LV_ALIGN_TOP_RIGHT, -10, 23);

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
    lv_obj_align(press_container, LV_ALIGN_TOP_RIGHT, -10, 48);

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
    lv_obj_align(humid_label, LV_ALIGN_TOP_RIGHT, -10, 73);

    temp_label = lv_label_create(screen);
    snprintf(init_buf, sizeof(init_buf), "--.- °C");
    lv_label_set_text(temp_label, init_buf);
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(temp_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(temp_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(temp_label, LV_ALIGN_TOP_RIGHT, -10, 98);

    // IMU-Werte als freistehende Labels (ca. 40% von oben, 30% vom rechten Rand)
    lv_coord_t disp_w = lv_disp_get_hor_res(NULL);
    lv_coord_t disp_h = lv_disp_get_ver_res(NULL);
    if (disp_w <= 0) {
        disp_w = 800;
    }
    if (disp_h <= 0) {
        disp_h = 480;
    }

    // Koordinaten für beide Blöcke vorab berechnen (werden getauscht)
    lv_coord_t indoor_base_y = 170 + (disp_h / 4) - 35;
    lv_coord_t indoor_block_x = (disp_w / 2) - 285;

    lv_coord_t right_margin = (lv_coord_t)lrintf(disp_w * 0.30f) - 30 - 15;
    if (right_margin < 0) right_margin = 0;
    lv_coord_t top_offset = (lv_coord_t)lrintf(disp_h * 0.40f) - 40 + 10;
    if (top_offset < 0) top_offset = 0;
    lv_coord_t line_spacing = 18;

    // Innenluft jetzt an IMU-Position (rechts oben)
    lv_coord_t air_x = disp_w - right_margin - 160 - 3;
    lv_coord_t air_y = top_offset - 20;
    air_title_label_in = lv_label_create(screen);
    lv_label_set_text(air_title_label_in, womo_locale_get_string(STR_AIR_INDOOR));
    lv_obj_set_style_text_font(air_title_label_in, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(air_title_label_in, lv_color_black(), 0);
    lv_obj_set_width(air_title_label_in, 160);
    lv_obj_set_style_text_align(air_title_label_in, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(air_title_label_in, air_x, air_y);

    humid_label_in = lv_label_create(screen);
    lv_label_set_text(humid_label_in, "--.-- %");
    lv_obj_set_style_text_font(humid_label_in, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(humid_label_in, lv_color_black(), 0);
    lv_obj_set_width(humid_label_in, 75);
    lv_obj_set_style_text_align(humid_label_in, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(humid_label_in, air_x + 11, air_y + 25);

    temp_label_in = lv_label_create(screen);
    lv_label_set_text(temp_label_in, "--.- °C");
    lv_obj_set_style_text_font(temp_label_in, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(temp_label_in, lv_color_black(), 0);
    lv_obj_set_width(temp_label_in, 75);
    lv_obj_set_style_text_align(temp_label_in, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(temp_label_in, air_x + 85, air_y + 25);

    gas_label_in = lv_label_create(screen);
    lv_label_set_text(gas_label_in, PLACEHOLDER_IAQ);
    lv_obj_set_style_text_font(gas_label_in, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(gas_label_in, lv_color_black(), 0);
    lv_obj_set_width(gas_label_in, 160);
    lv_obj_set_style_text_align(gas_label_in, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(gas_label_in, air_x + 61, air_y + 68);  // IAQ hinter CO2 (gleiche Zeile)

    press_label_in = lv_label_create(screen);
    lv_label_set_text(press_label_in, PLACEHOLDER_CO2);
    lv_obj_set_style_text_font(press_label_in, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(press_label_in, lv_color_black(), 0);
    lv_obj_set_width(press_label_in, 160);
    lv_obj_set_style_text_align(press_label_in, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(press_label_in, air_x, air_y + 68);  // CO2 Mitte

    voc_label_in = lv_label_create(screen);
    lv_label_set_text(voc_label_in, PLACEHOLDER_BVOC);
    lv_obj_set_style_text_font(voc_label_in, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(voc_label_in, lv_color_black(), 0);
    lv_obj_set_width(voc_label_in, 160);
    lv_obj_set_style_text_align(voc_label_in, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(voc_label_in, air_x, air_y + 50);    // bVOC oben

    /* Unsichtbarer Touch-Bereich über dem Innen-Luftblock: Long-Press → Einfachansicht toggle */
    {
        lv_obj_t *air_in_touch = lv_btn_create(screen);
        lv_obj_set_size(air_in_touch, 163, 85);
        lv_obj_set_pos(air_in_touch, air_x, air_y);
        lv_obj_set_style_bg_opa(air_in_touch, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(air_in_touch, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_opa(air_in_touch, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(air_in_touch, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(air_in_touch, indoor_air_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    }

    // Farben an aktuelles Theme anpassen (Tag/Nacht)
    apply_text_theme_colors();

    // IMU jetzt an Indoor-Position (links/Mitte) + 250px nach rechts, 20px tiefer
    imu_pitch_label = lv_label_create(screen);
    lv_label_set_text(imu_pitch_label, "Pitch: --.-°");
    lv_obj_set_style_text_font(imu_pitch_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(imu_pitch_label, lv_color_black(), 0);
    lv_obj_set_pos(imu_pitch_label, indoor_block_x + 250, indoor_base_y + 20);

    imu_roll_label = lv_label_create(screen);
    lv_label_set_text(imu_roll_label, "Roll : --.-°");
    lv_obj_set_style_text_font(imu_roll_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(imu_roll_label, lv_color_black(), 0);
    lv_obj_set_pos(imu_roll_label, indoor_block_x + 250, indoor_base_y + 20 + line_spacing);

    imu_heading_label = lv_label_create(screen);
    lv_label_set_text(imu_heading_label, "-- (---°)");
    lv_obj_set_style_text_font(imu_heading_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(imu_heading_label, lv_color_black(), 0);
    lv_obj_set_pos(imu_heading_label, indoor_block_x + 250, indoor_base_y + 20 + (line_spacing * 2));

    // Unsichtbarer Touch-Bereich über Pitch/Roll/Heading für Long-Press → Kalibrierung
    {
        lv_obj_t *imu_touch = lv_btn_create(screen);
        lv_obj_set_size(imu_touch, 180, line_spacing * 3 + 10);
        lv_obj_set_pos(imu_touch, indoor_block_x + 250 - 5, indoor_base_y + 20 - 5);
        lv_obj_set_style_bg_opa(imu_touch, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(imu_touch, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_opa(imu_touch, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(imu_touch, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(imu_touch, imu_zero_area_cb, LV_EVENT_LONG_PRESSED, NULL);
    }

    /* GPS-Label: zeigt "GPS" oder Koordinaten, mit Rahmen wie status_label.
     * gps_button ist ein Alias auf dasselbe Objekt für die Theme-Farb-Logik. */
    lv_coord_t gps_offset_x = disp_w / 5;
    if (gps_offset_x < 0) {
        gps_offset_x = 0;
    }
    gps_button = create_round_button(screen, 40, lv_color_hex(0xC0C0C0),
                                      gps_label_event_cb, LV_EVENT_CLICKED);
    lv_obj_align(gps_button, LV_ALIGN_BOTTOM_LEFT, 80, -10);
    gps_icon_label = round_button_add_icon(gps_button, ICON_MY_LOCATION);
    gps_label = gps_button; // Alias für bestehende Referenzen

    /* GPS-Detail-Popup-Panel: erscheint rechts neben dem GPS-Button beim Klick.
     * Position: BOTTOM_LEFT, x=65 (GPS-Button ~55px breit + 10px Margin), y=-10.
     * Initial versteckt, wird per gps_label_event_cb eingeblendet. */
    gps_popup_panel = lv_obj_create(screen);
    lv_obj_set_size(gps_popup_panel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(gps_popup_panel, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_bg_opa(gps_popup_panel, LV_OPA_80, 0);
    lv_obj_set_style_border_width(gps_popup_panel, 2, 0);
    lv_obj_set_style_border_color(gps_popup_panel, lv_color_black(), 0);
    lv_obj_set_style_radius(gps_popup_panel, 6, 0);
    lv_obj_set_style_pad_all(gps_popup_panel, 6, 0);
    lv_obj_add_flag(gps_popup_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(gps_popup_panel, LV_ALIGN_BOTTOM_LEFT, 125, -10);
    gps_popup_text_label = lv_label_create(gps_popup_panel);
    lv_obj_set_style_text_font(gps_popup_text_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(gps_popup_text_label, lv_color_black(), 0);
    lv_label_set_text(gps_popup_text_label, "");

    /* Ortsname rechts neben GPS-Label.
     * Da gps_label per LV_ALIGN_BOTTOM_LEFT positioniert ist, kann
     * lv_obj_align_to() die absolute Position im ersten Frame nicht kennen.
     * → Gleiche Ausrichtung verwenden, x-Offset = gps_offset_x + geschätzte GPS-Button-Breite.
     * GPS-Button: "GPS" (3 Zeichen × ~9px) + 2×pad(6) + 2×border(2) ≈ 43px */
    location_label = lv_label_create(screen);
    lv_label_set_text(location_label, "Standort...");
    lv_obj_set_style_text_font(location_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(location_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(location_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(location_label, 200);
    lv_label_set_long_mode(location_label, LV_LABEL_LONG_DOT);
    lv_obj_align(location_label, LV_ALIGN_BOTTOM_LEFT, 10 + 115, -19);

    // cm → Pixel Umrechnung (anpassbar für physische Displaymaße)
    const float DISP_WIDTH_CM = 15.5f;
    const float DISP_HEIGHT_CM = 9.3f;
    const float px_per_cm_x = disp_w / DISP_WIDTH_CM;
    const float px_per_cm_y = disp_h / DISP_HEIGHT_CM;

    // Create water tank widgets (positioned above the gas bottles)
    fresh_water_tank = womo_tank_create(screen, 125, 110, WOMO_TANK_FRESH);
    if (fresh_water_tank) {
        womo_tank_set_caption(fresh_water_tank, "");
        womo_tank_set_no_data(fresh_water_tank);
        fresh_water_caption_label = lv_label_create(screen);
        lv_label_set_text(fresh_water_caption_label, womo_locale_get_string(STR_TANK_FRESH));
        lv_obj_set_style_text_font(fresh_water_caption_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(fresh_water_caption_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(fresh_water_caption_label, lv_color_black(), 0);
        lv_obj_align_to(fresh_water_caption_label, fresh_water_tank->container, LV_ALIGN_OUT_BOTTOM_MID, 0, -24);
    } else {
        ESP_LOGW(TAG, "Failed to create fresh water tank widget");
    }


    grey_water_tank = womo_tank_create(screen, 195, 110, WOMO_TANK_GREY);
    if (grey_water_tank) {
        womo_tank_set_caption(grey_water_tank, "");
        womo_tank_set_no_data(grey_water_tank);
        grey_water_caption_label = lv_label_create(screen);
        lv_label_set_text(grey_water_caption_label, womo_locale_get_string(STR_TANK_GREY));
        lv_obj_set_style_text_font(grey_water_caption_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(grey_water_caption_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(grey_water_caption_label, lv_color_black(), 0);
        lv_obj_align_to(grey_water_caption_label, grey_water_tank->container, LV_ALIGN_OUT_BOTTOM_MID, 0, -24);
        if (shore_label) {
            lv_obj_align_to(shore_label, grey_water_tank->container, LV_ALIGN_OUT_RIGHT_MID, 15, 0);
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
    gas_bottle_a = womo_gas_bottle_create(screen, 130, 230);  // Gas bottle A (90 - 20 + 50)
    gas_bottle_b = womo_gas_bottle_create(screen, 200, 230);  // Gas bottle B (160 - 20 + 50)
    
    // Set weights for bottles: 10.1 kg = 0%, 21 kg = 100%
    if (gas_bottle_a) {
        womo_gas_bottle_set_empty_weight(gas_bottle_a, 10.1f);  // Empty bottle (0%)
        womo_gas_bottle_set_full_weight(gas_bottle_a, 21.0f);   // Full bottle (100%)
        womo_gas_bottle_set_cap_label(gas_bottle_a, "");
        lv_obj_add_flag(gas_bottle_a->container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(gas_bottle_a->container, gas_bottle_clicked_cb, LV_EVENT_ALL, (void *)(uintptr_t)0);
        if (!gas_label_front) {
            gas_label_front = lv_label_create(screen);
            lv_label_set_text(gas_label_front, womo_locale_get_string(STR_GAS_FRONT));
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
            lv_label_set_text(gas_label_rear, womo_locale_get_string(STR_GAS_REAR));
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

    if (!tank_info_label) {
        tank_info_label = lv_label_create(screen);
        lv_label_set_text(tank_info_label, womo_locale_get_string(STR_TANK_FRESH_PLACEHOLDER));
        lv_obj_set_style_text_font(tank_info_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(tank_info_label, lv_color_black(), 0);
        lv_obj_set_style_text_align(tank_info_label, LV_TEXT_ALIGN_RIGHT, 0);
        if (fresh_water_tank && fresh_water_tank->container) {
            lv_obj_align_to(tank_info_label, fresh_water_tank->container, LV_ALIGN_OUT_LEFT_MID, -8, 0);
        } else {
            lv_obj_align(tank_info_label, LV_ALIGN_TOP_LEFT, 260, 120);
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
            lv_label_set_text(battery_kfz_label, womo_locale_get_string(STR_BATTERY_KFZ));
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

    // Elec-Labels (INA226) zwischen den beiden Batterien
    if (!elec_title_label && main_battery && secondary_battery) {
        // Mitte zwischen rechtem Rand der Board-Batterie und linkem Rand der KFZ-Batterie
        lv_coord_t board_right = lv_obj_get_x(secondary_battery->container)
                               + lv_obj_get_width(secondary_battery->container);
        lv_coord_t kfz_left   = lv_obj_get_x(main_battery->container);
        lv_coord_t mid_x      = (board_right + kfz_left) / 2;
        lv_coord_t bat_y      = lv_obj_get_y(main_battery->container);
        lv_coord_t bat_h      = lv_obj_get_height(main_battery->container);

        elec_title_label = lv_label_create(screen);
        lv_label_set_text(elec_title_label, "Strom");
        lv_obj_set_style_text_font(elec_title_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(elec_title_label, lv_color_black(), 0);
        lv_obj_set_style_text_align(elec_title_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(elec_title_label, mid_x - 20, bat_y);

        elec_vi_label = lv_label_create(screen);
        lv_label_set_text(elec_vi_label, "---");
        lv_obj_set_style_text_font(elec_vi_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(elec_vi_label, lv_color_black(), 0);
        lv_obj_set_style_text_align(elec_vi_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(elec_vi_label, mid_x - 30, bat_y + 16);

        elec_power_label = lv_label_create(screen);
        lv_label_set_text(elec_power_label, "---");
        lv_obj_set_style_text_font(elec_power_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(elec_power_label, lv_color_black(), 0);
        lv_obj_set_style_text_align(elec_power_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(elec_power_label, mid_x - 20, bat_y + bat_h / 2);
    }

    // RS485 Debug label (über KFZ-Batterie, fallback unten links)
#if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_INFO
    rs485_debug_label = lv_label_create(screen);
    lv_label_set_text(rs485_debug_label, womo_locale_get_string(STR_RS485_WAITING));
    lv_obj_set_style_text_font(rs485_debug_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rs485_debug_label, lv_color_make(255, 0, 0), 0); // Red for visibility
    if (main_battery && main_battery->container) {
        lv_obj_align_to(rs485_debug_label, main_battery->container, LV_ALIGN_OUT_TOP_MID, 0, -6);
    } else {
        lv_obj_align(rs485_debug_label, LV_ALIGN_BOTTOM_LEFT, 20, -60);
    }
#endif // CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_INFO
        
        // Mode label removed - theme now fully automatic based on real time
        
        // Create status button with inner inlay (right bottom)
        status_btn = lv_btn_create(screen);
        lv_obj_set_style_radius(status_btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(status_btn, lv_color_hex(0xC0C0C0), 0);
        lv_obj_set_style_bg_opa(status_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(status_btn, 0, 0);
        lv_obj_set_style_pad_all(status_btn, 0, 0);
        lv_obj_add_flag(status_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(status_btn, status_label_event_cb, LV_EVENT_CLICKED, NULL);

        status_inlay = lv_obj_create(status_btn);
        lv_obj_set_style_radius(status_inlay, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(status_inlay, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(status_inlay, 2, 0);
        lv_obj_set_style_border_color(status_inlay, lv_color_hex(0x000000), 0);
        lv_obj_set_style_pad_all(status_inlay, 0, 0);
        lv_obj_clear_flag(status_inlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        status_label = lv_label_create(status_btn);
        lv_obj_set_style_text_color(status_label, lv_color_black(), 0);

        status_label_last_text[0] = '\0';
        system_status_apply(true);
        lv_obj_align(status_btn, LV_ALIGN_BOTTOM_LEFT, 539, -10);
        
#if CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_INFO
        // Unsichtbarer Touch-Bereich rechts mitte zum Ein-/Ausblenden des Performance Monitors
        lv_obj_t *perf_toggle_btn = lv_obj_create(screen);
        lv_obj_set_size(perf_toggle_btn, 120, 80);
        lv_obj_align(perf_toggle_btn, LV_ALIGN_RIGHT_MID, 0, -20);
        lv_obj_set_style_bg_opa(perf_toggle_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(perf_toggle_btn, 0, 0);
        lv_obj_add_flag(perf_toggle_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(perf_toggle_btn, perf_monitor_toggle_event_cb, LV_EVENT_CLICKED, NULL);
#endif // CONFIG_LOG_DEFAULT_LEVEL >= ESP_LOG_INFO

        // LVGL-Timer für 1s-Updates und UI-Updates
        lv_timer_create(time_update_timer_cb, 1000, NULL);
        ui_update_timer = lv_timer_create(ui_update_timer_cb, UI_UPDATE_INTERVAL_DEFAULT_MS, NULL);
        if (!ui_update_timer) {
            ESP_LOGW(TAG, "Failed to create UI update timer");
        }

        apply_text_theme_colors();

#if WOMO_ENABLE_SCREENSHOT
        /* Globaler Long-Press-Handler: Screenshot auch bei geöffnetem Modal,
         * da der Backlight-Button dann vom Overlay verdeckt wäre. */
        {
            lv_indev_t *indev = lv_indev_get_next(NULL);
            while (indev) {
                if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
                    lv_indev_add_event_cb(indev, screenshot_indev_cb,
                                         LV_EVENT_LONG_PRESSED, NULL);
                    ESP_LOGI(TAG, "Screenshot Long-Press-Handler registriert (indev=%p)", (void*)indev);
                    break;
                }
                indev = lv_indev_get_next(indev);
            }
        }
#endif /* WOMO_ENABLE_SCREENSHOT */

        lvgl_port_unlock();
        ESP_LOGI(TAG, "UI build completed");
    }  // Ende LVGL-Lock
    else {
        ESP_LOGE(TAG, "LVGL lock timeout during UI build - keeping fallback screen");
    }

    // ── HTTPS-Mutex initialisieren (vor allen HTTP-Tasks) ─────────────
    womo_http_mutex_init();

    // ── WiFi-Verbindungsergebnis abholen ────────────────────────────────
    // WiFi läuft seit womo_wifi_init() im Hintergrund (connect_async).
    // Restwartezeit ≈ 0 ms wenn der Router erreichbar war; max 5s Fallback.
    esp_err_t wifi_err = womo_wifi_wait_connected(5000);

    if (wifi_err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected to Router-AP: %s", WIFI_SSID);
        if (womo_time_sync_ntp(false) == ESP_OK) {
            ESP_LOGI(TAG, "NTP sync started (background)");
        } else {
            ESP_LOGW(TAG, "NTP sync start failed");
        }
    } else {
        ESP_LOGW(TAG, "WiFi connection to Router-AP failed (%s) - RUTX11 eingeschaltet?",
                 esp_err_to_name(wifi_err));
    }

    // Auf gültige Zeit warten: NTP oder RS485-Timestamp (max 5s).
    // RS485 display_ready wurde in create_ui() bereits gesendet,
    // das Sensorboard sendet jetzt schon Pakete mit timestamp_ms.
    {
        bool time_ok = false;
        for (int i = 0; i < 50; i++) {  // max 5s (50 × 100ms)
            // 1) NTP/GPS hat System-Uhr gesetzt?
            struct tm t;
            if (womo_time_get(&t) == ESP_OK && t.tm_year >= (2024 - 1900)) {
                ESP_LOGI(TAG, "Time valid (system clock) after %d ms", i * 100);
                time_ok = true;
                break;
            }
            // 2) RS485-Timestamp vom Sensorboard verfügbar?
            taskENTER_CRITICAL(&display_data_spinlock);
            uint64_t ts_ms = latest_sensor_data.timestamp_ms;
            taskEXIT_CRITICAL(&display_data_spinlock);
            if (ts_ms > 0) {
                int64_t rs485_secs = (int64_t)(ts_ms / 1000ULL);
                // Plausibilitätscheck: nach 2024-01-01
                if (rs485_secs > 1704067200) {
                    womo_time_sync_gps((time_t)rs485_secs);
                    ESP_LOGI(TAG, "Time synced from RS485 after %d ms", i * 100);
                    time_ok = true;
                    break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (!time_ok) {
            ESP_LOGW(TAG, "No time source available after 5s – using DAY default");
        }

        // Sonnenzeiten sofort berechnen (mit Fallback-Koordinaten),
        // damit das Boot-Theme korrekte Tages-/Nachtgrenzen hat.
        // Wird später durch exakte GPS-Koordinaten überschrieben.
        if (time_ok) {
            if (womo_time_update_location(50.0, 10.0)) {
                uint8_t sr_h, sr_m, ss_h, ss_m;
                womo_time_get_sun_times(&sr_h, &sr_m, &ss_h, &ss_m);
                ESP_LOGI(TAG, "Boot sun times (approx): sunrise=%02d:%02d sunset=%02d:%02d",
                         sr_h, sr_m, ss_h, ss_m);
            }
        }
    }

    // ── Theme + Hintergrundbild mit korrekter Uhrzeit laden ───────────────
    // PNG-Decode läuft OHNE LVGL-Lock (15–60 s auf ESP32-S3).
    // Nur Theme-Berechnung + apply_text_theme_colors brauchen ein kurzes Lock.
    {
        womo_theme_mode_t boot_mode = womo_theme_update(WOMO_STATUS_OK);
        bool boot_is_day = theme_mode_is_daylike(boot_mode);
        const char *mode_names[] = {"DAY", "NIGHT", "SUNRISE", "SUNSET"};
        ESP_LOGI(TAG, "Boot theme: %s (mode=%d, ducato=%s)",
                 mode_names[boot_mode], boot_mode, boot_is_day ? "weiss" : "grau");

        // Theme-Farben + Screen-Hintergrundfarbe setzen (schnell: nur Stil-Ops)
        if (lvgl_port_lock(500)) {
            apply_text_theme_colors();
            womo_theme_apply_to_screen(NULL);
            lvgl_port_unlock();
        }

        // Backlight ist hardwareseitig seit dem frühen wavesahre_rgb_lcd_bl_on()-Aufruf
        // bereits an. Hier backlight_set(true) aufrufen damit backlight_on-Flag gesetzt
        // und backlight_update_label() (Label existiert jetzt) korrekt ist.
        // 150 ms warten damit LVGL beide Direct-Mode-Framebuffer gefüllt hat.
        ESP_LOGI(TAG, "Waiting for LVGL to render initial screen...");
        vTaskDelay(pdMS_TO_TICKS(150));
        ESP_LOGI(TAG, "Enabling backlight (state sync)");
        if (lvgl_port_lock(500)) {
            backlight_set(true);
            lvgl_port_unlock();
        }

        // Hintergrundbild + Logo laden.
        // Das Hintergrundbild kommt eingebettet aus der Firmware, das Logo optional später von SD.
        load_background_image(lv_scr_act(), boot_is_day);
    }

    if (!womo_sd_is_mounted() && sd_init_task_handle == NULL) {
        BaseType_t sd_created = xTaskCreateWithCaps(sd_init_task,
                                                    "sd_init",
                                                    4096,
                                                    NULL,
                                                    3,
                                                    &sd_init_task_handle,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (sd_created != pdPASS) {
            sd_init_task_handle = NULL;
            ESP_LOGW(TAG, "Async SD init task could not be started");
        }
    }

    if (rs485_init_task_handle == NULL) {
        BaseType_t rs485_created = xTaskCreateWithCaps(rs485_init_task,
                                                       "rs485_init",
                                                       4096,
                                                       NULL,
                                                       4,
                                                       &rs485_init_task_handle,
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (rs485_created != pdPASS) {
            rs485_init_task_handle = NULL;
            ESP_LOGW(TAG, "Async RS485 init task could not be started");
        }
    }

    // Router UCI Client initialisieren + Poll-Task VOR Backlight starten
    // damit Connectivity-Modal schneller Daten hat
    womo_router_uci_init();
    s_router_mutex = xSemaphoreCreateMutex();
    if (s_router_mutex) {
        /* Router-Poll macht HTTP + JSON + Mutex – kein NVS, kein Cache-Disable.
         * PSRAM-Stack ist sicher. Interner Heap ist nach PNG-Decode fragmentiert. */
        BaseType_t rc = xTaskCreateWithCaps(router_poll_task,
                                             "router_poll",
                                             8192,
                                             NULL,
                                             4,
                                             &s_router_poll_handle,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (rc != pdPASS) {
            s_router_poll_handle = NULL;
            ESP_LOGW(TAG, "Router-Poll-Task konnte nicht gestartet werden");
        }
    }

    // Backlight wurde bereits nach dem ersten LVGL-Theme-Apply eingeschaltet (nach PNG-Decode).
    // backlight_set(true) ist idempotent – kein erneuter Aufruf nötig.

    if (wifi_autoretry_handle == NULL) {
        BaseType_t created = xTaskCreateWithCaps(wifi_autoretry_task,
                                         "wifi_autoretry",
                                         4096,
                                         NULL,
                                         4,
                                         &wifi_autoretry_handle,
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
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
