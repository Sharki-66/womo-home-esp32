#pragma once

#include "womo_config.h"

/* WoMoHome ESP32 Sensor Board Configuration
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
 *(ADC2_CH6, U1TXD, RTC, GPIO17) Piezo | I/O/T|  🟨 |   17 | 10         W        10 | 38   | 🟨  | I/O/T | RGB_LED  (GPIO38)
 *        ADC2_CH7, U1RXD, RTC, GPIO18 | I/O/T|  ⬜ |   18 | 11         R        11 | 37   | 🟥  | I/O/T | ⚠ PSRAM (GPIO37, nicht nutzbar!)
 *  (ADC1_CH7, RTC) RS485 DE/RTS GPIO8 | I/O/T|  🟨 |    8 | 12         O        12 | 36   | 🟥  | I/O/T | ⚠ PSRAM (GPIO36, nicht nutzbar!)
 *                ADC1_CH2, RTC, GPIO3 | I/O/T|  ⬜ |    3 | 13         O        13 | 35   | 🟥  | I/O/T | ⚠ PSRAM (GPIO35, nicht nutzbar!)
 *                   GPIO46, STRAP/LOG | I/O/T|  🟩 |   46 | 14         M        14 | 0    | 🟩  | I/O/T | BOOT/STRAP, GPIO0
 *     (ADC1_CH8, RTC) RS485 TX, GPIO9 | I/O/T|  🟨 |    9 | 15         -        15 | 45   | 🟨  | I/O/T | HX711 SCK (GPIO45)
 *    (ADC1_CH9, RTC) RS485 RX, GPIO10 | I/O/T|  🟨 |   10 | 16         1        16 | 48   | 🟩  | I/O/T | Onboard WS2812 RGB-LED (GPIO48)
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
#define SENSOR_BOARD_NAME "ESP32-S3-WROMM-1"

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
// RS485 Communication (Display Link)
// Belegung gemäß Pinout-Tabelle (J1): TX=GPIO9, RX=GPIO10, DE=GPIO8
// GPIO35-37 sind beim N16R8-Modul intern vom Octal-PSRAM belegt!
// ====================================================================================
#define SENSOR_RS485_UART_PORT 2
#define SENSOR_RS485_BAUDRATE WOMO_RS485_BAUDRATE
#define SENSOR_RS485_TX_GPIO 9
#define SENSOR_RS485_RX_GPIO 10
#define SENSOR_RS485_DE_GPIO 8
#define SENSOR_RS485_RTS_GPIO SENSOR_RS485_DE_GPIO
#define SENSOR_RS485_BUFFER_SIZE 4096
#define SENSOR_RS485_READ_TIMEOUT_MS 50
#define SENSOR_RS485_HELLO_PENDING_INTERVAL_MS WOMO_RS485_HELLO_PENDING_MS
#define SENSOR_RS485_HELLO_READY_INTERVAL_MS   WOMO_RS485_HELLO_READY_MS
#define SENSOR_RS485_HEARTBEAT_INTERVAL_MS     WOMO_RS485_HEARTBEAT_INTERVAL_MS

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
// Analog ADC (Battery & Tank Sensors)
// ====================================================================================
#define SENSOR_BATT1_ADC_UNIT 1
#define SENSOR_BATT1_ADC_CHANNEL 3     // GPIO4 (Batt1 Kfz)
#define SENSOR_BATT2_ADC_UNIT 1
#define SENSOR_BATT2_ADC_CHANNEL 4     // GPIO5 (Batt2 Board)
#define SENSOR_TANK1_ADC_UNIT 1
#define SENSOR_TANK1_ADC_CHANNEL 0     // GPIO1 (Frischwasser)
#define SENSOR_TANK2_ADC_UNIT 1
#define SENSOR_TANK2_ADC_CHANNEL 1     // GPIO2 (Grauwasser)

// ====================================================================================
// Board Power / Multimedia GPIOs (LBE-Steuerung über bistabiles Relais)
// ====================================================================================
#define SENSOR_RGB_LED_GPIO 48             // Onboard WS2812 RGB-LED (GPIO48, bestätigt)
#define SENSOR_PWR_12V_ON_GPIO 11      // Schaltausgang: LBE EIN (Relais-Puls)
#define SENSOR_PWR_12V_OFF_GPIO 12     // Schaltausgang: LBE AUS (Relais-Puls)
#define SENSOR_PWR_12V_SENSE_GPIO 14   // Eingang: LBE Spannungsfeedback
#define SENSOR_AC_SENSE_GPIO 21        // Eingang: 230V Netzkontrolle
#define SENSOR_MULTIMEDIA_PWR_GPIO 13  // Schaltausgang: Multimedia Power

// ====================================================================================
// Display-Versorgung (P-Kanal MOSFET Q4, AO3401A)
// GPIO7: LOW = FET leitet = Display EIN
//        Hi-Z (Input) = R21 (100k) zieht Gate auf 5V → Vgs≈0V → FET sperrt = Display AUS
//        OUTPUT HIGH (3,3V) VERBOTEN: Vgs=−1,7V → AO3401A leitet → ~300mA Querstrom!
// ====================================================================================
#define SENSOR_DISPLAY_PWR_GPIO 7      // P-MOSFET Q4: LOW=ein, Hi-Z/Input=aus

// ====================================================================================
// Buzzer (passiver Piezo-Transducer, z.B. Murata PKM22EPPH4001-B0)
// GPIO17: frei, ADC2_CH6, RTC-fähig, LEDC-fähig — direkt ohne Transistor betreibbar
// LEDC: Timer 1, Kanal 0, Low-Speed-Mode, 10-Bit-Auflösung
// ====================================================================================
#define SENSOR_BUZZER_GPIO          17          // Piezo-Anschluss
#define SENSOR_BUZZER_LEDC_TIMER    LEDC_TIMER_1
#define SENSOR_BUZZER_LEDC_CHANNEL  LEDC_CHANNEL_0
#define SENSOR_BUZZER_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define SENSOR_BUZZER_LEDC_BITS     LEDC_TIMER_10_BIT
#define SENSOR_BUZZER_DUTY          512          // 50% Tastverhältnis (lautester Ton)

// ====================================================================================
// Deep Sleep / Wakeup
// GPIO6 = TOUCH_PAD_NUM6: kapazitiver Touch-Eingang, RTC-fähig, kein Strapping
// Kupferpad/Streifen direkt an GPIO6 — kein externer IC nötig
// Schwellwert: Touch gilt als ausgelöst wenn Messwert um >20% unter Baseline fällt
// ====================================================================================
#define SENSOR_WAKEUP_GPIO 6                    // Touch-Wakeup: GPIO6 = TOUCH_PAD_NUM6
#define SENSOR_WAKEUP_TOUCH_THRESHOLD_PCT 10    // Empfindlichkeit: 10% über Baseline = Touch erkannt

// ====================================================================================

// =============================================================================
// Gas Consumption Calculation (EMA-based rate tracking)
// =============================================================================
#define SENSOR_GAS_FILL_KG          11.0f    // Netto-Inhalt einer vollen 11-kg-Flasche
#define SENSOR_GAS_TARE_KG          10.5f    // Tara-Gewicht der leeren Flasche
#define SENSOR_GAS_ACTIVE_DEFAULT   -1       // -1 = Auto-Erkennung (A oder B)
#define SENSOR_GAS_RATE_ALPHA_1H    0.2f     // EMA-Glättungsfaktor (1h-Fenster)
#define SENSOR_GAS_RATE_ALPHA_2H    0.1f     // EMA-Glättungsfaktor (2h-Fenster)
#define SENSOR_GAS_RATE_MIN_DT_SEC  30       // Minimaler Messpunkt-Abstand (Sek)
#define SENSOR_GAS_ACTIVE_EPS_KG    0.05f    // Mindest-Änderung für Verbrauchserkennung

// =============================================================================
// WiFi STA Configuration (Verbindung zum RUTX11 Router)
// =============================================================================
#define SENSOR_WIFI_DEFAULT_SSID    WOMO_WIFI_DEFAULT_SSID
#define SENSOR_WIFI_DEFAULT_PASS    WOMO_WIFI_DEFAULT_PASS
#define SENSOR_WIFI_NVS_NAMESPACE   "wifi_cfg"
#define SENSOR_WIFI_NVS_KEY_SSID    "ssid"
#define SENSOR_WIFI_NVS_KEY_PASS    "pass"
#define SENSOR_WIFI_HOSTNAME        "womo-sensor"
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
// Log-Level Konfiguration (ESP_LOG_NONE/ERROR/WARN/INFO/DEBUG/VERBOSE)
// Hier zentral je Modul einstellen, ohne Code zu ändern.
// ====================================================================================
#define LOG_LEVEL_SENSOR_MAIN    ESP_LOG_INFO
#define LOG_LEVEL_DEEP_SLEEP     ESP_LOG_INFO
#define LOG_LEVEL_RS485          ESP_LOG_WARN
#define LOG_LEVEL_ANALOG         ESP_LOG_INFO
#define LOG_LEVEL_BNO055         ESP_LOG_INFO    // bno055_app + BNO055-Komponente
#define LOG_LEVEL_BME680         ESP_LOG_INFO    // bme680_app + bme680-Komponente
#define LOG_LEVEL_HX711          ESP_LOG_WARN
#define LOG_LEVEL_GASBEE_BLE     ESP_LOG_WARN
#define LOG_LEVEL_I2C_BUS        ESP_LOG_WARN
#define LOG_LEVEL_PCF8523        ESP_LOG_WARN
#define LOG_LEVEL_TIME_SYNC      ESP_LOG_WARN
#define LOG_LEVEL_WIFI           ESP_LOG_WARN    // wifi, mdns, esp_netif, dhcpc
