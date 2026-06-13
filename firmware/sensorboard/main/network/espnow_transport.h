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
 * Callback für empfangene JSON-Zeilen vom Display.
 * Wird aus einem dedizierten Task aufgerufen (nicht aus ISR-Kontext).
 */
typedef void (*espnow_recv_line_cb_t)(const char *json_str);

/**
 * ESP-NOW Transport initialisieren.
 * WiFi muss vorher gestartet sein (STA-Modus).
 * Kanal wird automatisch aus aktiver WiFi-Verbindung übernommen,
 * Fallback: WOMO_ESPNOW_CHANNEL_FALLBACK aus womo_config.h.
 */
esp_err_t espnow_transport_init(void);

/**
 * Callback registrieren der bei jedem empfangenen Frame aufgerufen wird.
 * json_str ist null-terminiert und maximal WOMO_ESPNOW_MAX_PAYLOAD Byte groß.
 */
void espnow_transport_set_recv_cb(espnow_recv_line_cb_t cb);

/**
 * Frame senden. data muss null-terminiert sein, len ohne null-Byte.
 * Maximal WOMO_ESPNOW_MAX_PAYLOAD Byte.
 * Thread-safe — darf aus beliebigem Task aufgerufen werden.
 */
esp_err_t espnow_transport_send(const char *data, size_t len);

/**
 * true sobald das Display mindestens einmal einen Frame gesendet hat
 * und seine MAC-Adresse bekannt ist (Unicast statt Broadcast).
 */
bool espnow_transport_peer_known(void);

#ifdef __cplusplus
}
#endif
