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
 * Kopplung + Verschlüsselung (Ablauf siehe womo_config.h, Kategorie 4):
 * Die Display-MAC liegt im NVS. Nutzdaten gehen nur per verschlüsseltem
 * Unicast (CCMP, PMK/LMK aus womo_credentials.h) und werden nur von dieser
 * MAC angenommen. Kommt WOMO_ESPNOW_LINK_DEAD_MS lang nichts vom Display
 * (oder ist keins gespeichert), ist das Kopplungsfenster offen: Beacon per
 * Broadcast senden und ein neues Display über dessen Beacon übernehmen.
 */

#include "network/espnow_transport.h"
#include "womo_config.h"

#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "espnow";

typedef struct {
    uint8_t data[WOMO_ESPNOW_MAX_PAYLOAD + 1];
    int     len;
    bool    beacon;     // Kopplungs-Beacon (Broadcast), wird im Task ausgewertet
    uint8_t src[6];
} espnow_rx_item_t;

static QueueHandle_t        s_recv_queue = NULL;
static TaskHandle_t         s_recv_task  = NULL;
static espnow_recv_line_cb_t s_recv_cb   = NULL;
static esp_timer_handle_t   s_beacon_timer = NULL;

static uint8_t       s_peer_mac[6]   = {0};
static bool          s_peer_known    = false;   // Display-MAC gespeichert
static bool          s_peer_rx_seen  = false;   // seit Boot Frame vom Display erhalten
static int64_t       s_last_peer_rx_us = 0;
static portMUX_TYPE  s_peer_lock     = portMUX_INITIALIZER_UNLOCKED;

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ── Kopplung ─────────────────────────────────────────────────────────────

static bool pair_window_open(void)
{
    taskENTER_CRITICAL(&s_peer_lock);
    bool open = !s_peer_known || s_last_peer_rx_us == 0 ||
                (esp_timer_get_time() - s_last_peer_rx_us) > (int64_t)WOMO_ESPNOW_LINK_DEAD_MS * 1000;
    taskEXIT_CRITICAL(&s_peer_lock);
    return open;
}

static esp_err_t peer_add_encrypted(const uint8_t mac[6])
{
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0;
    peer.encrypt = true;
    memcpy(peer.lmk, WOMO_ESPNOW_LMK, ESP_NOW_KEY_LEN);
    if (esp_now_is_peer_exist(mac)) {
        return esp_now_mod_peer(&peer);
    }
    return esp_now_add_peer(&peer);
}

static void peer_load(void)
{
    nvs_handle_t h;
    if (nvs_open(WOMO_ESPNOW_PEER_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t mac[6];
    size_t len = sizeof(mac);
    if (nvs_get_blob(h, WOMO_ESPNOW_PEER_NVS_KEY, mac, &len) == ESP_OK && len == sizeof(mac)) {
        memcpy(s_peer_mac, mac, 6);
        s_peer_known = true;
        ESP_LOGI(TAG, "Display-Peer aus NVS: " MACSTR, MAC2STR(mac));
    }
    nvs_close(h);
}

static void peer_save(const uint8_t mac[6])
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(WOMO_ESPNOW_PEER_NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, WOMO_ESPNOW_PEER_NVS_KEY, mac, 6);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Display-Peer nicht gespeichert: %s", esp_err_to_name(err));
    }
}

// Beacon eines Displays während offenem Kopplungsfenster → als Gegenstelle übernehmen
static void handle_beacon(const uint8_t src[6])
{
    if (!pair_window_open()) return;

    taskENTER_CRITICAL(&s_peer_lock);
    bool same = s_peer_known && memcmp(s_peer_mac, src, 6) == 0;
    uint8_t old[6];
    bool had_old = s_peer_known;
    memcpy(old, s_peer_mac, 6);
    taskEXIT_CRITICAL(&s_peer_lock);
    if (same) return;

    if (had_old && esp_now_is_peer_exist(old)) {
        esp_now_del_peer(old);
    }
    esp_err_t err = peer_add_encrypted(src);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_add_peer: %s", esp_err_to_name(err));
        return;
    }

    taskENTER_CRITICAL(&s_peer_lock);
    memcpy(s_peer_mac, src, 6);
    s_peer_known      = true;
    s_last_peer_rx_us = 0;      // Fenster bleibt offen, bis das Display verschlüsselt antwortet
    taskEXIT_CRITICAL(&s_peer_lock);

    peer_save(src);
    ESP_LOGI(TAG, "Display-Peer gekoppelt: " MACSTR, MAC2STR(src));
}

static void beacon_timer_cb(void *arg)
{
    (void)arg;
    if (!pair_window_open()) return;
    esp_now_send(BROADCAST_MAC, (const uint8_t *)WOMO_ESPNOW_PAIR_BEACON,
                 strlen(WOMO_ESPNOW_PAIR_BEACON));
}

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
    if (!s_recv_queue) return;

    espnow_rx_item_t item = {0};
    bool bcast = recv_info->des_addr && memcmp(recv_info->des_addr, BROADCAST_MAC, 6) == 0;
    if (bcast) {
        // Broadcast ist unverschlüsselt → nur Kopplungs-Beacons, nie Nutzdaten
        size_t blen = strlen(WOMO_ESPNOW_PAIR_BEACON);
        if ((size_t)len != blen || memcmp(data, WOMO_ESPNOW_PAIR_BEACON, blen) != 0) return;
        item.beacon = true;
    } else {
        // Nutzdaten nur von der gekoppelten Gegenstelle
        taskENTER_CRITICAL(&s_peer_lock);
        bool from_peer = s_peer_known && memcmp(s_peer_mac, recv_info->src_addr, 6) == 0;
        if (from_peer) {
            s_last_peer_rx_us = esp_timer_get_time();
            s_peer_rx_seen    = true;
        }
        taskEXIT_CRITICAL(&s_peer_lock);
        if (!from_peer) {
            ESP_LOGD(TAG, "Frame von fremder MAC " MACSTR " verworfen", MAC2STR(recv_info->src_addr));
            return;
        }
    }

    memcpy(item.src, recv_info->src_addr, 6);
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
            if (item.beacon) {
                handle_beacon(item.src);
            } else if (s_recv_cb && item.len > 0) {
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

    // TX-Power auf 10 dBm begrenzen (40 = 10 dBm in 0.25-dBm-Schritten)
    // Fahrzeuginterner Link <1 m, spart ~15-20 mA gegenüber Standard 20 dBm
    esp_wifi_set_max_tx_power(40);

    err = esp_now_set_pmk((const uint8_t *)WOMO_ESPNOW_PMK);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_set_pmk: %s", esp_err_to_name(err));
        return err;
    }

    esp_now_register_send_cb(espnow_send_cb);
    esp_now_register_recv_cb(espnow_recv_cb);

    // Broadcast-Peer nur für Kopplungs-Beacons (unverschlüsselt)
    esp_now_peer_info_t bc = {0};
    memcpy(bc.peer_addr, BROADCAST_MAC, 6);
    bc.channel = 0;
    bc.encrypt = false;
    esp_now_add_peer(&bc);

    peer_load();
    if (s_peer_known) {
        err = peer_add_encrypted(s_peer_mac);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_now_add_peer: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "Kein Display gekoppelt – Kopplungsfenster offen");
    }

    s_recv_queue = xQueueCreate(WOMO_ESPNOW_RECV_QUEUE_LEN, sizeof(espnow_rx_item_t));
    if (!s_recv_queue) return ESP_ERR_NO_MEM;

    BaseType_t r = xTaskCreatePinnedToCore(
        espnow_rx_task, "espnow_rx", 8192, NULL, 5, &s_recv_task, 0);
    if (r != pdPASS) return ESP_FAIL;

    const esp_timer_create_args_t targs = {
        .callback = beacon_timer_cb,
        .name     = "espnow_pair",
    };
    err = esp_timer_create(&targs, &s_beacon_timer);
    if (err == ESP_OK) {
        err = esp_timer_start_periodic(s_beacon_timer, (uint64_t)WOMO_ESPNOW_PAIR_BEACON_MS * 1000);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Beacon-Timer: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "ESP-NOW Transport bereit (verschlüsselt)");
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

    // Nutzdaten nie per Broadcast (wäre unverschlüsselt)
    taskENTER_CRITICAL(&s_peer_lock);
    bool known = s_peer_known;
    uint8_t dest[6];
    memcpy(dest, s_peer_mac, 6);
    taskEXIT_CRITICAL(&s_peer_lock);
    if (!known) return ESP_ERR_INVALID_STATE;

    esp_err_t err = esp_now_send(dest, (const uint8_t *)data, len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_now_send: %s", esp_err_to_name(err));
    }
    return err;
}

bool espnow_transport_peer_known(void)
{
    taskENTER_CRITICAL(&s_peer_lock);
    bool seen = s_peer_rx_seen;
    taskEXIT_CRITICAL(&s_peer_lock);
    return seen;
}
