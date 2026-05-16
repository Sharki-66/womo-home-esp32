# Active Context – WoMoHome Sensorboard

*Stand: 2026-05-16*

## Aktueller Branch
`feature/sensorboard-webdashboard`

## Laufende Arbeiten

### Web-Dashboard (in Review)
- SPIFFS-Dashboard (`spiffs/dashboard.html`) mit vollständiger Sensoranzeige und Steuerung
- HTTP-Endpunkte: `/`, `/dashboard.html`, `/horizon.html`, `/api/status`, `/api/data`, `/api/imu`, `/api/cmd`
- mDNS: `Womo-Sensor.local`

### INA226 (neu integriert)
- `main/sensors/ina226_sensor.c` und `.h` sind neu (untracked)
- Library: `k0i05/esp_ina226` v1.2.7
- `elec`-Topic (5s Intervall) sendet `v_bus`, `i_a`, `p_w`, `v_shunt_mv`
- **TODO:** `SENSOR_INA226_SHUNT_MOHM` nach Hardwareeinbau anpassen (aktuell: 1 mΩ Platzhalter)

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

1. **INA226 Shunt-Widerstand:** Realer Einbauwert muss nach Hardwaremontage gemessen und in `sensor_config.h` eingetragen werden (`SENSOR_INA226_SHUNT_MOHM`)
2. **Original WoMo-Tanksensoren:** 4-Stufen Reed-Schwimmer (5 Leitungen) noch nicht implementiert. GPIO17/18 vorgesehen, Widerstandsnetzwerk-Schema in `sensor_config.h` dokumentiert
3. **Kalibrierungs-Web-UI:** HX711/Batterie/Tank-Kalibrierung aktuell hardcodiert in `sensor_config.h`, NVS-Layer + Web-UI fehlt noch

## Letzte Kalibrierungen
- **HX711:** 2026-02-13, Referenz 5×3,6 kg = 18,0 kg
  - Kanal A: OFFSET=-275500, SCALE=0.04004
  - Kanal B: OFFSET=77056, SCALE=-0.15674
- **Batterie ADC:** 2026-05-03 nach Platinetausch (Q3 defekt)
  - SCALE=1.354, OFFSET=0 (beide Kanäle)
  - Referenz: Multimeter 13,0V, Anzeige vorher 9,8V
- **BME680 Temp-Offset:** 4,0°C (Board-Eigenerwärmung ESP32-S3)
- **BSEC State Version:** 3 (bei Sensortausch hochzählen!)
