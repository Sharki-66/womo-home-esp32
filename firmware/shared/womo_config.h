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

/** Baudrate – 57600: Kompromiss zwischen Geschwindigkeit und Stabilität
 *  mit dem Auto-DE-Transistor des Waveshare-Boards.
 *  115200 erzeugt Framing-Fehler durch DE-Toggle bei jedem Byte. */
#define WOMO_RS485_BAUDRATE                 57600

/** Heartbeat-Intervall (ms). Sender schickt alle X ms einen Heartbeat,
 *  Empfänger erkennt Verbindungsverlust nach ~2× diesem Wert. */
#define WOMO_RS485_HEARTBEAT_INTERVAL_MS    2000

/** Hello-Intervall (ms) solange Display noch nicht "ready" gemeldet hat. */
#define WOMO_RS485_HELLO_PENDING_MS         1000

/** Hello-Intervall (ms) nachdem Display "ready" ist (Keep-Alive). */
#define WOMO_RS485_HELLO_READY_MS           10000

/** Timeout (ms) für ACK-Antwort auf ein Kommando. */
#define WOMO_RS485_COMMAND_TIMEOUT_MS       3000

/** Maximale Anzahl gleichzeitig wartender Kommandos. */
#define WOMO_RS485_MAX_PENDING_CMDS         4

/** Timeout (ms) für uart_wait_tx_done – muss groß genug sein für
 *  das längste Paket bei der konfigurierten Baudrate. */
#define WOMO_RS485_TX_WAIT_TIMEOUT_MS       500

// ═══════════════════════════════════════════════════════════════════════
//  Kategorie 2: Topic-Intervalle (Sensor sendet, Display erwartet)
// ═══════════════════════════════════════════════════════════════════════

#define WOMO_TOPIC_CTRL_INTERVAL_MS         2000
#define WOMO_TOPIC_IMU_INTERVAL_MS          5000
#define WOMO_TOPIC_BAT_INTERVAL_MS          10000
#define WOMO_TOPIC_TANK_INTERVAL_MS         10000
#define WOMO_TOPIC_HX_INTERVAL_MS           10000
#define WOMO_TOPIC_GAS_INTERVAL_MS          10000
#define WOMO_TOPIC_BME_INTERVAL_MS          15000

// ═══════════════════════════════════════════════════════════════════════
//  Kategorie 3: Netzwerk / Infrastruktur
// ═══════════════════════════════════════════════════════════════════════

// WOMO_WIFI_DEFAULT_SSID und WOMO_WIFI_DEFAULT_PASS werden aus
// womo_credentials.h eingebunden (siehe oben).

/** IP-Adresse des RUTX11 Routers im internen Netz. */
#define WOMO_ROUTER_IP                      "192.168.10.1"
