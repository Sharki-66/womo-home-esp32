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

### Display 5V (GPIO7, P-MOSFET Q4 AO3401A) ⚠️
```
LOW (OUTPUT)  → Gate ≈ 0V → FET leitet → Display EIN ✅
Hi-Z (INPUT)  → R21 (100k) zieht Gate auf 5V → Vgs≈0V → FET sperrt → Display AUS ✅
HIGH (OUTPUT) → VERBOTEN! Vgs = 3,3−5 = −1,7V → FET leitet → ~300mA Querstrom ❌
```
Nach Deep-Sleep: `rtc_gpio_hold_dis()` + `rtc_gpio_deinit()` + `gpio_config(INPUT)` aufrufen.

### 12V Bordnetz (bistabiles Relais)
- EIN: 100ms-Puls auf GPIO11, dann LOW
- AUS: 100ms-Puls auf GPIO12, dann LOW
- Beide Pins **immer** LOW im Ruhezustand!

### Multimedia (GPIO13, N-Kanal MOSFET)
- HIGH = EIN, LOW = AUS
- Beim Boot auf LOW setzen (kein passiver Pulldown in Hardware)

### RS485 Half-Duplex
- 57600 Baud (115200 verursacht Framing-Fehler durch DE-Toggle)
- DE/RTS = GPIO8, automatisch von UART-Treiber gesteuert
- TX-Sperre 150ms nach letztem RX (Empfangsfenster sicherstellen)
- Mindestabstand zwischen zwei TX: 80ms

## Deep Sleep / Touch-Wakeup
- Wakeup-Pin: GPIO6 = TOUCH_PAD_NUM6 (kapazitiv, RTC-fähig)
- Schwellwert: 10% über Baseline
- Nach Wakeup: `gpio_hold_dis(SENSOR_RS485_DE_GPIO)` aufrufen

## NVS-Namespaces

| Namespace | Keys | Inhalt |
|---|---|---|
| `bno055` | `calib`, `pr_zero` | IMU-Kalibrierung, Pressure-Zero |
| `bme680` | `press_hist` | Luftdruckhistorie (96 Samples / 24h) |
| `bsec` | `state_in_<addr>_v<n>` | BSEC IAQ State (z.B. `state_in_76_v3`) |
| `gas_state` | `gas` | Gasverbrauch (Raten, Tare, Flaschentausch) |
| `tank_state` | `tank1`, `tank2` | Tank-Verbrauchsraten |
| `wifi_cfg` | `ssid`, `pass` | WLAN-Zugangsdaten (RUTX11) |

## Log-Level
Zentral in `sensor_config.h` (`LOG_LEVEL_*` Defines), kein Recompile der Module nötig.
Default: meiste Module auf `WARN`, `sensor_main` und `i2c_bus` auf `INFO`.
