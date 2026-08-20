/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * RS485 Sensor-Link – Topic-basierte Kommunikation zum Display
 *
 * Basiert auf archive/firmware-modem rs485_modem.c (Stand Feb 2026).
 * Modem-spezifischer Code (LTE, WiFi-AP, USB-CDC, PPP, AT-Kommandos)
 * wurde entfernt.  Alle Sensor-Publisher (bme, hx, gas, bat, tank, imu, ctrl)
 * und die Gas-Verbrauchslogik sind vollständig portiert.
 */

#include "network/espnow_modem.h"

#include "network/espnow_transport.h"
#include "network/ruuvi_ble_scanner.h"
#include "sensors/bno055_sensor.h"
#include "network/wifi/sensor_wifi.h"

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor_config.h"
#include "hal/deep_sleep.h"
#include "sensors/bme680_sensor.h"
#include "sensors/hx711_sensor.h"
#include "sensors/ina226_sensor.h"
#include "sensors/analog_sensor.h"
#include "time/time_sync.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define TAG "espnow_modem"

// ── Protokollparameter ──────────────────────────────────────────────────
#define RS485_LINE_MAX                4096
#define RS485_MAX_PENDING_CMDS        WOMO_SENSOR_MAX_PENDING_CMDS
#define RS485_COMMAND_TIMEOUT_MS      WOMO_SENSOR_COMMAND_TIMEOUT_MS

// Heartbeat / Handshake Intervalle

// ── Pending Command Tracking ────────────────────────────────────────────
typedef struct {
    bool in_use;
    bool warned;
    uint32_t seq;
    int64_t sent_us;
    char label[32];
} rs485_pending_cmd_t;

static rs485_pending_cmd_t s_pending[RS485_MAX_PENDING_CMDS];
static portMUX_TYPE s_pending_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_seq_lock     = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_tx_seq            = 1;
static uint32_t s_last_rx_seq       = 0;
static uint32_t s_last_ack_seq      = 0;
static int64_t  s_last_ack_time_us  = 0;
static bool     s_display_ready     = false;
static uint32_t s_display_ready_seen = 0;
static TaskHandle_t s_tx_task       = NULL;

// ── Forward-Deklarationen ───────────────────────────────────────────────

static void rs485_publish_ctrl(void);
static void rs485_publish_imu(void);
static void rs485_publish_bat(void);
static void rs485_publish_tank(void);
static void rs485_publish_hx(void);
static void rs485_publish_gas(void);
static void rs485_publish_bme_in(void);
static void rs485_publish_bme_out(void);
static void rs485_publish_elec(void);

// ── Hilfsfunktionen ─────────────────────────────────────────────────────

static const char *heading_to_compass(float heading_deg)
{
    static const char *dirs[] = {
        "N", "NNO", "NO", "ONO", "O", "OSO", "SO", "SSO",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    while (heading_deg < 0.0f) heading_deg += 360.0f;
    while (heading_deg >= 360.0f) heading_deg -= 360.0f;
    int index = (int)((heading_deg + 11.25f) / 22.5f) % 16;
    return dirs[index];
}

static inline int adc_mv_to_percent_cal(int mv, int empty_mv, int full_mv)
{
    // Kanalbezogene Tank-Kalibrierung: EMPTY_MV..FULL_MV => 0..100%
    if (full_mv <= empty_mv) {
        return 0;
    }
    if (mv <= empty_mv) {
        return 0;
    }
    if (mv >= full_mv) {
        return 100;
    }
    return ((mv - empty_mv) * 100) / (full_mv - empty_mv);
}

static int64_t rs485_epoch_ms(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0) {
        return ((int64_t)tv.tv_sec * 1000) + (tv.tv_usec / 1000);
    }
    return esp_timer_get_time() / 1000;
}

static double round2(double v)
{
    return nearbyint(v * 100.0) / 100.0;
}

static double ts_us_to_epoch_ms(int64_t ts_us)
{
    if (ts_us <= 0) {
        return 0.0;
    }
    int64_t now_us = esp_timer_get_time();
    double now_ms  = (double)rs485_epoch_ms();
    double delta_ms = (double)(now_us - ts_us) / 1000.0;
    if (delta_ms < 0.0) {
        delta_ms = 0.0;
    }
    return round2(now_ms - delta_ms);
}

// ── Sequence Tracking ───────────────────────────────────────────────────

static uint32_t rs485_next_seq(void)
{
    taskENTER_CRITICAL(&s_seq_lock);
    uint32_t seq = s_tx_seq++;
    if (s_tx_seq == 0) {
        s_tx_seq = 1;
    }
    taskEXIT_CRITICAL(&s_seq_lock);
    return seq == 0 ? rs485_next_seq() : seq;
}

static void rs485_record_rx_seq(uint32_t seq)
{
    if (seq == 0) return;
    taskENTER_CRITICAL(&s_seq_lock);
    s_last_rx_seq = seq;
    taskEXIT_CRITICAL(&s_seq_lock);
}

// ── Pending Command Management ──────────────────────────────────────────

static bool rs485_register_pending(uint32_t seq, const char *label)
{
    taskENTER_CRITICAL(&s_pending_lock);
    for (size_t i = 0; i < RS485_MAX_PENDING_CMDS; ++i) {
        if (!s_pending[i].in_use) {
            s_pending[i].in_use  = true;
            s_pending[i].warned  = false;
            s_pending[i].seq     = seq;
            s_pending[i].sent_us = esp_timer_get_time();
            strlcpy(s_pending[i].label, label ? label : "?", sizeof(s_pending[i].label));
            taskEXIT_CRITICAL(&s_pending_lock);
            return true;
        }
    }
    taskEXIT_CRITICAL(&s_pending_lock);
    ESP_LOGW(TAG, "No pending slot for seq=%u label=%s", (unsigned)seq, label ? label : "?");
    return false;
}

static void rs485_resolve_pending(uint32_t seq, bool success, const char *cmd, const char *err_str)
{
    taskENTER_CRITICAL(&s_pending_lock);
    for (size_t i = 0; i < RS485_MAX_PENDING_CMDS; ++i) {
        if (s_pending[i].in_use && s_pending[i].seq == seq) {
            s_pending[i].in_use = false;
            taskEXIT_CRITICAL(&s_pending_lock);
            if (success) {
                ESP_LOGI(TAG, "ACK OK seq=%u cmd=%s", (unsigned)seq, cmd ? cmd : s_pending[i].label);
            } else {
                ESP_LOGW(TAG, "ACK FAIL seq=%u cmd=%s err=%s", (unsigned)seq, cmd ? cmd : "?", err_str ? err_str : "?");
            }
            return;
        }
    }
    taskEXIT_CRITICAL(&s_pending_lock);
}

static void rs485_check_command_timeouts(void)
{
    int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_pending_lock);
    for (size_t i = 0; i < RS485_MAX_PENDING_CMDS; ++i) {
        if (s_pending[i].in_use) {
            int64_t age_ms = (now - s_pending[i].sent_us) / 1000;
            if (age_ms > RS485_COMMAND_TIMEOUT_MS) {
                if (!s_pending[i].warned) {
                    s_pending[i].warned = true;
                    taskEXIT_CRITICAL(&s_pending_lock);
                    ESP_LOGW(TAG, "CMD timeout seq=%u label=%s (%lld ms)",
                             (unsigned)s_pending[i].seq, s_pending[i].label, (long long)age_ms);
                    taskENTER_CRITICAL(&s_pending_lock);
                }
                if (age_ms > RS485_COMMAND_TIMEOUT_MS * 3) {
                    s_pending[i].in_use = false;
                }
            }
        }
    }
    taskEXIT_CRITICAL(&s_pending_lock);
}

// ── Frame Senden ────────────────────────────────────────────────────────

static esp_err_t rs485_send_frame(const char *label, cJSON *payload, bool need_ack)
{
    if (!payload) return ESP_ERR_INVALID_STATE;

    uint32_t seq = rs485_next_seq();
    cJSON_AddNumberToObject(payload, "seq", seq);
    if (need_ack) {
        cJSON_AddBoolToObject(payload, "need_ack", true);
    }

    char *json = cJSON_PrintUnformatted(payload);
    if (!json) return ESP_ERR_NO_MEM;

    size_t len = strlen(json);
    esp_err_t err = espnow_transport_send(json, len);
    cJSON_free(json);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TX %s seq=%u fehlgeschlagen: %s", label, (unsigned)seq, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "TX %s seq=%u len=%u", label, (unsigned)seq, (unsigned)len);

    if (need_ack) {
        rs485_register_pending(seq, label);
    }
    return ESP_OK;
}

static esp_err_t rs485_send_ack(uint32_t ack_seq, bool success, const char *label, const char *error)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(root, "type", "cmd_ack");
    cJSON_AddNumberToObject(root, "ack_seq", ack_seq);
    cJSON_AddStringToObject(root, "status", success ? "ok" : "error");
    if (label && label[0]) cJSON_AddStringToObject(root, "cmd", label);
    if (!success && error && error[0]) cJSON_AddStringToObject(root, "err", error);
    esp_err_t err = rs485_send_frame("ack", root, false);
    cJSON_Delete(root);
    return err;
}

// ── Hello / Heartbeat ───────────────────────────────────────────────────


// ── WiFi Passwort-Anfrage ans Display ─────────────────────────────────
esp_err_t espnow_modem_request_wifi_pass(void)
{
    ESP_LOGI(TAG, "→ Frage Display nach WiFi-Passwort...");
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(root, "type", "wifi_pass_request");
    cJSON_AddStringToObject(root, "cmd", "wifi_pass_request");
    esp_err_t err = rs485_send_frame("wifi_pass_request", root, true);
    cJSON_Delete(root);
    return err;
}

// Void-Wrapper für den Auth-Failure-Callback (erwartet void(void))
static void rs485_wifi_auth_fail_cb(void)
{
    espnow_modem_request_wifi_pass();
}


// ── ACK Handling ────────────────────────────────────────────────────────

static void rs485_handle_ack_packet(const cJSON *root)
{
    const cJSON *ack_seq_obj = cJSON_GetObjectItem(root, "ack_seq");
    if (!cJSON_IsNumber(ack_seq_obj)) return;
    uint32_t ack_seq = (uint32_t)ack_seq_obj->valuedouble;

    const cJSON *status = cJSON_GetObjectItem(root, "status");
    const char *status_str = (cJSON_IsString(status) && status->valuestring) ? status->valuestring : "ok";
    bool success = (strcmp(status_str, "ok") == 0);
    const cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    const char *cmd_str = (cJSON_IsString(cmd) && cmd->valuestring) ? cmd->valuestring : NULL;
    const cJSON *err = cJSON_GetObjectItem(root, "err");
    const char *err_str = (cJSON_IsString(err) && err->valuestring) ? err->valuestring : NULL;

    rs485_resolve_pending(ack_seq, success, cmd_str, err_str);

    taskENTER_CRITICAL(&s_seq_lock);
    s_last_ack_seq     = ack_seq;
    s_last_ack_time_us = esp_timer_get_time();
    taskEXIT_CRITICAL(&s_seq_lock);
}

static void rs485_handle_display_ready(void)
{
    s_display_ready = true;
    s_display_ready_seen++;
    ESP_LOGI(TAG, "Display bereit (count=%u)", (unsigned)s_display_ready_seen);
    
    // Initial-Burst: Alle Topics sofort einmal senden
    ESP_LOGI(TAG, "Sende initiale Sensor-Daten...");
    rs485_publish_ctrl();
    vTaskDelay(pdMS_TO_TICKS(20));
    rs485_publish_imu();
    vTaskDelay(pdMS_TO_TICKS(20));
    rs485_publish_bat();
    vTaskDelay(pdMS_TO_TICKS(20));
    rs485_publish_tank();
    vTaskDelay(pdMS_TO_TICKS(20));
    rs485_publish_hx();
    vTaskDelay(pdMS_TO_TICKS(20));
    rs485_publish_gas();
    vTaskDelay(pdMS_TO_TICKS(20));
    rs485_publish_bme_in();
    vTaskDelay(pdMS_TO_TICKS(20));
    rs485_publish_bme_out();
    vTaskDelay(pdMS_TO_TICKS(20));
    rs485_publish_elec();
    ESP_LOGI(TAG, "Initiale Sensor-Daten gesendet");
}

// ── Power / GPIO ────────────────────────────────────────────────────────

static bool s_radio_on = false;
static bool s_pwr_on_state = true;   /* Soft-Flag: beim Boot=true, kein GPIO14-Sense nötig */
static bool s_power_gpio_inited = false;
static volatile bool s_shutdown_pending = false;

// Gas-Verbrauchsstate (mit NVS-Persistenz)
typedef struct {
    double last_net_a;
    double last_net_b;
    int64_t last_ts_us;
    double rate1h;
    double rate2h;
    float ref_full_a;   // Gewicht bei Flaschenwechsel Kanal A (0 = nicht gesetzt → Fallback auf TARE)
    float ref_full_b;   // Gewicht bei Flaschenwechsel Kanal B (0 = nicht gesetzt → Fallback auf TARE)
} gas_state_t;

static gas_state_t s_gas_state = {0};
static int s_gas_active_override = SENSOR_GAS_ACTIVE_DEFAULT; // -1 = auto
static bool s_ref_pending_a = false;  // nächste HX711-Messung als Voll-Referenz A speichern
static bool s_ref_pending_b = false;  // nächste HX711-Messung als Voll-Referenz B speichern

#define GAS_NVS_NAMESPACE "gas_state"
#define GAS_NVS_KEY       "gas"

static void gas_state_load_from_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(GAS_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    size_t size = sizeof(gas_state_t);
    nvs_get_blob(handle, GAS_NVS_KEY, &s_gas_state, &size);
    nvs_close(handle);
}

static void gas_state_save_to_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(GAS_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_blob(handle, GAS_NVS_KEY, &s_gas_state, sizeof(gas_state_t));
    nvs_commit(handle);
    nvs_close(handle);
}

// Tank-Verbrauchsstate (mit NVS-Persistenz)
// Frischwasser: Verbrauch → rest_h = wie lange hält der Vorrat
// Grauwasser: Füllrate → rest_h = wie lange bis voll
typedef struct {
    double last_liters;      // Letzter Füllstand in Litern
    int64_t last_ts_us;      // Zeitstempel der letzten Messung
    double rate1h;           // L/h über 1h Fenster (negativ=Verbrauch, positiv=Füllung)
    double rate2h;           // L/h über 2h Fenster
} tank_state_t;

static tank_state_t s_tank1_state = {0}; // Frischwasser (100L)
static tank_state_t s_tank2_state = {0}; // Grauwasser (92L)

#define TANK_NVS_NAMESPACE "tank_state"
#define TANK1_NVS_KEY "tank1"
#define TANK2_NVS_KEY "tank2"

static void tank_state_load_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(TANK_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return;
    }

    size_t size = sizeof(tank_state_t);
    nvs_get_blob(handle, TANK1_NVS_KEY, &s_tank1_state, &size);
    size = sizeof(tank_state_t);
    nvs_get_blob(handle, TANK2_NVS_KEY, &s_tank2_state, &size);
    nvs_close(handle);
}

static void tank_state_save_to_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(TANK_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return;
    }

    nvs_set_blob(handle, TANK1_NVS_KEY, &s_tank1_state, sizeof(tank_state_t));
    nvs_set_blob(handle, TANK2_NVS_KEY, &s_tank2_state, sizeof(tank_state_t));
    nvs_commit(handle);
    nvs_close(handle);
}

static void rs485_power_gpio_init(void)
{
    if (s_power_gpio_inited) return;

    gpio_config_t out_cfg = {
        .pin_bit_mask = BIT64(SENSOR_PWR_12V_ON_GPIO) | BIT64(SENSOR_PWR_12V_OFF_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);
    gpio_set_level(SENSOR_PWR_12V_ON_GPIO, 0);
    gpio_set_level(SENSOR_PWR_12V_OFF_GPIO, 0);

    gpio_config_t in_cfg = {
        .pin_bit_mask = BIT64(SENSOR_PWR_12V_SENSE_GPIO) | BIT64(SENSOR_AC_SENSE_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);

    s_power_gpio_inited = true;
}

static bool rs485_read_gpio_level(gpio_num_t gpio, bool fallback)
{
    if (!s_power_gpio_inited) rs485_power_gpio_init();
    int level = gpio_get_level(gpio);
    return (level < 0) ? fallback : (level != 0);
}

static bool rs485_board_power_on(void)
{
    return s_pwr_on_state;
}

static bool rs485_ac_present(void)
{
    return rs485_read_gpio_level(SENSOR_AC_SENSE_GPIO, false);
}

// 12V Bordnetz ein-/ausschalten per GPIO-Puls (bistabiles Relais, 2 s Puls)
static esp_err_t rs485_set_12v_power(bool enable)
{
    if (!s_power_gpio_inited) rs485_power_gpio_init();
    gpio_num_t pin = enable ? SENSOR_PWR_12V_ON_GPIO : SENSOR_PWR_12V_OFF_GPIO;
    gpio_set_level(pin, 1);
    vTaskDelay(pdMS_TO_TICKS(100));  // 100 ms Puls für bistabiles Relais
    gpio_set_level(pin, 0);

    // Bei 12V AUS wird Radio zwangsläufig abgeschaltet
    if (!enable && s_radio_on) {
        s_radio_on = false;
        ESP_LOGI(TAG, "Radio forced off (12V disabled)");
    }

    ESP_LOGI(TAG, "12V power %s (GPIO%d pulsed)", enable ? "ON" : "OFF", (int)pin);
    return ESP_OK;
}

// Radio/Multimedia ein-/ausschalten (nur wenn 12V aktiv)
static esp_err_t rs485_set_radio(bool enable)
{
    if (enable && !rs485_board_power_on()) {
        ESP_LOGW(TAG, "Radio ON rejected: 12V not active");
        return ESP_ERR_INVALID_STATE;
    }
    s_radio_on = enable;
    gpio_set_level(SENSOR_MULTIMEDIA_PWR_GPIO, enable ? 1 : 0); /* N-MOSFET: HIGH=EIN, LOW=AUS */
    ESP_LOGI(TAG, "Radio %s (GPIO%d=%d)", enable ? "ON" : "OFF", SENSOR_MULTIMEDIA_PWR_GPIO, enable ? 1 : 0);
    return ESP_OK;
}

// ── Topic Publishers ────────────────────────────────────────────────────

static volatile bool s_ctrl_immediate = false;

static void rs485_publish_ctrl(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "type", "ctrl");
    cJSON_AddBoolToObject(root, "pwr_on", rs485_board_power_on());
    cJSON_AddBoolToObject(root, "radio_on", s_radio_on);
    cJSON_AddBoolToObject(root, "ac_present", rs485_ac_present());

    // RTC-Batteriestatus
    time_sync_status_t ts_status = {0};
    if (time_sync_get_status(&ts_status) == ESP_OK) {
        cJSON_AddBoolToObject(root, "rtc_bat_low",      ts_status.rtc_battery_low);
        cJSON_AddBoolToObject(root, "rtc_bat_switched", ts_status.rtc_bat_switched);
    }

    // Diagnose-Felder (ehemals hb-Topic)
    int64_t now_us = esp_timer_get_time();
    cJSON_AddNumberToObject(root, "uptime", round2((double)now_us / 1000000.0));
    cJSON_AddNumberToObject(root, "heap",   (double)esp_get_free_heap_size());

    rs485_send_frame("ctrl", root, false);
    cJSON_Delete(root);
}

static void rs485_publish_imu(void)
{
    bno055_imu_snapshot_t imu = {0};
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "type", "imu");

    if (bno055_imu_get_snapshot(&imu) && imu.valid) {
        cJSON_AddNumberToObject(root, "yaw_deg", round2(imu.yaw_deg));
        cJSON_AddNumberToObject(root, "pitch_deg", round2(imu.pitch_deg));
        cJSON_AddNumberToObject(root, "roll_deg", round2(imu.roll_deg));
        cJSON_AddStringToObject(root, "hdg", heading_to_compass(imu.yaw_deg));
        cJSON *cal = cJSON_CreateObject();
        cJSON_AddNumberToObject(cal, "sys", imu.cal_sys);
        cJSON_AddNumberToObject(cal, "gyro", imu.cal_gyro);
        cJSON_AddNumberToObject(cal, "acc", imu.cal_accel);
        cJSON_AddNumberToObject(cal, "mag", imu.cal_mag);
        cJSON_AddItemToObject(root, "cal", cal);
        cJSON_AddBoolToObject(root, "calibrated", imu.calibrated);
        cJSON_AddNumberToObject(root, "ts", ts_us_to_epoch_ms(imu.timestamp_us));
    } else {
        cJSON_AddNumberToObject(root, "yaw_deg", 0.0);
        cJSON_AddNumberToObject(root, "pitch_deg", 0.0);
        cJSON_AddNumberToObject(root, "roll_deg", 0.0);
        cJSON_AddStringToObject(root, "hdg", "N");
        cJSON *cal = cJSON_CreateObject();
        cJSON_AddNumberToObject(cal, "sys", 0);
        cJSON_AddNumberToObject(cal, "gyro", 0);
        cJSON_AddNumberToObject(cal, "acc", 0);
        cJSON_AddNumberToObject(cal, "mag", 0);
        cJSON_AddItemToObject(root, "cal", cal);
        cJSON_AddBoolToObject(root, "calibrated", false);
    }
    rs485_send_frame("imu", root, false);
    cJSON_Delete(root);
}

static void rs485_publish_bme_in(void)
{
    bme680_snapshot_t bme = {0};
    bme680_app_get_snapshot(&bme);
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "type", "bme_in");
    if (bme.indoor.valid) {
        const char *chip = bme.indoor.chip == BME_CHIP_BME680 ? "bme680" :
                           bme.indoor.chip == BME_CHIP_BME280 ? "bme280" :
                           bme.indoor.chip == BME_CHIP_BME260 ? "bme260" : "unknown";
        cJSON_AddStringToObject(root, "chip", chip);
        cJSON_AddNumberToObject(root, "addr", bme.indoor_addr);
        cJSON_AddNumberToObject(root, "temp_c", round2(bme.indoor.temperature_c));
        cJSON_AddNumberToObject(root, "rh_pct", round2(bme.indoor.humidity_pct));
        cJSON_AddNumberToObject(root, "press_hpa", round2(bme.indoor.pressure_hpa));
        if (bme.indoor.gas_valid)
            cJSON_AddNumberToObject(root, "gas_kohm", round2(bme.indoor.gas_kohm));
        if (bme.indoor.iaq_valid) {
            cJSON_AddNumberToObject(root, "iaq", round2(bme.indoor.iaq));
            cJSON_AddNumberToObject(root, "iaq_acc", bme.indoor.iaq_accuracy);
            cJSON_AddNumberToObject(root, "eco2_ppm", round2(bme.indoor.eco2_ppm));
            cJSON_AddNumberToObject(root, "bvoc_ppm", round2(bme.indoor.bvoc_ppm));
        }
        cJSON_AddNumberToObject(root, "ts", ts_us_to_epoch_ms(bme.indoor.timestamp_us));
    } else {
        // Kein Kabel-Sensor → Ruuvi Innen-Tag als Fallback
        ruuvi_snapshot_t ruuvi = {0};
        ruuvi_ble_scanner_get_indoor(&ruuvi);
        if (ruuvi.valid) {
            cJSON_AddStringToObject(root, "chip", "ruuvi");
            cJSON_AddNumberToObject(root, "temp_c",    round2(ruuvi.temp_c));
            cJSON_AddNumberToObject(root, "rh_pct",    round2(ruuvi.humidity_pct));
            cJSON_AddNumberToObject(root, "press_hpa", round2(ruuvi.pressure_hpa));
            if (ruuvi.battery_valid)
                cJSON_AddNumberToObject(root, "batt_mv", ruuvi.battery_mv);
            cJSON_AddNumberToObject(root, "ts",        ts_us_to_epoch_ms(ruuvi.timestamp_us));
        } else {
            cJSON_AddNumberToObject(root, "temp_c",    0.0);
            cJSON_AddNumberToObject(root, "rh_pct",    0.0);
            cJSON_AddNumberToObject(root, "press_hpa", 0.0);
        }
    }
    rs485_send_frame("bme_in", root, false);
    cJSON_Delete(root);
}

static void rs485_publish_bme_out(void)
{
    bme680_snapshot_t bme = {0};
    bme680_app_get_snapshot(&bme);
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "type", "bme_out");
    if (bme.outdoor.valid) {
        const char *chip = bme.outdoor.chip == BME_CHIP_BME680 ? "bme680" :
                           bme.outdoor.chip == BME_CHIP_BME280 ? "bme280" :
                           bme.outdoor.chip == BME_CHIP_BME260 ? "bme260" : "unknown";
        cJSON_AddStringToObject(root, "chip", chip);
        cJSON_AddNumberToObject(root, "addr", bme.outdoor_addr);
        cJSON_AddNumberToObject(root, "temp_c", round2(bme.outdoor.temperature_c));
        cJSON_AddNumberToObject(root, "rh_pct", round2(bme.outdoor.humidity_pct));
        cJSON_AddNumberToObject(root, "press_hpa", round2(bme.outdoor.pressure_hpa));

        // Luftdruck-Trend: bevorzugt 3h, Fallback 1h
        bool have_trend = false;
        bme680_pressure_trend_t trend_state = BME680_TREND_STEADY;
        float trend_hpa_h = 0.0f;
        if (bme.outdoor.press_trend_3h_valid) {
            trend_state = bme.outdoor.press_trend_3h_state;
            trend_hpa_h = bme.outdoor.press_trend_3h_hpa_h;
            have_trend = true;
            if (bme.outdoor.press_trend_1h_valid) {
                bool t3_rising  = (trend_state == BME680_TREND_RISING || trend_state == BME680_TREND_RISING_FAST);
                bool t3_falling = (trend_state == BME680_TREND_FALLING || trend_state == BME680_TREND_FALLING_FAST);
                bme680_pressure_trend_t s1h = bme.outdoor.press_trend_1h_state;
                bool t1_rising  = (s1h == BME680_TREND_RISING || s1h == BME680_TREND_RISING_FAST);
                bool t1_falling = (s1h == BME680_TREND_FALLING || s1h == BME680_TREND_FALLING_FAST);
                if ((t3_rising && !t1_rising) || (t3_falling && !t1_falling)) {
                    trend_state = s1h;
                    trend_hpa_h = bme.outdoor.press_trend_1h_hpa_h;
                }
            }
        } else if (bme.outdoor.press_trend_1h_valid) {
            trend_state = bme.outdoor.press_trend_1h_state;
            trend_hpa_h = bme.outdoor.press_trend_1h_hpa_h;
            have_trend = true;
        }
        if (have_trend) {
            const char *s = "steady";
            switch (trend_state) {
                case BME680_TREND_FALLING_FAST: s = "fall_fast"; break;
                case BME680_TREND_FALLING:      s = "fall_slow"; break;
                case BME680_TREND_STEADY:       s = "steady";    break;
                case BME680_TREND_RISING:       s = "rise_slow"; break;
                case BME680_TREND_RISING_FAST:  s = "rise_fast"; break;
            }
            cJSON_AddStringToObject(root, "press_trend_state", s);
            cJSON_AddNumberToObject(root, "press_trend_hpa_h", round2(trend_hpa_h));
        }
        if (bme.outdoor.gas_valid)
            cJSON_AddNumberToObject(root, "gas_kohm", round2(bme.outdoor.gas_kohm));
        cJSON_AddNumberToObject(root, "ts", ts_us_to_epoch_ms(bme.outdoor.timestamp_us));
    } else {
        // Kein Kabel-Sensor → Ruuvi Außen-Tag als Fallback
        ruuvi_snapshot_t ruuvi = {0};
        ruuvi_ble_scanner_get_outdoor(&ruuvi);
        if (ruuvi.valid) {
            cJSON_AddStringToObject(root, "chip", "ruuvi");
            cJSON_AddNumberToObject(root, "temp_c", round2(ruuvi.temp_c));
            cJSON_AddNumberToObject(root, "rh_pct", round2(ruuvi.humidity_pct));
            if (ruuvi.has_pressure) {
                cJSON_AddNumberToObject(root, "press_hpa", round2(ruuvi.pressure_hpa));
            } else {
                // Außen-Tag ohne Drucksensor (z. B. Pro 3in1) → Druck vom Innensensor
                if (bme.indoor.valid) {
                    cJSON_AddNumberToObject(root, "press_hpa", round2(bme.indoor.pressure_hpa));
                } else {
                    ruuvi_snapshot_t ruuvi_in = {0};
                    ruuvi_ble_scanner_get_indoor(&ruuvi_in);
                    if (ruuvi_in.valid && ruuvi_in.has_pressure)
                        cJSON_AddNumberToObject(root, "press_hpa", round2(ruuvi_in.pressure_hpa));
                }
            }
            if (ruuvi.battery_valid)
                cJSON_AddNumberToObject(root, "batt_mv", ruuvi.battery_mv);
            cJSON_AddNumberToObject(root, "ts", ts_us_to_epoch_ms(ruuvi.timestamp_us));
        } else {
            cJSON_AddNumberToObject(root, "temp_c",    0.0);
            cJSON_AddNumberToObject(root, "rh_pct",    0.0);
            cJSON_AddNumberToObject(root, "press_hpa", 0.0);
        }
    }
    rs485_send_frame("bme_out", root, false);
    cJSON_Delete(root);
}

static void rs485_publish_bat(void)
{
    int mv = 0;
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "type", "bat");

    // Batterie 1 (Kfz): <1V = nicht angeschlossen → nc=true
    bool batt1_ok = (analog_read_mv_avg(SENSOR_BATT1_ADC_CHANNEL, &mv, 3) == ESP_OK);
    bool batt1_connected = batt1_ok && (mv > 1000);  // > 1V = verbunden
    cJSON_AddNumberToObject(root, "b1", round2(batt1_connected ? (double)mv / 1000.0 : 0.0));
    cJSON_AddBoolToObject(root, "nc1", !batt1_connected);

    // Batterie 2 (Board): <1V = nicht angeschlossen → nc=true
    bool batt2_ok = (analog_read_mv_avg(SENSOR_BATT2_ADC_CHANNEL, &mv, 3) == ESP_OK);
    bool batt2_connected = batt2_ok && (mv > 1000);  // > 1V = verbunden
    cJSON_AddNumberToObject(root, "b2", round2(batt2_connected ? (double)mv / 1000.0 : 0.0));
    cJSON_AddBoolToObject(root, "nc2", !batt2_connected);

    rs485_send_frame("bat", root, false);
    cJSON_Delete(root);
}

static void rs485_publish_hx(void)
{
    hx711_snapshot_t hx = {0};
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "type", "hx");

    if (hx711_app_get_snapshot(&hx) == ESP_OK && (hx.valid_a || hx.valid_b)) {
        if (hx.valid_a) cJSON_AddNumberToObject(root, "a", round2(hx.kg_a));
        if (hx.valid_b) cJSON_AddNumberToObject(root, "b", round2(hx.kg_b));
        if (hx.valid_a && hx.valid_b)
            cJSON_AddNumberToObject(root, "sum", round2(hx.kg_a + hx.kg_b));
        cJSON_AddNumberToObject(root, "ts", ts_us_to_epoch_ms(hx.timestamp_us));
    } else {
        cJSON_AddBoolToObject(root, "nc", true);
    }
    rs485_send_frame("hx", root, false);
    cJSON_Delete(root);
}

static void rs485_publish_gas(void)
{
    static bool s_gas_nvs_loaded = false;
    if (!s_gas_nvs_loaded) {
        gas_state_load_from_nvs();
        s_gas_nvs_loaded = true;
    }

    hx711_snapshot_t hx = {0};
    hx711_app_get_snapshot(&hx);

    const double fill_kg = SENSOR_GAS_FILL_KG;
    const double tare_kg = SENSOR_GAS_TARE_KG;  // Fallback wenn keine Voll-Referenz gesetzt
    const double no_bottle_thr = SENSOR_GAS_NO_BOTTLE_THRESHOLD_KG;

    double net_a = hx.valid_a ? hx.kg_a : 0.0;
    double net_b = hx.valid_b ? hx.kg_b : 0.0;

    // Flaschenpräsenz: unter Schwellwert = keine Flasche auf der Waage
    bool nc_a = !hx.valid_a || net_a < no_bottle_thr;
    bool nc_b = !hx.valid_b || net_b < no_bottle_thr;

    // Voll-Referenz erfassen (erste Messung nach Flaschenwechsel)
    bool nvs_dirty = false;
    if (s_ref_pending_a && hx.valid_a && !nc_a) {
        s_gas_state.ref_full_a = (float)net_a;
        s_ref_pending_a = false;
        nvs_dirty = true;
        ESP_LOGI(TAG, "Voll-Referenz A gesetzt: %.2f kg", net_a);
    }
    if (s_ref_pending_b && hx.valid_b && !nc_b) {
        s_gas_state.ref_full_b = (float)net_b;
        s_ref_pending_b = false;
        nvs_dirty = true;
        ESP_LOGI(TAG, "Voll-Referenz B gesetzt: %.2f kg", net_b);
    }
    if (nvs_dirty) gas_state_save_to_nvs();

    int active = (s_gas_active_override >= 0) ? s_gas_active_override : SENSOR_GAS_ACTIVE_DEFAULT;
    if (active < 0) {
        if (hx.valid_a && !nc_a) active = 0;
        else if (hx.valid_b && !nc_b) active = 1;
    }

    double net = (active == 0) ? net_a : (active == 1) ? net_b : 0.0;

    // Füllstandsberechnung: referenzbasiert wenn Voll-Referenz gesetzt, sonst Fallback auf Tara
    double pct = 0.0, pct_a = 0.0, pct_b = 0.0;
    if (fill_kg > 0.0) {
        double leer_a = (s_gas_state.ref_full_a > 0.0f) ? s_gas_state.ref_full_a - fill_kg : tare_kg;
        double leer_b = (s_gas_state.ref_full_b > 0.0f) ? s_gas_state.ref_full_b - fill_kg : tare_kg;
        pct_a = nc_a ? 0.0 : ((net_a - leer_a) / fill_kg) * 100.0;
        pct_b = nc_b ? 0.0 : ((net_b - leer_b) / fill_kg) * 100.0;
        if (active == 0) pct = pct_a;
        else if (active == 1) pct = pct_b;
    }
    if (pct_a < 0.0) pct_a = 0.0; else if (pct_a > 100.0) pct_a = 100.0;
    if (pct_b < 0.0) pct_b = 0.0; else if (pct_b > 100.0) pct_b = 100.0;
    if (pct < 0.0) pct = 0.0; else if (pct > 100.0) pct = 100.0;

    // Verbrauchsraten berechnen (EMA-geglättet)
    double rate1h = s_gas_state.rate1h;
    double rate2h = s_gas_state.rate2h;
    if (hx.timestamp_us > s_gas_state.last_ts_us) {
        double dt_s = (double)(hx.timestamp_us - s_gas_state.last_ts_us) / 1e6;
        if (dt_s >= SENSOR_GAS_RATE_MIN_DT_SEC) {
            double d_a = net_a - s_gas_state.last_net_a;
            double d_b = net_b - s_gas_state.last_net_b;
            double d_active = (active == 0) ? d_a : (active == 1) ? d_b : (d_a + d_b);
            double eps = SENSOR_GAS_ACTIVE_EPS_KG;

            if (s_gas_active_override < 0) {
                if ((d_a < -eps) && !(d_b < -eps)) active = 0;
                else if ((d_b < -eps) && !(d_a < -eps)) active = 1;
            }

            if (d_active <= -eps) {
                // Nur echte Verbräuche über der Schwelle glätten
                double rate_kg_per_h = (-d_active) / dt_s * 3600.0;
                rate1h = rate1h * (1.0 - SENSOR_GAS_RATE_ALPHA_1H) + rate_kg_per_h * SENSOR_GAS_RATE_ALPHA_1H;
                rate2h = rate2h * (1.0 - SENSOR_GAS_RATE_ALPHA_2H) + rate_kg_per_h * SENSOR_GAS_RATE_ALPHA_2H;
            } else {
                // Bei Rauschen/Zuordnung oder Füllung langsam gegen 0 ausblenden
                rate1h *= (1.0 - SENSOR_GAS_RATE_ALPHA_1H);
                rate2h *= (1.0 - SENSOR_GAS_RATE_ALPHA_2H);
            }

            if (rate1h < 0.0) rate1h = 0.0;
            if (rate2h < 0.0) rate2h = 0.0;

            s_gas_state.last_net_a  = net_a;
            s_gas_state.last_net_b  = net_b;
            s_gas_state.last_ts_us  = hx.timestamp_us;
            s_gas_state.rate1h      = rate1h;
            s_gas_state.rate2h      = rate2h;
            gas_state_save_to_nvs();
        }
    } else if (s_gas_state.last_ts_us == 0 && hx.timestamp_us > 0) {
        s_gas_state.last_net_a = net_a;
        s_gas_state.last_net_b = net_b;
        s_gas_state.last_ts_us = hx.timestamp_us;
        rate1h = 0.0;
        rate2h = 0.0;
    }

    double rest_h = 0.0;
    double rate_for_rest = (rate2h > 0.0) ? rate2h : rate1h;
    if (rate_for_rest > 0.0001) {
        if (active == 0) rest_h = net_a / rate_for_rest;
        else if (active == 1) rest_h = net_b / rate_for_rest;
        else rest_h = (net_a + net_b) / rate_for_rest;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "type", "gas");
    cJSON_AddNumberToObject(root, "active", active);
    cJSON_AddNumberToObject(root, "net", round2(net));
    cJSON_AddNumberToObject(root, "rate1h", round2(rate1h));
    cJSON_AddNumberToObject(root, "rate2h", round2(rate2h));
    cJSON_AddNumberToObject(root, "rest_h", round2(rest_h));
    cJSON_AddNumberToObject(root, "net_a", round2(net_a));
    cJSON_AddNumberToObject(root, "net_b", round2(net_b));
    cJSON_AddNumberToObject(root, "cap_kg", round2(fill_kg));
    cJSON_AddNumberToObject(root, "pct", round2(pct));
    cJSON_AddNumberToObject(root, "pct_a", round2(pct_a));
    cJSON_AddNumberToObject(root, "pct_b", round2(pct_b));
    if (nc_a) cJSON_AddBoolToObject(root, "nc_a", true);
    if (nc_b) cJSON_AddBoolToObject(root, "nc_b", true);

    ESP_LOGI(TAG, "GAS: active=%d, %.2f kg (%.0f%%), A=%.2f kg%s, B=%.2f kg%s, rate=%.3f/%.3f kg/h, rest=%.1f h",
             active, fill_kg, pct, net_a, nc_a ? " (nc)" : "", net_b, nc_b ? " (nc)" : "",
             rate1h, rate2h, rest_h);

    rs485_send_frame("gas", root, false);
    cJSON_Delete(root);
}

static void rs485_publish_tank(void)
{
    static bool s_tank_nvs_loaded = false;
    if (!s_tank_nvs_loaded) {
        tank_state_load_from_nvs();
        s_tank_nvs_loaded = true;
    }

    int mv = 0;
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "type", "tank");

    bool tank1_ok = (analog_read_mv_avg(SENSOR_TANK1_ADC_CHANNEL, &mv, 3) == ESP_OK);
    int tank1_pct = tank1_ok ? adc_mv_to_percent_cal(mv, SENSOR_TANK1_EMPTY_MV, SENSOR_TANK1_FULL_MV) : 0;
    bool tank2_ok = (analog_read_mv_avg(SENSOR_TANK2_ADC_CHANNEL, &mv, 3) == ESP_OK);
    int tank2_pct = tank2_ok ? adc_mv_to_percent_cal(mv, SENSOR_TANK2_EMPTY_MV, SENSOR_TANK2_FULL_MV) : 0;

    // Verbrauchsberechnung Frischwasser (100L, Verbrauch negativ)
    double tank1_liters = tank1_ok ? (tank1_pct / 100.0) * 100.0 : 0.0;
    double tank1_rate1h = 0.0, tank1_rate2h = 0.0, tank1_rest_h = 0.0;
    int64_t now_us = esp_timer_get_time();
    bool tank1_state_changed = false;
    if (tank1_ok && s_tank1_state.last_ts_us > 0) {
        double dt_s = (now_us - s_tank1_state.last_ts_us) / 1000000.0;
        if (dt_s >= SENSOR_TANK_RATE_MIN_DT_SEC) {
            double dt_h = dt_s / 3600.0;
            double rate = (tank1_liters - s_tank1_state.last_liters) / dt_h;
            s_tank1_state.rate1h = s_tank1_state.rate1h * (1.0 - SENSOR_TANK_RATE_ALPHA_1H)
                                 + rate * SENSOR_TANK_RATE_ALPHA_1H;
            s_tank1_state.rate2h = s_tank1_state.rate2h * (1.0 - SENSOR_TANK_RATE_ALPHA_2H)
                                 + rate * SENSOR_TANK_RATE_ALPHA_2H;
            s_tank1_state.last_liters = tank1_liters;
            s_tank1_state.last_ts_us = now_us;
            tank1_state_changed = true;
        }
    } else if (tank1_ok) {
        s_tank1_state.last_liters = tank1_liters;
        s_tank1_state.last_ts_us = now_us;
    }
    tank1_rate1h = s_tank1_state.rate1h;
    tank1_rate2h = s_tank1_state.rate2h;
    // Restlaufzeit: Negativ bei Verbrauch
    if (tank1_rate2h < -0.1 && tank1_liters > 0) {
        tank1_rest_h = tank1_liters / (-tank1_rate2h);
    }

    // Füllratenberechnung Grauwasser (92L, Füllung positiv)
    double tank2_liters = tank2_ok ? (tank2_pct / 100.0) * 92.0 : 0.0;
    double tank2_rate1h = 0.0, tank2_rate2h = 0.0, tank2_rest_h = 0.0;
    bool tank2_state_changed = false;
    if (tank2_ok && s_tank2_state.last_ts_us > 0) {
        double dt_s = (now_us - s_tank2_state.last_ts_us) / 1000000.0;
        if (dt_s >= SENSOR_TANK_RATE_MIN_DT_SEC) {
            double dt_h = dt_s / 3600.0;
            double rate = (tank2_liters - s_tank2_state.last_liters) / dt_h;
            s_tank2_state.rate1h = s_tank2_state.rate1h * (1.0 - SENSOR_TANK_RATE_ALPHA_1H)
                                 + rate * SENSOR_TANK_RATE_ALPHA_1H;
            s_tank2_state.rate2h = s_tank2_state.rate2h * (1.0 - SENSOR_TANK_RATE_ALPHA_2H)
                                 + rate * SENSOR_TANK_RATE_ALPHA_2H;
            s_tank2_state.last_liters = tank2_liters;
            s_tank2_state.last_ts_us = now_us;
            tank2_state_changed = true;
        }
    } else if (tank2_ok) {
        s_tank2_state.last_liters = tank2_liters;
        s_tank2_state.last_ts_us = now_us;
    }
    tank2_rate1h = s_tank2_state.rate1h;
    tank2_rate2h = s_tank2_state.rate2h;
    // Zeit bis voll: Positiv bei Füllung
    double remaining_l = 92.0 - tank2_liters;
    if (tank2_rate2h > 0.1 && remaining_l > 0) {
        tank2_rest_h = remaining_l / tank2_rate2h;
    }

    // NVS speichern wenn State aktualisiert wurde
    if (tank1_state_changed || tank2_state_changed) {
        tank_state_save_to_nvs();
    }

    cJSON_AddNumberToObject(root, "t1", tank1_pct);
    cJSON_AddNumberToObject(root, "t2", tank2_pct);
    cJSON_AddBoolToObject(root, "nc1", !tank1_ok);
    cJSON_AddBoolToObject(root, "nc2", !tank2_ok);
    cJSON_AddNumberToObject(root, "t1_l", round2(tank1_liters));
    cJSON_AddNumberToObject(root, "t2_l", round2(tank2_liters));
    cJSON_AddNumberToObject(root, "t1_rate1h", round2(tank1_rate1h));
    cJSON_AddNumberToObject(root, "t1_rate2h", round2(tank1_rate2h));
    cJSON_AddNumberToObject(root, "t1_rest_h", round2(tank1_rest_h));
    cJSON_AddNumberToObject(root, "t2_rate1h", round2(tank2_rate1h));
    cJSON_AddNumberToObject(root, "t2_rate2h", round2(tank2_rate2h));
    cJSON_AddNumberToObject(root, "t2_rest_h", round2(tank2_rest_h));
    
    ESP_LOGI(TAG, "TANK: Frisch=%d%% (%.1fL), rate=%.2f/%.2f L/h, rest=%.1fh | Grau=%d%% (%.1fL), rate=%.2f/%.2f L/h, rest=%.1fh",
             tank1_pct, tank1_liters, tank1_rate1h, tank1_rate2h, tank1_rest_h,
             tank2_pct, tank2_liters, tank2_rate1h, tank2_rate2h, tank2_rest_h);
    
    rs485_send_frame("tank", root, false);
    cJSON_Delete(root);
}

// ── Topic-Scheduler ─────────────────────────────────────────────────────

typedef struct {
    const char *name;
    uint32_t interval_ms;
    TickType_t last_sent;
    void (*publish)(void);
} rs485_topic_t;

static void rs485_publish_elec(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "type", "elec");

    ina226_snapshot_t ina = {0};
    if (ina226_app_get_snapshot(&ina) == ESP_OK) {
        cJSON_AddNumberToObject(root, "v_bus",     round2(ina.v_bus_v));
        cJSON_AddNumberToObject(root, "i_a",       (double)roundf(ina.i_a * 100.0f) / 100.0);
        cJSON_AddNumberToObject(root, "p_w",       (double)roundf(ina.p_w * 10.0f) / 10.0);
        cJSON_AddNumberToObject(root, "v_shunt_mv",(double)roundf(ina.v_shunt_mv * 1000.0f) / 1000.0);
        cJSON_AddNumberToObject(root, "ts",        (double)(ina.timestamp_us / 1000LL));
    } else {
        cJSON_AddBoolToObject(root, "nc", true);
    }

    rs485_send_frame("elec", root, false);
    cJSON_Delete(root);
}

static rs485_topic_t s_topics[] = {
    { "ctrl",  WOMO_TOPIC_CTRL_INTERVAL_MS, 0, rs485_publish_ctrl },
    { "imu",   WOMO_TOPIC_IMU_INTERVAL_MS,  0, rs485_publish_imu  },
    { "bat",   WOMO_TOPIC_BAT_INTERVAL_MS,  0, rs485_publish_bat  },
    { "tank",  WOMO_TOPIC_TANK_INTERVAL_MS, 0, rs485_publish_tank },
    { "hx",    WOMO_TOPIC_HX_INTERVAL_MS,   0, rs485_publish_hx   },
    { "gas",     WOMO_TOPIC_GAS_INTERVAL_MS,  0, rs485_publish_gas     },
    { "bme_in",  WOMO_TOPIC_BME_INTERVAL_MS,   0, rs485_publish_bme_in  },
    { "bme_out", WOMO_TOPIC_BME_INTERVAL_MS,   0, rs485_publish_bme_out },
    { "elec",    WOMO_TOPIC_ELEC_INTERVAL_MS,  0, rs485_publish_elec    },
};
#define RS485_TOPIC_COUNT (sizeof(s_topics) / sizeof(s_topics[0]))
static size_t s_topic_rr = 0;

// ── Command Execution ───────────────────────────────────────────────────

static bool rs485_execute_command(const cJSON *root, const char *cmd_str, esp_err_t *out_err)
{
    if (!cmd_str) {
        if (out_err) *out_err = ESP_ERR_INVALID_ARG;
        return false;
    }

    esp_err_t cmd_err = ESP_OK;
    bool handled = true;

    if (strcmp(cmd_str, "display_ready") == 0) {
        rs485_handle_display_ready();
    // level_start/level_stop entfernt – Fast-Mode wird automatisch über
    // /api/imu gesteuert (horizon.html pollt → Fast, Seite zu → Normal)
    } else if (strcmp(cmd_str, "wifi_config") == 0) {
        const cJSON *j_ssid = cJSON_GetObjectItem(root, "ssid");
        const cJSON *j_pass = cJSON_GetObjectItem(root, "pass");
        const char *new_ssid = cJSON_IsString(j_ssid) ? j_ssid->valuestring : NULL;
        const char *new_pass = cJSON_IsString(j_pass) ? j_pass->valuestring : NULL;
        if (new_ssid) {
            cmd_err = sensor_wifi_set_credentials(new_ssid, new_pass ? new_pass : "");
            ESP_LOGI(TAG, "WiFi-Credentials aktualisiert: SSID=%s", new_ssid);
        } else {
            ESP_LOGW(TAG, "wifi_config: kein 'ssid' Feld");
            cmd_err = ESP_ERR_INVALID_ARG;
        }
    } else if (strcmp(cmd_str, "tare_a") == 0 || strcmp(cmd_str, "tare_b") == 0) {
        ESP_LOGI(TAG, "Tare command: %s", cmd_str);
        s_gas_state = (gas_state_t){0};
        gas_state_save_to_nvs();
    } else if (strcmp(cmd_str, "gas_bottle_replace") == 0) {
        const cJSON *slot    = cJSON_GetObjectItem(root, "slot");
        const cJSON *channel = cJSON_GetObjectItem(root, "channel");
        const char *chan = (cJSON_IsString(channel) && channel->valuestring) ? channel->valuestring : "";
        int slot_idx = cJSON_IsNumber(slot) ? (int)slot->valuedouble : -1;

        int override = -1;
        if (slot_idx == 0 || (chan && (chan[0] == 'a' || chan[0] == 'A'))) {
            override = 0;
        } else if (slot_idx == 1 || (chan && (chan[0] == 'b' || chan[0] == 'B'))) {
            override = 1;
        }

        s_gas_active_override = override;
        // Alte Referenzen beibehalten bis neue Messung vorliegt, EMA zurücksetzen
        gas_state_t reset = {.ref_full_a = s_gas_state.ref_full_a, .ref_full_b = s_gas_state.ref_full_b};
        s_gas_state = reset;
        gas_state_save_to_nvs();
        // Nächste gültige HX711-Messung als Voll-Referenz für den jeweiligen Kanal speichern
        if (override == 0 || override < 0) s_ref_pending_a = true;
        if (override == 1 || override < 0) s_ref_pending_b = true;
        ESP_LOGI(TAG, "Gas bottle replace: slot=%d channel=%s -> active_override=%d, ref pending: A=%d B=%d",
                 slot_idx, chan, override, s_ref_pending_a, s_ref_pending_b);
    } else if (strcmp(cmd_str, "pwr_12v_on") == 0) {
        s_pwr_on_state = true;
        cmd_err = rs485_set_12v_power(true);
        s_ctrl_immediate = true;
    } else if (strcmp(cmd_str, "pwr_12v_off") == 0) {
        s_pwr_on_state = false;
        cmd_err = rs485_set_12v_power(false);
        s_ctrl_immediate = true;
        s_shutdown_pending = true;  // Nach ACK-Versand in Deep Sleep
    } else if (strcmp(cmd_str, "radio_on") == 0) {
        cmd_err = rs485_set_radio(true);
        s_ctrl_immediate = true;
    } else if (strcmp(cmd_str, "radio_off") == 0) {
        cmd_err = rs485_set_radio(false);
        s_ctrl_immediate = true;
    } else if (strcmp(cmd_str, "imu_zero") == 0) {
        cmd_err = bno055_app_zero_pitch_roll();
    } else {
        handled = false;
        cmd_err = ESP_ERR_INVALID_ARG;
    }

    if (out_err) *out_err = cmd_err;
    return handled;
}

// ── RX Line Processing ──────────────────────────────────────────────────

static bool rs485_line_is_ascii(const char *line)
{
    if (!line) return false;
    const unsigned char *p = (const unsigned char *)line;
    while (*p) {
        if (*p < 0x20 || *p > 0x7E) {
            ESP_LOGW(TAG, "Sensor frame dropped (non-ASCII 0x%02X)", (unsigned)*p);
            return false;
        }
        ++p;
    }
    return true;
}

static void rs485_process_rx_line(const char *line)
{
    if (!line) return;

    while (*line && (unsigned char)*line < 0x20) ++line;
    if (*line == '\0') return;

    if (!rs485_line_is_ascii(line)) return;

    while (*line && *line != '{' && *line != '[') ++line;
    if (*line == '\0') {
        ESP_LOGW(TAG, "RX line without JSON start ignored");
        return;
    }

    const char *end_brace   = strrchr(line, '}');
    const char *end_bracket = strrchr(line, ']');
    const char *end = end_brace;
    if (end_bracket && (!end || end_bracket > end)) end = end_bracket;
    if (!end) {
        ESP_LOGW(TAG, "RX line without JSON end ignored");
        return;
    }

    size_t json_len = (size_t)(end - line + 1);
    if (json_len >= RS485_LINE_MAX) {
        ESP_LOGW(TAG, "RX JSON too long (%zu)", json_len);
        return;
    }

    char clean_line[RS485_LINE_MAX];
    memcpy(clean_line, line, json_len);
    clean_line[json_len] = '\0';
    line = clean_line;

    ESP_LOGI(TAG, "RX: %s", line);

    cJSON *root = cJSON_Parse(line);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed");
        return;
    }

    const cJSON *seq_obj = cJSON_GetObjectItem(root, "seq");
    uint32_t rx_seq = 0;
    if (cJSON_IsNumber(seq_obj)) {
        rx_seq = (uint32_t)seq_obj->valuedouble;
        rs485_record_rx_seq(rx_seq);
    }

    bool need_ack = false;
    const cJSON *need_ack_obj = cJSON_GetObjectItem(root, "need_ack");
    if (cJSON_IsBool(need_ack_obj)) {
        need_ack = cJSON_IsTrue(need_ack_obj);
    } else if (rx_seq != 0) {
        need_ack = true;
    }

    const cJSON *type_obj = cJSON_GetObjectItem(root, "type");
    const char *type = (cJSON_IsString(type_obj) && type_obj->valuestring) ? type_obj->valuestring : NULL;
    const cJSON *cmd_obj = cJSON_GetObjectItem(root, "cmd");
    const char *cmd_str = (cJSON_IsString(cmd_obj) && cmd_obj->valuestring) ? cmd_obj->valuestring : NULL;

    const char *ack_label = type && type[0] ? type : (cmd_str ? cmd_str : "frame");
    bool ack_success = false;
    char ack_error[96] = "";

    if (type && (strcmp(type, "ack") == 0 || strcmp(type, "cmd_ack") == 0)) {
        rs485_handle_ack_packet(root);
        need_ack = false;
        ack_success = true;
    } else if (type && (strcmp(type, "hb") == 0 || strcmp(type, "hello") == 0)) {
        /* Display-Heartbeat/-Hello: kein ACK nötig, Peer-Erkennung läuft via espnow_transport */
        ack_success = true;
        need_ack = false;
    } else if (cmd_str) {
        esp_err_t cmd_err = ESP_OK;
        bool handled = rs485_execute_command(root, cmd_str, &cmd_err);
        ack_success = (handled && cmd_err == ESP_OK);
        if (!handled) {
            strncpy(ack_error, "unknown command", sizeof(ack_error) - 1);
        } else if (cmd_err != ESP_OK) {
            strncpy(ack_error, esp_err_to_name(cmd_err), sizeof(ack_error) - 1);
        }
    } else {
        strncpy(ack_error, "missing cmd", sizeof(ack_error) - 1);
    }

    if (need_ack && rx_seq != 0) {
        esp_err_t ack_err = rs485_send_ack(rx_seq, ack_success, ack_label, ack_success ? NULL : ack_error);
        if (ack_err != ESP_OK) {
            ESP_LOGW(TAG, "ACK send failed: %s", esp_err_to_name(ack_err));
        }
    }

    cJSON_Delete(root);

    // Nach ACK: Deep Sleep wenn pwr_12v_off empfangen wurde
    if (s_shutdown_pending) {
        s_shutdown_pending = false;
        deep_sleep_enter();             // Kehrt nicht zurück
    }
}

// ── TX Task ─────────────────────────────────────────────────────────────
// RX läuft im espnow_transport: espnow_rx_task → rs485_process_rx_line()

static void rs485_tx_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "TX task started (topic-based, %d topics)", (int)RS485_TOPIC_COUNT);

    const TickType_t loop_delay = pdMS_TO_TICKS(100);

    // Initialisiere Topic-Timer
    TickType_t now_init = xTaskGetTickCount();
    for (size_t i = 0; i < RS485_TOPIC_COUNT; ++i) {
        s_topics[i].last_sent = now_init;
    }

    while (true) {
        TickType_t now = xTaskGetTickCount();

        // Display-Peer über ESP-NOW bekannt? → Topics einmalig freischalten + Initial-Burst
        if (!s_display_ready && espnow_transport_peer_known()) {
            rs485_handle_display_ready();
        }

        // Sofort-Ctrl nach Command-Ausführung (Button-Feedback)
        if (s_display_ready && s_ctrl_immediate) {
            s_ctrl_immediate = false;
            rs485_publish_ctrl();
            s_topics[0].last_sent = now;  // ctrl-Timer zurücksetzen
        }

        // Round-Robin: pro Loop maximal 1 Topic senden
        if (s_display_ready) {
            for (size_t i = 0; i < RS485_TOPIC_COUNT; ++i) {
                size_t idx = (s_topic_rr + i) % RS485_TOPIC_COUNT;
                if ((now - s_topics[idx].last_sent) >= pdMS_TO_TICKS(s_topics[idx].interval_ms)) {
                    s_topics[idx].publish();
                    s_topics[idx].last_sent = now;
                    s_topic_rr = (idx + 1) % RS485_TOPIC_COUNT;
                    break;
                }
            }
        }

        rs485_check_command_timeouts();
        vTaskDelay(loop_delay);
    }
}

// ── Init ────────────────────────────────────────────────────────────────

esp_err_t espnow_modem_init(void)
{
    if (s_tx_task) {
        return ESP_OK;
    }

    esp_err_t err = espnow_transport_init();
    if (err != ESP_OK) {
        return err;
    }
    espnow_transport_set_recv_cb(rs485_process_rx_line);

    rs485_power_gpio_init();

    BaseType_t tx_created = xTaskCreate(rs485_tx_task, "espnow_tx", 6144, NULL, 5, &s_tx_task);
    if (tx_created != pdPASS) {
        return ESP_FAIL;
    }

    sensor_wifi_set_auth_fail_cb(rs485_wifi_auth_fail_cb);

    ESP_LOGI(TAG, "ESP-NOW sensor link bereit");
    return ESP_OK;
}

// ── Web-Snapshot: alle Sensordaten als JSON-String ──────────────────────

esp_err_t espnow_modem_get_snapshot(char **json_out)
{
    if (!json_out) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON_AddNumberToObject(root, "ts", (double)rs485_epoch_ms());

    // ctrl ──────────────────────────────────────────────────────────────
    {
        cJSON *j = cJSON_AddObjectToObject(root, "ctrl");
        cJSON_AddBoolToObject(j, "pwr_on",    s_pwr_on_state);
        cJSON_AddBoolToObject(j, "radio_on",  s_radio_on);
        cJSON_AddBoolToObject(j, "ac_present", rs485_ac_present());
    }

    // imu ───────────────────────────────────────────────────────────────
    {
        bno055_imu_snapshot_t imu = {0};
        bool ok = bno055_imu_get_snapshot(&imu) && imu.valid;
        cJSON *j = cJSON_AddObjectToObject(root, "imu");
        cJSON_AddBoolToObject(j, "valid", ok);
        if (ok) {
            cJSON_AddNumberToObject(j, "yaw",   round2(imu.yaw_deg));
            cJSON_AddNumberToObject(j, "pitch", round2(imu.pitch_deg));
            cJSON_AddNumberToObject(j, "roll",  round2(imu.roll_deg));
            cJSON_AddStringToObject(j, "hdg",   heading_to_compass(imu.yaw_deg));
            cJSON *cal = cJSON_AddObjectToObject(j, "cal");
            cJSON_AddNumberToObject(cal, "sys",  imu.cal_sys);
            cJSON_AddNumberToObject(cal, "gyro", imu.cal_gyro);
            cJSON_AddNumberToObject(cal, "acc",  imu.cal_accel);
            cJSON_AddNumberToObject(cal, "mag",  imu.cal_mag);
            cJSON_AddBoolToObject(j, "calibrated", imu.calibrated);
        }
    }

    // bme ───────────────────────────────────────────────────────────────
    {
        bme680_snapshot_t bme = {0};
        bme680_app_get_snapshot(&bme);
        cJSON *j = cJSON_AddObjectToObject(root, "bme");

        cJSON *out = cJSON_AddObjectToObject(j, "out");
        cJSON_AddBoolToObject(out, "valid", bme.outdoor.valid);
        if (bme.outdoor.valid) {
            cJSON_AddNumberToObject(out, "temp",  round2(bme.outdoor.temperature_c));
            cJSON_AddNumberToObject(out, "rh",    round2(bme.outdoor.humidity_pct));
            cJSON_AddNumberToObject(out, "press", round2(bme.outdoor.pressure_hpa));
            if (bme.outdoor.gas_valid)
                cJSON_AddNumberToObject(out, "gas", round2(bme.outdoor.gas_kohm));
            if (bme.outdoor.iaq_valid) {
                cJSON_AddNumberToObject(out, "iaq",     round2(bme.outdoor.iaq));
                cJSON_AddNumberToObject(out, "iaq_acc", bme.outdoor.iaq_accuracy);
            }
            const char *trend = "steady";
            if (bme.outdoor.press_trend_1h_valid) {
                switch (bme.outdoor.press_trend_1h_state) {
                    case BME680_TREND_FALLING_FAST: trend = "falling_fast"; break;
                    case BME680_TREND_FALLING:       trend = "falling";      break;
                    case BME680_TREND_RISING:        trend = "rising";       break;
                    case BME680_TREND_RISING_FAST:   trend = "rising_fast";  break;
                    default:                         trend = "steady";       break;
                }
            }
            cJSON_AddStringToObject(out, "trend", trend);
        }

        cJSON *in = cJSON_AddObjectToObject(j, "in");
        cJSON_AddBoolToObject(in, "valid", bme.indoor.valid);
        if (bme.indoor.valid) {
            cJSON_AddNumberToObject(in, "temp", round2(bme.indoor.temperature_c));
            cJSON_AddNumberToObject(in, "rh",   round2(bme.indoor.humidity_pct));
            if (bme.indoor.gas_valid)
                cJSON_AddNumberToObject(in, "gas", round2(bme.indoor.gas_kohm));
            if (bme.indoor.iaq_valid) {
                cJSON_AddNumberToObject(in, "iaq",     round2(bme.indoor.iaq));
                cJSON_AddNumberToObject(in, "iaq_acc", bme.indoor.iaq_accuracy);
                cJSON_AddNumberToObject(in, "eco2",    round2(bme.indoor.eco2_ppm));
                cJSON_AddNumberToObject(in, "bvoc",    round2(bme.indoor.bvoc_ppm));
            }
        }
    }

    // bat ───────────────────────────────────────────────────────────────
    {
        int mv = 0;
        cJSON *j = cJSON_AddObjectToObject(root, "bat");
        bool b1_ok = (analog_read_mv_avg(SENSOR_BATT1_ADC_CHANNEL, &mv, 3) == ESP_OK) && (mv > 1000);
        cJSON_AddNumberToObject(j, "b1",  b1_ok ? round2(mv / 1000.0) : 0.0);
        cJSON_AddBoolToObject(j,   "nc1", !b1_ok);
        bool b2_ok = (analog_read_mv_avg(SENSOR_BATT2_ADC_CHANNEL, &mv, 3) == ESP_OK) && (mv > 1000);
        cJSON_AddNumberToObject(j, "b2",  b2_ok ? round2(mv / 1000.0) : 0.0);
        cJSON_AddBoolToObject(j,   "nc2", !b2_ok);
    }

    // tank ──────────────────────────────────────────────────────────────
    {
        int mv = 0;
        cJSON *j = cJSON_AddObjectToObject(root, "tank");
        bool t1_ok = (analog_read_mv_avg(SENSOR_TANK1_ADC_CHANNEL, &mv, 3) == ESP_OK);
        int  t1_pct = t1_ok ? adc_mv_to_percent_cal(mv, SENSOR_TANK1_EMPTY_MV, SENSOR_TANK1_FULL_MV) : 0;
        cJSON_AddNumberToObject(j, "t1",       t1_pct);
        cJSON_AddBoolToObject(j,   "nc1",      !t1_ok);
        double t1_l = t1_ok ? (t1_pct / 100.0) * 100.0 : 0.0;
        cJSON_AddNumberToObject(j, "t1_l",     round2(t1_l));
        cJSON_AddNumberToObject(j, "t1_rate2h", round2(s_tank1_state.rate2h));
        double t1_rest = (s_tank1_state.rate2h < -0.1 && t1_l > 0.0)
                         ? t1_l / (-s_tank1_state.rate2h) : 0.0;
        cJSON_AddNumberToObject(j, "t1_rest_h", round2(t1_rest));

        bool t2_ok = (analog_read_mv_avg(SENSOR_TANK2_ADC_CHANNEL, &mv, 3) == ESP_OK);
        int  t2_pct = t2_ok ? adc_mv_to_percent_cal(mv, SENSOR_TANK2_EMPTY_MV, SENSOR_TANK2_FULL_MV) : 0;
        cJSON_AddNumberToObject(j, "t2",       t2_pct);
        cJSON_AddBoolToObject(j,   "nc2",      !t2_ok);
        double t2_l = t2_ok ? (t2_pct / 100.0) * 92.0 : 0.0;
        cJSON_AddNumberToObject(j, "t2_l",     round2(t2_l));
        cJSON_AddNumberToObject(j, "t2_rate2h", round2(s_tank2_state.rate2h));
        double t2_rest = (s_tank2_state.rate2h > 0.1)
                         ? (92.0 - t2_l) / s_tank2_state.rate2h : 0.0;
        cJSON_AddNumberToObject(j, "t2_rest_h", round2(t2_rest));
    }

    // hx + gas ──────────────────────────────────────────────────────────
    {
        hx711_snapshot_t hx = {0};
        hx711_app_get_snapshot(&hx);
        bool nc = !(hx.valid_a || hx.valid_b);

        cJSON *jh = cJSON_AddObjectToObject(root, "hx");
        cJSON_AddBoolToObject(jh, "nc", nc);
        if (hx.valid_a) cJSON_AddNumberToObject(jh, "a", round2(hx.kg_a));
        if (hx.valid_b) cJSON_AddNumberToObject(jh, "b", round2(hx.kg_b));

        const double fill_kg = SENSOR_GAS_FILL_KG;
        const double tare_kg = SENSOR_GAS_TARE_KG;
        const double no_bottle_thr = SENSOR_GAS_NO_BOTTLE_THRESHOLD_KG;
        double net_a = hx.valid_a ? hx.kg_a : 0.0;
        double net_b = hx.valid_b ? hx.kg_b : 0.0;
        bool nc_a = !hx.valid_a || net_a < no_bottle_thr;
        bool nc_b = !hx.valid_b || net_b < no_bottle_thr;

        cJSON *jg = cJSON_AddObjectToObject(root, "gas");
        cJSON_AddBoolToObject(jg, "nc",   nc_a && nc_b);
        cJSON_AddBoolToObject(jg, "nc_a", nc_a);
        cJSON_AddBoolToObject(jg, "nc_b", nc_b);
        if (!(nc_a && nc_b)) {
            int active = (s_gas_active_override >= 0)
                         ? s_gas_active_override : SENSOR_GAS_ACTIVE_DEFAULT;
            if (active < 0) {
                if (!nc_a) active = 0;
                else if (!nc_b) active = 1;
            }
            double net = (active == 0) ? net_a : (active == 1) ? net_b : 0.0;
            double pct = 0.0, pct_a = 0.0, pct_b = 0.0;
            if (fill_kg > 0.0) {
                double leer_a = (s_gas_state.ref_full_a > 0.0f) ? s_gas_state.ref_full_a - fill_kg : tare_kg;
                double leer_b = (s_gas_state.ref_full_b > 0.0f) ? s_gas_state.ref_full_b - fill_kg : tare_kg;
                pct_a = nc_a ? 0.0 : ((net_a - leer_a) / fill_kg) * 100.0;
                pct_b = nc_b ? 0.0 : ((net_b - leer_b) / fill_kg) * 100.0;
                pct   = (active == 0) ? pct_a : (active == 1) ? pct_b : 0.0;
            }
            if (pct_a < 0.0) { pct_a = 0.0; } else if (pct_a > 100.0) { pct_a = 100.0; }
            if (pct_b < 0.0) { pct_b = 0.0; } else if (pct_b > 100.0) { pct_b = 100.0; }
            if (pct   < 0.0) { pct   = 0.0; } else if (pct   > 100.0) { pct   = 100.0; }
            double leer_active = (active == 0)
                ? ((s_gas_state.ref_full_a > 0.0f) ? s_gas_state.ref_full_a - fill_kg : tare_kg)
                : ((s_gas_state.ref_full_b > 0.0f) ? s_gas_state.ref_full_b - fill_kg : tare_kg);
            double rest_h = (s_gas_state.rate2h > 0.05 && net > leer_active)
                            ? (net - leer_active) / s_gas_state.rate2h : 0.0;
            cJSON_AddNumberToObject(jg, "active", active);
            cJSON_AddNumberToObject(jg, "net",    round2(net));
            cJSON_AddNumberToObject(jg, "pct",    round2(pct));
            cJSON_AddNumberToObject(jg, "pct_a",  round2(pct_a));
            cJSON_AddNumberToObject(jg, "pct_b",  round2(pct_b));
            cJSON_AddNumberToObject(jg, "rate1h", round2(s_gas_state.rate1h));
            cJSON_AddNumberToObject(jg, "rate2h", round2(s_gas_state.rate2h));
            cJSON_AddNumberToObject(jg, "rest_h", round2(rest_h));
        }
    }

    *json_out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return (*json_out) ? ESP_OK : ESP_ERR_NO_MEM;
}

// ── Web-Command API ─────────────────────────────────────────────────────

static void shutdown_timer_cb(void *arg)
{
    (void)arg;
    deep_sleep_enter(); // kehrt nicht zurück
}

static esp_err_t execute_cmd_with_root(const cJSON *root, const char *cmd)
{
    esp_err_t err = ESP_OK;
    bool handled  = rs485_execute_command(root, cmd, &err);
    if (!handled)  return ESP_ERR_NOT_FOUND;
    if (err != ESP_OK) return err;

    if (s_shutdown_pending) {
        s_shutdown_pending = false;
        esp_timer_handle_t t;
        const esp_timer_create_args_t targs = {
            .callback = shutdown_timer_cb,
            .name     = "web_shutdown",
        };
        if (esp_timer_create(&targs, &t) == ESP_OK) {
            esp_timer_start_once(t, 500000); // 500 ms
        }
    }
    return ESP_OK;
}

esp_err_t espnow_modem_execute_cmd(const char *cmd)
{
    if (!cmd || cmd[0] == '\0') return ESP_ERR_INVALID_ARG;
    return execute_cmd_with_root(NULL, cmd);
}

esp_err_t espnow_modem_execute_cmd_json(const cJSON *root)
{
    if (!root) return ESP_ERR_INVALID_ARG;
    const cJSON *cmd_item = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    const char *cmd = cJSON_GetStringValue(cmd_item);
    if (!cmd || cmd[0] == '\0') return ESP_ERR_INVALID_ARG;
    return execute_cmd_with_root(root, cmd);
}
