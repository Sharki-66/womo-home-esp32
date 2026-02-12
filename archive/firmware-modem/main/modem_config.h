#pragma once

/*
 * Modem Board Configuration (WaveShare ESP32-S3-A-SIM7670X-4G)
 * Pin mapping schematic: https://www.waveshare.com/w/upload/8/8f/ESP32-S3-A-SIM7670X-4G_Schematic.pdf
 * 
 * Based on WaveShare schematic and P3 header pinout
 */

// ====================================================================================
// Board Information
// ====================================================================================
#define MODEM_BOARD_NAME "ESP32-S3-A-SIM7670X-4G"
#define MODEM_TYPE "SIM7670X"

// ====================================================================================
// Complete GPIO Map (ESP32-S3 WROOM-1)
// ====================================================================================
/*
 * GPIO | Header | Pin | Board Usage                    | Available     | Notes
 * -----|--------|-----|--------------------------------|---------------|----------------------------------
 * 0    | -      | -   | Boot mode / Strapping          | 🟩 LIMITED    | Has external pullup, use with caution
 * 1    | P2     | 10  | ADC1_CH0                       | ⬜ FREE       | Available for analog input
 * 2    | -      | -   | ADC1_CH1 / I2C SCL (MAX17048)  | 🟨 USED       | Hardware Rev1: MAX17048 SCL
 * 3    | -      | -   | ADC1_CH2 / I2C SDA (MAX17048)  | 🟨 USED       | Hardware Rev1: MAX17048 SDA
 * 4    | P3     | 4   | ADC1_CH3 / SD CARD             | 🟩 RESERVED   | SD Card
 * 5    | P3     | 5   | ADC1_CH4 / SD CARD             | 🟩 RESERVED   | SD Card
 * 6    | P3     | 6   | ADC1_CH5 / SD CARD             | 🟩 RESERVED   | SD Card
 * 7    | P3     | 10  | ADC1_CH6 / Camera D0           | 🟨 USED       | Battery1 ADC (KFZ)
 * 8    | P3     | 11  | ADC1_CH7 / Camera D1           | 🟨 USED       | Battery2 ADC (Board)
 * 9    | P3     | 12  | ADC1_CH8 / Camera D2           | 🟨 USED       | Tank1 ADC (Frischwasser)
 * 10   | P3     | 13  | ADC1_CH9 / Camera D3           | 🟨 USED       | Tank2 ADC (Grauwasser)
 * 11   | P3     | 14  | Camera D4                      | 🟨 USED       | RS485 TX (Kommunikation Display)
 * 12   | P3     | 15  | Camera D5                      | 🟨 USED       | RS485 RX (Kommunikation Display)
 * 13   | P3     | 16  | Camera D6                      | 🟨 USED       | HX711 SCK (GAS Flasche 1 + 2)
 * 14   | P3     | 17  | Camera D7                      | 🟨 USED       | HX711 DATA (GAS Flasche 1 + 2)
 * 15   | P3     | 8   | Camera SIO_DTA   4.7 kOhm      | 🟨 USED       | I2C SCL ext
 * 16   | P3     | 9   | Camera SIO_CLK   4.7 kOhm      | 🟨 USED       | I2C SDA ext
 * 17   | P2     | 17  | TXD1                           | 🟩 RESERVED   | UART1 RX
 * 18   | P2     | 16  | RXD1                           | 🟩 RESERVED   | UART1 TX
 * 19   | -      | -   | USB D-                         | 🟩 RESERVED   | USB OTG (system use)
 * 20   | -      | -   | USB D+                         | 🟩 RESERVED   | USB OTG (system use)
 * 21   | P2     | 11  | GPIO21                         | 🟨 USED       | RS485 RTS/DE (Kommunikation Display)
 * 22   | -      | -   |                                | 🟩 RESERVED   | Internal
 * 23   | -      | -   |                                | 🟩 RESERVED   | Internal
 * 24   | -      | -   | SPI Flash/PSRAM (intern)       | 🟩 RESERVED   | Internal
 * 25   | -      | -   | SPI Flash/PSRAM (intern)       | 🟩 RESERVED   | Internal
 * 26   | -      | -   | SPI Flash/PSRAM (intern)       | 🟩 RESERVED   | Internal
 * 27   | -      | -   | SPI Flash/PSRAM (intern)       | 🟩 RESERVED   | Internal
 * 28   | -      | -   | SPI Flash/PSRAM (intern)       | 🟩 RESERVED   | Internal
 * 29   | -      | -   | SPI Flash/PSRAM (intern)       | 🟩 RESERVED   | Internal
 * 30   | -      | -   | SPI Flash/PSRAM (intern)       | 🟩 RESERVED   | Internal
 * 31   | -      | -   | SPI Flash/PSRAM (intern)       | 🟩 RESERVED   | Internal
 * 32   | -      | -   | SPI Flash/PSRAM (intern)       | 🟩 RESERVED   | Internal
 * 33   | P2     | 8   | GPIO33                         | 🟩 RESERVED   | Modem PWRKEY
 * 34   | P2     | 12  | Camera XCLK                    | 🟨 USED       | 12 V EIN
 * 35   | P2     | 15  | Camera HREF                    | 🟨 USED       | 12 V AUS
 * 36   | P2     | 14  | Camera VSYNC                   | 🟨 USED       | 12 V Kontrolle
 * 37   | P2     | 13  | Camera PCLK                    | 🟨 USED       | 220 V Kontrolle
 * 38   | -      | -   | GPIO38 RGB_CTL (WS2812)        | 🟩 RESERVED   | RGB1 LED
 * 39   | P2     | 6   | GPIO39 / JTAG MTCK             | 🟩 LIMITED    | JTAG if enabled
 * 40   | P2     | 5   | GPIO40 / JTAG MTDO             | 🟩 LIMITED    | JTAG if enabled
 * 41   | P2     | 4   | GPIO41 / JTAG MTDI             | 🟩 LIMITED    | JTAG if enabled
 * 42   | P2     | 9   | GPIO42 / JTAG MTMS             | 🟩 RESERVED   | JTAG
 * 43   | P2     | 2   | UART0 TX (U0TXD) Console       | 🟩 RESERVED   | Debug console (P2 Pin 2)
 * 44   | P2     | 3   | UART0 RX (U0RXD) Console       | 🟩 RESERVED   | Debug console (P2 Pin 3)
 * 45   | P2     | 7   | Strapping pin                  | 🟩 LIMITED    | VDD_SPI voltage selector
 * 46   | P3     | 7   | Strapping pin                  | 🟩 RESERVED   | SD Card (ROM message print control)
 * 47   | -      | -   | GPIO47 / XTAL_N                | 🟩 RESERVED   | 
 * 48   | -      | -   | GPIO48 / XTAL_P                | 🟩 RESERVED   | 
 *
 * P3 Header (19 pins):
 *  1: VCC3V3,  2: VCC3V3,  3: GND,    4: GPIO4,  5: GPIO5,  6: GPIO6,  7: GPIO46,
 *  8: GPIO15,  9: GPIO16, 10: GPIO7, 11: GPIO8, 12: GPIO9, 13: GPIO10, 14: GPIO11,
 * 15: GPIO12, 16: GPIO13, 17: GPIO14, 18: UVBUS, 19: GND
 *
 * P2 Header (19 pins):
 *  1: GND,  2: U0TXD,   3: U0RXD,   4: GPIO41,  5: GPIO40,  6: GPIO39,  7: GPIO45,  8: GPIO33,
 *  9: GPIO42,  10: GPIO1,  11: GPIO21, 12: GPIO34, 13: GPIO37, 14: GPIO36,
 * 15: GPIO35, 16: RXD1,   17: TXD1,   18: VBAT,   19: GND
 *
 */

// ====================================================================================
// I2C Bus Configuration
// ====================================================================================
#define MODEM_I2C_PORT 0
#define MODEM_I2C_SDA_GPIO 3   // Rev1: MAX17048 SDA (interner Bus)
#define MODEM_I2C_SCL_GPIO 2   // Rev1: MAX17048 SCL (interner Bus)
#define MODEM_I2C_SPEED_HZ 50000
#define MODEM_I2C_PULLUP_ENABLE 1

// Zweiter I2C-Bus (extern) für Sensoren auf Header, nutzt separaten Controller (I2C_NUM_1)
#define MODEM_I2C_EXT_PORT 1
#define MODEM_I2C_EXT_SDA_GPIO 16  // P3-9
#define MODEM_I2C_EXT_SCL_GPIO 15  // P3-8
#define MODEM_I2C_EXT_SPEED_HZ 100000
#define MODEM_I2C_EXT_PULLUP_ENABLE 1

// ====================================================================================
// HX711 Load Cell ADC (Dual Channel for Gas Bottles)
// ====================================================================================
#define MODEM_HX711_DOUT_GPIO 14  // P3-16
#define MODEM_HX711_SCK_GPIO 13   // P3-15
#define MODEM_HX711_READY_TIMEOUT_MS 200U
#define MODEM_HX711_READY_RETRY_COUNT 2
#define MODEM_HX711_READY_BACKOFF_MS 50U
#define MODEM_HX711_STARTUP_DELAY_MS 1000U
#define MODEM_HX711_POLL_INTERVAL_MS 10000U
#define MODEM_HX711_AVG_SAMPLES 3U
#define MODEM_HX711_ENABLE_CHANNEL_B 1

// Kalibrierwerte (aus Walter, ggf. anpassen)
#define MODEM_HX711_OFFSET_A  -275400
#define MODEM_HX711_SCALE_A 0.0388783f
#define MODEM_HX711_OFFSET_B    77600
#define MODEM_HX711_SCALE_B -0.15450f

// ====================================================================================
// Gas-Logik (Placeholder, basiert auf HX711)
// ====================================================================================
// Nominale Füllmenge je Flasche in kg (für %-Berechnung)
#define MODEM_GAS_FILL_KG 11.0f
// Tara (leere Flasche) je Flasche in kg
#define MODEM_GAS_TARE_KG 10.5f
// Voreingestellter aktiver Slot: -1 = unbekannt/auto, 0 = Flasche A, 1 = Flasche B
#define MODEM_GAS_ACTIVE_DEFAULT -1
// Glättung für Verbrauchsraten (Exponentiell, 0..1)
#define MODEM_GAS_RATE_ALPHA_1H 0.2f
#define MODEM_GAS_RATE_ALPHA_2H 0.1f
// Minimaler Zeitabstand zwischen zwei Verbrauchs-Samples (Sekunden)
#define MODEM_GAS_RATE_MIN_DT_SEC 30
// Schwelle für automatische Verbrauchs-Erkennung pro Kanal (kg pro Sample)
#define MODEM_GAS_ACTIVE_EPS_KG 0.05f

// ====================================================================================
// RS485 Communication (Display Link)
// Hinweis: Camera Pins GPIO11/12 für RS485, GPIO21 für DE
// ====================================================================================
#define MODEM_RS485_UART_PORT 2
#define MODEM_RS485_BAUDRATE 115200
#define MODEM_RS485_TX_GPIO 12    // P3-14 (Camera D4)
#define MODEM_RS485_RX_GPIO 11    // P3-15 (Camera D5)
#define MODEM_RS485_DE_GPIO 21    // P2-11 (RTS/DE)
#define MODEM_RS485_RTS_GPIO MODEM_RS485_DE_GPIO
#define MODEM_RS485_BUFFER_SIZE 4096
#define MODEM_RS485_READ_TIMEOUT_MS 50
#define MODEM_RS485_HELLO_PENDING_INTERVAL_MS 1000U
#define MODEM_RS485_HELLO_READY_INTERVAL_MS 10000U
#define MODEM_RS485_HEARTBEAT_INTERVAL_MS 2000U

// ====================================================================================
// Analog ADC (Battery & Tank Sensors)
// Hinweis: BAT_ADC -> R86 ist laut Schaltplan NC; GPIO1 bleibt frei, wenn R86 unbestückt bleibt.
// ====================================================================================
// ESP32-S3 ADC1: GPIO1-GPIO10 (channels 0-9)
// Using GPIO7-GPIO10 to avoid conflicts with I2C and SD-Card pins
#define MODEM_BATT1_ADC_UNIT    1
#define MODEM_BATT1_ADC_CHANNEL 6  // GPIO7 (Board Battery)
#define MODEM_BATT2_ADC_UNIT    1
#define MODEM_BATT2_ADC_CHANNEL 7  // GPIO8 (Vehicle Battery)
#define MODEM_TANK1_ADC_UNIT    1
#define MODEM_TANK1_ADC_CHANNEL 8  // GPIO9 (Fresh Water Tank)
#define MODEM_TANK2_ADC_UNIT    1
#define MODEM_TANK2_ADC_CHANNEL 9  // GPIO10 (Grey Water Tank)

// ====================================================================================
// Modem UART Configuration (SIM7670X)
// ====================================================================================
#define MODEM_UART_PORT 1      // Modem UART on GPIO17/18 (per schematic)
#define MODEM_UART_BAUD 115200
#define MODEM_UART_TX_GPIO 18       // ESP TX -> Modem RX
#define MODEM_UART_RX_GPIO 17       // ESP RX <- Modem TX
// Note: DTR on GPIO45, RI on GPIO40 available but not used for flow control
// Hardware flow control (RTS/CTS) not available on this board

// Modem Control Pins (if available - check schematic for actual connections)
#define MODEM_PWRKEY_GPIO 33   // Power key / VVBAT gate (GPIO33, P2-8)
#define MODEM_RESET_GPIO  -1   // Not connected on this board
#define MODEM_FLIGHT_GPIO -1   // Flight mode not connected/controlled
// Board Power / Multimedia GPIOs
#define MODEM_PWR_12V_ON_GPIO    34  // Schaltausgang: Boardspannung EIN
#define MODEM_PWR_12V_OFF_GPIO   35  // Schaltausgang: Boardspannung AUS
#define MODEM_PWR_12V_SENSE_GPIO 36  // Eingang: Boardspannung Feedback
#define MODEM_AC_SENSE_GPIO      37  // Eingang: 230V Netzkontrolle

// DEBUG: Wenn definiert, treibt das Modem GPIO36 als Ausgang um den 12V-Sense
// ohne echte Hardware zu simulieren. Vor Produktivbetrieb auskommentieren!
#define MODEM_DEBUG_SIMULATE_12V_SENSE

// ====================================================================================