# Tech Context – WoMoHome Sensorboard

## Build-System
- **ESP-IDF:** v5.5.2 (`source /home/hajo/esp/v5.5.2/esp-idf/export.sh`)
- **Build:** `cd firmware/sensorboard && idf.py build`
- **Flash/Monitor:** Ausschließlich über VS Code Tasks starten – Claude führt keine Flash-Befehle aus

## Komponenten (idf_component.yml)

| Komponente | Version | Zweck |
|---|---|---|
| `espressif/i2c_bus` | `*` | Gemeinsamer I2C-Bus für alle Sensoren |
| `espressif/mdns` | `*` | mDNS (`Womo-Sensor.local`) |
| `espressif/led_strip` | `*` | WS2812 RGB-LED (GPIO48) |
| `espressif/bme280` | `^0.1.1` | BME280/BME260 Außensensor (0x77) |
| `k0i05/esp_ina226` | `^1.2.7` | INA226 Strom-/Leistungsmessung |
| BNO055 | custom | IMU (eigener Treiber über i2c_bus) |
| BSEC | Bosch v3.3V/3s/4d | IAQ-Berechnung für BME680 |

## PSRAM-Einschränkung (kritisch!)
Das N16R8-Modul nutzt **Octal-PSRAM** → GPIO35, GPIO36, GPIO37 sind intern belegt und dürfen **nicht** als I/O verwendet werden.

## I2C-Bus
- Port 0, SDA=GPIO16, SCL=GPIO15, 100 kHz
- Alle Sensoren (BNO055, BME680, BME280, INA226, PCF8523) hängen am selben Bus
- `espressif/i2c_bus` Komponente, thread-safe

## GPIO-Besonderheiten

### Display 5V (GPIO7, N-Kanal-Treiber Q5 2N7002 vor P-MOSFET Q4 AO3401A)
Seit der neuen Platine (Commit `760b25e`) treibt GPIO7 nur noch das Gate von Q5:
```
HIGH (OUTPUT) → Q5 leitet → Q4-Gate LOW → Q4 leitet → Display EIN
LOW / Hi-Z    → Q5 sperrt → R21 (100k) zieht Q4-Gate auf 5V → Display AUS
```
OUTPUT HIGH ist sicher (kein direkter Vgs-Querstrom mehr). Boot: `rtc_gpio_hold_dis()` + `rtc_gpio_deinit()`, dann OUTPUT LOW; Deep-Sleep: LOW + `rtc_gpio_hold_en()`. Quelle: `sensor_config.h` (`SENSOR_DISPLAY_PWR_GPIO`), `app_main.c`, `deep_sleep.c`.

### 12V Bordnetz (bistabiles Relais)
- EIN: Puls `SENSOR_PWR_12V_PULSE_MS` (500 ms) auf GPIO11, dann LOW
- AUS: Puls `SENSOR_PWR_12V_PULSE_MS` auf GPIO12, dann LOW
- Beide Pins **immer** LOW im Ruhezustand! Eine Konstante für alle 3 Puls-Stellen
  (app_main.c Boot, espnow_modem.c Laufzeit, deep_sleep.c AUS).
- **12V-Sense GPIO14** (`SENSOR_PWR_12V_SENSE_*`): `ctrl.pwr_on` meldet den gemessenen
  Bordnetzzustand (Display-Button = Realität), Grace `SENSOR_PWR_12V_SENSE_GRACE_MS`
  nach Puls. Polarität `SENSOR_PWR_12V_SENSE_ACTIVE_HIGH` — TODO(hardware): messen.
- ⚠️ **Bekannter Hardware-Bug (2026-09), siehe `progress.md`:** Treiber Q1/Q2 für J7
  „12V EIN"/„12V AUS" sind IRLML2244 (P-Kanal, High-Side, schaltet +12V), der EBL
  erwartet aber Pull-to-GND (Low-Side). Relais schaltet dadurch nicht — unabhängig
  von Pulslänge/Sense. Fix: Q1/Q2 → IRLML6244 (N-Kanal, gleiches Pinout).

### Multimedia (GPIO13, N-Kanal MOSFET)
- HIGH = EIN, LOW = AUS
- Beim Boot auf LOW setzen (kein passiver Pulldown in Hardware)

### ESP-NOW (ersetzt RS485 seit 2026-06)
- Max. 250 Byte pro Frame (`WOMO_ESPNOW_MAX_PAYLOAD`), Kanal = WLAN-Kanal, sonst `WOMO_ESPNOW_CHANNEL_FALLBACK`
- TX-Leistung auf 10 dBm begrenzt (Link im Fahrzeug < 1 m)
- GPIO8 (ehem. RS485-DE, `SENSOR_ESPNOW_DE_LEGACY_GPIO`) wird im Deep-Sleep LOW gehalten

## Deep Sleep / Touch-Wakeup
- Wakeup-Pin: GPIO6 = TOUCH_PAD_NUM6 (kapazitiv, RTC-fähig)
- Schwellwert: 10% über Baseline

## NVS-Namespaces

| Namespace | Keys | Inhalt |
|---|---|---|
| `bno055` | `calib`, `pr_zero` | IMU-Kalibrierung, Pressure-Zero |
| `bme680` | `press_hist` | Luftdruckhistorie (96 Samples / 24h) |
| `bsec` | `state_in_<addr>_v<n>` | BSEC IAQ State (z.B. `state_in_76_v3`) |
| `gas_state` | `gas` | Gasverbrauch (Raten, Voll-Referenzen, letzter Wert mit Uhrzeit + Quelle für Lücken-Überbrückung; altes 48-Byte-Layout wird migriert) |
| `tank_state` | `tank1`, `tank2` | Tank-Verbrauchsraten |
| `wifi_cfg` | `ssid`, `pass` | WLAN-Zugangsdaten (RUTX11) |
| `ruuvi_cfg` | `indoor_mac`, `outdoor_mac` | MAC-Filter Ruuvi |
| `gasbee_cfg` | `bottle_a_mac`, `bottle_b_mac` | Zuordnung GasBee → Flasche A/B |

## Log-Level
Zentral in `sensor_config.h` (`LOG_LEVEL_*` Defines), kein Recompile der Module nötig.
Default: meiste Module auf `WARN`, `sensor_main` und `i2c_bus` auf `INFO`.
