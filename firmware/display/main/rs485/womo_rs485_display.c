/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#include "womo_rs485_display.h"
#include "womo_config.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include "network/womo_wifi.h"
#include "time/womo_time.h"
#include "hardware/buzzer.h"

static const char *TAG = "rs485_display";

// RS485 Hardware Configuration
#define RS485_UART_NUM      UART_NUM_1
#define RS485_TX_GPIO       16  // Hardware connector pin (Waveshare: TXD=16)
#define RS485_RX_GPIO       15  // Hardware connector pin (Waveshare: RXD=15)
#define RS485_DE_GPIO       -1  // DE handled by onboard transistor logic
#define RS485_BAUD_RATE     WOMO_RS485_BAUDRATE
#define RS485_BUF_SIZE      16384
#define RS485_LINE_MAX      16384
// Maximale Länge für ein unvollständiges Fragment, bevor wir es verwerfen
#define RS485_FRAGMENT_MAX_KEEP 4096
#define RS485_RX_IDLE_GUARD_US   30000   // TX-Sperre nach RX für ca. 30 ms
#define RS485_LINE_STALE_US      200000  // 200 ms: line_buffer-Daten ohne CRLF verwerfen
#define RS485_FRAG_STALE_US      500000  // 500 ms: Fragment-Buffer verwerfen
    
    // Log einen kurzen Ausschnitt eines Fragments (Anfang/Ende), um Framing-Probleme schneller zu sehen
    static void log_fragment_preview(const char *buf, size_t len)
    {
        if (!buf || len == 0) {
            ESP_LOGW(TAG, "Fragment preview: empty");
            return;
        }
    
        const size_t head_len = (len < 16) ? len : 16;
        const size_t tail_len = (len <= 32) ? 0 : 16;
        char head[17] = {0};
        char tail[17] = {0};
    
        memcpy(head, buf, head_len);
        head[head_len] = '\0';
    
        if (tail_len > 0) {
            memcpy(tail, buf + (len - tail_len), tail_len);
            tail[tail_len] = '\0';
            ESP_LOGW(TAG, "Fragment len=%u head=\"%s\" tail=\"%s\"", (unsigned)len, head, tail);
        } else {
            ESP_LOGW(TAG, "Fragment len=%u head=\"%s\"", (unsigned)len, head);
        }
    }

#define RS485_HEARTBEAT_INTERVAL_MS   WOMO_RS485_HEARTBEAT_INTERVAL_MS
#define RS485_COMMAND_TIMEOUT_MS      WOMO_RS485_COMMAND_TIMEOUT_MS
#define RS485_MAX_PENDING_CMDS        WOMO_RS485_MAX_PENDING_CMDS

// State
static bool s_initialized = false;
static womo_sensor_data_t s_latest_data = {0};
static SemaphoreHandle_t s_data_mutex = NULL;
static SemaphoreHandle_t s_tx_mutex = NULL;
static womo_rs485_data_cb_t s_data_callback = NULL;
static void *s_callback_user_data = NULL;
static womo_rs485_event_cb_t s_event_callback = NULL;
static void *s_event_user_data = NULL;
static TaskHandle_t s_heartbeat_task = NULL;
static uint8_t s_uart_buffer[RS485_BUF_SIZE] = {0};
static char s_line_buffer[RS485_LINE_MAX] = {0};
static char s_fragment_buffer[RS485_LINE_MAX] = {0};
static size_t s_fragment_len = 0;
static int64_t s_fragment_start_us = 0;
static volatile bool s_rx_line_pending = false;  // true solange RX-Task eine Zeile akkumuliert (kein CRLF)
static volatile bool s_tx_active = false;        // true während UART TX läuft (Echo-Unterdrückung)

typedef struct {
    bool in_use;
    bool warned;
    uint32_t seq;
    char label[32];
    int64_t sent_us;
} rs485_pending_cmd_t;

static rs485_pending_cmd_t s_pending_cmds[RS485_MAX_PENDING_CMDS] = {0};
static portMUX_TYPE s_pending_cmd_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_seq_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_tx_seq_counter = 1;
static uint32_t s_last_rx_seq = 0;
static uint32_t s_last_ack_seq = 0;
static int64_t s_last_ack_time_us = 0;
static int64_t s_last_heartbeat_rx_us = 0;
static int64_t s_last_heartbeat_tx_us = 0;
static int64_t s_last_rx_activity_us = 0;

static void rs485_emit_event(womo_rs485_event_t event)
{
    if (s_event_callback) {
        s_event_callback(event, s_event_user_data);
    }
}

// Forward declarations
static void rs485_rx_task(void *arg);
static void rs485_heartbeat_task(void *arg);
static void rs485_process_line(char *line_buffer, size_t captured_len);
static bool rs485_find_json_end(const char *start, char **end_ptr);
static void parse_json_packet(const char *json_str, size_t raw_line_len, bool truncated_before_parse);
static bool rs485_recent_rx_activity(void);
static esp_err_t rs485_write_and_wait(const char *payload);
static esp_err_t rs485_send_simple_command(const char *cmd);
static uint32_t rs485_next_seq(void);
static esp_err_t rs485_send_frame_ex(cJSON *root, const char *pending_cmd, bool priority);
static esp_err_t rs485_send_frame(cJSON *root, const char *pending_cmd);
static esp_err_t rs485_send_ack(uint32_t rx_seq, bool ok, const char *cmd, const char *err_msg);
static void rs485_handle_ack_packet(const cJSON *root);
static void rs485_record_rx_seq(uint32_t seq);
static void rs485_add_pending(uint32_t seq, const char *label);
static void rs485_resolve_pending(uint32_t seq, bool success, const char *label_from_packet, const char *err_text);
static void rs485_check_command_timeouts(void);
static esp_err_t rs485_send_heartbeat(void);

static bool rs485_recent_rx_activity(void)
{
    if (s_last_rx_activity_us == 0) {
        return false;
    }

    const int64_t now = esp_timer_get_time();
    return (now - s_last_rx_activity_us) < RS485_RX_IDLE_GUARD_US;
}

static esp_err_t rs485_write_and_wait(const char *payload)
{
    if (!s_initialized || !payload) {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t len = strlen(payload);
    if (len == 0) {
        return ESP_OK;
    }

    // ── Auto-DE Preamble ──────────────────────────────────────────────
    // Der Auto-DE-Transistor auf dem Waveshare-Board toggelt DE bei
    // JEDEM Stop-Bit (TX→HIGH für ~8,7 µs bei 115200 Baud).
    // Dadurch wechselt der Transceiver bei jedem Byte kurz auf RX und
    // der Bus geht in den (undefinierten) Idle-Zustand → Framing-Errors.
    // Workaround: 32 Null-Bytes als Preamble senden, damit der Empfänger
    // genug Transitionen sieht, um sich einzusynchronisieren.
    // Der Sensor-Parser ignoriert Bytes außerhalb 0x20–0x7E automatisch.
    static const uint8_t preamble[32] = {0};
    size_t total = sizeof(preamble) + len;
    char *tx_buf = malloc(total);
    if (!tx_buf) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(tx_buf, preamble, sizeof(preamble));
    memcpy(tx_buf + sizeof(preamble), payload, len);

    s_tx_active = true;
    int written = uart_write_bytes(RS485_UART_NUM, tx_buf, total);
    free(tx_buf);

    if (written <= 0) {
        s_tx_active = false;
        return ESP_FAIL;
    }

    // Timeout muss groß genug sein für die aktuelle Baudrate.
    // Bei 9600 Baud braucht ein 200-Byte-Paket ~210 ms → 500 ms Timeout.
    // Bei 115200 Baud braucht es ~17 ms → 500 ms schadet nicht.
    esp_err_t wait_err = uart_wait_tx_done(RS485_UART_NUM, pdMS_TO_TICKS(WOMO_RS485_TX_WAIT_TIMEOUT_MS));
    if (wait_err != ESP_OK) {
        ESP_LOGW(TAG, "uart_wait_tx_done failed: %s", esp_err_to_name(wait_err));
        s_tx_active = false;
        return wait_err;
    }

    // Auto-DE-Transistor: TX-Echo landet im RX-Buffer.
    // Half-Duplex: Sensor wartet 150 ms nach RX vor eigenem TX,
    // daher sind direkt nach unserem TX keine Sensor-Bytes im Buffer.
    uart_flush_input(RS485_UART_NUM);
    s_tx_active = false;

    return ESP_OK;
}

static uint32_t rs485_next_seq(void)
{
    uint32_t seq = 0;
    taskENTER_CRITICAL(&s_seq_lock);
    seq = s_tx_seq_counter++;
    if (s_tx_seq_counter == 0) {
        s_tx_seq_counter = 1;
    }
    taskEXIT_CRITICAL(&s_seq_lock);
    if (seq == 0) {
        return rs485_next_seq();
    }
    return seq;
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
    strncpy(entry.label, label, sizeof(entry.label) - 1);
    entry.label[sizeof(entry.label) - 1] = '\0';

    bool stored = false;
    taskENTER_CRITICAL(&s_pending_cmd_lock);
    for (size_t i = 0; i < RS485_MAX_PENDING_CMDS; ++i) {
        if (s_pending_cmds[i].in_use && strncmp(s_pending_cmds[i].label, label, sizeof(s_pending_cmds[i].label)) == 0) {
            /* Bereits ausstehend: seq + Zeitstempel aktualisieren, damit das ACK
             * der erneut gesendeten Nachricht den Eintrag korrekt auflöst. */
            s_pending_cmds[i].seq     = seq;
            s_pending_cmds[i].sent_us = entry.sent_us;
            s_pending_cmds[i].warned  = false;
            stored = true;
            break;
        }
        if (!s_pending_cmds[i].in_use && !stored) {
            s_pending_cmds[i] = entry;
            stored = true;
        }
    }
    taskEXIT_CRITICAL(&s_pending_cmd_lock);

    if (!stored) {
        ESP_LOGW(TAG, "Pending command buffer full, dropping tracking for %s (seq=%lu)",
                 label, (unsigned long)seq);
    }
}

static void rs485_resolve_pending(uint32_t seq,
                                  bool success,
                                  const char *label_from_packet,
                                  const char *err_text)
{
    if (seq == 0) {
        return;
    }

    rs485_pending_cmd_t resolved = {0};
    bool found = false;

    taskENTER_CRITICAL(&s_pending_cmd_lock);
    for (size_t i = 0; i < RS485_MAX_PENDING_CMDS; ++i) {
        if (s_pending_cmds[i].in_use && s_pending_cmds[i].seq == seq) {
            resolved = s_pending_cmds[i];
            s_pending_cmds[i].in_use = false;
            s_pending_cmds[i].warned = false;
            found = true;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_pending_cmd_lock);

    const char *label = label_from_packet;
    if ((!label || label[0] == '\0') && resolved.label[0] != '\0') {
        label = resolved.label;
    }
    if (!label || label[0] == '\0') {
        label = "unknown";
    }

    if (found) {
        ESP_LOGI(TAG,
                 "RS485 ACK for %s (seq=%lu) status=%s",
                 label,
                 (unsigned long)seq,
                 success ? "ok" : "err");
    } else {
        ESP_LOGW(TAG,
                 "Unexpected RS485 ACK for seq=%lu (label=%s)",
                 (unsigned long)seq,
                 label);
    }

    if (!success && err_text && err_text[0] != '\0') {
        ESP_LOGW(TAG, "RS485 ACK error detail: %s", err_text);
    }
}

static void rs485_check_command_timeouts(void)
{
    typedef struct {
        uint32_t seq;
        char label[32];
        int64_t age_us;
    } timeout_log_t;

    timeout_log_t expired[RS485_MAX_PENDING_CMDS];
    size_t expired_count = 0;
    const int64_t now_us = esp_timer_get_time();
    const int64_t timeout_us = (int64_t)RS485_COMMAND_TIMEOUT_MS * 1000;

    taskENTER_CRITICAL(&s_pending_cmd_lock);
    for (size_t i = 0; i < RS485_MAX_PENDING_CMDS; ++i) {
        if (!s_pending_cmds[i].in_use || s_pending_cmds[i].warned) {
            continue;
        }
        int64_t age_us = now_us - s_pending_cmds[i].sent_us;
        if (age_us >= timeout_us) {
            s_pending_cmds[i].in_use = false;   /* Slot freigeben, nicht nur warnen */
            s_pending_cmds[i].warned = false;
            if (expired_count < RS485_MAX_PENDING_CMDS) {
                expired[expired_count].seq = s_pending_cmds[i].seq;
                expired[expired_count].age_us = age_us;
                strncpy(expired[expired_count].label,
                        s_pending_cmds[i].label,
                        sizeof(expired[expired_count].label) - 1);
                expired[expired_count].label[sizeof(expired[expired_count].label) - 1] = '\0';
                expired_count++;
            }
        }
    }
    taskEXIT_CRITICAL(&s_pending_cmd_lock);

    for (size_t i = 0; i < expired_count; ++i) {
        double age_ms = (double)expired[i].age_us / 1000.0;
        ESP_LOGW(TAG,
                 "RS485 command %s (seq=%lu) waiting for ACK for %.0f ms",
                 expired[i].label[0] ? expired[i].label : "unknown",
                 (unsigned long)expired[i].seq,
                 age_ms);
    }
}

static esp_err_t rs485_send_frame_ex(cJSON *root, const char *pending_cmd, bool priority)
{
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t seq = rs485_next_seq();
    cJSON_AddNumberToObject(root, "seq", (double)seq);

    if (!cJSON_GetObjectItemCaseSensitive(root, "ts")) {
        cJSON_AddNumberToObject(root, "ts", (double)(esp_timer_get_time() / 1000));
    }

    if (!cJSON_GetObjectItemCaseSensitive(root, "need_ack")) {
        cJSON_AddBoolToObject(root, "need_ack", true);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }

    size_t len = strlen(json_str);
    char *payload = malloc(len + 3);
    if (!payload) {
        cJSON_free(json_str);
        return ESP_ERR_NO_MEM;
    }

    memcpy(payload, json_str, len);
    payload[len]     = '\r';
    payload[len + 1] = '\n';
    payload[len + 2] = '\0';

    TickType_t mutex_ticks = priority ? pdMS_TO_TICKS(500) : 0;
    if (s_tx_mutex && xSemaphoreTake(s_tx_mutex, mutex_ticks) != pdTRUE) {
        free(payload);
        cJSON_free(json_str);
        return priority ? ESP_ERR_TIMEOUT : ESP_OK;
    }

    esp_err_t err = rs485_write_and_wait(payload);

    if (s_tx_mutex) {
        xSemaphoreGive(s_tx_mutex);
    }

    free(payload);
    cJSON_free(json_str);

    if (err == ESP_OK && pending_cmd && pending_cmd[0] != '\0') {
        rs485_add_pending(seq, pending_cmd);
    }

    return err;
}

static esp_err_t rs485_send_frame(cJSON *root, const char *pending_cmd)
{
    return rs485_send_frame_ex(root, pending_cmd, true);
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
    cJSON_AddNumberToObject(root, "ack_seq", (double)rx_seq);
    cJSON_AddStringToObject(root, "status", ok ? "ok" : "err");
    cJSON_AddBoolToObject(root, "need_ack", false);
    if (cmd && cmd[0] != '\0') {
        cJSON_AddStringToObject(root, "cmd", cmd);
    }
    if (!ok && err_msg && err_msg[0] != '\0') {
        cJSON_AddStringToObject(root, "err", err_msg);
    }

    esp_err_t err = rs485_send_frame(root, NULL);
    cJSON_Delete(root);
    return err;
}

static esp_err_t rs485_send_heartbeat(void)
{
    // Heartbeat nur senden wenn Sensorboard länger als 10 s nicht gesendet hat.
    // Während normaler Kommunikation (Sensorboard sendet alle 2 s) entfällt
    // der Display-Heartbeat komplett → kein TX-Echo auf Half-Duplex-Bus.
    if (s_last_rx_activity_us != 0) {
        int64_t silence_us = esp_timer_get_time() - s_last_rx_activity_us;
        if (silence_us < 10000000) {   // < 10 Sekunden
            return ESP_OK;
        }
    }

    if (rs485_recent_rx_activity() || s_rx_line_pending) {
        return ESP_OK;
    }

    cJSON *hb = cJSON_CreateObject();
    if (!hb) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(hb, "type", "hb");
    cJSON_AddBoolToObject(hb, "need_ack", false);
    cJSON_AddNumberToObject(hb, "uptime", (double)esp_timer_get_time() / 1000000.0);
    if (s_last_rx_seq != 0) {
        cJSON_AddNumberToObject(hb, "rx_seq", (double)s_last_rx_seq);
    }
    if (s_last_ack_seq != 0) {
        cJSON_AddNumberToObject(hb, "last_ack", (double)s_last_ack_seq);
    }

    esp_err_t err = rs485_send_frame_ex(hb, NULL, false);
    if (err == ESP_OK) {
        s_last_heartbeat_tx_us = esp_timer_get_time();
    }
    cJSON_Delete(hb);
    return err;
}

static void rs485_heartbeat_task(void *arg)
{
    (void)arg;
    while (true) {
        if (s_initialized) {
            esp_err_t hb_err = rs485_send_heartbeat();
            if (hb_err != ESP_OK) {
                ESP_LOGW(TAG, "Heartbeat send failed: %s", esp_err_to_name(hb_err));
            }
            rs485_check_command_timeouts();
        }
        vTaskDelay(pdMS_TO_TICKS(RS485_HEARTBEAT_INTERVAL_MS));
    }
}

static void rs485_handle_ack_packet(const cJSON *root)
{
    if (!root) {
        return;
    }

    const cJSON *ack_val = cJSON_GetObjectItem(root, "ack_seq");
    if (!cJSON_IsNumber(ack_val)) {
        ack_val = cJSON_GetObjectItem(root, "ack");   /* Fallback */
    }
    if (!cJSON_IsNumber(ack_val)) {
        ESP_LOGW(TAG, "ACK packet missing numeric ack_seq/ack");
        return;
    }

    uint32_t ack_seq = (uint32_t)ack_val->valuedouble;
    const cJSON *status = cJSON_GetObjectItem(root, "status");
    const char *status_str = (cJSON_IsString(status) && status->valuestring)
                                 ? status->valuestring
                                 : "ok";
    bool success = (strcmp(status_str, "ok") == 0);
    const cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    const char *cmd_str = (cJSON_IsString(cmd) && cmd->valuestring)
                              ? cmd->valuestring
                              : NULL;
    const cJSON *err = cJSON_GetObjectItem(root, "err");
    const char *err_str = (cJSON_IsString(err) && err->valuestring)
                              ? err->valuestring
                              : NULL;

    rs485_resolve_pending(ack_seq, success, cmd_str, err_str);

    taskENTER_CRITICAL(&s_seq_lock);
    s_last_ack_seq = ack_seq;
    s_last_ack_time_us = esp_timer_get_time();
    taskEXIT_CRITICAL(&s_seq_lock);
}

static esp_err_t rs485_send_simple_command(const char *cmd)
{
    if (!cmd) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "cmd", cmd);
    esp_err_t err = rs485_send_frame(root, cmd);
    cJSON_Delete(root);
    return err;
}

esp_err_t womo_rs485_display_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    
    // Create mutexes for data protection and TX serialization
    s_data_mutex = xSemaphoreCreateMutex();
    if (!s_data_mutex) {
        ESP_LOGE(TAG, "Failed to create data mutex");
        return ESP_ERR_NO_MEM;
    }
    s_tx_mutex = xSemaphoreCreateMutex();
    if (!s_tx_mutex) {
        ESP_LOGE(TAG, "Failed to create TX mutex");
        vSemaphoreDelete(s_data_mutex);
        s_data_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    // Configure UART (like demo example)
    uart_config_t uart_config = {
        .baud_rate = RS485_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,  // DEFAULT (wie alte Version + Waveshare-Demo)
    };
    
    // Install driver (RX buffer, TX buffer=0 wie alte Version + Waveshare-Demo)
    esp_err_t err = uart_driver_install(RS485_UART_NUM, RS485_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_data_mutex);
        vSemaphoreDelete(s_tx_mutex);
        s_data_mutex = NULL;
        s_tx_mutex = NULL;
        return err;
    }
    
    // Configure parameters second
    err = uart_param_config(RS485_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(err));
        uart_driver_delete(RS485_UART_NUM);
        vSemaphoreDelete(s_data_mutex);
        vSemaphoreDelete(s_tx_mutex);
        s_data_mutex = NULL;
        s_tx_mutex = NULL;
        return err;
    }
    
    // Set pins third - TX must be set even for RX-only to control DE/RE via transistor
    err = uart_set_pin(RS485_UART_NUM,
                       RS485_TX_GPIO,
                       RS485_RX_GPIO,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        uart_driver_delete(RS485_UART_NUM);
        vSemaphoreDelete(s_data_mutex);
        vSemaphoreDelete(s_tx_mutex);
        s_data_mutex = NULL;
        s_tx_mutex = NULL;
        return err;
    }
    
    ESP_LOGI(TAG, "UART pins configured: TX=%d RX=%d (TX controls DE/RE via transistor)", 
             RS485_TX_GPIO, RS485_RX_GPIO);
    
    // HINWEIS: UART_SIGNAL_TXD_INV darf NICHT gesetzt werden!
    // Der Auto-DE-Transistor auf dem Waveshare-Board erkennt TX-Aktivität
    // am Pin-Level (idle=HIGH→RX, active=LOW→TX). Bei invertiertem TX
    // ist idle=LOW → Transceiver permanent im TX-Modus → RX tot.

    // Plain UART-Modus (wie alte funktionierende Version + Waveshare-Demo):
    // Kein uart_set_mode, kein uart_set_rx_timeout, kein RS485 Half-Duplex.
    // Auto-DE-Transistor steuert DE/RE über TX-Pin-Aktivität.

    // ── Loopback-Diagnose: UART intern kurzschließen (TX→RX ohne Hardware) ──
    {
        uart_set_loop_back(RS485_UART_NUM, true);
        uart_flush_input(RS485_UART_NUM);
        const char *test_msg = "LOOPBACK_OK\r\n";
        int test_len = (int)strlen(test_msg);
        uart_write_bytes(RS485_UART_NUM, test_msg, test_len);
        uart_wait_tx_done(RS485_UART_NUM, pdMS_TO_TICKS(100));

        uint8_t lb_buf[32];
        int lb_read = uart_read_bytes(RS485_UART_NUM, lb_buf, sizeof(lb_buf) - 1,
                                      pdMS_TO_TICKS(100));
        if (lb_read > 0) {
            lb_buf[lb_read] = '\0';
            ESP_LOGI(TAG, "LOOPBACK OK: sent %d, recv %d bytes → UART%d TX+RX funktioniert",
                     test_len, lb_read, RS485_UART_NUM);
        } else {
            ESP_LOGE(TAG, "LOOPBACK FAIL: sent %d, recv 0 bytes → UART%d TX defekt!",
                     test_len, RS485_UART_NUM);
        }
        uart_set_loop_back(RS485_UART_NUM, false);
        uart_flush_input(RS485_UART_NUM);
    }

    ESP_LOGI(TAG, "RS485 plain UART mode (auto-DE transistor, post-TX echo flush)");
    
    // Start RX task with large stack (16KB for JSON parsing + LVGL callback + buffers)
    BaseType_t task_created = xTaskCreateWithCaps(rs485_rx_task, "rs485_rx", 16384, NULL, 5, NULL,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create RX task");
        uart_driver_delete(RS485_UART_NUM);
        vSemaphoreDelete(s_data_mutex);
        vSemaphoreDelete(s_tx_mutex);
        s_data_mutex = NULL;
        s_tx_mutex = NULL;
        return ESP_FAIL;
    }
    
    s_initialized = true;
    if (!s_heartbeat_task) {
        BaseType_t hb_created = xTaskCreateWithCaps(rs485_heartbeat_task,
                                            "rs485_hb",
                                            4096,
                                            NULL,
                                            4,
                                            &s_heartbeat_task,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (hb_created != pdPASS) {
            ESP_LOGW(TAG, "Failed to create heartbeat task");
        }
    }
    ESP_LOGI(TAG, "RS485 display receiver initialized (UART%d, %d baud)", RS485_UART_NUM, RS485_BAUD_RATE);
    return ESP_OK;
}

void womo_rs485_set_data_callback(womo_rs485_data_cb_t callback, void *user_data)
{
    s_data_callback = callback;
    s_callback_user_data = user_data;
}

void womo_rs485_set_event_callback(womo_rs485_event_cb_t callback, void *user_data)
{
    s_event_callback = callback;
    s_event_user_data = user_data;
}

bool womo_rs485_get_latest_data(womo_sensor_data_t *data)
{
    if (!data || !s_initialized) {
        return false;
    }
    
    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(data, &s_latest_data, sizeof(womo_sensor_data_t));
        xSemaphoreGive(s_data_mutex);
        return true;
    }
    
    return false;
}

esp_err_t womo_rs485_send_level_start(void)
{
    esp_err_t err = rs485_send_simple_command("level_start");
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent level_start command");
    }
    return err;
}

esp_err_t womo_rs485_send_level_stop(void)
{
    esp_err_t err = rs485_send_simple_command("level_stop");
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent level_stop command");
    }
    return err;
}

esp_err_t womo_rs485_send_tare_a(void)
{
    esp_err_t err = rs485_send_simple_command("tare_a");
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent tare_a command");
    }
    return err;
}

esp_err_t womo_rs485_send_tare_b(void)
{
    esp_err_t err = rs485_send_simple_command("tare_b");
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent tare_b command");
    }
    return err;
}

esp_err_t womo_rs485_send_display_ready(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "cmd", "display_ready");
    cJSON_AddStringToObject(root, "role", "display");
    cJSON_AddNumberToObject(root, "uptime", (double)esp_timer_get_time() / 1000000.0);

    esp_err_t err = rs485_send_frame(root, "display_ready");
    cJSON_Delete(root);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent display_ready acknowledgement");
    } else {
        ESP_LOGW(TAG, "Failed to send display_ready acknowledgement: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t womo_rs485_send_wifi_control(bool enable, const char *ssid, const char *password)
{
    if (enable) {
        bool have_ssid = (ssid && ssid[0] != '\0');
        if (have_ssid) {
            cJSON *root = cJSON_CreateObject();
            if (!root) {
                return ESP_ERR_NO_MEM;
            }

            cJSON_AddStringToObject(root, "cmd", "wifi_set_credentials");
            cJSON_AddStringToObject(root, "ssid", ssid);
            cJSON_AddStringToObject(root, "password", password ? password : "");

            esp_err_t cred_err = rs485_send_frame(root, "wifi_set_credentials");
            cJSON_Delete(root);
            if (cred_err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to send WiFi credentials: %s", esp_err_to_name(cred_err));
                return cred_err;
            }
            ESP_LOGI(TAG, "WiFi credentials sent for SSID '%s'", ssid);
        } else {
            ESP_LOGW(TAG, "WiFi enable requested without SSID - skipping credential update");
        }

        esp_err_t enable_err = rs485_send_simple_command("wifi_enable_sta");
        if (enable_err == ESP_OK) {
            ESP_LOGI(TAG, "Sent wifi_enable_sta command");
        } else {
            ESP_LOGW(TAG, "wifi_enable_sta command failed: %s", esp_err_to_name(enable_err));
        }
        return enable_err;
    }

    esp_err_t disable_err = rs485_send_simple_command("wifi_disable_sta");
    if (disable_err == ESP_OK) {
        ESP_LOGI(TAG, "Sent wifi_disable_sta command");
    } else {
        ESP_LOGW(TAG, "wifi_disable_sta command failed: %s", esp_err_to_name(disable_err));
    }
    return disable_err;
}

esp_err_t womo_rs485_send_wifi_credentials(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == '\0') {
        ESP_LOGW(TAG, "wifi_credentials: kein SSID angegeben");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;

    cJSON_AddStringToObject(root, "cmd", "wifi_config");
    cJSON_AddStringToObject(root, "ssid", ssid);
    cJSON_AddStringToObject(root, "pass", pass ? pass : "");

    esp_err_t err = rs485_send_frame(root, "wifi_config");
    cJSON_Delete(root);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi-Credentials an Sensor gesendet: SSID='%s'", ssid);
    } else {
        ESP_LOGW(TAG, "WiFi-Credentials senden fehlgeschlagen: %s", esp_err_to_name(err));
    }
    return err;
}


esp_err_t womo_rs485_send_lte_control(bool enable)
{
    const char *cmd = enable ? "lte_enable" : "lte_disable";
    esp_err_t err = rs485_send_simple_command(cmd);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent %s command", cmd);
    } else {
        ESP_LOGW(TAG, "Failed to send %s command: %s", cmd, esp_err_to_name(err));
    }
    return err;
}

esp_err_t womo_rs485_send_gas_bottle_replace(uint8_t slot, const char *channel)
{
    if (slot > 1) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *channel_value = channel;
    if (!channel_value || channel_value[0] == '\0') {
        channel_value = (slot == 0) ? "front" : "back";
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "cmd", "gas_bottle_replace");
    cJSON_AddNumberToObject(root, "slot", (double)slot);
    cJSON_AddStringToObject(root, "channel", channel_value);

    const char *label = (slot == 0) ? "gas_bottle_replace_front" : "gas_bottle_replace_rear";
    esp_err_t err = rs485_send_frame(root, label);
    cJSON_Delete(root);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent gas_bottle_replace (%s/slot=%u)", channel_value, (unsigned)slot);
    } else {
        ESP_LOGW(TAG, "Failed to send gas_bottle_replace (%s/slot=%u): %s", channel_value, (unsigned)slot, esp_err_to_name(err));
    }

    return err;
}

esp_err_t womo_rs485_send_pwr_12v(bool enable)
{
    const char *cmd = enable ? "pwr_12v_on" : "pwr_12v_off";
    esp_err_t err = rs485_send_simple_command(cmd);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent %s command", cmd);
    } else {
        ESP_LOGW(TAG, "Failed to send %s: %s", cmd, esp_err_to_name(err));
    }
    return err;
}

esp_err_t womo_rs485_send_radio(bool enable)
{
    const char *cmd = enable ? "radio_on" : "radio_off";
    esp_err_t err = rs485_send_simple_command(cmd);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent %s command", cmd);
    } else {
        ESP_LOGW(TAG, "Failed to send %s: %s", cmd, esp_err_to_name(err));
    }
    return err;
}

esp_err_t womo_rs485_send_imu_zero(void)
{
    esp_err_t err = rs485_send_simple_command("imu_zero");
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent imu_zero command (Pitch/Roll nullen)");
    } else {
        ESP_LOGW(TAG, "Failed to send imu_zero: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t womo_rs485_send_buzzer(bool enable)
{
    const char *cmd = enable ? "buzzer_on" : "buzzer_off";
    esp_err_t err = rs485_send_simple_command(cmd);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent %s command", cmd);
    } else {
        ESP_LOGW(TAG, "Failed to send %s: %s", cmd, esp_err_to_name(err));
    }
    return err;
}

esp_err_t womo_rs485_send_touch_click(bool enable)
{
    const char *cmd = enable ? "touch_click_on" : "touch_click_off";
    esp_err_t err = rs485_send_simple_command(cmd);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent %s command", cmd);
    } else {
        ESP_LOGW(TAG, "Failed to send %s: %s", cmd, esp_err_to_name(err));
    }
    return err;
}

esp_err_t womo_rs485_send_buzzer_alert(bool is_alarm)
{
    const char *cmd = is_alarm ? "buzzer_alarm" : "buzzer_warn";
    esp_err_t err = rs485_send_simple_command(cmd);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Sent %s command", cmd);
    } else {
        ESP_LOGW(TAG, "Failed to send %s: %s", cmd, esp_err_to_name(err));
    }
    return err;
}

static void rs485_flush_line(char *line_buffer, size_t *line_pos)
{
    if (*line_pos == 0) {
        return;
    }

    s_rx_line_pending = false;   // Zeile komplett → TX (ACK) erlauben
    line_buffer[*line_pos] = '\0';
    rs485_process_line(line_buffer, *line_pos);
    *line_pos = 0;
}

static void rs485_process_line(char *line_buffer, size_t captured_len)
{
    if (!line_buffer || captured_len == 0) {
        return;
    }

    char *working_buffer = line_buffer;
    size_t working_len = captured_len;

    // Vorhandenes Fragment fortsetzen
    if (s_fragment_len > 0) {
        // Fragment-Timeout: Verwerfe veraltete Fragmente,
        // damit neue Nachrichten nicht an Stale-Daten angehängt werden.
        int64_t frag_age_us = esp_timer_get_time() - s_fragment_start_us;
        if (s_fragment_start_us > 0 && frag_age_us > RS485_FRAG_STALE_US) {
            ESP_LOGW(TAG,
                     "Fragment timeout (age=%.0f ms, len=%zu) - discarding",
                     (double)frag_age_us / 1000.0, s_fragment_len);
            s_fragment_len = 0;
            s_fragment_buffer[0] = '\0';
            s_fragment_start_us = 0;
            // Weiter ohne Fragment: neue Daten als eigenständige Zeile behandeln
        }
    }

    if (s_fragment_len > 0) {
        size_t space = (RS485_LINE_MAX > s_fragment_len + 1) ? (RS485_LINE_MAX - s_fragment_len - 1) : 0;
        size_t copy_len = (captured_len < space) ? captured_len : space;
        if (copy_len > 0) {
            memcpy(&s_fragment_buffer[s_fragment_len], line_buffer, copy_len);
            s_fragment_len += copy_len;
            s_fragment_buffer[s_fragment_len] = '\0';
            working_buffer = s_fragment_buffer;
            working_len = s_fragment_len;
            ESP_LOGW(TAG, "Incomplete JSON fragment (len=%zu) - storing for reassembly", working_len);
            log_fragment_preview(working_buffer, working_len);

            if (s_fragment_len > RS485_FRAGMENT_MAX_KEEP) {
                ESP_LOGW(TAG, "Fragment exceeded %u bytes, dropping partial JSON", (unsigned)RS485_FRAGMENT_MAX_KEEP);
                s_fragment_len = 0;
                s_fragment_buffer[0] = '\0';
                s_fragment_start_us = 0;
                working_buffer = line_buffer;
                working_len = captured_len;
                working_buffer[working_len] = '\0';
                return;
            }
        }

        if (copy_len < captured_len) {
            ESP_LOGW(TAG, "Fragment reassembly buffer overflow (dropped %zu trailing bytes)", captured_len - copy_len);
            s_fragment_len = 0;
            s_fragment_buffer[0] = '\0';
            s_fragment_start_us = 0;
            working_buffer = line_buffer;
            working_len = captured_len;
            working_buffer[working_len] = '\0';
        }
    } else {
        working_buffer[working_len] = '\0';
    }

    // Suche nach JSON-Anfang und -Ende
    char *cursor = working_buffer;
    while (*cursor) {
        while (*cursor && (unsigned char)*cursor < 0x20) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }

        // Unser Protokoll: jedes JSON-Objekt beginnt mit {"type":...
        // Daher nur {" als gültigen Start akzeptieren.
        // Garbled Bytes vom Auto-DE-Transistor (z.B. {7\xBF...) werden so übersprungen.
        while (*cursor) {
            if (*cursor == '{' && *(cursor + 1) == '"') break;
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }

        char *json_start = cursor;
        char *json_end = NULL;
        if (!rs485_find_json_end(json_start, &json_end)) {
            size_t remaining = strlen(json_start);
            if (remaining >= RS485_LINE_MAX - 1 || remaining > RS485_FRAGMENT_MAX_KEEP) {
                ESP_LOGW(TAG,
                         "Incomplete JSON fragment too large (%zu bytes) - clearing assembly buffer",
                         remaining);
                log_fragment_preview(json_start, remaining > RS485_FRAGMENT_MAX_KEEP ? RS485_FRAGMENT_MAX_KEEP : remaining);
                s_fragment_len = 0;
                s_fragment_buffer[0] = '\0';
                s_fragment_start_us = 0;
            } else {
                memmove(s_fragment_buffer, json_start, remaining);
                s_fragment_len = remaining;
                s_fragment_buffer[s_fragment_len] = '\0';
                if (s_fragment_start_us == 0) {
                    s_fragment_start_us = esp_timer_get_time();
                }
                ESP_LOGW(TAG,
                         "Incomplete JSON fragment (remaining=%zu bytes, captured=%zu) - storing for reassembly",
                         remaining,
                         working_len);
                log_fragment_preview(s_fragment_buffer, s_fragment_len);
            }
            return;
        }

        char saved = json_end[1];
        bool truncated_segment = (saved != '\0');
        json_end[1] = '\0';

        parse_json_packet(json_start, working_len, truncated_segment);

        json_end[1] = saved;
        cursor = json_end + 1;
    }

    s_fragment_len = 0;
    s_fragment_buffer[0] = '\0';
    s_fragment_start_us = 0;
}

static bool rs485_find_json_end(const char *start, char **end_ptr)
{
    if (!start || !end_ptr || (*start != '{' && *start != '[')) {
        return false;
    }

    int brace_depth = (*start == '{') ? 1 : 0;
    int bracket_depth = (*start == '[') ? 1 : 0;
    bool in_string = false;
    bool escape = false;

    for (const char *p = start + 1; *p; ++p) {
        char ch = *p;
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }

        if (ch == '{') {
            ++brace_depth;
        } else if (ch == '}') {
            if (brace_depth == 0) {
                return false;
            }
            --brace_depth;
            if (brace_depth == 0 && bracket_depth == 0) {
                *end_ptr = (char *)p;
                return true;
            }
        } else if (ch == '[') {
            ++bracket_depth;
        } else if (ch == ']') {
            if (bracket_depth == 0) {
                return false;
            }
            --bracket_depth;
            if (brace_depth == 0 && bracket_depth == 0) {
                *end_ptr = (char *)p;
                return true;
            }
        }
    }

    return false;
}

static void rs485_rx_task(void *arg)
{
    ESP_LOGI(TAG, "RS485 RX task started");
    
    size_t line_pos = 0;
    uint32_t packet_count = 0;
    
    while (true) {
        int len = uart_read_bytes(RS485_UART_NUM, s_uart_buffer, sizeof(s_uart_buffer), pdMS_TO_TICKS(100));
        
        if (len > 0) {
            // Half-duplex Echo-Unterdrückung: während einer aktiven
            // Übertragung gelesene Bytes sind TX-Echo, nicht Sensorboard-Daten.
            if (s_tx_active) {
                continue;
            }

            int64_t now_us = esp_timer_get_time();

            // Stale-Check: Wenn seit letztem RX > 200 ms vergangen und noch
            // ungeflusht Daten im line_buffer liegen, stammen diese aus einer
            // abgebrochenen Übertragung → verwerfen, damit die neue Nachricht
            // nicht mit den alten Bytes vermischt wird.
            if (line_pos > 0 && s_last_rx_activity_us > 0) {
                int64_t gap_us = now_us - s_last_rx_activity_us;
                if (gap_us > RS485_LINE_STALE_US) {
                    ESP_LOGW(TAG,
                             "Stale line_buffer discarded (%zu bytes, gap=%.0f ms)",
                             line_pos, (double)gap_us / 1000.0);
                    line_pos = 0;
                }
            }

            packet_count++;
            ESP_LOGI(TAG, "*** RX: %d bytes (pkt#%lu) ***", len, packet_count);
            s_last_rx_activity_us = now_us;

            for (int i = 0; i < len; i++) {
                char c = (char)s_uart_buffer[i];

                if (c == '\n' || c == '\r') {
                    rs485_flush_line(s_line_buffer, &line_pos);
                    continue;
                }

                if (c == '\0') {
                    continue;
                }

                if (line_pos < sizeof(s_line_buffer) - 1) {
                    s_line_buffer[line_pos++] = c;
                } else {
                    ESP_LOGW(TAG,
                             "Line buffer overflow at %zu bytes (max %zu) - dropping partial JSON",
                             line_pos,
                             sizeof(s_line_buffer) - 1);
                    line_pos = 0;
                }
            }
        }

        s_rx_line_pending = (line_pos > 0);
    }
}

static void parse_json_packet(const char *json_str, size_t raw_line_len, bool truncated_before_parse)
{
    if (!json_str) {
        return;
    }

    size_t segment_len = strlen(json_str);
    ESP_LOGI(TAG,
             "parse_json: start (segment_len=%zu, raw_len=%zu, truncated=%s)",
             segment_len,
             raw_line_len,
             truncated_before_parse ? "yes" : "no");
    
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGW(TAG,
                 "JSON parse failed (segment_len=%zu, raw_len=%zu, truncated=%s)",
                 segment_len,
                 raw_line_len,
                 truncated_before_parse ? "yes" : "no");
        ESP_LOGW(TAG, "Offending JSON snippet: %s", json_str);
        rs485_emit_event(WOMO_RS485_EVENT_INVALID_JSON);
        return;
    }
    
    ESP_LOGI(TAG, "JSON parsed OK");
    
    const cJSON *seq_obj = cJSON_GetObjectItem(root, "seq");
    uint32_t seq_value = 0;
    bool have_seq = false;
    if (cJSON_IsNumber(seq_obj)) {
        seq_value = (uint32_t)seq_obj->valuedouble;
        have_seq = true;
        rs485_record_rx_seq(seq_value);
    }

    // Systemzeit vom Sensor übernehmen, wenn unsere Zeit ungültig ist (vor NTP-Sync)
    const cJSON *ts_obj = cJSON_GetObjectItem(root, "ts");
    if (cJSON_IsNumber(ts_obj)) {
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        // Wenn Systemzeit < 2024 (ungültig), vom Sensor übernehmen
        if (timeinfo.tm_year < (2024 - 1900)) {
            int64_t sensor_ts_ms = (int64_t)ts_obj->valuedouble;
            struct timeval tv = {
                .tv_sec = sensor_ts_ms / 1000,
                .tv_usec = (sensor_ts_ms % 1000) * 1000
            };
            if (settimeofday(&tv, NULL) == 0) {
                ESP_LOGI(TAG, "Systemzeit vom Sensor übernommen: %lld ms", sensor_ts_ms);
                // Zeit als synced markieren damit Weather-Task starten kann
                womo_time_mark_synced_rs485();
            } else {
                ESP_LOGW(TAG, "settimeofday fehlgeschlagen");
            }
        }
    }

    bool need_ack = true;
    const cJSON *need_ack_obj = cJSON_GetObjectItem(root, "need_ack");
    if (cJSON_IsBool(need_ack_obj)) {
        need_ack = cJSON_IsTrue(need_ack_obj);
    }

    const cJSON *cmd_obj = cJSON_GetObjectItem(root, "cmd");
    cJSON *type_obj = cJSON_GetObjectItem(root, "type");
    const char *type = (cJSON_IsString(type_obj) && type_obj->valuestring)
                           ? type_obj->valuestring
                           : NULL;
    const char *cmd = (cJSON_IsString(cmd_obj) && cmd_obj->valuestring)
                          ? cmd_obj->valuestring
                          : NULL;
    const char *frame_kind = type ? type : cmd;
    const char *fallback_label = frame_kind ? frame_kind : "frame";

    bool ack_needed = have_seq && need_ack;
    bool ack_success = false;
    char ack_error_buf[96] = "";
    const char *ack_error = NULL;
    const char *ack_label = fallback_label;

    if (!frame_kind) {
        ESP_LOGW(TAG, "No type/cmd field");
        ack_error = "missing type/cmd";
        if (ack_needed) {
            rs485_send_ack(seq_value, false, ack_label, ack_error);
        }
        cJSON_Delete(root);
        rs485_emit_event(WOMO_RS485_EVENT_INVALID_JSON);
        return;
    }

    ESP_LOGI(TAG, "Frame: %s", frame_kind);

    if (strcmp(frame_kind, "ack") == 0 || strcmp(frame_kind, "cmd_ack") == 0) {
        rs485_handle_ack_packet(root);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(frame_kind, "hb") == 0) {
        s_last_heartbeat_rx_us = esp_timer_get_time();
        rs485_emit_event(WOMO_RS485_EVENT_HEARTBEAT);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(frame_kind, "hello") == 0) {
        ack_needed = false;  // handshake handled via display_ready
        const cJSON *fw = cJSON_GetObjectItem(root, "fw");
        const cJSON *uptime = cJSON_GetObjectItem(root, "uptime");
        if (cJSON_IsString(fw)) {
            ESP_LOGI(TAG, "Sensorboard hello: fw=%s", fw->valuestring);
        }
        if (cJSON_IsNumber(uptime)) {
            ESP_LOGI(TAG, "Sensorboard uptime: %.0f s", uptime->valuedouble);
        }
        rs485_emit_event(WOMO_RS485_EVENT_HELLO);
        womo_rs485_send_display_ready();
        cJSON_Delete(root);
        return;
    }

    // ── WiFi Passwort-Anfrage vom Sensor ──────────────────────────────
    if (strcmp(frame_kind, "wifi_pass_request") == 0) {
        ESP_LOGI(TAG, "Sensor fragt nach WiFi-Passwort");
        // Aktuell verbundene SSID holen
        char ssid[33] = "";
        esp_err_t ssid_err = womo_wifi_get_ssid(ssid, sizeof(ssid));
        if (ssid_err == ESP_OK && ssid[0] != '\0') {
            // Passwort aus NVS Known-List holen
            char pwd[64] = "";
            esp_err_t pwd_err = womo_wifi_get_known_credentials(ssid, pwd, sizeof(pwd));
            if (pwd_err == ESP_OK && pwd[0] != '\0') {
                ESP_LOGI(TAG, "Sende Credentials an Sensor: SSID='%s'", ssid);
                womo_rs485_send_wifi_credentials(ssid, pwd);
                ack_success = true;
            } else {
                ESP_LOGW(TAG, "Kein Passwort für SSID '%s' im NVS gefunden", ssid);
                ack_error = "no password stored";
            }
        } else {
            ESP_LOGW(TAG, "Display nicht mit WiFi verbunden – kann kein Passwort liefern");
            ack_error = "display not connected";
        }
        if (ack_needed) {
            rs485_send_ack(seq_value, ack_success, "wifi_pass_request", ack_error);
        }
        cJSON_Delete(root);
        return;
    }


    // ── Topic-basierte Verarbeitung (Merge in s_latest_data) ──
    // Jedes Topic aktualisiert nur seinen Teil; der Rest bleibt erhalten.

    bool topic_handled = false;
    womo_sensor_data_t notify_snapshot = {0};

    if (strcmp(frame_kind, "ctrl") == 0) {
        if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_latest_data.power.valid = true;
            cJSON *pwr_on    = cJSON_GetObjectItem(root, "pwr_on");
            cJSON *radio     = cJSON_GetObjectItem(root, "radio_on");
            cJSON *ac        = cJSON_GetObjectItem(root, "ac_present");
            cJSON *rtc_bat   = cJSON_GetObjectItem(root, "rtc_bat_low");
            cJSON *rtc_bsf   = cJSON_GetObjectItem(root, "rtc_bat_switched");
            cJSON *buzzer    = cJSON_GetObjectItem(root, "buzzer_on");
            cJSON *touch_clk = cJSON_GetObjectItem(root, "touch_click_on");
            if (cJSON_IsBool(pwr_on))   s_latest_data.power.pwr_12v_on       = cJSON_IsTrue(pwr_on);
            if (cJSON_IsBool(radio))    s_latest_data.power.radio_on          = cJSON_IsTrue(radio);
            if (cJSON_IsBool(ac))       s_latest_data.power.ac_present        = cJSON_IsTrue(ac);
            if (cJSON_IsBool(rtc_bat))  s_latest_data.power.rtc_bat_low       = cJSON_IsTrue(rtc_bat);
            if (cJSON_IsBool(rtc_bsf))  s_latest_data.power.rtc_bat_switched  = cJSON_IsTrue(rtc_bsf);
            if (cJSON_IsBool(buzzer)) {
                s_latest_data.power.buzzer_on = cJSON_IsTrue(buzzer);
                display_buzzer_set_enabled(s_latest_data.power.buzzer_on);
            }
            if (cJSON_IsBool(touch_clk)) {
                s_latest_data.power.touch_click_on = cJSON_IsTrue(touch_clk);
                display_buzzer_set_click_enabled(s_latest_data.power.touch_click_on);
            }
            notify_snapshot = s_latest_data;
            xSemaphoreGive(s_data_mutex);
            topic_handled = true;
            ack_success = true;
        }
    }
    else if (strcmp(frame_kind, "imu") == 0) {
        if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            cJSON *yaw = cJSON_GetObjectItem(root, "yaw_deg");
            cJSON *pitch = cJSON_GetObjectItem(root, "pitch_deg");
            cJSON *roll = cJSON_GetObjectItem(root, "roll_deg");
            cJSON *hdg = cJSON_GetObjectItem(root, "hdg");
            cJSON *cal = cJSON_GetObjectItem(root, "cal");
            if (yaw || pitch || roll || (hdg && cJSON_IsString(hdg))) {
                s_latest_data.bno.valid = true;
                if (yaw) s_latest_data.bno.heading_deg = (float)yaw->valuedouble;
                if (pitch) s_latest_data.bno.pitch_deg = (float)pitch->valuedouble;
                if (roll) s_latest_data.bno.roll_deg = (float)roll->valuedouble;
                if (hdg && cJSON_IsString(hdg))
                    snprintf(s_latest_data.bno.direction, sizeof(s_latest_data.bno.direction), "%s", hdg->valuestring);
                if (cal && cJSON_IsObject(cal)) {
                    cJSON *cs = cJSON_GetObjectItem(cal, "sys");
                    cJSON *cg = cJSON_GetObjectItem(cal, "gyro");
                    cJSON *ca = cJSON_GetObjectItem(cal, "acc");
                    cJSON *cm = cJSON_GetObjectItem(cal, "mag");
                    if (cs) s_latest_data.bno.cal_sys   = (uint8_t)cs->valueint;
                    if (cg) s_latest_data.bno.cal_gyro  = (uint8_t)cg->valueint;
                    if (ca) s_latest_data.bno.cal_accel = (uint8_t)ca->valueint;
                    if (cm) s_latest_data.bno.cal_mag   = (uint8_t)cm->valueint;
                }
            }
            notify_snapshot = s_latest_data;
            xSemaphoreGive(s_data_mutex);
            topic_handled = true;
            ack_success = true;
        }
    }
    else if (strcmp(frame_kind, "bme") == 0) {
        if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            // 0x76 = indoor, 0x77 = outdoor
            for (cJSON *sensor = root->child; sensor != NULL; sensor = sensor->next) {
                if (!cJSON_IsObject(sensor) || !sensor->string) continue;
                bool is_indoor = (strcasecmp(sensor->string, "0x76") == 0);
                bool is_outdoor = (strcasecmp(sensor->string, "0x77") == 0);
                if (!is_indoor && !is_outdoor) continue;

                cJSON *temp_c    = cJSON_GetObjectItem(sensor, "temp_c");
                cJSON *rh_pct    = cJSON_GetObjectItem(sensor, "rh_pct");
                cJSON *press_hpa = cJSON_GetObjectItem(sensor, "press_hpa");
                cJSON *gas_kohm  = cJSON_GetObjectItem(sensor, "gas_kohm");
                cJSON *iaq_val   = cJSON_GetObjectItem(sensor, "iaq");
                cJSON *iaq_acc   = cJSON_GetObjectItem(sensor, "iaq_acc");
                cJSON *eco2      = cJSON_GetObjectItem(sensor, "eco2_ppm");
                cJSON *bvoc      = cJSON_GetObjectItem(sensor, "bvoc_ppm");
                cJSON *trend_st  = cJSON_GetObjectItem(sensor, "press_trend_state");
                cJSON *trend_hpa = cJSON_GetObjectItem(sensor, "press_trend_hpa_h");

                if (is_indoor) {
                    s_latest_data.bme680_indoor.valid = true;
                    if (temp_c)    s_latest_data.bme680_indoor.temperature_c     = (float)temp_c->valuedouble;
                    if (rh_pct)    s_latest_data.bme680_indoor.humidity_percent   = (float)rh_pct->valuedouble;
                    if (press_hpa) s_latest_data.bme680_indoor.pressure_hpa       = (float)press_hpa->valuedouble;
                    if (gas_kohm)  s_latest_data.bme680_indoor.gas_kohm           = (float)gas_kohm->valuedouble;
                    if (iaq_val)   s_latest_data.bme680_indoor.iaq                = (uint16_t)iaq_val->valueint;
                    if (iaq_acc)   s_latest_data.bme680_indoor.iaq_accuracy       = (uint8_t)iaq_acc->valueint;
                    if (eco2)      s_latest_data.bme680_indoor.eco2_ppm           = (float)eco2->valuedouble;
                    if (bvoc)      s_latest_data.bme680_indoor.bvoc_ppm           = (float)bvoc->valuedouble;
                    if (cJSON_IsString(trend_st) && trend_st->valuestring) {
                        strncpy(s_latest_data.bme680_indoor.press_trend_state, trend_st->valuestring,
                                sizeof(s_latest_data.bme680_indoor.press_trend_state) - 1);
                    } else {
                        strncpy(s_latest_data.bme680_indoor.press_trend_state, "steady",
                                sizeof(s_latest_data.bme680_indoor.press_trend_state) - 1);
                    }
                    if (trend_hpa) s_latest_data.bme680_indoor.press_trend_slope_hpa_h = (float)trend_hpa->valuedouble;
                } else {
                    s_latest_data.bme680.valid = true;
                    if (temp_c)    s_latest_data.bme680.temperature_c     = (float)temp_c->valuedouble;
                    if (rh_pct)    s_latest_data.bme680.humidity_percent   = (float)rh_pct->valuedouble;
                    if (press_hpa) s_latest_data.bme680.pressure_hpa       = (float)press_hpa->valuedouble;
                    if (gas_kohm)  s_latest_data.bme680.gas_kohm           = (float)gas_kohm->valuedouble;
                    if (iaq_val)   s_latest_data.bme680.iaq                = (uint16_t)iaq_val->valueint;
                    if (iaq_acc)   s_latest_data.bme680.iaq_accuracy       = (uint8_t)iaq_acc->valueint;
                    if (eco2)      s_latest_data.bme680.eco2_ppm           = (float)eco2->valuedouble;
                    if (bvoc)      s_latest_data.bme680.bvoc_ppm           = (float)bvoc->valuedouble;
                    if (cJSON_IsString(trend_st) && trend_st->valuestring) {
                        strncpy(s_latest_data.bme680.press_trend_state, trend_st->valuestring,
                                sizeof(s_latest_data.bme680.press_trend_state) - 1);
                    } else {
                        strncpy(s_latest_data.bme680.press_trend_state, "steady",
                                sizeof(s_latest_data.bme680.press_trend_state) - 1);
                    }
                    if (trend_hpa) s_latest_data.bme680.press_trend_slope_hpa_h = (float)trend_hpa->valuedouble;
                }
            }
            s_latest_data.bme_topic_rx_us = esp_timer_get_time();
            notify_snapshot = s_latest_data;
            xSemaphoreGive(s_data_mutex);
            topic_handled = true;
            ack_success = true;
        }
    }
    else if (strcmp(frame_kind, "bat") == 0) {
        if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_latest_data.battery.valid = true;
            cJSON *b1 = cJSON_GetObjectItem(root, "b1");
            cJSON *b2 = cJSON_GetObjectItem(root, "b2");
            cJSON *nc1 = cJSON_GetObjectItem(root, "nc1");
            cJSON *nc2 = cJSON_GetObjectItem(root, "nc2");
            if (b1)  s_latest_data.battery.battery1_v = (float)b1->valuedouble;
            if (b2)  s_latest_data.battery.battery2_v = (float)b2->valuedouble;
            if (cJSON_IsBool(nc1)) s_latest_data.battery.nc1 = cJSON_IsTrue(nc1);
            if (cJSON_IsBool(nc2)) s_latest_data.battery.nc2 = cJSON_IsTrue(nc2);
            s_latest_data.bat_topic_rx_us = esp_timer_get_time();
            notify_snapshot = s_latest_data;
            xSemaphoreGive(s_data_mutex);
            topic_handled = true;
            ack_success = true;
        }
    }
    else if (strcmp(frame_kind, "elec") == 0) {
        if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            cJSON *nc = cJSON_GetObjectItem(root, "nc");
            if (cJSON_IsBool(nc) && cJSON_IsTrue(nc)) {
                s_latest_data.elec.valid = true;
                s_latest_data.elec.nc    = true;
            } else {
                cJSON *v_bus  = cJSON_GetObjectItem(root, "v_bus");
                cJSON *i_a    = cJSON_GetObjectItem(root, "i_a");
                cJSON *p_w    = cJSON_GetObjectItem(root, "p_w");
                s_latest_data.elec.valid = true;
                s_latest_data.elec.nc    = false;
                if (v_bus) s_latest_data.elec.v_bus_v = (float)v_bus->valuedouble;
                if (i_a)   s_latest_data.elec.i_a     = (float)i_a->valuedouble;
                if (p_w)   s_latest_data.elec.p_w     = (float)p_w->valuedouble;
            }
            s_latest_data.elec_topic_rx_us = esp_timer_get_time();
            notify_snapshot = s_latest_data;
            xSemaphoreGive(s_data_mutex);
            topic_handled = true;
            ack_success   = true;
        }
    }
    else if (strcmp(frame_kind, "hx") == 0) {
        if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_latest_data.hx711.valid = true;
            cJSON *a = cJSON_GetObjectItem(root, "a");
            cJSON *b = cJSON_GetObjectItem(root, "b");
            cJSON *sum = cJSON_GetObjectItem(root, "sum");
            cJSON *nc = cJSON_GetObjectItem(root, "nc");
            if (a) s_latest_data.hx711.weight_a_kg = (float)a->valuedouble;
            if (b) s_latest_data.hx711.weight_b_kg = (float)b->valuedouble;
            if (sum) s_latest_data.hx711.weight_sum_kg = (float)sum->valuedouble;
            if (cJSON_IsBool(nc)) s_latest_data.hx711.nc = cJSON_IsTrue(nc);
            notify_snapshot = s_latest_data;
            xSemaphoreGive(s_data_mutex);
            topic_handled = true;
            ack_success = true;
        }
    }
    else if (strcmp(frame_kind, "gas") == 0) {
        if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_latest_data.gas.valid = true;
            cJSON *active = cJSON_GetObjectItem(root, "active");
            cJSON *net = cJSON_GetObjectItem(root, "net");
            cJSON *rate1h = cJSON_GetObjectItem(root, "rate1h");
            cJSON *rate2h = cJSON_GetObjectItem(root, "rate2h");
            cJSON *rest_h = cJSON_GetObjectItem(root, "rest_h");
            cJSON *net_a = cJSON_GetObjectItem(root, "net_a");
            cJSON *net_b = cJSON_GetObjectItem(root, "net_b");
            cJSON *cap_kg = cJSON_GetObjectItem(root, "cap_kg");
            cJSON *pct = cJSON_GetObjectItem(root, "pct");
            cJSON *pct_a = cJSON_GetObjectItem(root, "pct_a");
            cJSON *pct_b = cJSON_GetObjectItem(root, "pct_b");
            if (cJSON_IsNumber(active)) s_latest_data.gas.active_idx = (int8_t)active->valueint;
            if (cJSON_IsNumber(net))    s_latest_data.gas.net_kg = (float)net->valuedouble;
            if (cJSON_IsNumber(rate1h)) s_latest_data.gas.rate_kgph_1h = (float)rate1h->valuedouble;
            if (cJSON_IsNumber(rate2h)) s_latest_data.gas.rate_kgph_2h = (float)rate2h->valuedouble;
            if (cJSON_IsNumber(rest_h)) s_latest_data.gas.rest_hours = (float)rest_h->valuedouble;
            if (cJSON_IsNumber(net_a))  s_latest_data.gas.net_a_kg = (float)net_a->valuedouble;
            if (cJSON_IsNumber(net_b))  s_latest_data.gas.net_b_kg = (float)net_b->valuedouble;
            if (cJSON_IsNumber(cap_kg)) s_latest_data.gas.cap_kg = (float)cap_kg->valuedouble;
            if (cJSON_IsNumber(pct)) {
                float v = (float)pct->valuedouble;
                if (v < 0) v = 0;
                if (v > 100) v = 100;
                s_latest_data.gas.pct = v;
            }
            if (cJSON_IsNumber(pct_a)) {
                float v = (float)pct_a->valuedouble;
                if (v < 0) v = 0;
                if (v > 100) v = 100;
                s_latest_data.gas.pct_a = v;
            }
            if (cJSON_IsNumber(pct_b)) {
                float v = (float)pct_b->valuedouble;
                if (v < 0) v = 0;
                if (v > 100) v = 100;
                s_latest_data.gas.pct_b = v;
            }
            s_latest_data.gas_topic_rx_us = esp_timer_get_time();
            notify_snapshot = s_latest_data;
            xSemaphoreGive(s_data_mutex);
            topic_handled = true;
            ack_success = true;
        }
    }
    else if (strcmp(frame_kind, "tank") == 0) {
        if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            s_latest_data.tank.valid = true;
            cJSON *t1 = cJSON_GetObjectItem(root, "t1");
            cJSON *t2 = cJSON_GetObjectItem(root, "t2");
            cJSON *nc1 = cJSON_GetObjectItem(root, "nc1");
            cJSON *nc2 = cJSON_GetObjectItem(root, "nc2");
            cJSON *t1_l = cJSON_GetObjectItem(root, "t1_l");
            cJSON *t2_l = cJSON_GetObjectItem(root, "t2_l");
            cJSON *t1_rate1h = cJSON_GetObjectItem(root, "t1_rate1h");
            cJSON *t1_rate2h = cJSON_GetObjectItem(root, "t1_rate2h");
            cJSON *t1_rest_h = cJSON_GetObjectItem(root, "t1_rest_h");
            cJSON *t2_rate1h = cJSON_GetObjectItem(root, "t2_rate1h");
            cJSON *t2_rate2h = cJSON_GetObjectItem(root, "t2_rate2h");
            cJSON *t2_rest_h = cJSON_GetObjectItem(root, "t2_rest_h");
            
            if (t1) s_latest_data.tank.tank1_percent = (uint8_t)t1->valueint;
            if (t2) s_latest_data.tank.tank2_percent = (uint8_t)t2->valueint;
            if (nc1 && cJSON_IsBool(nc1)) s_latest_data.tank.nc1 = cJSON_IsTrue(nc1);
            if (nc2 && cJSON_IsBool(nc2)) s_latest_data.tank.nc2 = cJSON_IsTrue(nc2);
            if (t1_l && cJSON_IsNumber(t1_l)) s_latest_data.tank.tank1_liters = (float)t1_l->valuedouble;
            if (t2_l && cJSON_IsNumber(t2_l)) s_latest_data.tank.tank2_liters = (float)t2_l->valuedouble;
            if (t1_rate1h && cJSON_IsNumber(t1_rate1h)) s_latest_data.tank.tank1_rate1h = (float)t1_rate1h->valuedouble;
            if (t1_rate2h && cJSON_IsNumber(t1_rate2h)) s_latest_data.tank.tank1_rate2h = (float)t1_rate2h->valuedouble;
            if (t1_rest_h && cJSON_IsNumber(t1_rest_h)) s_latest_data.tank.tank1_rest_h = (float)t1_rest_h->valuedouble;
            if (t2_rate1h && cJSON_IsNumber(t2_rate1h)) s_latest_data.tank.tank2_rate1h = (float)t2_rate1h->valuedouble;
            if (t2_rate2h && cJSON_IsNumber(t2_rate2h)) s_latest_data.tank.tank2_rate2h = (float)t2_rate2h->valuedouble;
            if (t2_rest_h && cJSON_IsNumber(t2_rest_h)) s_latest_data.tank.tank2_rest_h = (float)t2_rest_h->valuedouble;
            s_latest_data.tank_topic_rx_us = esp_timer_get_time();
            notify_snapshot = s_latest_data;
            xSemaphoreGive(s_data_mutex);
            topic_handled = true;
            ack_success = true;
        }
    }
    else {
        ESP_LOGW(TAG, "Unrecognized RS485 frame: %s", frame_kind);
        rs485_emit_event(WOMO_RS485_EVENT_INVALID_JSON);
        snprintf(ack_error_buf, sizeof(ack_error_buf), "frame %s not supported", frame_kind);
        ack_error = ack_error_buf;
    }

    // Callback mit dem kompletten Merge-Snapshot
    if (topic_handled && s_data_callback) {
        s_data_callback(&notify_snapshot, s_callback_user_data);
    }

    if (ack_needed) {
        esp_err_t ack_err = rs485_send_ack(seq_value, ack_success, ack_label, ack_error);
        if (ack_err != ESP_OK) {
            ESP_LOGW(TAG,
                     "Failed to send ACK for %s (seq=%lu): %s",
                     ack_label,
                     (unsigned long)seq_value,
                     esp_err_to_name(ack_err));
        }
    }

    cJSON_Delete(root);
}
