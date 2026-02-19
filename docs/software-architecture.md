# Software-Architektur

Detaillierte Beschreibung der Software-Architektur für das WoMo Home Control System.

---

## 🏗️ System-Übersicht

```
┌─────────────────────────────────────────────────────────────┐
│                    WoMo Home Control System                 │
└─────────────────────────────────────────────────────────────┘
           │                                    │
           │                                    │
┌──────────▼─────────────┐         ┌───────────▼──────────────┐
│  Waveshare ESP32-S3    │         │  Walter Modem ESP32-S3   │
│  (Display Controller)  │◄───────►│  (Sensor Controller)     │
│                        │  UART   │                          │
│  - LVGL GUI            │  JSON   │  - Sensor Collection     │
│  - Touch Handler       │         │  - LTE-M/NB-IoT          │
│  - WiFi AP/STA         │         │  - GPS/GLONASS           │
│  - User Input          │         │  - I2C Bus Master        │
│  - Settings Storage    │         │  - Data Processing       │
└────────────────────────┘         └──────────┬───────────────┘
                                              │
                                              │ I2C
                                              │
                                   ┌──────────▼───────────┐
                                   │   PCA9548A MUX       │
                                   │   8 I2C Channels     │
                                   └──────────┬───────────┘
                                              │
                    ┌─────────────────────────┼─────────────┐
                    │                         │             │
              ┌─────▼─────┐           ┌──────▼──────┐  ┌───▼────┐
              │  Sensoren │           │  Sensoren   │  │ GPIOs  │
              │  CH0-CH3  │           │  CH4-CH7    │  │PCF8575 │
              └───────────┘           └─────────────┘  └────────┘
```

---

## 🎯 Architektur-Prinzipien

### Zwei-Controller-Ansatz

**Warum zwei ESP32-S3?**

```
✅ Separation of Concerns
   - Waveshare: UI & User Interaction
   - Walter: Sensors & Communication

✅ Lastverteilung
   - Display-Rendering (LVGL) ist CPU-intensiv
   - LTE/GPS + Sensoren parallel

✅ Fehlertoleranz
   - Display-Crash beeinflusst Sensoren nicht
   - Sensoren arbeiten unabhängig weiter

✅ Wartbarkeit
   - Klare Zuständigkeiten
   - Getrennte Firmware-Updates
   - Einfacheres Debugging
```

---

## 📡 Kommunikations-Protokoll (UART)

### JSON-basiertes Protokoll

**Format:**
```json
{
  "type": "sensor_data|command|response|error",
  "timestamp": 1234567890,
  "data": { ... }
}
```

### Nachrichten-Typen

#### 1. Sensor-Daten (Walter → Waveshare)
```json
{
  "type": "sensor_data",
  "timestamp": 1735216800,
  "data": {
    "bme280_indoor": {
      "temperature": 22.5,
      "humidity": 45.2,
      "pressure": 1013.25
    },
    "bme280_outdoor": {
      "temperature": 18.3,
      "humidity": 68.1,
      "pressure": 1012.80
    },
    "ina226_solar": {
      "voltage": 14.2,
      "current": 5.3,
      "power": 75.26
    },
    "ina226_battery": {
      "voltage": 12.6,
      "current": -2.1,
      "power": -26.46
    },
    "bno055": {
      "pitch": 2.3,
      "roll": -1.5,
      "heading": 245.0
    },
    "gps": {
      "lat": 52.5200,
      "lon": 13.4050,
      "altitude": 45.0,
      "satellites": 12
    }
  }
}
```

#### 2. Kommandos (Waveshare → Walter)
```json
{
  "type": "command",
  "timestamp": 1735216800,
  "data": {
    "action": "read_sensor",
    "sensor": "bme280_indoor"
  }
}
```

```json
{
  "type": "command",
  "timestamp": 1735216800,
  "data": {
    "action": "set_gpio",
    "pin": "P0",
    "value": 1
  }
}
```

#### 3. Antworten (Walter → Waveshare)
```json
{
  "type": "response",
  "timestamp": 1735216800,
  "data": {
    "status": "ok",
    "message": "Command executed"
  }
}
```

#### 4. Fehler
```json
{
  "type": "error",
  "timestamp": 1735216800,
  "data": {
    "code": 404,
    "message": "Sensor not found",
    "sensor": "unknown_sensor"
  }
}
```

---

## 🖥️ Waveshare Firmware

### Projekt-Struktur

```
firmware/waveshare-main/
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
│
├── main/
│   ├── CMakeLists.txt
│   ├── main.c
│   ├── Kconfig.projbuild
│   │
│   ├── gui/
│   │   ├── gui_main.c          # Haupt-GUI Loop
│   │   ├── gui_main.h
│   │   ├── gui_screens.c       # Screen Management
│   │   ├── gui_wifi.c          # WiFi Config Screen
│   │   ├── gui_sensors.c       # Sensor Display
│   │   ├── gui_settings.c      # Einstellungen
│   │   └── gui_styles.c        # LVGL Styles
│   │
│   ├── uart/
│   │   ├── uart_comm.c         # UART Kommunikation
│   │   ├── uart_comm.h
│   │   ├── json_parser.c       # JSON De/Serialisierung
│   │   └── protocol.h          # Protokoll-Definitionen
│   │
│   ├── wifi/
│   │   ├── wifi_manager.c      # WiFi AP/STA Management
│   │   └── wifi_manager.h
│   │
│   ├── storage/
│   │   ├── nvs_storage.c       # NVS für Settings
│   │   └── nvs_storage.h
│   │
│   └── config/
│       └── config.h            # Globale Konfiguration
│
└── components/
    └── lvgl/                   # LVGL Library (managed)
```

### Hauptkomponenten

#### main.c
```c
void app_main(void)
{
    // 1. Initialisierung
    nvs_flash_init();
    
    // 2. Display & Touch
    display_init();
    touch_init();
    
    // 3. LVGL
    lvgl_init();
    gui_create_screens();
    
    // 4. WiFi
    wifi_init();
    
    // 5. UART
    uart_init();
    
    // 6. Tasks starten
    xTaskCreate(gui_task, "GUI", 8192, NULL, 5, NULL);
    xTaskCreate(uart_rx_task, "UART_RX", 4096, NULL, 4, NULL);
    xTaskCreate(wifi_task, "WiFi", 4096, NULL, 3, NULL);
}
```

#### GUI Task (LVGL)
```c
void gui_task(void *pvParameters)
{
    while (1) {
        // LVGL Handler
        lv_timer_handler();
        
        // Update Sensor-Anzeigen
        update_sensor_displays();
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

#### UART RX Task
```c
void uart_rx_task(void *pvParameters)
{
    char rx_buffer[1024];
    
    while (1) {
        int len = uart_read_bytes(UART_NUM, rx_buffer, 
                                  sizeof(rx_buffer), 100 / portTICK_PERIOD_MS);
        
        if (len > 0) {
            // JSON parsen
            cJSON *json = cJSON_Parse(rx_buffer);
            
            // Nachricht verarbeiten
            handle_incoming_message(json);
            
            cJSON_Delete(json);
        }
    }
}
```

---

## 🛰️ Walter Firmware

### Projekt-Struktur

```
firmware/walter-sensor/
├── CMakeLists.txt
├── sdkconfig.defaults
├── dependencies.lock
│
├── main/
│   ├── CMakeLists.txt
│   ├── main.c
│   ├── idf_component.yml      # Walter Modem Dependency
│   ├── Kconfig.projbuild
│   │
│   ├── sensors/
│   │   ├── sensor_manager.c   # Zentrales Sensor-Management
│   │   ├── sensor_manager.h
│   │   ├── bme280_driver.c    # BME280 Treiber
│   │   ├── ina226_driver.c    # INA226 Treiber
│   │   ├── bno055_driver.c    # BNO055 Treiber
│   │   ├── ads1115_driver.c   # ADS1115 Treiber
│   │   ├── hx711_driver.c     # HX711 Treiber
│   │   └── pca9548a.c         # Multiplexer Control
│   │
│   ├── modem/
│   │   ├── lte_manager.c      # LTE-M/NB-IoT
│   │   ├── lte_manager.h
│   │   ├── gps_manager.c      # GPS/GLONASS
│   │   ├── gps_manager.h
│   │   ├── mqtt_client.c      # MQTT für Cloud
│   │   └── http_client.c      # HTTP für Updates
│   │
│   ├── uart/
│   │   ├── uart_comm.c        # UART zu Waveshare
│   │   ├── uart_comm.h
│   │   ├── json_builder.c     # JSON Serialisierung
│   │   └── protocol.h
│   │
│   ├── gpio/
│   │   ├── pcf8575_driver.c   # GPIO Expander
│   │   └── relay_control.c    # Relais-Steuerung
│   │
│   └── config/
│       └── config.h
│
└── components/
```

### Hauptkomponenten

#### main.c
```c
void app_main(void)
{
    // 1. Initialisierung
    nvs_flash_init();
    
    // 2. I2C Bus
    i2c_master_init();
    
    // 3. PCA9548A Multiplexer
    pca9548a_init();
    
    // 4. Sensoren
    sensor_manager_init();
    
    // 5. Walter Modem
    walter_modem_init();
    lte_manager_init();
    gps_manager_init();
    
    // 6. UART zu Waveshare
    uart_comm_init();
    
    // 7. Tasks starten
    xTaskCreate(sensor_task, "Sensors", 4096, NULL, 5, NULL);
    xTaskCreate(lte_task, "LTE", 4096, NULL, 4, NULL);
    xTaskCreate(gps_task, "GPS", 4096, NULL, 4, NULL);
    xTaskCreate(uart_tx_task, "UART_TX", 4096, NULL, 4, NULL);
    xTaskCreate(uart_rx_task, "UART_RX", 4096, NULL, 3, NULL);
}
```

#### Sensor Task
```c
void sensor_task(void *pvParameters)
{
    TickType_t last_wake = xTaskGetTickCount();
    
    while (1) {
        // Alle Sensoren auslesen
        sensor_data_t data;
        
        // PCA9548A CH0: BME280 Indoor
        pca9548a_select_channel(0);
        bme280_read(&data.bme280_indoor);
        
        // PCA9548A CH1: BME280 Outdoor
        pca9548a_select_channel(1);
        bme280_read(&data.bme280_outdoor);
        
        // PCA9548A CH2: INA226 Solar
        pca9548a_select_channel(2);
        ina226_read(&data.ina226_solar);
        
        // ... weitere Sensoren ...
        
        // Daten zur Queue
        xQueueSend(sensor_queue, &data, 0);
        
        // 1 Sekunde warten
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}
```

#### UART TX Task
```c
void uart_tx_task(void *pvParameters)
{
    sensor_data_t data;
    
    while (1) {
        // Warte auf Sensor-Daten
        if (xQueueReceive(sensor_queue, &data, portMAX_DELAY)) {
            
            // JSON erstellen
            cJSON *json = cJSON_CreateObject();
            cJSON_AddStringToObject(json, "type", "sensor_data");
            cJSON_AddNumberToObject(json, "timestamp", time(NULL));
            
            cJSON *json_data = cJSON_CreateObject();
            
            // BME280 Indoor
            cJSON *bme_in = cJSON_CreateObject();
            cJSON_AddNumberToObject(bme_in, "temperature", data.bme280_indoor.temp);
            cJSON_AddNumberToObject(bme_in, "humidity", data.bme280_indoor.hum);
            cJSON_AddNumberToObject(bme_in, "pressure", data.bme280_indoor.press);
            cJSON_AddItemToObject(json_data, "bme280_indoor", bme_in);
            
            // ... weitere Sensoren ...
            
            cJSON_AddItemToObject(json, "data", json_data);
            
            // Senden
            char *json_str = cJSON_PrintUnformatted(json);
            uart_write_bytes(UART_NUM, json_str, strlen(json_str));
            uart_write_bytes(UART_NUM, "\n", 1);
            
            free(json_str);
            cJSON_Delete(json);
        }
    }
}
```

---

## 🔄 Datenfluss

### Normaler Betrieb

```
┌─────────────┐                      ┌──────────────┐
│   Walter    │                      │  Waveshare   │
│             │                      │              │
│  Sensoren   │                      │              │
│  auslesen   │─────JSON────────────→│  Empfangen   │
│  (1s Takt)  │   (UART 115200)      │              │
│             │                      │  Parsen      │
│  GPS read   │                      │              │
│  (5s Takt)  │─────JSON────────────→│  GUI Update  │
│             │                      │              │
│             │◄────Command──────────│  User Input  │
│  Ausführen  │   (on demand)        │              │
└─────────────┘                      └──────────────┘
```

### Cloud-Upload (Optional)

```
┌─────────────┐
│   Walter    │
│             │
│  Daten      │
│  sammeln    │
│             │
│  Puffer     │──┐
│  (60s)      │  │
└─────────────┘  │
                 │
                 ▼
         ┌───────────────┐
         │  LTE-M/NB-IoT │
         │               │
         │  MQTT/HTTP    │
         │               │
         └───────┬───────┘
                 │
                 ▼
         ┌───────────────┐
         │  Cloud Server │
         │  (AWS/Azure)  │
         └───────────────┘
```

---

## 💾 Datenspeicherung

### NVS (Non-Volatile Storage)

**Waveshare:**
```c
// WiFi Credentials
nvs_set_str(handle, "wifi_ssid", "MyWiFi");
nvs_set_str(handle, "wifi_pass", "password");

// User Settings
nvs_set_u8(handle, "brightness", 80);
nvs_set_u8(handle, "theme", THEME_DARK);

// Kalibrierung
nvs_set_blob(handle, "touch_cal", &cal_data, sizeof(cal_data));
```

**Walter:**
```c
// LTE APN
nvs_set_str(handle, "lte_apn", "iot.1nce.net");

// Sensor Offsets
nvs_set_float(handle, "bme280_offset", -0.5);
nvs_set_float(handle, "ina226_shunt", 0.075);

// GPS Last Position
nvs_set_blob(handle, "last_gps", &gps_data, sizeof(gps_data));
```

---

## 🔐 Fehlerbehandlung

### Sensor-Ausfälle

```c
esp_err_t read_sensor(sensor_t *sensor)
{
    esp_err_t ret = sensor->read_func(sensor->data);
    
    if (ret != ESP_OK) {
        sensor->error_count++;
        
        if (sensor->error_count > MAX_ERRORS) {
            // Sensor als defekt markieren
            sensor->status = SENSOR_FAILED;
            ESP_LOGE(TAG, "Sensor %s failed!", sensor->name);
            
            // Fallback-Werte
            use_fallback_data(sensor);
        }
    } else {
        sensor->error_count = 0;
        sensor->status = SENSOR_OK;
    }
    
    return ret;
}
```

### UART Timeout

```c
void uart_rx_task(void *pvParameters)
{
    TickType_t last_rx = xTaskGetTickCount();
    
    while (1) {
        int len = uart_read_bytes(...);
        
        if (len > 0) {
            last_rx = xTaskGetTickCount();
            handle_message(...);
        } else {
            // Timeout check
            if ((xTaskGetTickCount() - last_rx) > pdMS_TO_TICKS(5000)) {
                ESP_LOGW(TAG, "UART timeout! Reconnecting...");
                uart_reinit();
                last_rx = xTaskGetTickCount();
            }
        }
    }
}
```

---

## 📊 Performance

### Timing-Anforderungen

```
Sensor Polling:        1000ms (1Hz)
GPS Update:            5000ms (0.2Hz)
Display Refresh:       10ms (100Hz)
UART Latenz:          <50ms
Touch Response:       <100ms
```

### CPU-Last (geschätzt)

**Waveshare:**
```
LVGL Rendering:    ~40%
Touch Processing:  ~5%
UART:              ~5%
WiFi:              ~10%
Idle:              ~40%
```

**Walter:**
```
Sensor Reading:    ~20%
LTE/GPS:          ~30%
UART:             ~5%
Data Processing:  ~15%
Idle:             ~30%
```

---

**Stand:** 2025-01-26  
**Version:** 1.0