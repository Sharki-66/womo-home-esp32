# WoMoHome Sensorboard Firmware

ESP-IDF v5.5.2 · ESP32-S3-WROOM-1 (N16R8)

## Hardware

| Komponente | Beschreibung | Bus / Pin |
|---|---|---|
| **MCU** | Heemol ESP32-S3 N16R8 DevKitC-1 | — |
| **IMU** | BNO055 – direkt auf Platine, Lage, Kompass, Kalibrierung | I2C 0x28 |
| **Klima innen** | BME680 – Temp, Feuchte, Druck, IAQ (BSEC) | I2C 0x76 |
| **Klima außen** | BME680 – Temp, Feuchte, Druck, Trend (1h/3h) | I2C 0x77 |
| **Gasfüllstand** | HX711 – 2× Wägezelle (Kanal A + B) | DOUT=GPIO47, SCK=GPIO45 |
| **Batterien** | 2× ADC mit Spannungsteiler | Kfz=GPIO4, Board=GPIO5 |
| **Tanks** | 2× Votronic kapazitiv (0–1V) | Frisch=GPIO1, Grau=GPIO2 |
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
- Nicht angeschlossen: < 1V

## RS485-Protokoll (Topic-basiert, v2)
CRLF-terminierte ASCII-JSON-Zeilen. Round-Robin, max. 1 Topic pro 100 ms.

| Topic | Intervall | Wichtige Felder |
|---|---|---|
| `hello` | bis ready | fw, uptime |
| `hb` | 30 s | uptime |
| `ctrl` | 2 s | pwr_on, radio_on, ac_present, wifi/lte_target/active |
| `imu` | 5 s | yaw_deg, pitch_deg, roll_deg, hdg, cal, calibrated |
| `bat` | 10 s | b1, b2, nc1, nc2 |
| `tank` | 10 s | t1, t2, nc1, nc2, t1_l, t2_l, rate1h/2h, rest_h |
| `hx` | 10 s | a, b, sum, nc |
| `gas` | 10 s | active, net, rate1h/2h, rest_h, cap_kg, pct, pct_a/b |
| `bme` | 15 s | 0x76: temp_c, rh_pct, press_hpa, iaq, eco2_ppm; 0x77: + press_trend_state, press_trend_hpa_h |

Nach `display_ready`: Initial-Burst aller Topics sofort.

## NVS-Namespaces

| Namespace | Key(s) | Inhalt |
|---|---|---|
| `bno055` | `calib`, `pr_zero` | Kalibrierungsoffsets, Pressure-Zero |
| `bme680` | `press_hist` | Luftdruck-Historie (96 Samples / 24h) |
| `bsec` | `state_in` | BSEC IAQ State (indoor Sensor) |
| `gas_state` | `gas` | Gasverbrauch (Raten, Tare, Flaschentausch) |
| `tank_state` | `tank1`, `tank2` | Tank-Verbrauchsraten |
| `wifi_cfg` | `ssid`, `pass` | WLAN-Zugangsdaten (RUTX11) |

## Projektstruktur

```
main/
├── app_main.c              Startup, Task-Orchestrierung
├── sensor_config.h         Pin-Belegung, Kalibrierung, Konstanten
├── hal/
│   └── sensor_i2c_bus      I2C-Bus Init
├── sensors/
│   ├── analog_sensor       ADC (Batterien, Tanks)
│   ├── bme680_sensor       BME680 + BSEC + Drucktrend
│   ├── bno055_sensor       IMU + Kalibrierung
│   └── hx711_sensor        Wägezellen (Gas)
├── network/
│   ├── rs485_modem         JSON-Serialisierung aller Topics
│   ├── womo_rs485          UART-Transport, Framing, ACK
│   └── wifi/
│       ├── sensor_wifi     STA-Verbindung (RUTX11)
│       └── sensor_http     Webserver (Parkhilfe/Horizont)
└── time/
    ├── rtc_pcf8523         RTC-Treiber
    └── time_sync           NTP + RTC-Sync
```
