/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Callback für empfangene JSON-Frames vom Sensorboard.
 * Wird aus einem dedizierten Task aufgerufen (nicht aus ISR-Kontext).
 * json_str ist null-terminiert.
 */
typedef void (*womo_espnow_recv_cb_t)(const char *json_str);

/**
 * ESP-NOW Transport für das Display initialisieren.
 * WiFi muss vorher gestartet sein.
 * Kanal wird automatisch aus aktiver WiFi-Verbindung übernommen,
 * Fallback: WOMO_ESPNOW_CHANNEL_FALLBACK aus womo_config.h.
 */
esp_err_t womo_espnow_init(void);

/**
 * Callback registrieren für eingehende Sensorboard-Frames.
 */
void womo_espnow_set_recv_cb(womo_espnow_recv_cb_t cb);

/**
 * Frame an das Sensorboard senden.
 * data muss null-terminiert sein, len ohne null-Byte.
 * Maximal WOMO_ESPNOW_MAX_PAYLOAD Byte.
 * Thread-safe — darf aus beliebigem Task aufgerufen werden.
 */
esp_err_t womo_espnow_send(const char *data, size_t len);

/**
 * true sobald das Sensorboard mindestens einmal einen Frame gesendet hat
 * und seine MAC-Adresse bekannt ist (Unicast statt Broadcast).
 */
bool womo_espnow_peer_known(void);

#ifdef __cplusplus
}
#endif
