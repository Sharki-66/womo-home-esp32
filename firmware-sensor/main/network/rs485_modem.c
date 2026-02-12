#include "network/rs485_modem.h"

#include "network/womo_rs485.h"

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sensor_config.h"
#include "iot_usbh_modem.h"
#include "sensors/bme680_sensor.h"
#include "sensors/hx711_sensor.h"
#include "sensors/analog_sensor.h"
#include "web_wifi_imu.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define TAG "rs485_modem"

// Protokollparameter (analog zu Walter)
#define RS485_BUF_SIZE                4096
#define RS485_LINE_MAX                4096
#define RS485_MAX_PENDING_CMDS        4
#define RS485_COMMAND_TIMEOUT_MS      3000
#define RS485_MIN_IDLE_US             150000  // 150 ms TX-Sperre nach RX
#define RS485_IDLE_WAIT_MAX_US        400000
#define RS485_FULL_INTERVAL_MS        5000

// Heartbeat/Handshake Intervalle
#define RS485_HEARTBEAT_INTERVAL_MS   SENSOR_RS485_HEARTBEAT_INTERVAL_MS
#define RS485_HELLO_PENDING_MS        SENSOR_RS485_HELLO_PENDING_INTERVAL_MS
#define RS485_HELLO_READY_MS          SENSOR_RS485_HELLO_READY_INTERVAL_MS

// Pending Command Tracking
typedef struct {
    bool in_use;
    bool warned;
    uint32_t seq;
    int64_t sent_us;
    char label[32];
} rs485_pending_cmd_t;

static SemaphoreHandle_t s_tx_mutex = NULL;
static rs485_pending_cmd_t s_pending[RS485_MAX_PENDING_CMDS];
static portMUX_TYPE s_pending_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_seq_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_tx_seq = 1;
static uint32_t s_last_rx_seq = 0;
static uint32_t s_last_ack_seq = 0;
static int64_t s_last_ack_time_us = 0;
static int64_t s_last_rx_us = 0;
static int64_t s_last_rx_line_us = 0;
static int64_t s_last_rx_heartbeat_us = 0;
static int64_t s_last_tx_heartbeat_us = 0;
static int64_t s_last_full_heartbeat_us = 0;
static bool s_display_ready = false;
static uint32_t s_display_ready_seen = 0;
static uint32_t s_hello_sent = 0;
static uint32_t s_heartbeat_sent = 0;
static TaskHandle_t s_tx_task = NULL;
static TaskHandle_t s_rx_task = NULL;
static uint8_t s_rx_buffer[RS485_BUF_SIZE];
static char s_rx_line_buffer[RS485_LINE_MAX];

// Stack-Logintervall in ms
#define RS485_STACK_LOG_INTERVAL_MS   60000

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

static inline int adc_mv_to_percent(int mv)
{
    if (mv < 0) mv = 0;
    if (mv > 3300) mv = 3300;
    return (mv * 100) / 3300;
}

static TickType_t ms_to_ticks(uint32_t ms)
{
    if (ms == 0) {
        return 1;
    }
    TickType_t ticks = pdMS_TO_TICKS(ms);
    return (ticks == 0) ? 1 : ticks;
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
    double now_ms = (double)rs485_epoch_ms();
    double delta_ms = (double)(now_us - ts_us) / 1000.0;
    if (delta_ms < 0.0) {
        delta_ms = 0.0;
    }
    return round2(now_ms - delta_ms);
}

typedef struct {
    double last_press_hpa;
    int64_t last_ts_us;
    double trend_hpa_per_h;
} press_trend_state_t;

static press_trend_state_t s_press_trend_in = {0};
static press_trend_state_t s_press_trend_out = {0};

static double update_pressure_trend(double press_hpa, int64_t ts_us, press_trend_state_t *state)
{
    if (!state || ts_us <= 0) {
        return 0.0;
    }
    if (state->last_ts_us > 0 && ts_us > state->last_ts_us) {
        double dt_h = (double)(ts_us - state->last_ts_us) / 3600000000.0;
        double delta = press_hpa - state->last_press_hpa;
        const double min_dt_h = 0.25;       // mindestens 15 Minuten Abstand
        const double min_delta_hpa = 0.05;   // mindestens 0.05 hPa Änderung
        if (dt_h >= min_dt_h && fabs(delta) >= min_delta_hpa) {
            double inst = delta / dt_h;
            const double alpha = 0.2;
            state->trend_hpa_per_h = state->trend_hpa_per_h * (1.0 - alpha) + inst * alpha;
        } else {
            state->trend_hpa_per_h = 0.0;
        }
    }
    state->last_press_hpa = press_hpa;
    state->last_ts_us = ts_us;
    return state->trend_hpa_per_h;
}

static const char *pressure_trend_state(double trend_hpa_per_h)
{
    if (trend_hpa_per_h >= 0.5) {
        return "rise_fast";
    }
    if (trend_hpa_per_h >= 0.1) {
        return "rise_slow";
    }
    if (trend_hpa_per_h <= -0.5) {
        return "fall_fast";
    }
    if (trend_hpa_per_h <= -0.1) {
        return "fall_slow";
    }
    return "steady";
}

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
    if (seq == 0) {
        return;
    }
    taskENTER_CRITICAL(&s_seq_lock);
    s_last_rx_seq = seq;
    taskEXIT_CRITICAL(&s_seq_lock);
}

static void rs485_add_pending(uint32_t seq, const char *label)
{
    if (!seq || !label || label[0] == '\0') {
        return;
    }

    rs485_pending_cmd_t entry = {
        .in_use = true,
        .warned = false,
        .seq = seq,
        .sent_us = esp_timer_get_time(),
    };
    strlcpy(entry.label, label, sizeof(entry.label));

    bool stored = false;
    bool duplicate = false;
    taskENTER_CRITICAL(&s_pending_lock);
    for (size_t i = 0; i < RS485_MAX_PENDING_CMDS; ++i) {
        if (s_pending[i].in_use && strncmp(s_pending[i].label, label, sizeof(s_pending[i].label)) == 0) {
            duplicate = true;
            break;
        }
        if (!s_pending[i].in_use && !stored) {
            s_pending[i] = entry;
            stored = true;
        }
    }
    taskEXIT_CRITICAL(&s_pending_lock);

    if (duplicate) {
        ESP_LOGW(TAG, "Command %s already pending", label);
    } else if (!stored) {
        ESP_LOGW(TAG, "Pending buffer full, dropping tracking for %s (seq=%lu)", label, (unsigned long)seq);
    }
}

static void rs485_resolve_pending(uint32_t seq, bool success, const char *label_from_packet, const char *err_text)
{
    if (seq == 0) {
        return;
    }

    rs485_pending_cmd_t resolved = {0};

    taskENTER_CRITICAL(&s_pending_lock);
    for (size_t i = 0; i < RS485_MAX_PENDING_CMDS; ++i) {
        if (s_pending[i].in_use && s_pending[i].seq == seq) {
            resolved = s_pending[i];
            s_pending[i].in_use = false;
            s_pending[i].warned = false;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_pending_lock);

    const char *label = label_from_packet && label_from_packet[0] ? label_from_packet : resolved.label;
    if (!label || label[0] == '\0') {
        label = "unknown";
    }

    ESP_LOGI(TAG,
             "ACK seq=%lu label=%s status=%s",
             (unsigned long)seq,
             label,
             success ? "ok" : "err");

    if (!success && err_text && err_text[0] != '\0') {
        ESP_LOGW(TAG, "ACK error detail: %s", err_text);
    }
}

static bool rs485_label_pending(const char *label)
{
    if (!label || !label[0]) {
        return false;
    }

    bool pending = false;
    taskENTER_CRITICAL(&s_pending_lock);
    for (size_t i = 0; i < RS485_MAX_PENDING_CMDS; ++i) {
        if (s_pending[i].in_use && strncmp(s_pending[i].label, label, sizeof(s_pending[i].label)) == 0) {
            pending = true;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_pending_lock);
    return pending;
}

static void rs485_check_command_timeouts(void)
{
    const int64_t now_us = esp_timer_get_time();
    const int64_t timeout_us = (int64_t)RS485_COMMAND_TIMEOUT_MS * 1000;

    // Sammle abgelaufene Einträge außerhalb des Critical Sections für Logging
    rs485_pending_cmd_t expired[RS485_MAX_PENDING_CMDS];
    size_t expired_count = 0;

    taskENTER_CRITICAL(&s_pending_lock);
    for (size_t i = 0; i < RS485_MAX_PENDING_CMDS; ++i) {
        if (!s_pending[i].in_use) {
            continue;
        }
        int64_t age_us = now_us - s_pending[i].sent_us;
        if (age_us >= timeout_us) {
            s_pending[i].warned = true;
            s_pending[i].in_use = false;  // aufgeben, damit neue Frames nicht blockieren
            if (expired_count < RS485_MAX_PENDING_CMDS) {
                expired[expired_count++] = s_pending[i];
            }
        }
    }
    taskEXIT_CRITICAL(&s_pending_lock);

    for (size_t i = 0; i < expired_count; ++i) {
        double age_ms = (double)(now_us - expired[i].sent_us) / 1000.0;
        ESP_LOGW(TAG,
                 "RS485 cmd %s (seq=%lu) waiting for ACK for %.0f ms",
                 expired[i].label[0] ? expired[i].label : "unknown",
                 (unsigned long)expired[i].seq,
                 age_ms);
    }
}

static esp_err_t rs485_write_bytes(const char *payload, size_t len, uint32_t tx_time_ms)
{
    if (!payload || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_tx_mutex && xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = womo_rs485_write((const uint8_t *)payload, len, pdMS_TO_TICKS(tx_time_ms));

    if (s_tx_mutex) {
        xSemaphoreGive(s_tx_mutex);
    }
    return err;
}

static esp_err_t rs485_send_frame(const char *context, cJSON *root, bool default_need_ack)
{
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }

    // Warte auf kurze Bus-Stille nach letztem RX
    int64_t wait_start = esp_timer_get_time();
    for (;;) {
        int64_t now = esp_timer_get_time();
        int64_t last_rx = s_last_rx_us;
        if (last_rx == 0 || (now - last_rx) >= RS485_MIN_IDLE_US) {
            break;
        }
        if ((now - wait_start) >= RS485_IDLE_WAIT_MAX_US) {
            ESP_LOGW(TAG, "TX after idle wait timeout (last_rx %.0f ms)", (double)(now - last_rx) / 1000.0);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    bool need_ack = default_need_ack;
    cJSON *need_ack_item = cJSON_GetObjectItem(root, "need_ack");
    if (cJSON_IsBool(need_ack_item)) {
        need_ack = cJSON_IsTrue(need_ack_item);
    } else {
        cJSON_AddBoolToObject(root, "need_ack", need_ack);
    }

    uint32_t seq = rs485_next_seq();
    cJSON_AddNumberToObject(root, "seq", (double)seq);

        if (!cJSON_GetObjectItem(root, "ts")) {
            cJSON_AddNumberToObject(root, "ts", round2((double)rs485_epoch_ms()));
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }

    // Debug-Sanity: Verifiziere generiertes JSON (hilft bei Fragment-Fehlern)
    if (context && strcmp(context, "full") == 0) {
        cJSON *verify = cJSON_Parse(json_str);
        if (!verify) {
            ESP_LOGE(TAG, "Full JSON unparsable before TX (len=%zu)", strlen(json_str));
        } else {
            cJSON_Delete(verify);
        }
    }

    size_t len = strlen(json_str);
    // Platz für CRLF Terminator
    char *payload = (char *)malloc(len + 3);
    if (!payload) {
        cJSON_free(json_str);
        return ESP_ERR_NO_MEM;
    }
    size_t pos = 0;
    memcpy(&payload[pos], json_str, len);
    pos += len;
    payload[pos++] = '\r';
    payload[pos++] = '\n';
    payload[pos] = '\0';

    bool log_info = true;
    if (context && strcmp(context, "heartbeat") == 0) {
        log_info = false;
    }
    if (log_info) {
        ESP_LOGI(TAG, "TX %s seq=%lu len=%zu need_ack=%s", context ? context : "frame", (unsigned long)seq, pos, need_ack ? "true" : "false");
    }

    uint32_t tx_time_ms = (uint32_t)((pos * 10 * 1000 + (SENSOR_RS485_BAUDRATE - 1)) / SENSOR_RS485_BAUDRATE);
    // Geringes Baudrate-Budget mit Luft nach oben, um ESP_ERR_TIMEOUT bei langen Full-Frames abzufangen
    tx_time_ms = tx_time_ms + 150;  // Puffer fuer Scheduler/Jitter
    if (tx_time_ms < 200) {
        tx_time_ms = 200;
    } else if (tx_time_ms > 1500) {
        tx_time_ms = 1500;
    }

    esp_err_t write_err = rs485_write_bytes(payload, pos, tx_time_ms);

    free(payload);
    cJSON_free(json_str);

    if (write_err != ESP_OK) {
        ESP_LOGW(TAG, "write %s failed: %s (len=%zu timeout=%ums)",
                 context ? context : "frame",
                 esp_err_to_name(write_err),
                 pos,
                 (unsigned)tx_time_ms);
        return write_err;
    }

    if (need_ack) {
        rs485_add_pending(seq, context ? context : "frame");
    }

    // Kleine Pause nach Full-Frames, damit der Bus sicher Leerlauf hat
    if (context && strcmp(context, "full") == 0) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    return ESP_OK;
}

static esp_err_t rs485_send_ack(uint32_t rx_seq, bool ok, const char *cmd, const char *err_msg)
{
    if (rx_seq == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "ack");
    cJSON_AddNumberToObject(root, "ack", (double)rx_seq);
    cJSON_AddStringToObject(root, "status", ok ? "ok" : "err");
    cJSON_AddBoolToObject(root, "need_ack", false);
    if (cmd && cmd[0] != '\0') {
        cJSON_AddStringToObject(root, "cmd", cmd);
    }
    if (!ok && err_msg && err_msg[0] != '\0') {
        cJSON_AddStringToObject(root, "err", err_msg);
    }

    esp_err_t err = rs485_send_frame("ack", root, false);
    cJSON_Delete(root);
    return err;
}

static void rs485_send_hello(void)
{
    cJSON *hello = cJSON_CreateObject();
    if (!hello) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    double uptime_sec = (double)now_us / 1000000.0;
    cJSON_AddStringToObject(hello, "type", "hello");

    char fw_buf[64] = "Modem";
    const esp_app_desc_t *app = esp_app_get_description();
    if (app) {
        if (app->project_name[0] != '\0') {
            snprintf(fw_buf, sizeof(fw_buf), "%s %s", app->project_name, app->version);
        } else if (app->version[0] != '\0') {
            snprintf(fw_buf, sizeof(fw_buf), "%s", app->version);
        }
    }

    cJSON_AddStringToObject(hello, "fw", fw_buf);
    cJSON_AddNumberToObject(hello, "uptime", uptime_sec);
    cJSON_AddBoolToObject(hello, "display_ready", s_display_ready);
    cJSON_AddNumberToObject(hello, "ts", (double)(now_us / 1000));
    if (s_last_rx_seq != 0) {
        cJSON_AddNumberToObject(hello, "rx_seq", (double)s_last_rx_seq);
    }
    if (s_last_ack_seq != 0) {
        cJSON_AddNumberToObject(hello, "last_ack", (double)s_last_ack_seq);
    }

    esp_err_t send_err = rs485_send_frame("hello", hello, false);
    cJSON_Delete(hello);

    if (send_err == ESP_OK) {
        s_hello_sent++;
    }
}

static void rs485_send_heartbeat(void)
{
    int64_t now_us = esp_timer_get_time();
    cJSON *hb = cJSON_CreateObject();
    if (!hb) {
        return;
    }

    cJSON_AddStringToObject(hb, "type", "hb");
    cJSON_AddBoolToObject(hb, "need_ack", false);
    cJSON_AddNumberToObject(hb, "uptime", (double)now_us / 1000000.0);
    if (s_last_rx_seq != 0) {
        cJSON_AddNumberToObject(hb, "rx_seq", (double)s_last_rx_seq);
    }
    if (s_last_ack_seq != 0) {
        cJSON_AddNumberToObject(hb, "last_ack", (double)s_last_ack_seq);
    }

    esp_err_t err = rs485_send_frame("heartbeat", hb, false);
    if (err == ESP_OK) {
        s_last_tx_heartbeat_us = now_us;
        s_heartbeat_sent++;
    }
    cJSON_Delete(hb);
}

static void rs485_handle_ack_packet(const cJSON *root)
{
    if (!root) {
        return;
    }

    const cJSON *ack_val = cJSON_GetObjectItem(root, "ack");
    if (!cJSON_IsNumber(ack_val)) {
        ESP_LOGW(TAG, "ACK missing numeric ack");
        return;
    }

    uint32_t ack_seq = (uint32_t)ack_val->valuedouble;
    const cJSON *status = cJSON_GetObjectItem(root, "status");
    const char *status_str = (cJSON_IsString(status) && status->valuestring) ? status->valuestring : "ok";
    bool success = (strcmp(status_str, "ok") == 0);
    const cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    const char *cmd_str = (cJSON_IsString(cmd) && cmd->valuestring) ? cmd->valuestring : NULL;
    const cJSON *err = cJSON_GetObjectItem(root, "err");
    const char *err_str = (cJSON_IsString(err) && err->valuestring) ? err->valuestring : NULL;

    rs485_resolve_pending(ack_seq, success, cmd_str, err_str);

    taskENTER_CRITICAL(&s_seq_lock);
    s_last_ack_seq = ack_seq;
    s_last_ack_time_us = esp_timer_get_time();
    taskEXIT_CRITICAL(&s_seq_lock);
}

static void rs485_handle_display_ready(void)
{
    s_display_ready = true;
    s_display_ready_seen++;
    s_last_rx_heartbeat_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Display ready (count=%u)", (unsigned)s_display_ready_seen);
}

static bool s_wifi_target_enabled = true;
static bool s_lte_target_enabled = true;
static bool s_wifi_active = true;
static bool s_lte_active = true;
static bool s_power_gpio_inited = false;

static void rs485_power_gpio_init(void)
{
    if (s_power_gpio_inited) {
        return;
    }

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
    if (!s_power_gpio_inited) {
        rs485_power_gpio_init();
    }
    int level = gpio_get_level(gpio);
    if (level < 0) {
        return fallback;
    }
    return level != 0;
}

static bool rs485_board_power_on(void)
{
    return rs485_read_gpio_level(SENSOR_PWR_12V_SENSE_GPIO, false);
}

static bool rs485_ac_present(void)
{
    return rs485_read_gpio_level(SENSOR_AC_SENSE_GPIO, false);
}

static esp_err_t rs485_apply_wifi_control(bool enable, const char *ssid, const char *pw)
{
    // Optional AP-SSID/PW Update
    if (ssid && ssid[0]) {
        wifi_config_t cfg = {0};
        if (esp_wifi_get_config(WIFI_IF_AP, &cfg) == ESP_OK) {
            strlcpy((char *)cfg.ap.ssid, ssid, sizeof(cfg.ap.ssid));
            cfg.ap.ssid_len = strlen((char *)cfg.ap.ssid);
            if (pw && pw[0]) {
                strlcpy((char *)cfg.ap.password, pw, sizeof(cfg.ap.password));
                cfg.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
            } else {
                cfg.ap.password[0] = '\0';
                cfg.ap.authmode = WIFI_AUTH_OPEN;
            }
            esp_err_t cfg_err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
            if (cfg_err != ESP_OK) {
                ESP_LOGW(TAG, "WiFi config update failed: %s", esp_err_to_name(cfg_err));
            }
        }
    }

    esp_err_t err = ESP_OK;
    if (enable) {
        err = esp_wifi_start();
        if (err == ESP_ERR_WIFI_NOT_INIT) {
            ESP_LOGW(TAG, "WiFi not initialized");
        }
        if (err == ESP_ERR_WIFI_CONN || err == ESP_ERR_WIFI_NOT_STARTED || err == ESP_ERR_INVALID_STATE) {
            // Wenn schon aktiv oder AP ohne STA, als Erfolg behandeln
            err = ESP_OK;
        }
        s_wifi_active = (err == ESP_OK);
    } else {
        err = esp_wifi_stop();
        if (err == ESP_ERR_WIFI_NOT_INIT || err == ESP_ERR_WIFI_NOT_STARTED) {
            err = ESP_OK;
        }
        s_wifi_active = false;
    }
    return err;
}

static esp_err_t rs485_apply_lte_control(bool enable)
{
    // Deaktiviere Auto-Connect bei Aus, aktiviere danach wieder für Reconnects
    esp_err_t auto_err = usbh_modem_ppp_auto_connect(enable);
    if (auto_err != ESP_OK) {
        ESP_LOGW(TAG, "LTE auto_connect set failed: %s", esp_err_to_name(auto_err));
    }

    if (!enable) {
        esp_err_t stop_err = usbh_modem_ppp_stop();
        if (stop_err == ESP_ERR_INVALID_STATE) {
            stop_err = ESP_OK;
        }
        s_lte_active = false;
        return stop_err;
    }

    // Für Start muss auto_connect false sein; danach erneut aktivieren
    esp_err_t start_err = usbh_modem_ppp_auto_connect(false);
    if (start_err != ESP_OK) {
        ESP_LOGW(TAG, "LTE auto_connect disable before start failed: %s", esp_err_to_name(start_err));
    }
    start_err = usbh_modem_ppp_start(pdMS_TO_TICKS(10000));
    if (start_err == ESP_ERR_INVALID_STATE) {
        // Bereits aktiv
        start_err = ESP_OK;
    }
    s_lte_active = (start_err == ESP_OK);
    esp_err_t auto_reenable_err = usbh_modem_ppp_auto_connect(true);
    if (auto_reenable_err != ESP_OK) {
        ESP_LOGW(TAG, "LTE auto_connect re-enable failed: %s", esp_err_to_name(auto_reenable_err));
    }
    return start_err;
}

static void rs485_publish_full_snapshot(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }

    cJSON_AddStringToObject(root, "type", "full");
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    int64_t ts_ms = ((int64_t)tv.tv_sec * 1000) + (tv.tv_usec / 1000);
    cJSON_AddNumberToObject(root, "ts", round2((double)ts_ms));

    // HX711 (Gaswaage)
    hx711_snapshot_t hx = {0};
    if (hx711_app_get_snapshot(&hx) == ESP_OK && (hx.valid_a || hx.valid_b)) {
        cJSON *hx_obj = cJSON_CreateObject();
        if (hx.valid_a) {
            cJSON_AddNumberToObject(hx_obj, "a", round2(hx.kg_a));
        }
        if (hx.valid_b) {
            cJSON_AddNumberToObject(hx_obj, "b", round2(hx.kg_b));
        }
        if (hx.valid_a && hx.valid_b) {
            cJSON_AddNumberToObject(hx_obj, "sum", round2(hx.kg_a + hx.kg_b));
        }
        cJSON_AddNumberToObject(hx_obj, "ts", ts_us_to_epoch_ms(hx.timestamp_us));
        cJSON_AddItemToObject(root, "hx", hx_obj);
    } else {
        // Markiere HX als nicht verbunden, damit das Display keinen Placeholder-Fehler zählt
        cJSON *hx_obj = cJSON_CreateObject();
        cJSON_AddBoolToObject(hx_obj, "nc", true);
        cJSON_AddItemToObject(root, "hx", hx_obj);
    }

    // BME680 (Innen/Außen)
    bme680_snapshot_t bme = {0};
    bme680_app_get_snapshot(&bme);
    cJSON *bme_obj = cJSON_CreateObject();

    // Indoor (0x76)
    {
        cJSON *in = cJSON_CreateObject();
        if (bme.indoor.valid) {
            cJSON_AddNumberToObject(in, "temp_c", round2(bme.indoor.temperature_c));
            cJSON_AddNumberToObject(in, "rh_pct", round2(bme.indoor.humidity_pct));
            cJSON_AddNumberToObject(in, "press_hpa", round2(bme.indoor.pressure_hpa));
            if (bme.indoor.gas_valid) {
                cJSON_AddNumberToObject(in, "gas_kohm", round2(bme.indoor.gas_kohm));
            }
            if (bme.indoor.iaq_valid) {
                cJSON_AddNumberToObject(in, "iaq", round2(bme.indoor.iaq));
                cJSON_AddNumberToObject(in, "iaq_acc", round2(bme.indoor.iaq_accuracy));
                cJSON_AddNumberToObject(in, "eco2_ppm", round2(bme.indoor.eco2_ppm));
                cJSON_AddNumberToObject(in, "bvoc_ppm", round2(bme.indoor.bvoc_ppm));
            }
            cJSON_AddNumberToObject(in, "ts", ts_us_to_epoch_ms(bme.indoor.timestamp_us));
        } else {
            cJSON_AddNumberToObject(in, "temp_c", round2(0.0));
            cJSON_AddNumberToObject(in, "rh_pct", round2(0.0));
            cJSON_AddNumberToObject(in, "press_hpa", round2(0.0));
        }
        cJSON_AddItemToObject(bme_obj, "0x76", in);
    }

    // Outdoor (0x77)
    {
        cJSON *out = cJSON_CreateObject();
        if (bme.outdoor.valid) {
            cJSON_AddNumberToObject(out, "temp_c", round2(bme.outdoor.temperature_c));
            cJSON_AddNumberToObject(out, "rh_pct", round2(bme.outdoor.humidity_pct));
            cJSON_AddNumberToObject(out, "press_hpa", round2(bme.outdoor.pressure_hpa));
            double trend_out = update_pressure_trend(bme.outdoor.pressure_hpa, bme.outdoor.timestamp_us, &s_press_trend_out);
            cJSON_AddNumberToObject(out, "press_trend_hpa_h", round2(trend_out));
            const char *trend_state_out = pressure_trend_state(trend_out);
            if (trend_state_out) {
                cJSON_AddStringToObject(out, "press_trend_state", trend_state_out);
            }
            if (bme.outdoor.gas_valid) {
                cJSON_AddNumberToObject(out, "gas_kohm", round2(bme.outdoor.gas_kohm));
            }
            cJSON_AddNumberToObject(out, "ts", ts_us_to_epoch_ms(bme.outdoor.timestamp_us));
        } else {
            cJSON_AddNumberToObject(out, "temp_c", round2(0.0));
            cJSON_AddNumberToObject(out, "rh_pct", round2(0.0));
            cJSON_AddNumberToObject(out, "press_hpa", round2(0.0));
        }
        cJSON_AddItemToObject(bme_obj, "0x77", out);
    }

    cJSON_AddItemToObject(root, "bme", bme_obj);

    // BNO055 (IMU)
    web_wifi_imu_snapshot_t imu = {0};
    cJSON *imu_obj = cJSON_CreateObject();
    if (web_wifi_imu_get_snapshot(&imu) && imu.valid) {
        cJSON_AddNumberToObject(imu_obj, "yaw_deg", round2(imu.yaw_deg));
        cJSON_AddNumberToObject(imu_obj, "pitch_deg", round2(imu.pitch_deg));
        cJSON_AddNumberToObject(imu_obj, "roll_deg", round2(imu.roll_deg));
        cJSON_AddStringToObject(imu_obj, "hdg", heading_to_compass(imu.yaw_deg));

        cJSON *cal = cJSON_CreateObject();
        cJSON_AddNumberToObject(cal, "sys", round2(imu.cal_sys));
        cJSON_AddNumberToObject(cal, "gyro", round2(imu.cal_gyro));
        cJSON_AddNumberToObject(cal, "acc", round2(imu.cal_accel));
        cJSON_AddNumberToObject(cal, "mag", round2(imu.cal_mag));
        cJSON_AddItemToObject(imu_obj, "cal", cal);

        cJSON_AddBoolToObject(imu_obj, "calibrated", imu.calibrated);
        cJSON_AddNumberToObject(imu_obj, "ts", ts_us_to_epoch_ms(imu.timestamp_us));
    } else {
        // Placeholder IMU, falls noch keine gültigen Daten vorliegen
        cJSON_AddNumberToObject(imu_obj, "yaw_deg", round2(0.0));
        cJSON_AddNumberToObject(imu_obj, "pitch_deg", round2(0.0));
        cJSON_AddNumberToObject(imu_obj, "roll_deg", round2(0.0));
        cJSON_AddStringToObject(imu_obj, "hdg", "N");
        cJSON *cal = cJSON_CreateObject();
        cJSON_AddNumberToObject(cal, "sys", round2(0));
        cJSON_AddNumberToObject(cal, "gyro", round2(0));
        cJSON_AddNumberToObject(cal, "acc", round2(0));
        cJSON_AddNumberToObject(cal, "mag", round2(0));
        cJSON_AddItemToObject(imu_obj, "cal", cal);
        cJSON_AddBoolToObject(imu_obj, "calibrated", false);
        cJSON_AddNumberToObject(imu_obj, "ts", ts_us_to_epoch_ms(esp_timer_get_time()));
    }
    cJSON_AddItemToObject(root, "imu", imu_obj);

    // Batteriespannungen (GPIO7/8) in Volt + NC Flags
    int mv = 0;
    bool batt1_ok = (analog_read_mv(SENSOR_BATT1_ADC_CHANNEL, &mv) == ESP_OK);
    double batt1_v = batt1_ok ? ((double)mv / 1000.0) : 0.0;
    bool batt2_ok = (analog_read_mv(SENSOR_BATT2_ADC_CHANNEL, &mv) == ESP_OK);
    double batt2_v = batt2_ok ? ((double)mv / 1000.0) : 0.0;

    cJSON *bat = cJSON_CreateObject();
    cJSON_AddNumberToObject(bat, "b1", round2(batt1_v));
    cJSON_AddNumberToObject(bat, "b2", round2(batt2_v));
    cJSON_AddBoolToObject(bat, "nc1", !batt1_ok);
    cJSON_AddBoolToObject(bat, "nc2", !batt2_ok);
    cJSON_AddItemToObject(root, "bat", bat);

    // Tankfüllstände aus ADC (GPIO9/10) als Prozent (0-100) + NC Flags
    bool tank1_ok = (analog_read_mv(SENSOR_TANK1_ADC_CHANNEL, &mv) == ESP_OK);
    int tank1_pct = tank1_ok ? adc_mv_to_percent(mv) : 0;
    bool tank2_ok = (analog_read_mv(SENSOR_TANK2_ADC_CHANNEL, &mv) == ESP_OK);
    int tank2_pct = tank2_ok ? adc_mv_to_percent(mv) : 0;

    cJSON *tank = cJSON_CreateObject();
    cJSON_AddNumberToObject(tank, "t1", round2((double)tank1_pct));
    cJSON_AddNumberToObject(tank, "t2", round2((double)tank2_pct));
    cJSON_AddBoolToObject(tank, "nc1", !tank1_ok);
    cJSON_AddBoolToObject(tank, "nc2", !tank2_ok);
    cJSON_AddItemToObject(root, "tank", tank);

    // Analogeingänge GPIO7-10 (Batt/Tank mV)
    cJSON *adc = cJSON_CreateObject();
    mv = 0;
    if (analog_read_mv(SENSOR_BATT1_ADC_CHANNEL, &mv) == ESP_OK) {
        cJSON_AddNumberToObject(adc, "batt1_mv", round2((double)mv));
    } else {
        cJSON_AddBoolToObject(adc, "batt1_nc", true);
    }
    if (analog_read_mv(SENSOR_BATT2_ADC_CHANNEL, &mv) == ESP_OK) {
        cJSON_AddNumberToObject(adc, "batt2_mv", round2((double)mv));
    } else {
        cJSON_AddBoolToObject(adc, "batt2_nc", true);
    }
    if (analog_read_mv(SENSOR_TANK1_ADC_CHANNEL, &mv) == ESP_OK) {
        cJSON_AddNumberToObject(adc, "tank1_mv", round2((double)mv));
    } else {
        cJSON_AddBoolToObject(adc, "tank1_nc", true);
    }
    if (analog_read_mv(SENSOR_TANK2_ADC_CHANNEL, &mv) == ESP_OK) {
        cJSON_AddNumberToObject(adc, "tank2_mv", round2((double)mv));
    } else {
        cJSON_AddBoolToObject(adc, "tank2_nc", true);
    }
    cJSON_AddItemToObject(root, "adc", adc);

    // Steuer-/Power-Zustände
    bool board_power_on = rs485_board_power_on();
    bool ac_present = rs485_ac_present();
    cJSON *ctrl = cJSON_CreateObject();
    cJSON_AddBoolToObject(ctrl, "wifi_target", s_wifi_target_enabled);
    cJSON_AddBoolToObject(ctrl, "wifi_active", s_wifi_active);
    cJSON_AddBoolToObject(ctrl, "lte_target", s_lte_target_enabled);
    cJSON_AddBoolToObject(ctrl, "lte_active", s_lte_active);
    cJSON_AddBoolToObject(ctrl, "pwr_on", board_power_on);
    cJSON_AddBoolToObject(ctrl, "ac_present", ac_present);
    cJSON_AddItemToObject(root, "ctrl", ctrl);

    // Gasdaten Platzhalter
    cJSON *gas = cJSON_CreateObject();
    cJSON_AddNumberToObject(gas, "active", 0);
    cJSON_AddNumberToObject(gas, "net", 0.0);
    cJSON_AddNumberToObject(gas, "rate1h", 0.0);
    cJSON_AddNumberToObject(gas, "rate2h", 0.0);
    cJSON_AddNumberToObject(gas, "rest_h", 0.0);
    cJSON_AddNumberToObject(gas, "net_a", 0.0);
    cJSON_AddNumberToObject(gas, "net_b", 0.0);
    cJSON_AddNumberToObject(gas, "cap_kg", 0.0);
    cJSON_AddNumberToObject(gas, "pct", 0.0);
    cJSON_AddNumberToObject(gas, "pct_a", 0.0);
    cJSON_AddNumberToObject(gas, "pct_b", 0.0);
    cJSON_AddItemToObject(root, "gas", gas);

    bool request_ack = s_display_ready;
    esp_err_t send_err = rs485_send_frame("full", root, request_ack);
    if (send_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send full snapshot: %s", esp_err_to_name(send_err));
    }
    cJSON_Delete(root);
}

static bool rs485_execute_command(const cJSON *root, const char *cmd_str, esp_err_t *out_err)
{
    if (!cmd_str) {
        if (out_err) {
            *out_err = ESP_ERR_INVALID_ARG;
        }
        return false;
    }

    esp_err_t cmd_err = ESP_OK;
    bool handled = true;

    if (strcmp(cmd_str, "display_ready") == 0) {
        rs485_handle_display_ready();
    } else if (strcmp(cmd_str, "level_start") == 0 || strcmp(cmd_str, "level_stop") == 0) {
        ESP_LOGI(TAG, "Level command: %s", cmd_str);
        // (Kein Level-Transfer implementiert; nur Logging/Ack)
    } else if (strcmp(cmd_str, "tare_a") == 0 || strcmp(cmd_str, "tare_b") == 0) {
        ESP_LOGI(TAG, "Tare command: %s", cmd_str);
        // HX711 nicht aktiv, daher derzeit nur Logging
    } else if (strcmp(cmd_str, "gas_bottle_replace") == 0) {
        const cJSON *slot = cJSON_GetObjectItem(root, "slot");
        const cJSON *channel = cJSON_GetObjectItem(root, "channel");
        const char *chan = (cJSON_IsString(channel) && channel->valuestring) ? channel->valuestring : "";
        int slot_idx = cJSON_IsNumber(slot) ? (int)slot->valuedouble : -1;
        ESP_LOGI(TAG, "Gas bottle replace: slot=%d channel=%s", slot_idx, chan);
        // Keine Gaslogik vorhanden; placeholder
    } else if (strcmp(cmd_str, "wifi_control") == 0) {
        const cJSON *enable = cJSON_GetObjectItem(root, "enable");
        const cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
        const cJSON *pw = cJSON_GetObjectItem(root, "pw");
        bool en = cJSON_IsBool(enable) ? cJSON_IsTrue(enable) : true;
        const char *ssid_str = (cJSON_IsString(ssid) && ssid->valuestring) ? ssid->valuestring : "";
        const char *pw_str = (cJSON_IsString(pw) && pw->valuestring) ? pw->valuestring : "";
        s_wifi_target_enabled = en;
        cmd_err = rs485_apply_wifi_control(en, ssid_str, pw_str);
        ESP_LOGI(TAG, "WiFi control: enable=%d ssid='%s' pw_len=%d err=%s",
                 en ? 1 : 0,
                 ssid_str,
                 (int)strlen(pw_str),
                 esp_err_to_name(cmd_err));
    } else if (strcmp(cmd_str, "lte_control") == 0) {
        const cJSON *enable = cJSON_GetObjectItem(root, "enable");
        bool en = cJSON_IsBool(enable) ? cJSON_IsTrue(enable) : true;
        s_lte_target_enabled = en;
        cmd_err = rs485_apply_lte_control(en);
        ESP_LOGI(TAG, "LTE control: enable=%d err=%s", en ? 1 : 0, esp_err_to_name(cmd_err));
    } else {
        handled = false;
        cmd_err = ESP_ERR_INVALID_ARG;
    }

    if (out_err) {
        *out_err = cmd_err;
    }
    return handled;
}

static bool rs485_line_is_ascii(const char *line)
{
    if (!line) {
        return false;
    }
    const unsigned char *p = (const unsigned char *)line;
    while (*p) {
        if (*p < 0x20 || *p > 0x7E) {
            ESP_LOGW(TAG, "RS485 line dropped (non-ASCII 0x%02X)", (unsigned)*p);
            return false;
        }
        ++p;
    }
    return true;
}

static void rs485_process_rx_line(const char *line)
{
    if (!line) {
        return;
    }

    while (*line && (unsigned char)*line < 0x20) {
        ++line;
    }
    if (*line == '\0') {
        return;
    }

    if (!rs485_line_is_ascii(line)) {
        return;
    }

    while (*line && *line != '{' && *line != '[') {
        ++line;
    }
    if (*line == '\0') {
        ESP_LOGW(TAG, "RX line without JSON start ignored");
        return;
    }

    const char *end_brace = strrchr(line, '}');
    const char *end_bracket = strrchr(line, ']');
    const char *end = end_brace;
    if (end_bracket && (!end || end_bracket > end)) {
        end = end_bracket;
    }
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
    } else if (type && strcmp(type, "hb") == 0) {
        s_last_rx_heartbeat_us = esp_timer_get_time();
        if (!s_display_ready) {
            rs485_handle_display_ready();
        }
        ack_success = true;
        need_ack = false;
    } else if (type && strcmp(type, "hello") == 0) {
        s_last_rx_heartbeat_us = esp_timer_get_time();
        ack_success = true;
        need_ack = false;
        rs485_handle_display_ready();
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
}

static void rs485_rx_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "RX task started");
    TickType_t last_stack_log = xTaskGetTickCount();
    size_t line_pos = 0;

    while (true) {
        int len = womo_rs485_read(s_rx_buffer, sizeof(s_rx_buffer) - 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            s_last_rx_us = esp_timer_get_time();
            for (int i = 0; i < len; ++i) {
            char c = (char)s_rx_buffer[i];

                if (c == '\n' || c == '\r') {
                    if (line_pos > 0) {
                        s_rx_line_buffer[line_pos] = '\0';
                        s_last_rx_line_us = s_last_rx_us;
                        rs485_process_rx_line(s_rx_line_buffer);
                        line_pos = 0;
                    }
                } else if ((unsigned char)c >= 0x20 && (unsigned char)c <= 0x7E) {
                    if (line_pos < sizeof(s_rx_line_buffer) - 1) {
                        s_rx_line_buffer[line_pos++] = c;
                    } else {
                        line_pos = 0;
                    }
                } else {
                    // Ignore noise/non-ASCII
                    line_pos = 0;
                }
            }
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_stack_log) >= pdMS_TO_TICKS(RS485_STACK_LOG_INTERVAL_MS)) {
            UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGI(TAG, "RX stack watermark: %u words (%u bytes)", (unsigned)watermark, (unsigned)(watermark * sizeof(StackType_t)));
            last_stack_log = now;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void rs485_tx_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "TX task started");

    const TickType_t full_interval = pdMS_TO_TICKS(RS485_FULL_INTERVAL_MS);
    TickType_t last_full_send = xTaskGetTickCount() - full_interval;
    const TickType_t loop_delay = pdMS_TO_TICKS(100);

    const TickType_t hello_interval = ms_to_ticks(RS485_HELLO_PENDING_MS);
    const TickType_t heartbeat_interval = (RS485_HEARTBEAT_INTERVAL_MS > 0)
                                              ? ms_to_ticks(RS485_HEARTBEAT_INTERVAL_MS)
                                              : 0;
    TickType_t last_hello_send = xTaskGetTickCount() - hello_interval;
    TickType_t last_heartbeat_send = xTaskGetTickCount();

    rs485_send_hello();
    last_hello_send = xTaskGetTickCount();

    while (true) {
        TickType_t now = xTaskGetTickCount();
        bool send_full_due = ((now - last_full_send) >= full_interval);

        if (!s_display_ready && (now - last_hello_send) >= hello_interval) {
            rs485_send_hello();
            last_hello_send = now;
        }

        if (heartbeat_interval > 0 && s_display_ready && (now - last_heartbeat_send) >= heartbeat_interval) {
            rs485_send_heartbeat();
            last_heartbeat_send = now;
        }

        bool hb_after_last_full = (s_last_rx_heartbeat_us > s_last_full_heartbeat_us);
        bool full_pending = rs485_label_pending("full");
        if (s_display_ready && send_full_due && hb_after_last_full && !full_pending) {
            rs485_publish_full_snapshot();
            last_full_send = now;
            s_last_full_heartbeat_us = s_last_rx_heartbeat_us;
        }

        rs485_check_command_timeouts();
        vTaskDelay(loop_delay);
    }
}

esp_err_t rs485_modem_init(void)
{
    if (s_tx_task || s_rx_task) {
        return ESP_OK;
    }

    esp_err_t err = womo_rs485_init();
    if (err != ESP_OK) {
        return err;
    }

    rs485_power_gpio_init();

    s_tx_mutex = xSemaphoreCreateMutex();
    if (!s_tx_mutex) {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t rx_created = xTaskCreate(rs485_rx_task, "rs485_rx", 8192, NULL, 5, &s_rx_task);
    if (rx_created != pdPASS) {
        return ESP_FAIL;
    }

    BaseType_t tx_created = xTaskCreate(rs485_tx_task, "rs485_tx", 6144, NULL, 5, &s_tx_task);
    if (tx_created != pdPASS) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "RS485 modem link ready (UART%d)", (int)SENSOR_RS485_UART_PORT);
    return ESP_OK;
}
