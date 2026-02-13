#pragma once

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
 *                ADC1_CH5, RTC, GPIO6 | I/O/T|  ⬜ |    6 | 6          -         6 | 42   | 🟩  | I/O/T | MTMS, GPIO42
 *                ADC1_CH6, RTC, GPIO7 | I/O/T|  ⬜ |    7 | 7          S         7 | 41   | 🟩  | I/O/T | MTDI, GPIO41
 *     (ADC2_CH4, RTC, GPIO15) I2C SCL | I/O/T|  🟨 |   15 | 8          3         8 | 40   | 🟩  | I/O/T | MTDO, GPIO40
 *     (ADC2_CH5, RTC, GPIO16) I2C SDA | I/O/T|  🟨 |   16 | 9          -         9 | 39   | 🟩  | I/O/T | MTCK, GPIO39
 *        ADC2_CH6, U1TXD, RTC, GPIO17 | I/O/T|  ⬜ |   17 | 10         W        10 | 38   | 🟨  | I/O/T | RGB_LED  (GPIO38)
 *        ADC2_CH7, U1RXD, RTC, GPIO18 | I/O/T|  ⬜ |   18 | 11         R        11 | 37   | 🟨  | I/O/T | RS485 RX (GPIO37)
 *                ADC1_CH7, RTC, GPIO8 | I/O/T|  ⬜ |    8 | 12         O        12 | 36   | 🟨  | I/O/T | RS485 DE/RTS (GPIO36)
 *                ADC1_CH2, RTC, GPIO3 | I/O/T|  ⬜ |    3 | 13         O        13 | 35   | 🟨  | I/O/T | RS485 TX (GPIO35)
 *                   GPIO46, STRAP/LOG | I/O/T|  🟩 |   46 | 14         M        14 | 0    | 🟩  | I/O/T | BOOT/STRAP, GPIO0
 *                ADC1_CH8, RTC, GPIO9 | I/O/T|  ⬜ |    9 | 15         -        15 | 45   | 🟩  | I/O/T | VSPI/STRAP, GPIO45
 *               ADC1_CH9, RTC, GPIO10 | I/O/T|  ⬜ |   10 | 16         1        16 | 48   | 🟨  | I/O/T | HX711 DOUT (GPIO48)
 *  (ADC2_CH0, RTC, GPIO11) Multimedia | I/O/T|  🟨 |   11 | 17                  17 | 47   | 🟨  | I/O/T | HX711 SCK (GPIO47)
 *     (ADC2_CH1, RTC, GPIO12) 12V EIN | I/O/T|  🟨 |   12 | 18                  18 | 21   | 🟨  | I/O/T | AC 220V Sence (GPIO21, RTC)
 *     (ADC2_CH2, RTC, GPIO13) 12V AUS | I/O/T|  🟨 |   13 | 19                  19 | 20   | 🟩  | I/O/T | USB_D+, GPIO20, RTC
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
#define SENSOR_HX711_DOUT_GPIO 48
#define SENSOR_HX711_SCK_GPIO 47
#define SENSOR_HX711_READY_TIMEOUT_MS 200U
#define SENSOR_HX711_READY_RETRY_COUNT 2
#define SENSOR_HX711_READY_BACKOFF_MS 50U
#define SENSOR_HX711_STARTUP_DELAY_MS 1000U
#define SENSOR_HX711_POLL_INTERVAL_MS 10000U
#define SENSOR_HX711_AVG_SAMPLES 3U
#define SENSOR_HX711_ENABLE_CHANNEL_B 1

// Kalibrierwerte (Platzhalter; bei Bedarf anpassen)
#define SENSOR_HX711_OFFSET_A -275400
#define SENSOR_HX711_SCALE_A 0.0388783f
#define SENSOR_HX711_OFFSET_B 77600
#define SENSOR_HX711_SCALE_B -0.15450f

// ====================================================================================
// RS485 Communication (Display Link)
// Belegung gemäß Pinout-Tabelle (J3): RX=GPIO37, TX=GPIO36, DE/RTS=GPIO35
// ====================================================================================
#define SENSOR_RS485_UART_PORT 2
#define SENSOR_RS485_BAUDRATE 115200
#define SENSOR_RS485_TX_GPIO 35
#define SENSOR_RS485_RX_GPIO 37
#define SENSOR_RS485_DE_GPIO 36
#define SENSOR_RS485_RTS_GPIO SENSOR_RS485_DE_GPIO
#define SENSOR_RS485_BUFFER_SIZE 4096
#define SENSOR_RS485_READ_TIMEOUT_MS 50
#define SENSOR_RS485_HELLO_PENDING_INTERVAL_MS 1000U
#define SENSOR_RS485_HELLO_READY_INTERVAL_MS 10000U
#define SENSOR_RS485_HEARTBEAT_INTERVAL_MS 2000U

// ====================================================================================
// Analog ADC (Battery & Tank Sensors)
// ====================================================================================
#define SENSOR_BATT1_ADC_UNIT 1
#define SENSOR_BATT1_ADC_CHANNEL 3     // GPIO1 (Batt1 Kfz)
#define SENSOR_BATT2_ADC_UNIT 1
#define SENSOR_BATT2_ADC_CHANNEL 4     // GPIO2 (Batt2 Board)
#define SENSOR_TANK1_ADC_UNIT 1
#define SENSOR_TANK1_ADC_CHANNEL 0     // GPIO4 (Frischwasser)
#define SENSOR_TANK2_ADC_UNIT 1
#define SENSOR_TANK2_ADC_CHANNEL 1     // GPIO5 (Grauwasser)

// ====================================================================================
// Board Power / Multimedia GPIOs
// ====================================================================================
#define SENSOR_PWR_12V_ON_GPIO 12      // Schaltausgang: Boardspannung EIN
#define SENSOR_PWR_12V_OFF_GPIO 13     // Schaltausgang: Boardspannung AUS
#define SENSOR_PWR_12V_SENSE_GPIO 14   // Eingang: Boardspannung Feedback
#define SENSOR_AC_SENSE_GPIO 21        // Eingang: 230V Netzkontrolle
#define SENSOR_MULTIMEDIA_PWR_GPIO 11  // Schaltausgang: Multimedia Power

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
#define SENSOR_WIFI_DEFAULT_SSID    "Malibu-622"
#define SENSOR_WIFI_DEFAULT_PASS    ""          // Default leer; per NVS/RS485 änderbar
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
