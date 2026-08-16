/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/**
 * ble_manager.h
 *
 * Zentraler BLE-Stack-Eigentümer (NimBLE Central).
 * Startet NimBLE, verwaltet den Scan und leitet GAP-Events weiter.
 *
 * Module (GasBee, Ruuvi, …) registrieren sich hier – sie kennen sich
 * nicht gegenseitig und initialisieren kein eigenes NimBLE.
 */

#include <esp_err.h>
#include <host/ble_gap.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Callback für empfangene Advertisement-Pakete (alle BLE-Geräte in Reichweite). */
typedef void (*ble_adv_handler_t)(const struct ble_gap_disc_desc *disc);

/** Callback für Connection-Events (CONNECT, DISCONNECT, NOTIFY_RX). */
typedef int (*ble_conn_event_handler_t)(struct ble_gap_event *event, void *arg);

/**
 * @brief NimBLE-Stack initialisieren und Scan starten.
 *        Darf nur einmal aufgerufen werden – vor allen anderen BLE-Modulen.
 */
esp_err_t ble_manager_init(void);

/**
 * @brief Advertisement-Handler registrieren (max. 4).
 *        Wird für jeden BLE_GAP_EVENT_DISC aufgerufen.
 */
void ble_manager_register_adv_handler(ble_adv_handler_t fn);

/**
 * @brief Connection-Event-Handler setzen (nur ein Handler, typisch GasBee).
 *        Empfängt CONNECT, DISCONNECT, NOTIFY_RX.
 */
void ble_manager_set_conn_handler(ble_conn_event_handler_t fn, void *arg);

/**
 * @brief Verbindung zu einer BLE-Adresse aufbauen.
 *        Bricht laufenden Scan ab, verbindet, startet Scan danach neu.
 */
void ble_manager_connect(const ble_addr_t *addr);

/**
 * @brief Scan (neu) starten.
 *        Wird intern nach Connect/Disconnect aufgerufen; kann auch extern gerufen werden.
 */
void ble_manager_start_scan(void);

#ifdef __cplusplus
}
#endif
