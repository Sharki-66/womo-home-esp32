# Active Context – WoMoHome Sensorboard

*Stand: 2026-05-18*

## Aktueller Branch
`feature/sensorboard-webdashboard`

## Laufende Arbeiten

### Web-Dashboard (in Review)
- SPIFFS-Dashboard (`spiffs/dashboard.html`) mit vollständiger Sensoranzeige und Steuerung
- HTTP-Endpunkte: `/`, `/dashboard.html`, `/horizon.html`, `/api/status`, `/api/data`, `/api/imu`, `/api/cmd`
- mDNS: `Womo-Sensor.local`

### INA226 (kalibriert 2026-05-18)
- `main/sensors/ina226_sensor.c` und `.h`
- Library: `k0i05/esp_ina226` v1.2.7
- `elec`-Topic (5s Intervall) sendet `v_bus`, `i_a`, `p_w`, `v_shunt_mv`
- Shunt: 10 mΩ, MAX_CURRENT: 3 A (Display+Board inkl. Relais, LSB=91 µA)
- Misst NUR Display/Sensorboard-Stromkreis, nicht Fahrzeug-Hauptleitung

## Geänderte Dateien (nicht committed)

| Datei | Änderung |
|---|---|
| `main/CMakeLists.txt` | INA226-Sensor hinzugefügt |
| `main/app_main.c` | `ina226_app_start()` integriert |
| `main/idf_component.yml` | `k0i05/esp_ina226` Dependency |
| `main/network/rs485_modem.c` | `rs485_publish_elec()` Topic-Publisher |
| `main/sensor_config.h` | INA226-Konfiguration |
| `main/sensors/bme680_sensor.c` | Drucktrend-Verbesserungen |
| `spiffs/dashboard.html` | Web-Dashboard |
| `README.md` | INA226, Web-Dashboard, aktualisierte Topic-Tabelle |

## Offene Entscheidungen / TODOs

1. **BNO055 (IMU):** Antwortet nicht im I2C-Scan (erwartet 0x28/0x29, gefunden: 0x40/0x68/0x76/0x77) → Hardwarefehler auf neuer Platine klären
2. **Original WoMo-Tanksensoren:** 4-Stufen Reed-Schwimmer (5 Leitungen) noch nicht implementiert. GPIO17/18 vorgesehen, Widerstandsnetzwerk-Schema in `sensor_config.h` dokumentiert
3. **Kalibrierungs-Web-UI:** HX711/Batterie/Tank-Kalibrierung aktuell hardcodiert in `sensor_config.h`, NVS-Layer + Web-UI fehlt noch
4. **Deep Sleep:** Noch nicht getestet auf neuer Platine

## Letzte Kalibrierungen
- **HX711:** 2026-02-13, Referenz 5×3,6 kg = 18,0 kg
  - Kanal A: OFFSET=-275500, SCALE=0.04004
  - Kanal B: OFFSET=77056, SCALE=-0.15674
- **Batterie ADC:** 2026-05-18 neue Platine
  - SCALE=1.012, OFFSET=0 (beide Kanäle)
  - Referenz: Multimeter 13,0V, Anzeige vorher 17,4V (alter SCALE=1.354 war Q3-Defekt-Ausreißer)
- **BME680 Temp-Offset:** 4,0°C (Board-Eigenerwärmung ESP32-S3)
- **BSEC State Version:** 3 (bei Sensortausch hochzählen!)

## Hardware-Status neue Platine (2026-05-18)
- INA226 ✓ (10 mΩ, 3 A)
- BME680/BME280 ✓
- HX711 ✓
- GPIO7-Logik: HIGH=EIN (Q5+Q4 Treiberkette), nicht mehr invertiert
- BNO055 ✗ — nicht im I2C-Scan, Hardwarefehler offen
