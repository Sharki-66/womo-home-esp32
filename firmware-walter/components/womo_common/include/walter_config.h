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
#define WALTER_SENSOR_I2C_PORT 0
#define WALTER_SENSOR_I2C_SDA_GPIO 8
#define WALTER_SENSOR_I2C_SCL_GPIO 9
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
// BME680 Environmental Sensor
// ====================================================================================
#define WALTER_ENABLE_BME680 1
#define WALTER_BME680_SENSOR_COUNT 1  // Only outdoor sensor for now
#define WALTER_BME680_ADDR_0 0x77  // Outdoor sensor
#define WALTER_BME680_ADDR_1 0x76  // Indoor sensor
#define WALTER_BME680_HEATER_TEMP_C 280
#define WALTER_BME680_HEATER_DURATION_MS 250
#define WALTER_BME680_AMBIENT_TEMP_C 25
#define WALTER_BME680_POLL_INTERVAL_MS 60000U  // 1 minute - slow environmental changes
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
#define WALTER_BME680_GAS_MIN_KOHM   0.0f
#define WALTER_BME680_GAS_MAX_KOHM 1200.0f
#define WALTER_BME680_GAS_MAX_DELTA_PER_SEC 5.0f     // 300 kΩ per minute

// ====================================================================================
// HX711 Load Cell ADC (Dual Channel)
// ====================================================================================
#define WALTER_ENABLE_HX711 1
#define WALTER_HX711_DOUT_GPIO 4
#define WALTER_HX711_SCK_GPIO 5
#define WALTER_HX711_READY_TIMEOUT_MS 200U
#define WALTER_HX711_READY_RETRY_COUNT 2
#define WALTER_HX711_READY_BACKOFF_MS 50U
#define WALTER_HX711_POLL_INTERVAL_MS 10000U  // 10 seconds cadence (slow weight changes)
#define WALTER_HX711_GAIN_SETTING 1  // 1=Channel A 128x, 2=Channel B 32x, 3=Channel A 64x
#define WALTER_HX711_AVG_SAMPLES 3   // Median of 3 samples with 15ms delay for 80Hz HX711
#define WALTER_HX711_ENABLE_CHANNEL_B 1  // Both platforms working!

// Platform A (on HX711 Channel B) - Raw values INCREASE with weight
#define WALTER_HX711_OFFSET_A -49331  // Raw value at 0kg (empty platform) - Nov 4, 2025
#define WALTER_HX711_SCALE_A 0.183f   // 7.2kg = 39319 counts → 0.183 g/count
#define WALTER_HX711_INVERT_A 0       // Normal: (raw - OFFSET) * SCALE

// Platform B (on HX711 Channel A) - Normal polarity  
#define WALTER_HX711_OFFSET_B 4180    // Calibrated empty value
#define WALTER_HX711_SCALE_B 0.04581f // 18kg = 392940 counts → 0.04581 g/count

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
#define WALTER_RS485_RX_GPIO 18
#define WALTER_RS485_DE_GPIO 16  // Used as RTS for RS485 half-duplex driver enable
#define WALTER_RS485_BUFFER_SIZE 512
#define WALTER_RS485_READ_TIMEOUT_MS 50

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
#define WALTER_LTE_TASK_STACK 4096
#define WALTER_LTE_TASK_PRIORITY 5

// ====================================================================================
// GPS/GNSS Configuration
// ====================================================================================
#define WALTER_ENABLE_GPS 1
#define WALTER_GPS_FIX_INTERVAL_MS 3600000U  // 1 hour between fixes
#define WALTER_GPS_TASK_STACK 4096
#define WALTER_GPS_TASK_PRIORITY 5

// ====================================================================================
// Analog Monitor (Battery & Tank Levels)
// ====================================================================================
#define WALTER_ENABLE_ANALOG 1
#define WALTER_ANALOG_SAMPLES_PER_READ 16
#define WALTER_ANALOG_POLL_INTERVAL_MS 10000U

// ADC Channel/Unit mapping (ESP32-S3: ADC1 supports channels 0..9 on GPIO1..10)
#define WALTER_BATT1_ADC_UNIT    1
#define WALTER_BATT1_ADC_CHANNEL 5
#define WALTER_BATT2_ADC_UNIT    1
#define WALTER_BATT2_ADC_CHANNEL 6
#define WALTER_TANK1_ADC_UNIT    1
#define WALTER_TANK1_ADC_CHANNEL 0
#define WALTER_TANK2_ADC_UNIT    1
#define WALTER_TANK2_ADC_CHANNEL 1

// Voltage Dividers (kOhm)
#define WALTER_BATTERY_DIVIDER_RHIGH_KOHM 100
#define WALTER_BATTERY_DIVIDER_RLOW_KOHM 15
#define WALTER_TANK_DIVIDER_RHIGH_KOHM 100
#define WALTER_TANK_DIVIDER_RLOW_KOHM 15

// Tank level calibration (ADC input in mV)
#define WALTER_TANK_EMPTY_MV 0
#define WALTER_TANK_FULL_MV 3000

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
