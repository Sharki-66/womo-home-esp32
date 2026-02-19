# WoMoHome Sensorboard Firmware

## Hardware
- **MCU**: Heemol ESP32-S3 N16R8 DevKitC-1
- **IMU**: BNO055 (I2C 0x28) – Lage, Kompass
- **Klima innen**: BME680 (I2C 0x76) – Temp, Feuchte, Druck, IAQ (BSEC)
- **Klima außen**: BME680 (I2C 0x77) – Temp, Feuchte, Druck, Luftdrucktrend
- **Gasfüllstand**: HX711 (2× Kanal A/B) – Wägezellen
- **Batterien**: ADC1_CH3 (GPIO4, Kfz), ADC1_CH4 (GPIO5, Board)
- **Tanks**: ADC1_CH0 (GPIO1, Frischwasser 100L), ADC1_CH1 (GPIO2, Grauwasser 92L)
- **RS485**: TX=GPIO12, RX=GPIO11, DE/RTS=GPIO21 (MAX3485, 115200 8N1)
- **I2C**: SDA=GPIO15, SCL=GPIO16
- **RTC**: PCF8523 (I2C 0x68)

## RS485-Protokoll (Topic-basiert, Protokoll v2)
Jedes Topic ist ein eigenständiges JSON-Paket (CRLF-terminiert):

| Topic | Intervall | Felder |
|-------|-----------|--------|
| `ctrl` | 2s | pwr_on, radio_on, ac_present, wifi_target/active, lte_target/active |
| `imu` | 5s | yaw_deg, pitch_deg, roll_deg, hdg, cal, calibrated |
| `bat` | 10s | b1, b2, nc1, nc2 |
| `tank` | 10s | t1, t2, nc1, nc2, t1_l, t2_l, t1/t2_rate1h/2h, t1/t2_rest_h |
| `hx` | 10s | a, b, sum, nc |
| `gas` | 10s | active, net, rate1h, rate2h, rest_h, net_a/b, cap_kg, pct, pct_a/b |
| `bme` | 15s | 0x76: temp_c, rh_pct, press_hpa, iaq, iaq_acc, eco2_ppm, bvoc_ppm; 0x77: + press_trend |
| `hello` | bis ready | fw, uptime |
| `hb` | 30s | uptime |

Nach `display_ready`-Kommando: Initial-Burst alle Topics sofort.

## Spannungsteiler ADC
Alle 4 ADC-Kanäle nutzen **82kΩ/15kΩ** Teiler (Verpolungsschutz):
- Batterien: 12V → ~1,85V am ADC (Faktor 97/15)
- Tanks: 1V (Votronic 0–1V) → ~0,15V am ADC (Faktor 97/15), 0–1000mV = 0–100%
- NC-Schwellwert: < 1V = nicht angeschlossen

## NVS-Namespaces
| Namespace | Inhalt |
|-----------|--------|
| `bno055_cal` | BNO055 Kalibrierungsoffsets |
| `bme_hist` | Luftdruck-Historie (96 Samples / 24h) |
| `bsec_ns` | BSEC IAQ Kalibrierungsstate (indoor) |
| `tank_state` | Tank-Verbrauchsraten (rate1h/2h) |
| `gas_state` | Gas-Verbrauchsraten (rate1h/2h, wird bei Tare/Flaschentausch gelöscht) |
