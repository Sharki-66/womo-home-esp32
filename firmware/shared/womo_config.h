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
//  Kategorie 1: Sensorboard-Protokoll (MUSS identisch sein)
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
#define WOMO_TOPIC_IMU_INTERVAL_MS          2000
#define WOMO_TOPIC_BAT_INTERVAL_MS          5000
#define WOMO_TOPIC_TANK_INTERVAL_MS         10000
#define WOMO_TOPIC_HX_INTERVAL_MS           10000
#define WOMO_TOPIC_GAS_INTERVAL_MS          10000
#define WOMO_TOPIC_BME_INTERVAL_MS          10000

// Eco-Modus (Display-Hintergrundlicht aus, kein Landstrom): seltener senden.
// Alle deutlich unter dem Link-Timeout des Displays (5 min).
#define WOMO_TOPIC_CTRL_ECO_INTERVAL_MS     10000
#define WOMO_TOPIC_ELEC_ECO_INTERVAL_MS     10000
#define WOMO_TOPIC_IMU_ECO_INTERVAL_MS      60000
#define WOMO_TOPIC_BAT_ECO_INTERVAL_MS      30000
#define WOMO_TOPIC_TANK_ECO_INTERVAL_MS     60000
#define WOMO_TOPIC_HX_ECO_INTERVAL_MS       60000
#define WOMO_TOPIC_GAS_ECO_INTERVAL_MS      60000
#define WOMO_TOPIC_BME_ECO_INTERVAL_MS      60000

// ═══════════════════════════════════════════════════════════════════════
//  Kategorie 3: Netzwerk / Infrastruktur
// ═══════════════════════════════════════════════════════════════════════

// WOMO_WIFI_DEFAULT_SSID und WOMO_WIFI_DEFAULT_PASS werden aus
// womo_credentials.h eingebunden (siehe oben).

/** Mindestlänge WLAN-Passwort (WPA2-PSK-Passphrase: 8–63 Zeichen, IEEE 802.11i).
 *  Offene Netze sind als ESP32-WLAN nicht zulässig (Display AP-Tab + wifi_config). */
#define WOMO_WIFI_PASS_MIN_LEN              8

/** IP-Adresse des RUTX11 Routers im internen Netz. */
#define WOMO_ROUTER_IP                      "192.168.55.1"

// ═══════════════════════════════════════════════════════════════════════
//  Kategorie 4: ESP-NOW Transport (ersetzt RS485)
// ═══════════════════════════════════════════════════════════════════════

/** WiFi-Kanal wenn kein Router verbunden. MUSS auf beiden Boards identisch sein. */
#define WOMO_ESPNOW_CHANNEL_FALLBACK        6

/** Hardware-Limit ESP-NOW Payload (Byte). Frames müssen darunter bleiben. */
#define WOMO_ESPNOW_MAX_PAYLOAD             250

/** Empfangs-Queue-Tiefe (Anzahl Frames). */
#define WOMO_ESPNOW_RECV_QUEUE_LEN          16

/* ── Kopplung + Verschlüsselung (E0/E1) ──────────────────────────────────
 * Jedes Board speichert die MAC der Gegenstelle im NVS (Namespace
 * WOMO_ESPNOW_PEER_NVS_NS) und nimmt Nutzdaten nur per verschlüsseltem
 * Unicast von ihr an. Gekoppelt wird über unverschlüsselte Broadcast-Beacons
 * (WOMO_ESPNOW_PAIR_BEACON, Broadcast kann ESP-NOW nicht verschlüsseln),
 * die ein Board nur sendet und annimmt, solange sein Kopplungsfenster offen ist:
 *   - beide: solange keine Gegenstelle gespeichert ist (Erstinbetriebnahme)
 *   - Display: WOMO_ESPNOW_PAIR_WINDOW_MS nach „Sensorboard neu koppeln“
 *   - Sensorboard: solange seit WOMO_ESPNOW_LINK_DEAD_MS nichts von der
 *     Gegenstelle kam (z. B. Display getauscht)
 * Schlüssel WOMO_ESPNOW_PMK/LMK in womo_credentials.h. */

/** Kopplungsfenster am Display nach Tastendruck (ms). */
#define WOMO_ESPNOW_PAIR_WINDOW_MS          60000

/** Ohne Frame der Gegenstelle so lange (ms) → Sensorboard öffnet Kopplung. */
#define WOMO_ESPNOW_LINK_DEAD_MS            10000

/** Sendeintervall der Kopplungs-Beacons (ms). */
#define WOMO_ESPNOW_PAIR_BEACON_MS          1000

/** Inhalt des Kopplungs-Beacons (Broadcast, unverschlüsselt). */
#define WOMO_ESPNOW_PAIR_BEACON             "{\"type\":\"pair\"}"

/** NVS-Namespace/-Key für die MAC der Gegenstelle. */
#define WOMO_ESPNOW_PEER_NVS_NS             "espnow_peer"
#define WOMO_ESPNOW_PEER_NVS_KEY            "mac"

#ifndef WOMO_ESPNOW_PMK
#error "WOMO_ESPNOW_PMK fehlt in womo_credentials.h (siehe womo_credentials.h.example)"
#endif
#ifndef WOMO_ESPNOW_LMK
#error "WOMO_ESPNOW_LMK fehlt in womo_credentials.h (siehe womo_credentials.h.example)"
#endif
_Static_assert(sizeof(WOMO_ESPNOW_PMK) == 17, "WOMO_ESPNOW_PMK muss genau 16 Zeichen haben");
_Static_assert(sizeof(WOMO_ESPNOW_LMK) == 17, "WOMO_ESPNOW_LMK muss genau 16 Zeichen haben");
