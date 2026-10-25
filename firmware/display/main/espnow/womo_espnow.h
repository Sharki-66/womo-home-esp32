/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
 * Frame verschlüsselt per Unicast an das gekoppelte Sensorboard senden.
 * Ohne Kopplung ESP_ERR_INVALID_STATE (kein Broadcast von Nutzdaten).
 * data muss null-terminiert sein, len ohne null-Byte.
 * Maximal WOMO_ESPNOW_MAX_PAYLOAD Byte.
 * Thread-safe — darf aus beliebigem Task aufgerufen werden.
 */
esp_err_t womo_espnow_send(const char *data, size_t len);

/**
 * true, wenn ein Sensorboard gekoppelt ist (MAC im NVS).
 */
bool womo_espnow_peer_known(void);

/**
 * MAC des gekoppelten Sensorboards. false, wenn keins gekoppelt ist.
 */
bool womo_espnow_get_peer(uint8_t mac[6]);

/**
 * Kopplungsfenster für WOMO_ESPNOW_PAIR_WINDOW_MS öffnen (z. B. nach Tausch
 * des Sensorboards). Schließt beim ersten verschlüsselten Frame der Gegenstelle.
 */
void womo_espnow_pair_start(void);

/**
 * Restzeit des Kopplungsfensters in s: 0 = geschlossen,
 * -1 = offen ohne Ende, weil noch kein Sensorboard gekoppelt ist.
 */
int womo_espnow_pair_remaining_s(void);

#ifdef __cplusplus
}
#endif
