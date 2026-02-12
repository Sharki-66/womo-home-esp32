#pragma once

/*
 * Walter configuration - all settings defined here directly.
 * No dependency on sdkconfig menuconfig - edit values here for quick iteration.
 * 
 * This avoids full rebuilds when changing configuration values.
 */

// ====================================================================================
// WiFi Access Point Configuration
// ====================================================================================
#define WALTER_AP_SSID "MalibuWifi"
#define WALTER_AP_PASSWORD "malibupass"
#define WALTER_AP_CHANNEL 6
#define WALTER_AP_MAX_CONNECTIONS 4
#define WALTER_AP_DNS_PRIMARY "8.8.8.8"
#define WALTER_AP_DNS_SECONDARY "1.1.1.1"

// ====================================================================================
// WiFi Station (Client) Configuration
// ====================================================================================
#define WALTER_STA_SSID "Solaris"
#define WALTER_STA_PASSWORD "Wlan7rh8kv66"

// ====================================================================================
// Network Configuration
// ====================================================================================
#define WALTER_ENABLE_NAT 1

// ====================================================================================
// Web UI Configuration (SoftAP-hosted status page)
// ====================================================================================
#define WALTER_ENABLE_WEBUI 1
#define WALTER_WEB_SERVER_PORT 80

// ====================================================================================
// Sensor Task Configuration
// ====================================================================================
#define WALTER_SENSOR_TASK_STACK 4096
#define WALTER_SENSOR_TASK_PRIORITY 5


// ====================================================================================
// Sensor Logging Control
// ====================================================================================
#define WALTER_SENSOR_LOG_BNO055 1
#define WALTER_SENSOR_LOG_BME680 1
#define WALTER_SENSOR_LOG_ANALOG 1

// ====================================================================================
// Main Sensor Loop Interval
// ====================================================================================
#define WALTER_SENSOR_LOOP_INTERVAL_MS 1000U

// ====================================================================================
// I2C Bus Configuration
// ====================================================================================
#define WALTER_SENSOR_I2C_PORT 1
#define WALTER_SENSOR_I2C_SDA_GPIO 11
#define WALTER_SENSOR_I2C_SCL_GPIO 12
#define WALTER_SENSOR_I2C_SPEED_HZ 100000
#define WALTER_SENSOR_I2C_ENABLE_INTERNAL_PULLUPS 1

// ====================================================================================
// BNO055 IMU Sensor
// ====================================================================================
#define WALTER_ENABLE_BNO055 1
#define WALTER_BNO055_I2C_ADDR 0x28
#define WALTER_BNO055_STARTUP_DELAY_MS 650
#define WALTER_BNO055_POST_RESET_DELAY_MS 700
#define WALTER_BNO055_POLL_INTERVAL_MS 300U    // Update IMU cadence to 0.3 seconds
#define WALTER_BNO055_MAX_EULER_STEP_DEG 0.0f  // Disable Euler jump suppression for raw values

// ====================================================================================
// GPS / GNSS via Walter Modem
// ====================================================================================
#define WALTER_ENABLE_GPS 1
#define WALTER_GPS_PAUSE_LTE 0          // LTE bleibt an (blockierende CFUN-Umschaltung vermeiden)
#define WALTER_GPS_FIRST_DELAY_MS 20000U         // erster Fix nach 20 Sekunden (schnelleres Debugging)
#define WALTER_GPS_REQUEST_INTERVAL_MS 30000U    // danach alle 60 Sekunden
#define WALTER_GPS_FIX_TIMEOUT_MS 60000U         // maximal 60 Sekunden auf Fix warten
#define WALTER_GPS_RETRY_DELAY_MS 15000U         // wait 15s after a failed request
#define WALTER_GPS_POLL_INTERVAL_MS 1000U        // poll GNSS fix buffer every second
#define WALTER_GPS_TASK_STACK 12288  // Mehr Stack für GNSS/LTE Sequenzen (Overflow bei 8k gesehen)
#define WALTER_GPS_TASK_PRIORITY WALTER_SENSOR_TASK_PRIORITY

// ====================================================================================
// BME680 Environmental Sensor
// ====================================================================================
#define WALTER_ENABLE_BME680 1
#define WALTER_BME680_SENSOR_COUNT 2  // Outdoor + indoor BME680
#define WALTER_BME680_ADDR_0 0x77  // Outdoor sensor
#define WALTER_BME680_ADDR_1 0x76  // Indoor sensor
#define WALTER_BME680_HEATER_TEMP_C 280
#define WALTER_BME680_HEATER_DURATION_MS 250
#define WALTER_BME680_AMBIENT_TEMP_C 25
#define WALTER_BME680_POLL_INTERVAL_MS 10000U  // alle 10 sec - slow environmental changes
#define WALTER_BME680_STARTUP_DELAY_MS 2000U

// BME680 plausibility thresholds
#define WALTER_BME680_TEMP_MIN_C   -20.0f
#define WALTER_BME680_TEMP_MAX_C    60.0f
#define WALTER_BME680_TEMP_MAX_DELTA_PER_SEC  0.05f  // 3°C per minute
#define WALTER_BME680_HUM_MIN_PCT    0.0f
#define WALTER_BME680_HUM_MAX_PCT  100.0f
#define WALTER_BME680_HUM_MAX_DELTA_PER_SEC   0.3f   // 18% RH per minute
#define WALTER_BME680_PRESS_MIN_HPA 300.0f
#define WALTER_BME680_PRESS_MAX_HPA 1100.0f
#define WALTER_BME680_PRESS_MAX_DELTA_PER_SEC 0.1f   // 6 hPa per minute

// ====================================================================================
// HX711 Load Cell ADC (Dual Channel)
// ====================================================================================
#define WALTER_ENABLE_HX711 1
#define WALTER_HX711_DOUT_GPIO 4
#define WALTER_HX711_SCK_GPIO 5
#define WALTER_HX711_READY_TIMEOUT_MS 200U
#define WALTER_HX711_READY_RETRY_COUNT 2
#define WALTER_HX711_READY_BACKOFF_MS 50U
#define WALTER_HX711_STARTUP_DELAY_MS 1000U  // Wartezeit nach dem Zuschalten der 3V3-Schiene
#define WALTER_HX711_POLL_INTERVAL_MS 10000U  // 10 seconds cadence (slow weight changes)
#define WALTER_HX711_GAIN_SETTING 1  // 1=Channel A 128x, 2=Channel B 32x, 3=Channel A 64x
#define WALTER_HX711_AVG_SAMPLES 3   // Median of 3 samples with 15ms delay for 80Hz HX711
#define WALTER_HX711_ENABLE_CHANNEL_B 1  // Enable once second load cell is connected

// Platform A (wired to HX711 Channel A @128x) - Raw values INCREASE with weight
#define WALTER_HX711_OFFSET_A  -275400   // 0 kg → raw ≈ -275400 (neu kalibriert)
#define WALTER_HX711_SCALE_A 0.0388783f  // 28 kg → raw Δ720197 ⇒ ~0.03888 g/count

// Platform B (wired to HX711 Channel B @32x) - Raw values DECREASE with weight
#define WALTER_HX711_OFFSET_B    77600   // 0 kg → raw ≈ 77600 (neu kalibriert)
#define WALTER_HX711_SCALE_B -0.15450f   // 28 kg → raw Δ-181230 ⇒ ~-0.1545 g/count

// HX711 plausibility thresholds (applied after conversion to kilograms)
#define WALTER_HX711_MIN_KG            -5.0f
#define WALTER_HX711_MAX_KG           200.0f
#define WALTER_HX711_MAX_DELTA_PER_SEC 50.0f  // Allow quick loading/unloading up to 50 kg/s

// ====================================================================================
// RS485 Communication  
// ====================================================================================
#define WALTER_ENABLE_RS485 1  // Re-enabled - GPS crash was not caused by RS485
#define WALTER_RS485_UART_PORT 2  // Changed from 1 to 2 (Walter Modem uses UART1)
#define WALTER_RS485_BAUDRATE 115200
#define WALTER_RS485_TX_GPIO 17
#define WALTER_RS485_RX_GPIO 16
#define WALTER_RS485_DE_GPIO 15  // DE/RE (RTS) for RS485 half-duplex driver enable
#define WALTER_RS485_BUFFER_SIZE 512
#define WALTER_RS485_READ_TIMEOUT_MS 50
#define WALTER_RS485_HELLO_PENDING_INTERVAL_MS 1000U   // Retry hello every 1s until display_ready
#define WALTER_RS485_HELLO_READY_INTERVAL_MS 10000U    // Refresh hello every 10s after ACK
#define WALTER_RS485_HEARTBEAT_INTERVAL_MS 2000U       // Heartbeat cadence once display is ready

// ====================================================================================
// LTE Modem / TCP Uplink
// ====================================================================================
#define WALTER_ENABLE_LTE 1
#define WALTER_LTE_UART_PORT 1
#define WALTER_LTE_APN "web.vodafone.de"
#define WALTER_LTE_USERNAME ""
#define WALTER_LTE_PASSWORD ""
#define WALTER_LTE_PIN ""
#define WALTER_LTE_ATTACH_TIMEOUT_SEC 180
#define WALTER_LTE_RETRY_DELAY_MS 10000U
#define WALTER_LTE_TCP_HOST "walterdemo.quickspot.io"
#define WALTER_LTE_TCP_PORT 1999
#define WALTER_LTE_TCP_SECURE 0
#define WALTER_LTE_SEND_INTERVAL_MS 30000U
#define WALTER_LTE_STATE_POLL_MS 1000U   // Poll modem registration state at 1s cadence (was 250ms)
#define WALTER_LTE_TASK_STACK 4096

// ====================================================================================
// Gasflaschen (HX711 Plattformen A/B)
// ====================================================================================
#define WALTER_GAS_TARA_KG            10.1f   // Leergewicht Flasche inkl. Halterung (typisch)
#define WALTER_GAS_TARA_KG_MAX        10.5f   // Oberes Ende der Toleranz
#define WALTER_GAS_FILL_KG            11.0f   // Nennfüllmenge pro Flasche
#define WALTER_GAS_MIN_NET_KG         0.5f    // Unterhalb davon: „leer“
#define WALTER_GAS_HISTORY_MINUTES    120     // Verlauf im RAM (bei 10s Poll ≈ 720 Samples)
#define WALTER_GAS_NVS_INTERVAL_MIN   10      // Checkpoints für Persistenz alle 10 Minuten
#define WALTER_GAS_SWAP_AB            1       // 1: Tausch Ausgabe A<->B (Front/Hinten vertauscht)
#define WALTER_LTE_TASK_PRIORITY 5

// ====================================================================================
// Analog Monitor (Battery & Tank Levels)
// ====================================================================================
#define WALTER_ENABLE_ANALOG 1
#define WALTER_ANALOG_SAMPLES_PER_READ 16
#define WALTER_ANALOG_POLL_INTERVAL_MS 10000U

// ADC Channel/Unit mapping (ESP32-S3: ADC1 supports channels 0..9 on GPIO1..10)
#define WALTER_BATT1_ADC_UNIT    1
#define WALTER_BATT1_ADC_CHANNEL 7  // GPIO8 (Board Batterie)
#define WALTER_BATT2_ADC_UNIT    1
#define WALTER_BATT2_ADC_CHANNEL 8  // GPIO9 (KFZ Batterie)
#define WALTER_TANK1_ADC_UNIT    1
#define WALTER_TANK1_ADC_CHANNEL 5  // GPIO6 (Frischwasser Tank)
#define WALTER_TANK2_ADC_UNIT    1
#define WALTER_TANK2_ADC_CHANNEL 6  // GPIO7 (Grauwasser Tank)

// Voltage Dividers (kOhm)
#define WALTER_BATTERY_DIVIDER_RHIGH_KOHM 100
#define WALTER_BATTERY_DIVIDER_RLOW_KOHM 18.2f // Feintuning: ~0.8% weniger Gain, 12.4 V → ~12.3 V Anzeige
#define WALTER_TANK_DIVIDER_RHIGH_KOHM 100
#define WALTER_TANK_DIVIDER_RLOW_KOHM 15

// Tank level calibration (ADC input in mV)
#define WALTER_TANK_EMPTY_MV 0
#define WALTER_TANK_FULL_MV 130   // 1 V vor Teiler ≈130 mV am ADC (100k/15k)

// Battery sensor output range (ADC input in mV)
#define WALTER_BATTERY_ADC_MIN_MV 0
#define WALTER_BATTERY_ADC_MAX_MV 3000

// Analog plausibility thresholds
#define WALTER_BATTERY_MIN_V              11.0f
#define WALTER_BATTERY_MAX_V              14.5f
#define WALTER_BATTERY_MAX_DELTA_PER_SEC   0.02f // 1.2 V/min change maximum
#define WALTER_TANK_MIN_PERCENT            0.0f
#define WALTER_TANK_MAX_PERCENT          100.0f
#define WALTER_TANK_MAX_DELTA_PER_SEC      0.1f  // 6 %/min maximum
