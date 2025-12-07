/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "hardware/waveshare_rgb_lcd_port.h"
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
#include "storage/womo_sd.h"
#include "rs485/womo_rs485_display.h"
#include "i2cdev.h"  // i2cdev for CH422G GPIO expander on display
#include "nvs.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <stdbool.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "womo_main";

#define RS485_MISSING_THRESHOLD 2

static const char *PLACEHOLDER_GAS = "Q -- kOhm";
static const char *PLACEHOLDER_PRESSURE = "---- hPa";
static const char *PLACEHOLDER_HUMIDITY = "--.-- %";
static const char *PLACEHOLDER_TEMPERATURE = "--.- °C";


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
static lv_obj_t *temp_label = NULL;   // Temperature display
static lv_obj_t *humid_label = NULL;  // Humidity display
static lv_obj_t *press_label = NULL;  // Pressure display
static lv_obj_t *gas_label = NULL;    // Gas resistance display
static lv_obj_t *air_title_label = NULL; // Air value heading
static lv_obj_t *imu_pitch_label = NULL;
static lv_obj_t *imu_roll_label = NULL;
static lv_obj_t *imu_heading_label = NULL;
static lv_obj_t *gps_label = NULL;    // GPS position
static lv_obj_t *fresh_water_caption_label = NULL; // Text unter Frischwasser-Tank
static lv_obj_t *grey_water_caption_label = NULL;  // Text unter Grauwasser-Tank
static lv_obj_t *bg_img = NULL;  // Background image
static lv_obj_t *rs485_debug_label = NULL; // RS485 debug status
static womo_weather_t *weather_widget = NULL; // Weather widget
static womo_battery_t *main_battery = NULL;   // Battery 1 widget
static womo_battery_t *secondary_battery = NULL; // Battery 2 widget
static lv_timer_t *ui_update_timer = NULL; // Periodic UI/IMU refresh timer
static const uint32_t UI_UPDATE_INTERVAL_DEFAULT_MS = 500;

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

// Gas bottle widgets
static womo_gas_bottle_t *gas_bottle_a = NULL; // Gas bottle A (HX711 channel A)
static womo_gas_bottle_t *gas_bottle_b = NULL; // Gas bottle B (HX711 channel B)

// Water tank widgets
static womo_tank_t *fresh_water_tank = NULL;
static womo_tank_t *grey_water_tank = NULL;

// RS485 packet counter
static uint32_t rs485_packet_count = 0;

typedef struct {
    uint16_t hx711;
    uint16_t battery;
    uint16_t bme680;
    uint16_t tank;
    uint16_t lte;
} rs485_missing_t;

static rs485_missing_t rs485_missing_counter = {0};

static portMUX_TYPE display_data_spinlock = portMUX_INITIALIZER_UNLOCKED;
static womo_sensor_data_t latest_sensor_data = {0};
static uint32_t latest_packet_count = 0;
static bool latest_data_valid = false;
static rs485_missing_t latest_missing_snapshot = {0};
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
static womo_status_level_t system_status_manual_level = WOMO_STATUS_OK;
static portMUX_TYPE system_status_spinlock = portMUX_INITIALIZER_UNLOCKED;

// RS485 data callback
static void rs485_data_received(const womo_sensor_data_t *data, void *user_data);
static void imu_labels_update(bool has_data,
                              float roll_deg,
                              float pitch_deg,
                              float heading_deg,
                              const char *direction);
static void openweather_update_cb(const womo_weather_http_data_t *data, void *user_data);
static womo_weather_condition_t map_openweather_condition(int weather_id, bool is_night);
static womo_weather_condition_t map_day_condition_to_night(womo_weather_condition_t condition);
static void update_connectivity_label(void);
static uint8_t wifi_rssi_to_percent(int8_t rssi);
static void ui_update_timer_cb(lv_timer_t *timer);
static void wifi_label_event_cb(lv_event_t *event);
static void connectivity_snapshot_fill(womo_connectivity_snapshot_t *snapshot,
                                       const womo_lte_status_t *lte_snapshot,
                                       uint8_t wifi_percent,
                                       const char *wifi_ssid,
                                       womo_wifi_status_t wifi_status,
                                       bool wifi_connected);
static void imu_orientation_set(womo_imu_orientation_t orientation, bool persist);
static esp_err_t imu_orientation_store_to_nvs(uint8_t value);
static void imu_orientation_apply(float *roll_deg, float *pitch_deg);
static void imu_orientation_load_from_nvs(void);
static void rs485_event_handler(womo_rs485_event_t event, void *user_data);

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

    if (title_label) lv_obj_set_style_text_color(title_label, text_color, 0);
    if (time_label) lv_obj_set_style_text_color(time_label, text_color, 0);
    if (date_label) lv_obj_set_style_text_color(date_label, text_color, 0);
    if (wifi_label) lv_obj_set_style_text_color(wifi_label, text_color, 0);
    if (air_title_label) lv_obj_set_style_text_color(air_title_label, text_color, 0);
    if (gas_label) lv_obj_set_style_text_color(gas_label, text_color, 0);
    if (press_label) lv_obj_set_style_text_color(press_label, text_color, 0);
    if (humid_label) lv_obj_set_style_text_color(humid_label, text_color, 0);
    if (temp_label) lv_obj_set_style_text_color(temp_label, text_color, 0);
    if (imu_pitch_label) lv_obj_set_style_text_color(imu_pitch_label, lv_color_white(), 0);
    if (imu_roll_label) lv_obj_set_style_text_color(imu_roll_label, lv_color_white(), 0);
    if (imu_heading_label) lv_obj_set_style_text_color(imu_heading_label, lv_color_white(), 0);
    if (gps_label) lv_obj_set_style_text_color(gps_label, text_color, 0);
    lv_color_t tank_label_color = lv_color_black();
    if (fresh_water_caption_label) lv_obj_set_style_text_color(fresh_water_caption_label, tank_label_color, 0);
    if (grey_water_caption_label) lv_obj_set_style_text_color(grey_water_caption_label, tank_label_color, 0);
    if (fresh_water_tank) womo_tank_set_text_color(fresh_water_tank, tank_label_color);
    if (grey_water_tank) womo_tank_set_text_color(grey_water_tank, tank_label_color);
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

static void update_connectivity_label(void)
{
    if (!wifi_label) {
        return;
    }

    char wifi_line[64];
    char lte_line[64];
    char combined[140];

    if (womo_wifi_is_connected()) {
        char ssid[33] = {0};
        if (womo_wifi_get_ssid(ssid, sizeof(ssid)) != ESP_OK || ssid[0] == '\0') {
            strcpy(ssid, "unknown");
        }
        int8_t rssi = womo_wifi_get_rssi();
        uint8_t percent = wifi_rssi_to_percent(rssi);
        snprintf(wifi_line, sizeof(wifi_line), "WiFi: %s %u%%", ssid, percent);
    } else {
        snprintf(wifi_line, sizeof(wifi_line), "WiFi: offline 0%%");
    }

    if (lte_status.valid && lte_status.registered) {
        const char *operator_name = lte_status.operator_name[0] ? lte_status.operator_name : "verbunden";
        snprintf(lte_line, sizeof(lte_line), "LTE : %s %u%%", operator_name, lte_status.signal_percent);
    } else if (lte_status.valid) {
        const char *operator_name = lte_status.operator_name[0] ? lte_status.operator_name : "offline";
        snprintf(lte_line, sizeof(lte_line), "LTE : %s 0%%", operator_name);
    } else {
        snprintf(lte_line, sizeof(lte_line), "LTE : -- 0%%");
    }

    snprintf(combined, sizeof(combined), "%s\n%s", wifi_line, lte_line);
    lv_label_set_text(wifi_label, combined);
}

static void connectivity_snapshot_fill(womo_connectivity_snapshot_t *snapshot,
                                       const womo_lte_status_t *lte_snapshot,
                                       uint8_t wifi_percent,
                                       const char *wifi_ssid,
                                       womo_wifi_status_t wifi_status,
                                       bool wifi_connected)
{
    if (!snapshot) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->wifi_connected = wifi_connected;
    snapshot->wifi_status = wifi_status;
    snapshot->wifi_signal_percent = wifi_percent;
    if (wifi_ssid) {
        strncpy(snapshot->wifi_ssid, wifi_ssid, sizeof(snapshot->wifi_ssid) - 1);
        snapshot->wifi_ssid[sizeof(snapshot->wifi_ssid) - 1] = '\0';
    }

    if (lte_snapshot) {
        snapshot->lte_valid = lte_snapshot->valid;
        snapshot->lte_registered = lte_snapshot->registered;
        snapshot->lte_signal_percent = lte_snapshot->signal_percent;
        if (lte_snapshot->operator_name[0] != '\0') {
            strncpy(snapshot->lte_operator,
                    lte_snapshot->operator_name,
                    sizeof(snapshot->lte_operator) - 1);
            snapshot->lte_operator[sizeof(snapshot->lte_operator) - 1] = '\0';
        }
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

    for (int i = 0; i < SYSTEM_STATUS_SOURCE_MAX; ++i) {
        if (!entries_copy[i].active) {
            continue;
        }

        if (entries_copy[i].level > resolved) {
            resolved = entries_copy[i].level;
            detail_id = entries_copy[i].detail_id;
            detail_valid = (detail_id < STR_MAX);
        } else if (entries_copy[i].level == resolved && !detail_valid && entries_copy[i].detail_id < STR_MAX) {
            detail_id = entries_copy[i].detail_id;
            detail_valid = true;
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

    if (detail_valid) {
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
    if (source == SYSTEM_STATUS_SOURCE_MANUAL) {
        system_status_manual_level = WOMO_STATUS_OK;
    }
    taskEXIT_CRITICAL(&system_status_spinlock);

    system_status_apply(false);
}

static void system_status_cycle_manual(void)
{
    womo_status_level_t next_level = WOMO_STATUS_OK;
    switch (system_status_manual_level) {
        case WOMO_STATUS_OK:
            next_level = WOMO_STATUS_WARNING;
            break;
        case WOMO_STATUS_WARNING:
            next_level = WOMO_STATUS_ERROR;
            break;
        case WOMO_STATUS_ERROR:
            next_level = WOMO_STATUS_CRITICAL;
            break;
        case WOMO_STATUS_CRITICAL:
        default:
            next_level = WOMO_STATUS_OK;
            break;
    }

    system_status_manual_level = next_level;
    if (next_level == WOMO_STATUS_OK) {
        system_status_clear(SYSTEM_STATUS_SOURCE_MANUAL);
    } else {
        system_status_raise(SYSTEM_STATUS_SOURCE_MANUAL, next_level, STR_MAX);
    }
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
                system_status_clear(SYSTEM_STATUS_SOURCE_RS485_WAITING);
            }
            break;
        case WOMO_RS485_EVENT_HEARTBEAT:
            // Heartbeats dienen aktuell nur zur Diagnose
            break;
        case WOMO_RS485_EVENT_INVALID_JSON:
            if (!rs485_invalid_data_active) {
                rs485_invalid_data_active = true;
                system_status_raise(SYSTEM_STATUS_SOURCE_RS485_INVALID,
                                    WOMO_STATUS_ERROR,
                                    STR_RS485_INVALID_DATA);
            }
            break;
        default:
            break;
    }
}


// Load background image from SD card
static bool load_background_image(lv_obj_t *screen)
{
    if (!womo_sd_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted, skipping background image");
        return false;
    }
    
    // Try to load Ducato.png from SD card
    const char* img_path = "/sdcard/SDCARD/images/Ducato.png";
    struct stat st;
    
    ESP_LOGI(TAG, "Loading background image: %s", img_path);
    
    if (stat(img_path, &st) != 0) {
        ESP_LOGW(TAG, "Background image not found: %s", img_path);
        return false;
    }
    
    // Open file with ESP-IDF FATFS
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
    
    // Create image object
    bg_img = lv_img_create(screen);
    
    // IMPORTANT: Set as background layer with explicit Z-index
    lv_obj_move_background(bg_img);
    lv_obj_set_style_pad_all(bg_img, 0, 0);
    
    // Position and size
    lv_obj_set_size(bg_img, 800, 480);
    lv_obj_align(bg_img, LV_ALIGN_CENTER, 0, 0);
    
    // Set image source (PNG will be decoded once)
    lv_img_set_src(bg_img, &img_dsc);
    
    // Use PNG's own transparency: Ducato = opaque, free areas = transparent
    // No additional opacity - let PNG alpha channel control transparency
    lv_obj_set_style_img_opa(bg_img, LV_OPA_COVER, 0);
    
    // Ensure image stays in background
    lv_obj_move_to_index(bg_img, 0);
    
    ESP_LOGI(TAG, "Background image applied to screen");
    return true;
}

// Touch event handler
static void screen_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_get_act(), &point);
        
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
        // Right side: cycle status (keep for system status simulation)
        else if (point.x > 400) {
            system_status_cycle_manual();
        }
        // Note: Theme mode is now fully automatic based on real time - no manual control needed
    }
}

static void ui_update_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    womo_sensor_data_t snapshot = {0};
    womo_lte_status_t lte_snapshot = {0};
    rs485_missing_t missing_snapshot = (rs485_missing_t){0};
    uint32_t packet_count = 0;
    bool data_valid = false;
    bool rs485_timeout_snapshot = false;
    bool rs485_waiting_snapshot = false;
    int64_t last_packet_us = 0;
    const int64_t now_us = esp_timer_get_time();
    bool modal_open = womo_connectivity_modal_is_open();

    taskENTER_CRITICAL(&display_data_spinlock);
    snapshot = latest_sensor_data;
    lte_snapshot = lte_status;
    missing_snapshot = latest_missing_snapshot;
    packet_count = latest_packet_count;
    data_valid = latest_data_valid;
    last_packet_us = rs485_last_packet_time_us;
    rs485_timeout_snapshot = rs485_timeout_active;
    rs485_waiting_snapshot = rs485_waiting_for_handshake;
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
            system_status_raise(SYSTEM_STATUS_SOURCE_RS485_TIMEOUT,
                                WOMO_STATUS_ERROR,
                                STR_RS485_TIMEOUT_SOURCE);
            system_status_clear(SYSTEM_STATUS_SOURCE_RS485_WAITING);
        } else {
            ESP_LOGI(TAG, "RS485 Timeout aufgehoben (Unterbrechung %.1f s)", elapsed_s);
            system_status_clear(SYSTEM_STATUS_SOURCE_RS485_TIMEOUT);
        }
    }

    // Connectivity label (WiFi + LTE)
    if (wifi_label) {
        static char last_connectivity_text[140] = "";
        char wifi_line[64];
        char lte_line[64];
        char combined[140];
        bool wifi_connected_now = womo_wifi_is_connected();
        uint8_t wifi_percent = 0;

        if (wifi_connected_now) {
            char ssid[33] = {0};
            if (womo_wifi_get_ssid(ssid, sizeof(ssid)) != ESP_OK || ssid[0] == '\0') {
                strcpy(ssid, "unknown");
            }
            int8_t rssi = womo_wifi_get_rssi();
            wifi_percent = wifi_rssi_to_percent(rssi);
            snprintf(wifi_line, sizeof(wifi_line), "WiFi: %s %u%%", ssid, wifi_percent);
        } else {
            snprintf(wifi_line, sizeof(wifi_line), "WiFi: offline 0%%");
        }

        if (lte_snapshot.valid && lte_snapshot.registered) {
            const char *operator_name = lte_snapshot.operator_name[0] ? lte_snapshot.operator_name : "verbunden";
            snprintf(lte_line, sizeof(lte_line), "LTE : %s %u%%", operator_name, lte_snapshot.signal_percent);
        } else if (lte_snapshot.valid) {
            const char *operator_name = lte_snapshot.operator_name[0] ? lte_snapshot.operator_name : "offline";
            snprintf(lte_line, sizeof(lte_line), "LTE : %s 0%%", operator_name);
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
            char modal_ssid[33] = {0};
            if (wifi_connected_now) {
                if (womo_wifi_get_ssid(modal_ssid, sizeof(modal_ssid)) != ESP_OK) {
                    modal_ssid[0] = '\0';
                }
            }
            womo_connectivity_snapshot_t modal_snapshot;
            womo_wifi_status_t wifi_status = womo_wifi_get_status();
            connectivity_snapshot_fill(&modal_snapshot,
                                       &lte_snapshot,
                                       wifi_percent,
                                       modal_ssid,
                                       wifi_status,
                                       wifi_connected_now);
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
                lv_obj_set_style_text_color(rs485_debug_label, lv_color_make(255, 0, 0), 0);
            }
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

    // BME680 labels
    if (gas_label || press_label || humid_label || temp_label) {
        static bool bme_has_data = false;
        static char last_gas_text[60] = "";
        static char last_press_text[40] = "";
        static char last_humid_text[40] = "";
        static char last_temp_text[40] = "";

        if (snapshot.bme680.valid) {
            if (gas_label) {
                char buf[60];
                int gas_value = (int)roundf(snapshot.bme680.gas_kohm);
                snprintf(buf, sizeof(buf), "Q %d kOhm", gas_value);
                if (strcmp(buf, last_gas_text) != 0) {
                    lv_label_set_text(gas_label, buf);
                    strncpy(last_gas_text, buf, sizeof(last_gas_text) - 1);
                    last_gas_text[sizeof(last_gas_text) - 1] = '\0';
                }
            }
            if (press_label) {
                char buf[40];
                snprintf(buf, sizeof(buf), "%.1f hPa", snapshot.bme680.pressure_hpa);
                if (strcmp(buf, last_press_text) != 0) {
                    lv_label_set_text(press_label, buf);
                    strncpy(last_press_text, buf, sizeof(last_press_text) - 1);
                    last_press_text[sizeof(last_press_text) - 1] = '\0';
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
        } else if (missing_snapshot.bme680 >= RS485_MISSING_THRESHOLD && bme_has_data) {
            if (gas_label && strcmp(PLACEHOLDER_GAS, last_gas_text) != 0) {
                lv_label_set_text(gas_label, PLACEHOLDER_GAS);
                strncpy(last_gas_text, PLACEHOLDER_GAS, sizeof(last_gas_text) - 1);
                last_gas_text[sizeof(last_gas_text) - 1] = '\0';
            }
            if (press_label && strcmp(PLACEHOLDER_PRESSURE, last_press_text) != 0) {
                lv_label_set_text(press_label, PLACEHOLDER_PRESSURE);
                strncpy(last_press_text, PLACEHOLDER_PRESSURE, sizeof(last_press_text) - 1);
                last_press_text[sizeof(last_press_text) - 1] = '\0';
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

    // Gas bottle widgets
    if (gas_bottle_a || gas_bottle_b) {
        static bool gas_has_data = false;
        static float last_weight_a = NAN;
        static float last_weight_b = NAN;

        if (snapshot.hx711.valid && missing_snapshot.hx711 < RS485_MISSING_THRESHOLD) {
            if (gas_bottle_a) {
                if (!gas_has_data || isnan(last_weight_a) || fabsf(snapshot.hx711.weight_a_kg - last_weight_a) > 0.05f) {
                    womo_gas_bottle_update_weight(gas_bottle_a, snapshot.hx711.weight_a_kg);
                }
            }
            if (gas_bottle_b) {
                if (!gas_has_data || isnan(last_weight_b) || fabsf(snapshot.hx711.weight_b_kg - last_weight_b) > 0.05f) {
                    womo_gas_bottle_update_weight(gas_bottle_b, snapshot.hx711.weight_b_kg);
                }
            }
            last_weight_a = snapshot.hx711.weight_a_kg;
            last_weight_b = snapshot.hx711.weight_b_kg;
            gas_has_data = true;
        } else if (missing_snapshot.hx711 >= RS485_MISSING_THRESHOLD && gas_has_data) {
            if (gas_bottle_a) {
                womo_gas_bottle_set_no_data(gas_bottle_a);
            }
            if (gas_bottle_b) {
                womo_gas_bottle_set_no_data(gas_bottle_b);
            }
            gas_has_data = false;
            last_weight_a = NAN;
            last_weight_b = NAN;
        }
    }

    // Battery widgets
    if (main_battery || secondary_battery) {
        static bool battery_has_data = false;
        static float last_battery1 = NAN;
        static float last_battery2 = NAN;

        if (snapshot.battery.valid && missing_snapshot.battery < RS485_MISSING_THRESHOLD) {
            if (main_battery) {
                if (!battery_has_data || isnan(last_battery1) || fabsf(snapshot.battery.battery1_v - last_battery1) > 0.05f) {
                    womo_battery_set_voltage(main_battery, snapshot.battery.battery1_v);
                }
            }
            if (secondary_battery) {
                if (!battery_has_data || isnan(last_battery2) || fabsf(snapshot.battery.battery2_v - last_battery2) > 0.05f) {
                    womo_battery_set_voltage(secondary_battery, snapshot.battery.battery2_v);
                }
            }
            last_battery1 = snapshot.battery.battery1_v;
            last_battery2 = snapshot.battery.battery2_v;
            battery_has_data = true;
        } else if (missing_snapshot.battery >= RS485_MISSING_THRESHOLD && battery_has_data) {
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

        if (snapshot.tank.valid && missing_snapshot.tank < RS485_MISSING_THRESHOLD) {
            if (fresh_water_tank && (!tank_has_data || last_tank1 != snapshot.tank.tank1_percent)) {
                womo_tank_set_level(fresh_water_tank, snapshot.tank.tank1_percent);
            }
            if (grey_water_tank && (!tank_has_data || last_tank2 != snapshot.tank.tank2_percent)) {
                womo_tank_set_level(grey_water_tank, snapshot.tank.tank2_percent);
            }
            last_tank1 = snapshot.tank.tank1_percent;
            last_tank2 = snapshot.tank.tank2_percent;
            tank_has_data = true;
        } else if (missing_snapshot.tank >= RS485_MISSING_THRESHOLD && tank_has_data) {
            if (fresh_water_tank) {
                womo_tank_set_no_data(fresh_water_tank);
            }
            if (grey_water_tank) {
                womo_tank_set_no_data(grey_water_tank);
            }
            tank_has_data = false;
        }
    }
}

// Timer callback for updating time display
static void time_update_timer_cb(lv_timer_t *timer)
{
    char time_str[32];
    char date_str[32];
    
    // Get current time as string
    if (womo_time_get_string(time_str, sizeof(time_str), "%H:%M:%S") == ESP_OK) {
        lv_label_set_text(time_label, time_str);
    }
    
    // Get current date with localized weekday (Mo 04.11.2025)
    char weekday_en[16];
    char full_date[128];
    
    if (womo_time_get_string(weekday_en, sizeof(weekday_en), "%w") == ESP_OK &&
        womo_time_get_string(date_str, sizeof(date_str), "%d.%m.%Y") == ESP_OK) {
        int day_index = atoi(weekday_en);  // 0=Sunday, 1=Monday, ...
        const char* weekday_str = womo_locale_get_weekday(day_index);
        snprintf(full_date, sizeof(full_date), "%s  %s", weekday_str, date_str);
        lv_label_set_text(date_label, full_date);
    }
    
    // Update WiFi status (2 lines: RSSI first, then SSID)
    update_connectivity_label();
    
    // Update sensor data every 5 seconds (counter % 5 == 0)
    static uint32_t sensor_counter = 0;
    sensor_counter++;
    
    // Sensor data now comes via RS485 from Walter - no local sensor reading needed
    
    // Only update theme automatically if auto mode is enabled (but much less frequently!)
    // Update theme only every 30 seconds to reduce CPU load (30s is enough for smooth twilight)
    if (womo_theme_is_auto_mode() && (sensor_counter % 30 == 0)) {
        womo_theme_update(womo_theme_get_status());
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
    womo_weather_condition_t condition = WEATHER_UNKNOWN;

    if (weather_id >= 200 && weather_id <= 232) {
        condition = WEATHER_TSTORMS;
    } else if (weather_id >= 300 && weather_id <= 321) {
        condition = WEATHER_CHANCERAIN;
    } else if (weather_id >= 500 && weather_id <= 504) {
        condition = WEATHER_RAIN;
    } else if (weather_id == 511) {
        condition = WEATHER_SLEET;
    } else if (weather_id >= 520 && weather_id <= 531) {
        condition = WEATHER_CHANCERAIN;
    } else if (weather_id >= 600 && weather_id <= 602) {
        condition = WEATHER_SNOW;
    } else if (weather_id >= 611 && weather_id <= 616) {
        condition = WEATHER_SLEET;
    } else if (weather_id >= 620 && weather_id <= 622) {
        condition = WEATHER_FLURRIES;
    } else if (weather_id == 701 || weather_id == 741) {
        condition = WEATHER_FOG;
    } else if (weather_id == 711 || weather_id == 721 || weather_id == 731 ||
               weather_id == 751 || weather_id == 761 || weather_id == 762) {
        condition = WEATHER_HAZY;
    } else if (weather_id == 771 || weather_id == 781) {
        condition = WEATHER_TSTORMS;
    } else if (weather_id == 800) {
        condition = WEATHER_SUNNY;
    } else if (weather_id == 801) {
        condition = WEATHER_PARTLYCLOUDY;
    } else if (weather_id == 802) {
        condition = WEATHER_MOSTLYSUNNY;
    } else if (weather_id == 803) {
        condition = WEATHER_MOSTLYCLOUDY;
    } else if (weather_id == 804) {
        condition = WEATHER_CLOUDY;
    } else {
        condition = WEATHER_UNKNOWN;
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

    ESP_LOGI(TAG, "OpenWeatherMap update: id=%d (%s), %.1f°C, night=%s",
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

static void wifi_label_event_cb(lv_event_t *event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    bool wifi_connected = womo_wifi_is_connected();
    womo_wifi_status_t wifi_status = womo_wifi_get_status();
    uint8_t wifi_percent = 0;
    char ssid[33] = {0};

    if (wifi_connected) {
        int8_t rssi = womo_wifi_get_rssi();
        wifi_percent = wifi_rssi_to_percent(rssi);
        if (womo_wifi_get_ssid(ssid, sizeof(ssid)) != ESP_OK) {
            ssid[0] = '\0';
        }
    }

    womo_connectivity_snapshot_t snapshot;
    connectivity_snapshot_fill(&snapshot,
                               &lte_status,
                               wifi_percent,
                               ssid,
                               wifi_status,
                               wifi_connected);
    womo_connectivity_modal_show(lv_scr_act(), &snapshot);
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
        system_status_raise(SYSTEM_STATUS_SOURCE_RS485_WAITING,
                            WOMO_STATUS_WARNING,
                            STR_RS485_WAITING_HELLO);
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
        
        // Try to load background image from SD card
        load_background_image(screen);
        
        // Apply initial theme (screen background = full opacity for theme colors)
        womo_theme_update(WOMO_STATUS_OK);
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
    lv_obj_set_style_text_font(time_label, WOMO_FONT_LARGE, 0);
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
        lv_obj_set_style_text_align(wifi_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(wifi_label, LV_ALIGN_TOP_LEFT, 10, 10);
        lv_obj_add_flag(wifi_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(wifi_label, wifi_label_event_cb, LV_EVENT_CLICKED, NULL);
        update_connectivity_label();
        
        // Weather data (top right) - Gas first, all one font size larger
    char init_buf[40];

    air_title_label = lv_label_create(screen);
    lv_label_set_text(air_title_label, "Luftwerte aussen");
    lv_obj_set_style_text_font(air_title_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(air_title_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(air_title_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(air_title_label, LV_ALIGN_TOP_RIGHT, -10, 10);

    gas_label = lv_label_create(screen);
    snprintf(init_buf, sizeof(init_buf), "Q -- kOhm");
    lv_label_set_text(gas_label, init_buf);
    lv_obj_set_style_text_font(gas_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(gas_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(gas_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(gas_label, LV_ALIGN_TOP_RIGHT, -10, 35);

    press_label = lv_label_create(screen);
    snprintf(init_buf, sizeof(init_buf), "---- hPa");
    lv_label_set_text(press_label, init_buf);
    lv_obj_set_style_text_font(press_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(press_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(press_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(press_label, LV_ALIGN_TOP_RIGHT, -10, 60);

        humid_label = lv_label_create(screen);
    snprintf(init_buf, sizeof(init_buf), "--.-- %%");
        lv_label_set_text(humid_label, init_buf);
        lv_obj_set_style_text_font(humid_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(humid_label, lv_color_black(), 0);
        lv_obj_set_style_text_align(humid_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(humid_label, LV_ALIGN_TOP_RIGHT, -10, 85);

        temp_label = lv_label_create(screen);
    snprintf(init_buf, sizeof(init_buf), "--.- °C");
        lv_label_set_text(temp_label, init_buf);
        lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(temp_label, lv_color_black(), 0);
        lv_obj_set_style_text_align(temp_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(temp_label, LV_ALIGN_TOP_RIGHT, -10, 110);

    // IMU-Werte als freistehende Labels (ca. 40% von oben, 30% vom rechten Rand)
    lv_coord_t disp_w = lv_disp_get_hor_res(NULL);
    lv_coord_t disp_h = lv_disp_get_ver_res(NULL);
    if (disp_w <= 0) {
        disp_w = 800;
    }
    if (disp_h <= 0) {
        disp_h = 480;
    }

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

    // GPS label (erstelle später, wenn nötig)
    gps_label = NULL;

    // Create water tank widgets (positioned above the gas bottles)
    fresh_water_tank = womo_tank_create(screen, 65, 110, WOMO_TANK_FRESH);
    if (fresh_water_tank) {
        womo_tank_set_caption(fresh_water_tank, "");
        womo_tank_set_no_data(fresh_water_tank);
        fresh_water_caption_label = lv_label_create(screen);
        lv_label_set_text(fresh_water_caption_label, "Frischwasser");
        lv_obj_set_style_text_font(fresh_water_caption_label, &lv_font_montserrat_12, 0);
        lv_obj_align_to(fresh_water_caption_label, fresh_water_tank->container, LV_ALIGN_OUT_BOTTOM_MID, 0, -21);
    } else {
        ESP_LOGW(TAG, "Failed to create fresh water tank widget");
    }

    grey_water_tank = womo_tank_create(screen, 135, 110, WOMO_TANK_GREY);
    if (grey_water_tank) {
        womo_tank_set_caption(grey_water_tank, "");
        womo_tank_set_no_data(grey_water_tank);
        grey_water_caption_label = lv_label_create(screen);
        lv_label_set_text(grey_water_caption_label, "Grauwasser");
        lv_obj_set_style_text_font(grey_water_caption_label, &lv_font_montserrat_12, 0);
        lv_obj_align_to(grey_water_caption_label, grey_water_tank->container, LV_ALIGN_OUT_BOTTOM_MID, 0, -21);
    } else {
        ESP_LOGW(TAG, "Failed to create grey water tank widget");
    }

    // Create gas bottle widgets (20px weiter nach links)
    // Display height is 480px, so 480 - 250 = 230px from top
    gas_bottle_a = womo_gas_bottle_create(screen, 70, 230);  // Gas bottle A (90 - 20)
    gas_bottle_b = womo_gas_bottle_create(screen, 140, 230);  // Gas bottle B (160 - 20)
    
    // Set weights for bottles: 10.1 kg = 0%, 21 kg = 100%
    if (gas_bottle_a) {
        womo_gas_bottle_set_empty_weight(gas_bottle_a, 10.1f);  // Empty bottle (0%)
        womo_gas_bottle_set_full_weight(gas_bottle_a, 21.0f);   // Full bottle (100%)
        womo_gas_bottle_set_cap_label(gas_bottle_a, "V");
    }
    if (gas_bottle_b) {
        womo_gas_bottle_set_empty_weight(gas_bottle_b, 10.1f);  // Empty bottle (0%)
        womo_gas_bottle_set_full_weight(gas_bottle_b, 21.0f);   // Full bottle (100%)
        womo_gas_bottle_set_cap_label(gas_bottle_b, "H");
    }
    
    // Create weather widget in top-right corner (icon sits beneath the weather values)
    weather_widget = womo_weather_create(screen);
    if (weather_widget) {
        // Set demo weather condition (cloudy with rain chance)
    bool theme_is_day = womo_theme_is_daytime();
    womo_weather_set_condition(weather_widget, WEATHER_CHANCERAIN, !theme_is_day);
        womo_weather_set_temperature(weather_widget, 18);  // 18°C demo temperature
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

        // Position: 4/5 der Bildschirmbreite und 3/4 der Bildschirmhöhe (zentriert auf den Widgetmaßen)
        lv_coord_t car_width = lv_obj_get_width(main_battery->container);
        lv_coord_t car_height = lv_obj_get_height(main_battery->container);
        lv_coord_t car_x = (lv_coord_t)((disp_w * 4) / 5) - (car_width / 2);
        lv_coord_t car_y = (lv_coord_t)((disp_h * 3) / 4) - (car_height / 2);
        if (car_x < 0) {
            car_x = 0;
        }
        if ((car_x + car_width) > disp_w) {
            car_x = disp_w - car_width;
        }
        if (car_y < 0) {
            car_y = 0;
        }
        if ((car_y + car_height) > disp_h) {
            car_y = disp_h - car_height;
        }
        womo_battery_set_pos(main_battery, car_x, car_y);
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

        // Position: 1/3 der Bildschirmbreite und 2/3 der Bildschirmhöhe (zentriert)
        lv_coord_t omo_width = lv_obj_get_width(secondary_battery->container);
        lv_coord_t omo_height = lv_obj_get_height(secondary_battery->container);
        lv_coord_t omo_x = (lv_coord_t)(disp_w / 3) - (omo_width / 2);
        lv_coord_t omo_y = (lv_coord_t)((disp_h * 2) / 3) - (omo_height / 2);
        if (omo_x < 0) {
            omo_x = 0;
        }
        if (omo_x + omo_width > disp_w) {
            omo_x = disp_w - omo_width;
        }
        if (omo_y < 0) {
            omo_y = 0;
        }
        if (omo_y + omo_height > disp_h) {
            omo_y = disp_h - omo_height - battery_bottom_margin;
        }

        womo_battery_set_pos(secondary_battery, omo_x, omo_y);
        ESP_LOGI(TAG, "Battery 2 widget created successfully");
    } else {
        ESP_LOGW(TAG, "Failed to create battery 2 widget");
    }
    
    // RS485 Debug label (bottom left, above mode label)
    rs485_debug_label = lv_label_create(screen);
    lv_label_set_text(rs485_debug_label, womo_locale_get_string(STR_RS485_WAITING));
    lv_obj_set_style_text_font(rs485_debug_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rs485_debug_label, lv_color_make(255, 0, 0), 0); // Red for visibility
    lv_obj_align(rs485_debug_label, LV_ALIGN_BOTTOM_LEFT, 20, -60);
        
        // Mode label removed - theme now fully automatic based on real time
        
        // Create status label (right bottom - higher to avoid FPS overlay)
        status_label = lv_label_create(screen);
        status_label_last_text[0] = '\0';
        system_status_apply(true);
        lv_obj_set_style_text_color(status_label, lv_color_black(), 0);
        lv_obj_align(status_label, LV_ALIGN_BOTTOM_LEFT, 553, -10);  // 20px weiter rechts: 533+20 = 553px, bottom -10px
        
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

    // Connect to WiFi in background
    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);
    if (womo_wifi_connect(WIFI_SSID, WIFI_PASSWORD, WIFI_MAX_RETRY) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected successfully");
        
        esp_err_t weather_err = womo_weather_http_start(openweather_update_cb, NULL);
        if (weather_err != ESP_OK) {
            ESP_LOGW(TAG, "Online weather updates disabled: %s", esp_err_to_name(weather_err));
        }

        // Sync time via NTP (non-blocking)
        if (womo_time_sync_ntp(true) == ESP_OK) {
            ESP_LOGI(TAG, "Time synchronized via NTP");
        } else {
            ESP_LOGW(TAG, "NTP sync failed, using internal RTC");
        }
    } else {
        ESP_LOGW(TAG, "WiFi connection failed");
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
        system_status_clear(SYSTEM_STATUS_SOURCE_RS485_WAITING);
        ESP_LOGI(TAG, "RS485 handshake abgeschlossen (erstes Datenpaket)");
    }

    if (rs485_invalid_data_active) {
        rs485_invalid_data_active = false;
        system_status_clear(SYSTEM_STATUS_SOURCE_RS485_INVALID);
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

    // LTE status
    if (snapshot.lte.valid) {
        if (rs485_missing_counter.lte > 0) {
            ESP_LOGI(TAG, "RS485: LTE data restored after %u missing packet(s)", rs485_missing_counter.lte);
        }
        rs485_missing_counter.lte = 0;

        lte_status.valid = true;
        lte_status.registered = snapshot.lte.registered;
        lte_status.rsrp_dbm = snapshot.lte.rsrp_dbm;
        lte_status.signal_percent = snapshot.lte.signal_percent;
        if (snapshot.lte.operator_name[0] != '\0') {
            snprintf(lte_status.operator_name, sizeof(lte_status.operator_name), "%s", snapshot.lte.operator_name);
        } else {
            lte_status.operator_name[0] = '\0';
        }
    } else {
        if (rs485_missing_counter.lte == 0) {
            ESP_LOGW(TAG, "RS485: LTE payload missing (packet %lu)", rs485_packet_count);
        }
        if (rs485_missing_counter.lte < UINT16_MAX) {
            rs485_missing_counter.lte++;
        }
        if (rs485_missing_counter.lte >= RS485_MISSING_THRESHOLD && lte_status.valid) {
            lte_status.valid = false;
            lte_status.registered = false;
            lte_status.operator_name[0] = '\0';
            lte_status.signal_percent = 0;
            ESP_LOGW(TAG, "RS485 LTE: keine Daten mehr (>= %u fehlende Pakete)", RS485_MISSING_THRESHOLD);
        }
    }

    if (lte_status.valid) {
        snapshot.lte.valid = true;
        snapshot.lte.registered = lte_status.registered;
        snapshot.lte.signal_percent = lte_status.signal_percent;
        snapshot.lte.rsrp_dbm = lte_status.rsrp_dbm;
        if (lte_status.operator_name[0] != '\0') {
            snprintf(snapshot.lte.operator_name, sizeof(snapshot.lte.operator_name), "%s", lte_status.operator_name);
        } else {
            snapshot.lte.operator_name[0] = '\0';
        }
    } else {
        snapshot.lte.valid = false;
        snapshot.lte.registered = false;
        snapshot.lte.signal_percent = 0;
        snapshot.lte.rsrp_dbm = 0;
        snapshot.lte.operator_name[0] = '\0';
    }

    // HX711 weights
    if (snapshot.hx711.valid) {
        if (rs485_missing_counter.hx711 > 0) {
            ESP_LOGI(TAG, "RS485: HX711 data restored after %u missing packet(s)",
                     rs485_missing_counter.hx711);
        }
        rs485_missing_counter.hx711 = 0;
    } else {
        if (rs485_missing_counter.hx711 == 0) {
            ESP_LOGW(TAG, "RS485: HX711 payload missing (packet %lu)", rs485_packet_count);
        }
        if (rs485_missing_counter.hx711 < UINT16_MAX) {
            rs485_missing_counter.hx711++;
        }
        if (rs485_missing_counter.hx711 == RS485_MISSING_THRESHOLD) {
            ESP_LOGW(TAG, "RS485: HX711 data missing twice - UI will show no data");
        }
    }

    // BME680 environmental data
    if (snapshot.bme680.valid) {
        if (rs485_missing_counter.bme680 > 0) {
            ESP_LOGI(TAG, "RS485: BME680 data restored after %u missing packet(s)",
                     rs485_missing_counter.bme680);
        }
        rs485_missing_counter.bme680 = 0;
    } else {
        if (rs485_missing_counter.bme680 == 0) {
            ESP_LOGW(TAG, "RS485: BME680 payload missing (packet %lu)", rs485_packet_count);
        }
        if (rs485_missing_counter.bme680 < UINT16_MAX) {
            rs485_missing_counter.bme680++;
        }
        if (rs485_missing_counter.bme680 == RS485_MISSING_THRESHOLD) {
            ESP_LOGW(TAG, "RS485: BME680 data missing twice - UI will show placeholders");
        }
    }

    // Battery data
    if (snapshot.battery.valid) {
        if (rs485_missing_counter.battery > 0) {
            ESP_LOGI(TAG, "RS485: Battery data restored after %u missing packet(s)",
                     rs485_missing_counter.battery);
        }
        rs485_missing_counter.battery = 0;
    } else {
        if (rs485_missing_counter.battery == 0) {
            ESP_LOGW(TAG, "RS485: Battery payload missing (packet %lu)", rs485_packet_count);
        }
        if (rs485_missing_counter.battery < UINT16_MAX) {
            rs485_missing_counter.battery++;
        }
        if (rs485_missing_counter.battery == RS485_MISSING_THRESHOLD) {
            ESP_LOGW(TAG, "RS485: Battery data missing twice - UI will show no data");
        }
    }

    // Tank data
    if (snapshot.tank.valid) {
        if (rs485_missing_counter.tank > 0) {
            ESP_LOGI(TAG, "RS485: Tank data restored after %u missing packet(s)",
                     rs485_missing_counter.tank);
        }
        rs485_missing_counter.tank = 0;
    } else {
        if (rs485_missing_counter.tank == 0) {
            ESP_LOGW(TAG, "RS485: Tank payload missing (packet %lu)", rs485_packet_count);
        }
        if (rs485_missing_counter.tank < UINT16_MAX) {
            rs485_missing_counter.tank++;
        }
        if (rs485_missing_counter.tank == RS485_MISSING_THRESHOLD) {
            ESP_LOGW(TAG, "RS485: Tank data missing twice - UI will show no data");
        }
    }

    taskENTER_CRITICAL(&display_data_spinlock);
    latest_sensor_data = snapshot;
    latest_packet_count = rs485_packet_count;
    latest_missing_snapshot = rs485_missing_counter;
    latest_data_valid = true;
    taskEXIT_CRITICAL(&display_data_spinlock);
}

