/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file womo_config.h
 * @brief Gemeinsame Konfiguration für Display und Sensorboard.
 *
 * Liegt unter firmware/shared/ und wird von beiden Firmware-Zweigen eingebunden.
 * Änderungen hier betreffen BEIDE Seiten – nach jeder Änderung müssen
 * Display UND Sensorboard neu gebaut und geflasht werden.
 *
 * Hardware-spezifische Werte (GPIOs, UART-Port, Buffer-Größen) gehören
 * NICHT hierher, sondern in die jeweilige lokale Config.
 *
 * WiFi-Zugangsdaten stehen in womo_credentials.h (nicht im Git!).
 * Zum Einrichten: womo_credentials.h.example → womo_credentials.h kopieren.
 */
#pragma once

#include "womo_credentials.h"

// ═══════════════════════════════════════════════════════════════════════
//  Kategorie 1: RS485 Protokoll (MUSS identisch sein)
// ═══════════════════════════════════════════════════════════════════════

/** Heartbeat-Intervall (ms): Display sendet alle X ms einen Heartbeat ans Sensorboard.
 *  Das Sensorboard erkennt daran den Display-Peer per ESP-NOW. */
#define WOMO_SENSOR_HEARTBEAT_INTERVAL_MS   2000

/** Timeout (ms) für ACK-Antwort auf ein Kommando (ESP-NOW). */
#define WOMO_SENSOR_COMMAND_TIMEOUT_MS      3000

/** Maximale Anzahl gleichzeitig wartender Kommandos. */
#define WOMO_SENSOR_MAX_PENDING_CMDS        4

// ═══════════════════════════════════════════════════════════════════════
//  Kategorie 2: Topic-Intervalle (Sensor sendet, Display erwartet)
// ═══════════════════════════════════════════════════════════════════════

#define WOMO_TOPIC_CTRL_INTERVAL_MS         2000
#define WOMO_TOPIC_ELEC_INTERVAL_MS         1000
#define WOMO_TOPIC_IMU_INTERVAL_MS          1000
#define WOMO_TOPIC_BAT_INTERVAL_MS          1000
#define WOMO_TOPIC_TANK_INTERVAL_MS         10000
#define WOMO_TOPIC_HX_INTERVAL_MS           10000
#define WOMO_TOPIC_GAS_INTERVAL_MS          10000
#define WOMO_TOPIC_BME_INTERVAL_MS          10000

// ═══════════════════════════════════════════════════════════════════════
//  Kategorie 3: Netzwerk / Infrastruktur
// ═══════════════════════════════════════════════════════════════════════

// WOMO_WIFI_DEFAULT_SSID und WOMO_WIFI_DEFAULT_PASS werden aus
// womo_credentials.h eingebunden (siehe oben).

/** IP-Adresse des RUTX11 Routers im internen Netz. */
#define WOMO_ROUTER_IP                      "192.168.10.1"

// ═══════════════════════════════════════════════════════════════════════
//  Kategorie 4: ESP-NOW Transport (ersetzt RS485)
// ═══════════════════════════════════════════════════════════════════════

/** WiFi-Kanal wenn kein Router verbunden. MUSS auf beiden Boards identisch sein. */
#define WOMO_ESPNOW_CHANNEL_FALLBACK        6

/** Hardware-Limit ESP-NOW Payload (Byte). Frames müssen darunter bleiben. */
#define WOMO_ESPNOW_MAX_PAYLOAD             250

/** Empfangs-Queue-Tiefe (Anzahl Frames). */
#define WOMO_ESPNOW_RECV_QUEUE_LEN          16
