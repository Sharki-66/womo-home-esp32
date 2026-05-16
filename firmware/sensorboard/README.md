# WoMoHome Sensorboard Firmware

ESP-IDF v5.5.2 · ESP32-S3-WROOM-1 (N16R8)

## Hardware

| Komponente | Beschreibung | Bus / Pin |
|---|---|---|
| **MCU** | Heemol ESP32-S3 N16R8 DevKitC-1 | — |
| **IMU** | BNO055 – Lage, Kompass, Kalibrierung | I2C 0x28 |
| **Klima innen** | BME680 – Temp, Feuchte, Druck, IAQ (BSEC 3.3V) | I2C 0x76 |
| **Klima außen** | BME280 oder BME260 – Temp, Feuchte, Druck, Trend (1h/3h) | I2C 0x77 |
| **Gasfüllstand** | HX711 – 2× Wägezelle (Kanal A + B) | DOUT=GPIO47, SCK=GPIO45 |
| **GasBee** | BLE-Gaswaage (ESP32-C3) – NimBLE Central, Auto-Reconnect | BLE |
| **INA226** | Strom-/Leistungsmessung Hauptleitung (Vbus 0–36V, ±81,92mV Shunt) | I2C 0x40 |
| **Batterien** | 2× ADC mit Spannungsteiler | Kfz=GPIO4, Board=GPIO5 |
| **Tanks** | 2× Votronic kapazitiv (0–1V) | Frisch=GPIO1 (100L), Grau=GPIO2 (92L) |
| **RTC** | PCF8523 – Echtzeituhr | I2C 0x68 |
| **RS485** | MAX3485, Half-Duplex, 57600 8N1 | TX=GPIO9, RX=GPIO10, DE=GPIO8 |
| **I2C** | 100 kHz, interne Pull-ups | SDA=GPIO16, SCL=GPIO15 |
| **RGB-LED** | WS2812 (onboard) | GPIO48 |

### Steuer-/Sense-GPIOs

| GPIO | Funktion | Richtung |
|---|---|---|
| 7  | Display 5V (P-MOSFET Q4: LOW=ein, Hi-Z=aus) | Ausgang |
| 11 | 12V Bordnetz EIN (bistabiles Relais, Puls) | Ausgang |
| 12 | 12V Bordnetz AUS (bistabiles Relais, Puls) | Ausgang |
| 13 | Multimedia Power (N-MOSFET, HIGH=ein) | Ausgang |
| 14 | 12V Bordnetz Feedback | Eingang |
| 21 | 230V AC Sense | Eingang |

### Spannungsteiler (ADC)
Alle Batterie-/Sense-Kanäle: **100 kΩ / 22 kΩ** Teiler → Faktor 122/22.
- Batterien: 12V → ~2,17V am ADC (BZV55B3V3 Zener-Schutz)
- Tanks: Votronic 0–1V → direkt am ADC, 0–1000 mV = 0–100%
- Nicht angeschlossen: < 1V → `nc=true`

## RS485-Protokoll (Topic-basiert, v2)
CRLF-terminierte ASCII-JSON-Zeilen. Round-Robin, max. 1 Topic pro 100 ms. Nach `display_ready`: Initial-Burst aller Topics sofort.

| Topic | Intervall | Felder |
|---|---|---|
| `hello` | bis ready | `fw`, `board`, `uptime`, `ts` |
| `hb` | 30 s | `uptime`, `ts`, `heap` |
| `ctrl` | 2 s | `pwr_on`, `radio_on`, `ac_present`; optional: `rtc_bat_low`, `rtc_bat_switched` |
| `imu` | 5 s | `yaw_deg`, `pitch_deg`, `roll_deg`, `hdg`, `cal:{sys,gyro,acc,mag}`, `calibrated`, `ts` |
| `bat` | 10 s | `b1`, `b2` (Volt), `nc1`, `nc2` |
| `tank` | 10 s | `t1`, `t2` (%), `nc1`, `nc2`, `t1_l`, `t2_l` (Liter), `t1_rate1h`, `t1_rate2h`, `t1_rest_h`, `t2_rate1h`, `t2_rate2h`, `t2_rest_h` |
| `hx` | 10 s | `a`, `b` (kg, optional), `sum` (optional), `nc`, `ts` |
| `gas` | 10 s | `active`, `net`, `rate1h`, `rate2h`, `rest_h`, `net_a`, `net_b`, `cap_kg`, `pct`, `pct_a`, `pct_b` |
| `bme` | 15 s | Objekte `"0x76"` (Indoor) und `"0x77"` (Outdoor), je mit `chip`, `addr`, `temp_c`, `rh_pct`, `press_hpa`, `ts`; Indoor zusätzlich: `gas_kohm`?, `iaq`, `iaq_acc`, `eco2_ppm`, `bvoc_ppm`; Outdoor zusätzlich: `press_trend_state`, `press_trend_hpa_h` |
| `elec` | 5 s | `v_bus` (V), `i_a` (A), `p_w` (W), `v_shunt_mv` (mV), `ts`; `nc=true` wenn INA226 nicht gefunden |

**`press_trend_state`-Werte:** `fall_fast` / `fall_slow` / `steady` / `rise_slow` / `rise_fast`
(bevorzugt 3h-Fenster, Fallback 1h; ab 4 bzw. 12 Samples à 15 min verfügbar)

**WiFi-Passwort-Anfrage:** Bei fehlenden WLAN-Zugangsdaten sendet das Sensorboard `type:"wifi_pass_request"` per RS485.

## Web-Dashboard (HTTP, Port 80)

Erreichbar unter `http://Womo-Sensor.local/` nach WiFi-Verbindung mit RUTX11.

| Endpunkt | Methode | Beschreibung |
|---|---|---|
| `/` | GET | Weiterleitung auf `/dashboard.html` |
| `/dashboard.html` | GET | Vollständiges Steuer- und Sensor-Dashboard |
| `/horizon.html` | GET | Horizont-/Parkhilfe-Anzeige |
| `/api/status` | GET | Systemstatus (JSON) |
| `/api/data` | GET | Aktueller Sensor-Snapshot (JSON) |
| `/api/imu` | GET | IMU-Snapshot (JSON) |
| `/api/cmd` | POST | Steuerkommandos (JSON, gleiche `cmd`-Syntax wie RS485) |

Statische Dateien (HTML, PNG, favicon) werden aus der SPIFFS-Partition `storage` → `/spiffs` geladen.

## NVS-Namespaces

| Namespace | Key(s) | Inhalt |
|---|---|---|
| `bno055` | `calib`, `pr_zero` | Kalibrierungsoffsets, Pressure-Zero |
| `bme680` | `press_hist` | Luftdruck-Historie (96 Samples / 24h) |
| `bsec` | `state_in_<addr>_v<n>` | BSEC IAQ State (sensorspezifisch, z.B. `state_in_76_v3`) |
| `gas_state` | `gas` | Gasverbrauch (Raten, Tare, Flaschentausch) |
| `tank_state` | `tank1`, `tank2` | Tank-Verbrauchsraten |
| `wifi_cfg` | `ssid`, `pass` | WLAN-Zugangsdaten (RUTX11) |

> **INA226 Kalibrierung:** `SENSOR_INA226_SHUNT_MOHM` in `sensor_config.h` auf den verbauten Shunt-Widerstand (mΩ) setzen. `SENSOR_INA226_MAX_CURRENT_A` bestimmt die Auflösung (Current_LSB = MAX_CURRENT / 32768 A). Maximale Shuntspannung: ±81,92 mV → I_max = 81,92 mV / R_shunt.

> **BSEC State Version:** Bei Tausch des Indoor-BME680 `SENSOR_BME680_BSEC_STATE_VERSION` in `sensor_config.h` hochzählen, damit kein alter Baseline-State auf den neuen Sensor angewandt wird.

## Projektstruktur

```
main/
├── app_main.c              Startup, Task-Orchestrierung
├── sensor_config.h         Pin-Belegung, Kalibrierung, Konstanten
├── hal/
│   ├── sensor_i2c_bus      I2C-Bus Init
│   └── deep_sleep          Touch-Wakeup, Deep-Sleep
├── sensors/
│   ├── analog_sensor       ADC (Batterien, Tanks), Median-of-3-Mittelung
│   ├── bme680_sensor       BME680/BME280/BME260 + BSEC + Drucktrend
│   ├── bno055_sensor       IMU + Kalibrierung
│   ├── hx711_sensor        Wägezellen (Gas)
│   └── ina226_sensor       INA226 Strom-/Leistungsmessung Hauptleitung
├── network/
│   ├── rs485_modem         JSON-Serialisierung aller Topics, Steuerlogik
│   ├── womo_rs485          UART-Transport, Framing, ACK
│   ├── gasbee_ble_client   NimBLE Central – BLE-Gaswaage GasBee
│   └── wifi/
│       ├── sensor_wifi     STA-Verbindung (RUTX11), NVS-Credentials
│       └── sensor_http     HTTP-Server (Dashboard, Parkhilfe, API)
└── time/
    ├── rtc_pcf8523         RTC-Treiber (PCF8523)
    └── time_sync           NTP + RTC-Sync
```
