/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * ESP-NOW Transport Layer – Sensorboard
 *
 * Sendet JSON-Frames an das Display und empfängt Kommandos.
 * Voll-Duplex: Senden und Empfangen laufen gleichzeitig ohne Kollisionen.
 *
 * Peer-Discovery: Sensorboard sendet zunächst per Broadcast.
 * Sobald das Display antwortet, wird seine MAC-Adresse gelernt
 * und für alle weiteren Sends als Unicast-Peer verwendet.
 */

#include "network/espnow_transport.h"
#include "womo_config.h"

#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "espnow";

typedef struct {
    uint8_t data[WOMO_ESPNOW_MAX_PAYLOAD + 1];
    int     len;
} espnow_rx_item_t;

static QueueHandle_t        s_recv_queue = NULL;
static TaskHandle_t         s_recv_task  = NULL;
static espnow_recv_line_cb_t s_recv_cb   = NULL;

static uint8_t       s_peer_mac[6]  = {0};
static bool          s_peer_known   = false;
static portMUX_TYPE  s_peer_lock    = portMUX_INITIALIZER_UNLOCKED;

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ── Callbacks ────────────────────────────────────────────────────────────

static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGD(TAG, "Send failed to " MACSTR, MAC2STR(tx_info->des_addr));
    }
}

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info,
                           const uint8_t *data, int len)
{
    if (!recv_info || !data || len <= 0 || len > WOMO_ESPNOW_MAX_PAYLOAD) return;

    // Display-MAC beim ersten empfangenen Paket lernen
    taskENTER_CRITICAL(&s_peer_lock);
    bool is_new = !s_peer_known ||
                  memcmp(s_peer_mac, recv_info->src_addr, 6) != 0;
    if (is_new) {
        memcpy(s_peer_mac, recv_info->src_addr, 6);
        s_peer_known = true;
    }
    taskEXIT_CRITICAL(&s_peer_lock);

    if (is_new) {
        if (!esp_now_is_peer_exist(recv_info->src_addr)) {
            esp_now_peer_info_t peer = {0};
            memcpy(peer.peer_addr, recv_info->src_addr, 6);
            peer.channel = 0;
            peer.encrypt = false;
            esp_now_add_peer(&peer);
        }
        ESP_LOGI(TAG, "Display-Peer gelernt: " MACSTR, MAC2STR(recv_info->src_addr));
    }

    if (!s_recv_queue) return;

    espnow_rx_item_t item;
    memcpy(item.data, data, len);
    item.data[len] = '\0';
    item.len = len;

    BaseType_t woke = pdFALSE;
    xQueueSendFromISR(s_recv_queue, &item, &woke);
    portYIELD_FROM_ISR(woke);
}

// ── RX Task ──────────────────────────────────────────────────────────────

static void espnow_rx_task(void *arg)
{
    espnow_rx_item_t item;
    while (true) {
        if (xQueueReceive(s_recv_queue, &item, portMAX_DELAY) == pdTRUE) {
            if (s_recv_cb && item.len > 0) {
                s_recv_cb((const char *)item.data);
            }
        }
    }
}

// ── Public API ───────────────────────────────────────────────────────────

esp_err_t espnow_transport_init(void)
{
    // Kanal aus aktiver WiFi-Verbindung lesen, Fallback wenn nicht verbunden
    uint8_t channel = WOMO_ESPNOW_CHANNEL_FALLBACK;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&channel, &second);
    if (channel == 0) channel = WOMO_ESPNOW_CHANNEL_FALLBACK;
    ESP_LOGI(TAG, "ESP-NOW Init (Kanal %u)", channel);

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init: %s", esp_err_to_name(err));
        return err;
    }

    esp_now_register_send_cb(espnow_send_cb);
    esp_now_register_recv_cb(espnow_recv_cb);

    // Broadcast-Peer für Discovery (solange Display-MAC unbekannt)
    esp_now_peer_info_t bc = {0};
    memcpy(bc.peer_addr, BROADCAST_MAC, 6);
    bc.channel = 0;
    bc.encrypt = false;
    esp_now_add_peer(&bc);

    s_recv_queue = xQueueCreate(WOMO_ESPNOW_RECV_QUEUE_LEN, sizeof(espnow_rx_item_t));
    if (!s_recv_queue) return ESP_ERR_NO_MEM;

    BaseType_t r = xTaskCreatePinnedToCore(
        espnow_rx_task, "espnow_rx", 8192, NULL, 5, &s_recv_task, 0);
    if (r != pdPASS) return ESP_FAIL;

    ESP_LOGI(TAG, "ESP-NOW Transport bereit");
    return ESP_OK;
}

void espnow_transport_set_recv_cb(espnow_recv_line_cb_t cb)
{
    s_recv_cb = cb;
}

esp_err_t espnow_transport_send(const char *data, size_t len)
{
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    if (len > WOMO_ESPNOW_MAX_PAYLOAD) {
        ESP_LOGW(TAG, "Frame zu groß: %u Byte (max %u)", (unsigned)len, WOMO_ESPNOW_MAX_PAYLOAD);
        return ESP_ERR_INVALID_SIZE;
    }

    taskENTER_CRITICAL(&s_peer_lock);
    uint8_t dest[6];
    memcpy(dest, s_peer_known ? s_peer_mac : BROADCAST_MAC, 6);
    taskEXIT_CRITICAL(&s_peer_lock);

    esp_err_t err = esp_now_send(dest, (const uint8_t *)data, len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_now_send: %s", esp_err_to_name(err));
    }
    return err;
}

bool espnow_transport_peer_known(void)
{
    taskENTER_CRITICAL(&s_peer_lock);
    bool known = s_peer_known;
    taskEXIT_CRITICAL(&s_peer_lock);
    return known;
}
