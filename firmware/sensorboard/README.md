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
| **GasBee** | BLE-Gaswaage (`firmware/gasbee_nrf`), bis zu 2× (Flasche A/B) – passiver Scan, Advertising-Format v1, nur wenn kein HX711-Modul am Kabel | BLE |
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
| `ctrl` | 2 s | `pwr_on` (12V-Sense GPIO14, s. u.), `radio_on`, `ac_present`; optional: `rtc_bat_low`, `rtc_bat_switched` |
| `imu` | 5 s | `yaw_deg`, `pitch_deg`, `roll_deg`, `hdg`, `cal:{sys,gyro,acc,mag}`, `calibrated`, `ts` |
| `bat` | 10 s | `b1`, `b2` (Volt), `nc1`, `nc2` |
| `tank` | 10 s | `t1`, `t2` (%), `nc1`, `nc2`, `t1_l`, `t2_l` (Liter), `t1_rate1h`, `t1_rate2h`, `t1_rest_h`, `t2_rate1h`, `t2_rate2h`, `t2_rest_h` |
| `hx` | 10 s | `src` (`hx`/`ble`/`none`), `a`, `b` (kg, optional), `sum` (optional), `nc`, `ts`; nur bei `src:"ble"` je Flasche `batt_a_mv`/`batt_a_pct`, `chg_a`, `cal_a` (analog `_b`) |
| `gas` | 10 s | `src`, `active`, `net`, `rate1h`, `rate2h`, `rest_h`, `net_a`, `net_b`, `cap_kg`, `pct`, `pct_a`, `pct_b` |
| `bme_in` | 15 s | `chip` (`bme680`/`bme280`/`bme260`/`ruuvi`), `addr`?, `temp_c`, `rh_pct`, `press_hpa`, `ts`; BME680 zusätzlich: `gas_kohm`? (wenn Gas gültig) sowie `iaq`, `iaq_acc` (0–3), `eco2_ppm`, `bvoc_ppm` – erst nach BSEC-Burn-in (fehlen während der Stabilisierung); bei Ruuvi-Fallback zusätzlich `batt_mv`? |
| `bme_out` | 15 s | `chip`, `addr`?, `temp_c`, `rh_pct`, `press_hpa`, `ts`, `press_trend_state`?, `press_trend_hpa_h`?, `gas_kohm`? (wenn Gas gültig); **kein `iaq`/`eco2`/`bvoc` für den Außensensor**; bei Ruuvi-Fallback zusätzlich `batt_mv`? |
| `elec` | 5 s | `v_bus` (V), `i_a` (A), `p_w` (W), `v_shunt_mv` (mV), `ts`; `nc=true` wenn INA226 nicht gefunden |
| `ruuvi_scan` | nur auf Anfrage | `tags:[{mac, rssi, temp_c, has_pressure, age_s}, …]` – alle gesehenen Ruuvi-Tags (ungefiltert), Antwort auf `cmd:"ruuvi_scan_request"` |
| `gasbee_scan` | nur auf Anfrage | `tags:[{mac, rssi, weight_kg, batt_mv, batt_pct, chg, cal, seq, age_s}, …]` – alle gesehenen GasBees (ungefiltert), Antwort auf `cmd:"gasbee_scan_request"` |

**Ruuvi-MAC-Konfiguration:** Display → Sensorboard `cmd:"ruuvi_config"` (`indoor_mac`, `outdoor_mac`) setzt den MAC-Filter des Ruuvi-BLE-Scanners zur Laufzeit und persistiert ihn in NVS (`ruuvi_cfg`); leer = Slot deaktiviert. Kein Compile-Time-Default mehr.

### GasBee BLE (Gaswaage, Advertising-Format v1)

Quelle der Vorgabe: `firmware/gasbee_nrf` (XIAO nRF52840). Das Sensorboard **scannt passiv und verbindet sich nicht** (`gasbee_ble_scanner.c`).

- GasBee misst alle 5 min (Testmodus: 1 min) und sendet danach 10 Advertising-Pakete im 1-s-Abstand, alle mit derselben `seq`. Ausgewertet wird nur das erste Paket einer neuen `seq` (jede `seq` ≠ der letzten gilt als neu).
- Erkennung: Complete Local Name `"GasBee"` + Manufacturer Data mit Company-ID `0xFFFF` (SIG-reserviert für Tests/interne Nutzung) und Format-Version 1. Unbekannte Version → Paket verwerfen.
- Unterscheidung mehrerer GasBees per (random static, fester) BLE-Adresse → Zuordnung Flasche A/B per MAC.

| Byte (nach Company-ID) | Typ | Feld | Ungültig |
|---|---|---|---|
| 0 | uint8 | Format-Version = 1 | – |
| 1–2 | int16 LE | Bruttogewicht der Waage in 10 g (±327,67 kg), ohne Tara | `0x8000` |
| 3–4 | uint16 LE | Akkuspannung in mV | `0xFFFF` |
| 5 | uint8 | Sequenzzähler, +1 pro Messzyklus (255 → 0) | – |
| 6 | uint8 | Flags: Bit 0 HX711 ok, Bit 1 Akku lädt, Bit 2 kalibriert, Bit 3–7 reserviert (ignoriert) | – |

Tara, Voll-Referenz, Netto, Füllstand und Verbrauchsrate berechnet das Sensorboard genau wie beim Kabel-HX711. Akku-% per LiPo-Kennlinie `SENSOR_GASBEE_LIPO_CURVE` (TODO: am echten Akku verifizieren). Veraltet nach `SENSOR_GASBEE_STALE_MS` (2 Zyklen + Sendefenster) ohne neue `seq`.

**Quellenwahl (`sensors/gas_source.c`):** HX711-Modul am Kabel vorhanden (DOUT antwortet) → HX711 liefert A+B (Vorrang). Sonst GasBee A/B, sofern aktuell. Sonst `nc`. Eine fehlende Wägezelle bei vorhandenem HX711-Modul ist nicht erkennbar – dieser Mischbetrieb ist nicht vorgesehen. Beim Wechsel HX711 ↔ GasBee wird die Basis der Verbrauchsrate neu gesetzt.

**Verbrauch über Ausschalt-Lücken:** Ausschalt-Lücke: `gas_state` speichert Netto A/B mit Uhrzeit (RTC/NTP) und Quelle; beim ersten Messwert nach dem Boot wird der mittlere Verbrauch über die Lücke direkt als `rate1h`/`rate2h` übernommen (gleiche Quelle, Lücke ≤ `SENSOR_GAS_GAP_MAX_S` = 1 Tag, Abnahme ≥ `SENSOR_GAS_ACTIVE_EPS_KG`), sonst Raten auf 0 und neue Basis.

**GasBee-MAC-Konfiguration:** Display → Sensorboard `cmd:"gasbee_config"` (`bottle_a_mac`, `bottle_b_mac`), NVS `gasbee_cfg`; leer = Slot deaktiviert. `cmd:"gasbee_scan_request"` → Antwort-Topic `gasbee_scan`.

**`press_trend_state`-Werte:** `fall_fast` / `fall_slow` / `steady` / `rise_slow` / `rise_fast`
(bevorzugt 3h-Fenster, Fallback 1h; ab 4 bzw. 12 Samples à 15 min verfügbar)

**BME680 IAQ / `iaq_acc`:** BSEC braucht einen Burn-in – `iaq`/`eco2_ppm`/`bvoc_ppm` werden erst
gesendet, wenn ein IAQ-Wert vorliegt (ca. 5 min, fabrikneuer Sensor 20–30 min). `iaq_acc` läuft
0 → 3 (0 = nicht stabilisiert, 1 = unsicher, 2 = mittel, 3 = kalibriert); Stufe 3 typ. nach
einigen Stunden, das Auto-Kalibriermodell ist nach ~4 Tagen Dauerbetrieb voll eingeschwungen
(4d-Config). Der BSEC-State wird ab Accuracy ≥ 3 in NVS gesichert, sodass ein Neustart nahe der
zuletzt erreichten Genauigkeit beginnt. Das Display nutzt IAQ erst ab `iaq_acc >= 2` für Alarme.

**12V-Bordnetz (bistabiles LBE-Relais):** `cmd:"pwr_12v_on"` / `pwr_12v_off` löst je einen
Relais-Puls von `SENSOR_PWR_12V_PULSE_MS` (500 ms) aus – dieselbe Konstante für Boot-EIN-Puls,
Laufzeit-Puls und Deep-Sleep-AUS-Puls. Das ctrl-Feld `pwr_on` meldet bei aktivem
`SENSOR_PWR_12V_SENSE_ENABLE` den an GPIO14 **gemessenen** Zustand statt nur des kommandierten;
`SENSOR_PWR_12V_SENSE_GRACE_MS` (2500 ms) nach jedem Puls wird noch der kommandierte Zustand
gemeldet, bis die 12V-Schiene eingeschwungen ist. Polarität: `SENSOR_PWR_12V_SENSE_ACTIVE_HIGH`
(TODO: per Messung verifizieren).

⚠️ **Bekannter Hardware-Bug (2026-09):** Die Treiber Q1/Q2 für J7 „12V EIN"/„12V AUS" sind mit
IRLML2244 (P-Kanal, High-Side, schaltet +12V) bestückt. Der EBL erwartet an diesen Pins aber
Pull-to-GND (Low-Side). Das Relais schaltet dadurch nicht – unabhängig von Pulslänge oder Sense.
Fix: Q1/Q2 → IRLML6244 (N-Kanal, gleiches SOT-23-Pinout), sofern Source bereits an GND liegt.

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
| `gas_state` | `gas` | Gasverbrauch (Raten, Voll-Referenzen, letzter Wert mit Uhrzeit + Quelle) |
| `tank_state` | `tank1`, `tank2` | Tank-Verbrauchsraten |
| `wifi_cfg` | `ssid`, `pass` | WLAN-Zugangsdaten (RUTX11) |
| `ruuvi_cfg` | `indoor_mac`, `outdoor_mac` | MAC-Filter Ruuvi-BLE-Scanner (vom Display konfiguriert) |
| `gasbee_cfg` | `bottle_a_mac`, `bottle_b_mac` | Zuordnung GasBee → Flasche A/B (vom Display konfiguriert) |

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
│   ├── hx711_sensor        Wägezellen (Gas, Kabel)
│   ├── gas_source          Quellenwahl Gasgewicht: HX711 (Vorrang) oder GasBee
│   └── ina226_sensor       INA226 Strom-/Leistungsmessung Hauptleitung
├── network/
│   ├── rs485_modem         JSON-Serialisierung aller Topics, Steuerlogik
│   ├── womo_rs485          UART-Transport, Framing, ACK
│   ├── ble_manager         NimBLE-Stack, passiver Scan, Advertising-Dispatch
│   ├── ruuvi_ble_scanner   Ruuvi DF5 (Innen/Außen)
│   ├── gasbee_ble_scanner  GasBee Advertising v1 (Flasche A/B)
│   └── wifi/
│       ├── sensor_wifi     STA-Verbindung (RUTX11), NVS-Credentials
│       └── sensor_http     HTTP-Server (Dashboard, Parkhilfe, API)
└── time/
    ├── rtc_pcf8523         RTC-Treiber (PCF8523)
    └── time_sync           NTP + RTC-Sync
```
