/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "womo_config.h"

/* WoMoHome ESP32 Sensor Board Konfiguration
 * Sensor Board Configuration (ESP32-S3 DevKitC-1, N16R8)
 * Stand: Feb 2026 — Pinbelegung gemäß DevKitC-1 Header-Block
 *
 * Pinout Übersicht ESP32-S3 WROOM-1 (DevKitC-1):
 *
 *                                             Detail J1 (links)        |           Detail J3 (rechts)
 *                            Funktion | Type | Use | Name | Pin        |       Pin | NAME | USE | Type  | Funktion
 * ------------------------------------|------|-----|------|------------------------|------|-----|-------|------------------------------------------
 *                  3.3 V power supply |    P |  🟩 |  3V3 | 1          E         1 | GND  | 🟩  | G     | Ground
 *                  3.3 V power supply |    P |  🟩 |  3V3 | 2          S         2 | TX   | 🟩  | I/O/T | U0TXD, GPIO43, CLK_OUT1
 *                               Reset |    I |  🟩 |  RST | 3          P         3 | RX   | 🟩  | I/O/T | U0RXD, GPIO44, CLK_OUT2
 *    (ADC1_CH3, RTC, GPIO4) Batt1 KFZ | I/O/T|  🟨 |    4 | 4          3         4 | 1    | 🟨  | I/O/T | Tank1 Frisch (GPIO1, RTC, ADC1_CH0)
 *  (ADC1_CH4, RTC, GPIO5) Batt2 Board | I/O/T|  🟨 |    5 | 5          2         5 | 2    | 🟨  | I/O/T | Tank2 Grau (GPIO2, RTC, ADC1_CH1)
 *         DC1_CH5, RTC, GPIO6) WAKEUP | I/O/T|  🟨 |    6 | 6          -         6 | 42   | 🟩  | I/O/T | MTMS, GPIO42
 *   (ADC1_CH6, RTC, GPIO7) Display 5V | I/O/T|  🟨 |    7 | 7          S         7 | 41   | 🟩  | I/O/T | MTDI, GPIO41
 *     (ADC2_CH4, RTC, GPIO15) I2C SCL | I/O/T|  🟨 |   15 | 8          3         8 | 40   | 🟩  | I/O/T | MTDO, GPIO40
 *     (ADC2_CH5, RTC, GPIO16) I2C SDA | I/O/T|  🟨 |   16 | 9          -         9 | 39   | 🟩  | I/O/T | MTCK, GPIO39
 *(ADC2_CH6, U1TXD, RTC, GPIO17) Licht | I/O/T|  🟨 |   17 | 10         W        10 | 38   | 🟨  | I/O/T | RGB_LED  (GPIO38)
 *(ADC2_CH7, U1RXD, RTC, GPIO18) Pumpe | I/O/T|  🟨 |   18 | 11         R        11 | 37   | 🟥  | I/O/T | ⚠ PSRAM (GPIO37, nicht nutzbar!)
 *               (ADC1_CH7, RTC) GPIO8 | I/O/T|  ⬜ |    8 | 12         O        12 | 36   | 🟥  | I/O/T | ⚠ PSRAM (GPIO36, nicht nutzbar!)
 *                ADC1_CH2, RTC, GPIO3 | I/O/T|  ⬜ |    3 | 13         O        13 | 35   | 🟥  | I/O/T | ⚠ PSRAM (GPIO35, nicht nutzbar!)
 *                   GPIO46, STRAP/LOG | I/O/T|  🟩 |   46 | 14         M        14 | 0    | 🟩  | I/O/T | BOOT/STRAP, GPIO0
 *               (ADC1_CH8, RTC) GPIO9 | I/O/T|  ⬜ |    9 | 15         -        15 | 45   | 🟨  | I/O/T | HX711 SCK (GPIO45)
 *              (ADC1_CH9, RTC) GPIO10 | I/O/T|  ⬜ |   10 | 16         1        16 | 48   | 🟩  | I/O/T | Onboard WS2812 RGB-LED (GPIO48)
 *     (ADC2_CH0, RTC, GPIO11) 12V EIN | I/O/T|  🟨 |   11 | 17                  17 | 47   | 🟨  | I/O/T | HX711 DOUT (GPIO47)
 *     (ADC2_CH1, RTC, GPIO12) 12V AUS | I/O/T|  🟨 |   12 | 18                  18 | 21   | 🟨  | I/O/T | AC 220V Sence (GPIO21, RTC)
 *  (ADC2_CH2, RTC, GPIO13) Multimedia | I/O/T|  🟨 |   13 | 19                  19 | 20   | 🟩  | I/O/T | USB_D+, GPIO20, RTC
 *   (ADC2_CH3, RTC, GPIO14) 12V Sence | I/O/T|  🟨 |   14 | 20                  20 | 19   | 🟩  | I/O/T | USB_D-, GPIO19, RTC
 *                    5 V power supply |    P |  🟩 |   5V | 21                  21 | GND  | 🟩  | G     | Ground
 *                              Ground |    G |  🟩 |  GND | 22                  22 | GND  | 🟩  | G     | Ground
 * ------------------------------------|------|-----|------|------------------------|------|------|-----|-------|-----------------------------------------
 *
 * Legende:
 * - Funktion: Hauptfunktion des Pins (laut Datenblatt, kann mehrfach belegt sein)
 * - Type: I=Input, O=Output, P=Power, G=Ground, T=Touch, RTC=RTC Power Domain, ADC=ADC-Kanal
 * - Use: 🟩 = Fest belegt für Hauptfunktion, 🟨 = Sensor Board, ⬜ = Frei/Nicht belegt
 */

// ====================================================================================
// Board Information
// ====================================================================================
#define SENSOR_BOARD_NAME "ESP32-S3-WROOM-1"

// ====================================================================================
// I2C-Bus für Sensoren auf Header J1 (SDA=GPIO16, SCL=GPIO15)
// ====================================================================================
#define SENSOR_I2C_EXT_PORT 0
#define SENSOR_I2C_EXT_SDA_GPIO 16
#define SENSOR_I2C_EXT_SCL_GPIO 15
#define SENSOR_I2C_EXT_SPEED_HZ 100000
#define SENSOR_I2C_EXT_PULLUP_ENABLE 1

// ====================================================================================
// HX711 Load Cell ADC (Dual Channel for Gas Bottles)
// ====================================================================================
#define SENSOR_HX711_DOUT_GPIO 47
#define SENSOR_HX711_SCK_GPIO 45
#define SENSOR_HX711_READY_TIMEOUT_MS 200U
#define SENSOR_HX711_READY_RETRY_COUNT 2
#define SENSOR_HX711_READY_BACKOFF_MS 50U
#define SENSOR_HX711_STARTUP_DELAY_MS 1000U
#define SENSOR_HX711_POLL_INTERVAL_MS 10000U
#define SENSOR_HX711_AVG_SAMPLES 3U
#define SENSOR_HX711_ENABLE_CHANNEL_B 1

// Kalibrierwerte (Kalibriert 2026-02-13, Referenz: 5×3.6 kg = 18.0 kg)
#define SENSOR_HX711_OFFSET_A -275500
#define SENSOR_HX711_SCALE_A 0.04004f
#define SENSOR_HX711_OFFSET_B 77056
#define SENSOR_HX711_SCALE_B -0.15674f

// ====================================================================================
// ESP-NOW Transport (Display Link) – vormals RS485 GPIO9/10/8
// GPIO8 (ehemaliger RS485-DE) wird im Deep-Sleep LOW gehalten (floating prevention).
// ====================================================================================
#define SENSOR_ESPNOW_DE_LEGACY_GPIO  8   /* Ehemaliger RS485-DE: im Deep-Sleep LOW halten */

// ====================================================================================
// BNO055 IMU – Achsen-Remapping für Einbaulage
// ====================================================================================
// Mögliche Werte (aus bno055.h axis_t enum):
//   POSITIVE_X (0x00), NEGATIVE_X (0x04)
//   POSITIVE_Y (0x01), NEGATIVE_Y (0x05)
//   POSITIVE_Z (0x02), NEGATIVE_Z (0x06)
//
// Fahrzeugkoordinaten: X→vorne, Y→links, Z→oben
// Hier eintragen, welche Sensor-Achse der jeweiligen Fahrzeug-Achse entspricht.
// WICHTIG: Die drei Achsen MÜSSEN ein rechtshändiges System bilden!
//   Prüfung: new_X × new_Y = new_Z (Kreuzprodukt)
//   Sonst liefert der BNO055-Fusion fehlerhafte Euler-Winkel.
//
// Einbaulage Sensorboard im Fahrzeug (physische Messung):
//   Sensor-X zeigt nach OBEN (Fahrzeug-Hochachse)
//   Sensor-Y zeigt in FAHRTRICHTUNG (Fahrzeug-Längsachse, vorne)
//   Sensor-Z zeigt nach RECHTS/LINKS (Fahrzeug-Querachse)
//
// BNO055 Euler: Roll um X-Achse, Pitch um Y-Achse, Heading um Z-Achse
// → Mapping: Sensor-Z→logisch X (Roll/Quer), Sensor-Y→logisch Y (Pitch/Längs), Sensor-X→logisch Z (Heading/Hoch)
//   Rechte-Hand-Check: Z × (−Y) = X ✓
#define SENSOR_BNO055_AXIS_X  POSITIVE_Z
#define SENSOR_BNO055_AXIS_Y  NEGATIVE_Y
#define SENSOR_BNO055_AXIS_Z  POSITIVE_X

// ====================================================================================
// INA226 – Strom-/Leistungsmessung Hauptleitung
// ====================================================================================
#define SENSOR_INA226_I2C_ADDR      0x40    // A0=GND, A1=GND
#define SENSOR_INA226_I2C_CLK_HZ    400000  // Fast-Mode

// Shunt-Widerstand in mΩ (Milliohm)
// 10 mΩ (0,01 Ω) → I_max = 81,92 mV / 10 mΩ = 8,192 A (bei MAX_CURRENT_A=100 ist LSB ~3 mA)
#define SENSOR_INA226_SHUNT_MOHM    10

// Maximaler Messstrom (A) – bestimmt Current_LSB und damit die Auflösung
// Current_LSB = MAX_CURRENT / 32768 → kleiner Wert = besser Auflösung, aber geringerer Bereich
// 3 A: misst Display+Sensorboard inkl. Relais-Schaltstöme (max ~2 A), LSB = 91 µA
#define SENSOR_INA226_MAX_CURRENT_A 3

// Abfrageintervall des Messtasks (unabhängig vom ESP-NOW Publish-Intervall WOMO_TOPIC_ELEC_INTERVAL_MS)
#define SENSOR_INA226_POLL_INTERVAL_MS 500U

// ====================================================================================
// BME Sensor I2C Adressen (für einfache Änderung ohne Bibliotheks-Modifikation)
// ====================================================================================
#define SENSOR_BME280_I2C_ADDR      0x77    // BME280/BME260 Adresse
#define SENSOR_BME680_I2C_ADDR      0x76    // BME680 Adresse

// ====================================================================================
// Analog ADC (Battery & Tank Sensors)
// ====================================================================================
#define SENSOR_BATT1_ADC_CHANNEL 3     // GPIO4 (Batt1 Kfz)
#define SENSOR_BATT2_ADC_CHANNEL 4     // GPIO5 (Batt2 Board)
#define SENSOR_TANK1_ADC_CHANNEL 0     // GPIO1 (Frischwasser)
#define SENSOR_TANK2_ADC_CHANNEL 1     // GPIO2 (Grauwasser)

// Batterie-Kalibrierung (U_kalibriert = U_gemessen * SCALE + OFFSET_mV)
// Pipeline: ADC-mV × (122/22) = U_roh → × SCALE + OFFSET = U_kalibriert
// Für Feinabgleich: SCALE = Multimeter_mV / (Anzeige_mV / alter_SCALE)
// Kalibriert 2026-05-18 neue Platine:
//   Multimeter 13,0 V → Anzeige 17,4 V mit altem SCALE=1.354
//   U_roh = 17400/1.354 = 12850 mV → SCALE = 13000/12850 = 1.012
//   (alter SCALE=1.354 war Ausreißer wegen Q3-Defekt auf Vorgängerplatine)
#define SENSOR_BATT1_CAL_SCALE 1.012f
#define SENSOR_BATT1_CAL_OFFSET_MV 0
#define SENSOR_BATT2_CAL_SCALE 1.012f
#define SENSOR_BATT2_CAL_OFFSET_MV 0

// Tank-Kalibrierung (U_kalibriert = U_gemessen * SCALE + OFFSET_mV)
// Hinweis: 0-1V Sensoren liegen direkt am ADC (kein Spannungsteiler).
// Feinabgleich gegen Multimeter moeglich, falls ADC systematisch abweicht.
#define SENSOR_TANK1_CAL_SCALE 1.00f
#define SENSOR_TANK1_CAL_OFFSET_MV 0
#define SENSOR_TANK2_CAL_SCALE 1.00f
#define SENSOR_TANK2_CAL_OFFSET_MV 0

// Tank-Prozent-Mapping pro Kanal (EMPTY_MV..FULL_MV => 0..100%)
// Frischwasser-Referenz: voll bei ca. 1005 mV gemessen.
#define SENSOR_TANK1_EMPTY_MV 0
#define SENSOR_TANK1_FULL_MV 1000
#define SENSOR_TANK2_EMPTY_MV 0
#define SENSOR_TANK2_FULL_MV 1000

// ====================================================================================
// TODO: Wohnmobil-Tanksensoren (4-Stufen Reed-Schwimmer, 5 Leitungen)
// ====================================================================================
// Die originalen WoMo-Tanksensoren haben 5 Leitungen:
//   Masse, 25%, 50%, 75%, Voll (100%)
// Die Schalter schließen kumulativ (bei 75% sind 25%+50%+75% alle geschlossen).
//
// Geplante Lösung: 1 ADC-Pin pro Tank über ein Widerstandsnetzwerk
//   KONFLIKT (2026-08-30): GPIO17/18 sind NICHT mehr frei – auf der Hardware
//   bereits für Licht (17) und Pumpe (18) verdrahtet (SENSOR_LIGHT_GPIO /
//   SENSOR_PUMP_GPIO oben). Vor Umsetzung dieses TODOs andere freie Pins wählen.
//
// Schaltung (Beispielbelegung, R-Werte noch zu verifizieren):
//   3.3V ─── R_pull(10kΩ) ─── ADC-Pin
//   25%-Schalter  ─── R1(4kΩ) ───┐
//   50%-Schalter  ─── R2(3kΩ) ───┤── ADC-Pin
//   75%-Schalter  ─── R3(2kΩ) ───┤
//  100%-Schalter  ─── R4(1kΩ) ───┘
//   Signalmasse   ─── GND
//
// Erwartete ADC-Spannungen (5 eindeutige Pegel), ca.:
//   0%   → ~3300 mV   (kein Schalter geschlossen)
//   25%  → ~2350 mV
//   50%  → ~1650 mV
//   75%  → ~1130 mV
//  100%  →  ~140 mV
//
// Implementierung (noch ausstehend):
//   - analog_sensor.c: Pegelerkennung anhand ADC-Spannungsschwellen
//   - sensor_config.h: GPIO + Schwellwerte hier eintragen
//   - Bestehende SENSOR_TANK1/2_ADC_CHANNEL bzw. neue Defines anlegen
// ====================================================================================

// ====================================================================================
// Board Power / Multimedia GPIOs (LBE-Steuerung über bistabiles Relais)
// ====================================================================================
#define SENSOR_RGB_LED_GPIO 48             // Onboard WS2812 RGB-LED (GPIO48, bestätigt)
#define SENSOR_PWR_12V_ON_GPIO 11      // Schaltausgang: LBE EIN (Relais-Puls)
#define SENSOR_PWR_12V_OFF_GPIO 12     // Schaltausgang: LBE AUS (Relais-Puls)
#define SENSOR_PWR_12V_SENSE_GPIO 14   // Eingang: LBE Spannungsfeedback
#define SENSOR_AC_SENSE_GPIO 21        // Eingang: 230V Netzkontrolle
#define SENSOR_MULTIMEDIA_PWR_GPIO 13  // Schaltausgang: Multimedia Power

// Pulslänge für das bistabile LBE-Relais (EIN/AUS). Gilt für Boot-EIN-Puls
// (app_main.c), Laufzeit-Puls (espnow_modem.c) und Deep-Sleep-AUS-Puls
// (deep_sleep.c). 100 ms reichten dem Relais nicht zum sicheren Umschalten.
// Ein längerer Puls schadet der Stabrelais-Spule nicht, solange der Pin danach
// wieder auf LOW geht (macht der Code).
// TODO(hardware): Mindestpulslänge des konkreten EBL-Relais aus Messung/Datenblatt
// bestätigen und ggf. reduzieren.
#define SENSOR_PWR_12V_PULSE_MS 500

// 12V-Sense (GPIO14): echte Rückmeldung des Bordnetz-Relais.
// 1 = ctrl-Topic-Feld `pwr_on` meldet den an GPIO14 GEMESSENEN Zustand (Display-
//     Button zeigt Realität statt nur des kommandierten Zustands).
// 0 = reines Soft-Flag (nur der zuletzt kommandierte Zustand).
#define SENSOR_PWR_12V_SENSE_ENABLE      1
// Aktiver Pegel des Sense-Eingangs. Annahme analog AC-Sense: 12V vorhanden -> HIGH.
// TODO(hardware): per Messung an GPIO14 verifizieren; bei invertierter Beschaltung 0.
#define SENSOR_PWR_12V_SENSE_ACTIVE_HIGH 1
// Nach einem Relais-Puls braucht die 12V-Schiene Zeit zum Auf-/Abbau. Solange wird
// weiter der kommandierte Zustand gemeldet, nicht der Sense-Pin.
#define SENSOR_PWR_12V_SENSE_GRACE_MS    2500
// GPIO13-Defekt (2026-08-30) geklärt: echter Hardware-Schaden durch Verpolung
// an Steckverbinder J7 ("LT452 Malibu", 10-pol. Molex Micro-Fit) – Multimedia-
// Leitung lag dabei auf GND, ESP32-Ausgang wurde bei HIGH hart kurzgeschlossen
// (Ausgangstreiber vermutlich durchlegiert). Kein Software-Fix möglich.
// Ebenfalls betroffen: GPIO17/Licht (lag auf vertauschtem 12V-Kontroll-Signal).
// GPIO18/Pumpe unbeschädigt trotz gleichem Vorfall.
// Entscheidung: Kein Pin-Remapping auf dieser Platine – neue Platinenrevision
// wird bestellt (korrekter J7, RS485-Transceiver entfernt, Q5 richtig gepolt).
// Diese Config gilt für die NEUE Platine mit unbeschädigten GPIOs 13/17/18.

// Licht/Pumpe: auf der Hardware bereits verdrahtet, Firmware-Ansteuerung noch
// nicht implementiert. Gleiches Schaltprinzip wie Multimedia (N-MOSFET,
// HIGH=ein/LOW=aus, kein Hold nötig – siehe deep_sleep.c).
// KONFLIKT: GPIO17/18 waren im Tanksensor-TODO weiter unten (Zeile ~174) als
// "frei" für die geplanten Original-WoMo-Tanksensoren (Tank1/Tank2) vorgesehen
// – das stimmt nicht mehr, Pins sind belegt. Tanksensor-Planung muss auf
// andere Pins ausweichen, bevor sie umgesetzt wird.
#define SENSOR_LIGHT_GPIO 17           // Schaltausgang: Licht (Hardware fertig, Firmware TODO)
#define SENSOR_PUMP_GPIO 18            // Schaltausgang: Pumpe (Hardware fertig, Firmware TODO)

// ====================================================================================
// Display-Versorgung (neue Platine: N-Kanal Treiber Q5/2N7002 vor P-Kanal Q4/AO3401A)
// GPIO7 → R22(10k) → Q5-Gate: HIGH = Q5 leitet → Q4-Gate LOW → AO3401A leitet = Display EIN
//                               LOW/Hi-Z = Q5 sperrt → R21(100k) zieht Q4-Gate auf 5V → Display AUS
// OUTPUT HIGH ist sicher (treibt nur Q5-Gate, kein direkter Vgs-Querstrom mehr).
// ====================================================================================
#define SENSOR_DISPLAY_PWR_GPIO 7      // HIGH=ein (Q5→Q4), LOW/Hi-Z=aus

// ====================================================================================
// Deep Sleep / Wakeup
// GPIO6 = TOUCH_PAD_NUM6: kapazitiver Touch-Eingang, RTC-fähig, kein Strapping
// Kupferpad/Streifen direkt an GPIO6 — kein externer IC nötig
// Schwellwert: Touch gilt als ausgelöst wenn Messwert um >20% unter Baseline fällt
// ====================================================================================
#define SENSOR_WAKEUP_GPIO 6                    // Touch-Wakeup: GPIO6 = TOUCH_PAD_NUM6
#define SENSOR_WAKEUP_TOUCH_THRESHOLD_PCT 10    // Empfindlichkeit: 10% über Baseline = Touch erkannt

// ====================================================================================

// ====================================================================================
// BME680 / BSEC Konfiguration
// ====================================================================================
// Platinen-Eigenerwärmung: Der ESP32-S3 erwärmt das Board um typisch 3-6 °C.
// BSEC zieht diesen Offset von der gemessenen Temperatur ab, bevor es IAQ,
// CO2eq und VOCeq berechnet. Zu klein → IAQ bleibt zu hoch (wie ohne Offset).
// Zu groß → BSEC überkompensiert und IAQ wird künstlich niedrig.
// Kalibrierung: Sensortemperatur mit Referenz-Thermometer vergleichen und die
// Differenz hier eintragen (Ref = Sensor − Offset).
#define SENSOR_BME680_TEMP_OFFSET_C  4.0f   // Typisch 3-6 °C für ESP32-S3 DevKitC
// Bei Tausch des Innen-BME680 hochzaehlen, damit BSEC keinen alten Baseline-State laedt.
#define SENSOR_BME680_BSEC_STATE_VERSION 3

// =============================================================================
// Gas Consumption Calculation (EMA-based rate tracking)
// =============================================================================
#define SENSOR_GAS_FILL_KG                11.0f  // Netto-Inhalt einer vollen 11-kg-Flasche (alle Flaschentypen)
#define SENSOR_GAS_TARE_KG                10.5f  // Fallback-Tara wenn noch keine Voll-Referenz gesetzt (Stahlflasche)
#define SENSOR_GAS_NO_BOTTLE_THRESHOLD_KG  1.0f  // Unter diesem Gewicht = keine Flasche auf der Waage (HX711: nc, GasBee: leer/0 %)
#define SENSOR_GAS_ACTIVE_DEFAULT   -1       // -1 = Auto-Erkennung (A oder B)
#define SENSOR_GAS_RATE_ALPHA_1H    0.2f     // EMA-Glättungsfaktor (1h-Fenster)
#define SENSOR_GAS_RATE_ALPHA_2H    0.1f     // EMA-Glättungsfaktor (2h-Fenster)
#define SENSOR_GAS_RATE_MIN_DT_SEC  30       // Minimaler Messpunkt-Abstand (Sek)
#define SENSOR_GAS_ACTIVE_EPS_KG    0.05f    // Mindest-Änderung für Verbrauchserkennung
#define SENSOR_GAS_GAP_MAX_S        86400    // Max. Ausschalt-Lücke, über die der Verbrauch gemittelt wird (1 Tag)

// =============================================================================
// Tank Rate Calculation (EMA-based, analog zu Gas)
// =============================================================================
#define SENSOR_TANK_RATE_ALPHA_1H   0.2f     // EMA-Glättungsfaktor (1h-Fenster)
#define SENSOR_TANK_RATE_ALPHA_2H   0.1f     // EMA-Glättungsfaktor (2h-Fenster)
#define SENSOR_TANK_RATE_MIN_DT_SEC 30       // Minimaler Messpunkt-Abstand (Sek)

// =============================================================================
// WiFi STA Configuration (Verbindung zum RUTX11 Router)
// =============================================================================
#define SENSOR_WIFI_DEFAULT_SSID    WOMO_WIFI_DEFAULT_SSID
#define SENSOR_WIFI_DEFAULT_PASS    WOMO_WIFI_DEFAULT_PASS
#define SENSOR_WIFI_NVS_NAMESPACE   "wifi_cfg"
#define SENSOR_WIFI_NVS_KEY_SSID    "ssid"
#define SENSOR_WIFI_NVS_KEY_PASS    "pass"
#define SENSOR_WIFI_HOSTNAME        "Womo-Sensor"
#define SENSOR_WIFI_MDNS_INSTANCE   "WoMoHome Sensor Board"
#define SENSOR_WIFI_RETRY_MAX       5           // Verbindungsversuche bevor Pause
#define SENSOR_WIFI_RETRY_PAUSE_MS  30000       // 30s Pause nach fehlgeschlagenen Versuchen

// HTTP Server (Parkhilfe / Horizont)
#define SENSOR_HTTP_PORT            80
#define SENSOR_SPIFFS_BASE_PATH     "/spiffs"
#define SENSOR_SPIFFS_PARTITION     "storage"
#define SENSOR_SPIFFS_MAX_FILES     5

// NTP Zeit-Synchronisation (setzt RTC nach WiFi-Verbindung)
#define SENSOR_NTP_SERVER           "pool.ntp.org"

// ====================================================================================
// Ruuvi BLE Scanner
// Routing ausschließlich per MAC – konfiguriert über Display (Einstellungen > RTC / Ruuvi),
// per RS485 cmd "ruuvi_config" übertragen und hier in NVS persistiert.
// Kein Compile-Time-Default: unkonfigurierter Slot ist deaktiviert (kein fremder Tag
// aus Nachbarfahrzeugen wird akzeptiert), bis der Nutzer eine MAC im Display einträgt.
// Innen-Tag: z. B. RuuviTag 4in1 oder Standard
// Außen-Tag: z. B. RuuviTag Pro 3in1 (kein Druck) oder Pro 4in1 (mit Druck)
// Bei Außen-Tag ohne Drucksensor: Druck automatisch vom Innensensor übernommen.
// ====================================================================================
#define SENSOR_RUUVI_NVS_NAMESPACE   "ruuvi_cfg"
#define SENSOR_RUUVI_NVS_KEY_INDOOR  "indoor_mac"
#define SENSOR_RUUVI_NVS_KEY_OUTDOOR "outdoor_mac"
#define SENSOR_RUUVI_SCAN_MAX_TAGS   8     // Max. Einträge in der Scan-Liste ans Display
#define SENSOR_RUUVI_SCAN_MAX_AGE_S  60    // Tags älter als das fliegen aus der Scan-Liste

// ====================================================================================
// BLE-Scan (ble_manager, gemeinsam für Ruuvi + GasBee)
// Passiv: Ruuvi und GasBee senden alle Daten im Advertising, keine Scan-Response nötig.
// Tastverhältnis 100 % (Window = Intervall, Empfehlung der GasBee-Vorgabe). Mit 30/50 ms
// kamen im Test (2026-10-17, WLAN/ESP-NOW aktiv) nur 2/10 GasBee-Pakete pro seq an –
// den Rest nimmt sich der Coex-Arbiter. Pakete pro seq werden geloggt (gasbee_ble).
// Einheit: 0,625 ms.
// ====================================================================================
#define SENSOR_BLE_SCAN_ITVL         0x0050  // 80 × 0,625 ms = 50 ms
#define SENSOR_BLE_SCAN_WINDOW       0x0050  // 80 × 0,625 ms = 50 ms (= Intervall)

// ====================================================================================
// GasBee BLE (Gaswaage, firmware/gasbee_nrf) – Advertising-Format v1
// Kein Verbindungsaufbau: Daten kommen ausschließlich aus dem Advertising
// (Name "GasBee" + Manufacturer Data, Company-ID 0xFFFF). Format siehe README.
// Zuordnung Flasche A/B per MAC – konfiguriert über Display (Einstellungen >
// RTC / Temp / Gas), per cmd "gasbee_config" übertragen und hier in NVS persistiert.
// Vorrang: Ist das HX711-Modul am Kabel vorhanden, liefert es A+B, GasBee wird ignoriert.
// ====================================================================================
#define SENSOR_GASBEE_NVS_NAMESPACE  "gasbee_cfg"
#define SENSOR_GASBEE_NVS_KEY_A      "bottle_a_mac"
#define SENSOR_GASBEE_NVS_KEY_B      "bottle_b_mac"
#define SENSOR_GASBEE_COMPANY_ID     0xFFFF  // SIG-reserviert für Tests/interne Nutzung
#define SENSOR_GASBEE_FORMAT_VERSION 1
#define SENSOR_GASBEE_CYCLE_S        300     // Messzyklus GasBee (5 min; Testmodus GasBee: 60)
#define SENSOR_GASBEE_BURST_S        10      // Sendefenster pro Zyklus (10 Pakete × 1 s)
// Ohne neue seq für > 2 Zyklen (+ Sendefenster) gilt die Flasche als nicht verbunden
#define SENSOR_GASBEE_STALE_MS       ((2 * SENSOR_GASBEE_CYCLE_S + SENSOR_GASBEE_BURST_S) * 1000LL)
#define SENSOR_GASBEE_SCAN_MAX_TAGS  8       // Max. Einträge in der Scan-Liste ans Display
#define SENSOR_GASBEE_SCAN_MAX_AGE_S (SENSOR_GASBEE_CYCLE_S + 60) // Scan-Liste: mind. 1 Zyklus halten
// Einrichten am Display: nur GasBees im Konfigurationsmodus (USB, sendet jede Sekunde),
// die innerhalb dieser Zeit gesehen wurden, gelten als "angesteckt"
#define SENSOR_GASBEE_CFG_MAX_AGE_S  5
// Kalibrierung per GATT (gasbee_gatt_cal.c): Verbinden + Discovery + Write, danach trennen
#define SENSOR_GASBEE_CAL_TIMEOUT_MS 15000
// Kalibrier-Sitzung (Display-Assistent): Flasche gilt solange als nc (kein Alarm, keine Rate).
// Sicherheitsgrenze, falls gasbee_cal_end ausbleibt
#define SENSOR_GASBEE_CAL_SESSION_MAX_S 900

// LiPo-Kennlinie (1 Zelle, 4,2 V voll): Stützpunkte {mV, %}, absteigend, linear interpoliert.
// Quelle: verbreitete 1S-LiPo-Ruhespannungstabelle (RC-/Maker-Community), NICHT gemessen.
// TODO(kalibrierung): Am echten GasBee-Akku verifizieren – Akku vollladen, unter
// Normalbetrieb entladen und mV-Werte gegen entnommene Kapazität aufzeichnen.
#define SENSOR_GASBEE_LIPO_CURVE { \
    {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80}, {3980, 75}, \
    {3950,  70}, {3910, 65}, {3870, 60}, {3850, 55}, {3840, 50}, {3820, 45}, \
    {3800,  40}, {3790, 35}, {3770, 30}, {3750, 25}, {3730, 20}, {3710, 15}, \
    {3690,  10}, {3610,  5}, {3270,  0} }

// ====================================================================================
// Log-Level Konfiguration (ESP_LOG_NONE/ERROR/WARN/INFO/DEBUG/VERBOSE)
// Hier zentral je Modul einstellen, ohne Code zu ändern.
// ====================================================================================
#define LOG_LEVEL_SENSOR_MAIN    ESP_LOG_INFO
#define LOG_LEVEL_DEEP_SLEEP     ESP_LOG_WARN
#define LOG_LEVEL_SENSORBOARD    ESP_LOG_INFO    // temporär INFO für ESP-NOW-ACK-Fehlersuche
#define LOG_LEVEL_ANALOG         ESP_LOG_WARN
#define LOG_LEVEL_BNO055         ESP_LOG_WARN    // bno055_app + BNO055-Komponente
#define LOG_LEVEL_BME680         ESP_LOG_WARN    // bme680_app + bme680-Komponente
#define LOG_LEVEL_BME260         ESP_LOG_WARN    // bme260_app (falls separate Implementierung)
#define LOG_LEVEL_HX711          ESP_LOG_WARN
#define LOG_LEVEL_INA226         ESP_LOG_WARN    // ina226 Strom-/Leistungsmessung (DEBUG)
#define LOG_LEVEL_BLE_MANAGER    ESP_LOG_INFO    // temporär INFO für Ruuvi-Scan-Fehlersuche
#define LOG_LEVEL_GASBEE_BLE     ESP_LOG_INFO    // temporär INFO: Erkennung GasBee nRF (XIAO) prüfen
#define LOG_LEVEL_GASBEE_CAL     ESP_LOG_INFO    // Kalibrierung per GATT (selten, immer protokollieren)
#define LOG_LEVEL_GAS_SOURCE     ESP_LOG_INFO    // Wechsel der Gas-Quelle (HX711/GasBee) – selten
#define LOG_LEVEL_RUUVI_BLE      ESP_LOG_INFO    // temporär INFO für Außensensor-Fehlersuche
#define LOG_LEVEL_I2C_BUS        ESP_LOG_INFO    // temporär INFO für I2C-Scan Diagnose
#define LOG_LEVEL_PCF8523        ESP_LOG_WARN
#define LOG_LEVEL_TIME_SYNC      ESP_LOG_WARN
#define LOG_LEVEL_WIFI           ESP_LOG_WARN    // wifi, mdns, esp_netif, dhcpc
