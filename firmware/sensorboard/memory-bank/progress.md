# Progress – WoMoHome Sensorboard

*Stand: 2026-05-16*

## Fertig (produktiv)

| Feature | Anmerkung |
|---|---|
| RS485-Protokoll v2 (Topic-basiert) | Round-Robin, ACK, Half-Duplex-Timing |
| BME680 (innen, 0x76) + BSEC | IAQ, CO2eq, VOCeq, Drucktrend |
| BME280/BME260 (außen, 0x77) | Drucktrend (1h/3h), 96-Sample-History |
| BNO055 IMU | Euler, Heading, Kalibrierungsstatus, NVS |
| HX711 Wägezellen (Gas A+B) | 3-Sample-Mittel, EMA-Rate, NVS-Persistenz |
| GasBee BLE Client | NimBLE Central, Auto-Reconnect, Gasverbrauchslogik |
| INA226 Strom/Leistung | Neu integriert (feature-Branch), `elec`-Topic |
| Analog ADC (Bat + Tank) | Median-of-3, Kalibrierung, NC-Erkennung |
| 12V Bordnetz Steuerung | Bistabiles Relais, Puls-Logik |
| Multimedia-Steuerung | N-MOSFET GPIO13 |
| Display 5V P-MOSFET | Sicherheitslogik (Hi-Z = AUS, nie HIGH!) |
| Deep-Sleep + Touch-Wakeup | GPIO6 kapazitiv, RTC-fähig |
| PCF8523 RTC | I2C 0x68, NTP-Sync nach WiFi-Connect |
| WiFi STA (RUTX11) | NVS-Credentials, mDNS, Retry-Logik |
| HTTP Web-Dashboard | `/api/*`, SPIFFS, mDNS `Womo-Sensor.local` |
| WiFi-Credential-Request via RS485 | `wifi_pass_request` → Display zeigt Dialog |

## In Arbeit / Offen

| Feature | Status | Nächste Schritte |
|---|---|---|
| INA226 Shunt-Kalibrierung | Hardware nicht eingebaut | Shunt messen → `SENSOR_INA226_SHUNT_MOHM` setzen |
| Original WoMo-Tanksensoren | Planung | GPIO17/18, Widerstandsnetzwerk, 4-Stufen Reed |
| Kalibrierungs-Web-UI | Konzept | NVS-Layer + Web-Formular für HX711/Bat/Tank |
| Web-Dashboard mergen | Feature-Branch | Review → PR → main |

## Bekannte Probleme / Einschränkungen

| Problem | Beschreibung | Workaround |
|---|---|---|
| 115200 Baud RS485 | Framing-Fehler durch DE-Toggle | Fix: 57600 Baud (aktuell korrekt) |
| GPIO7 HIGH verboten | P-MOSFET Querstrom ~300mA | Nur OUTPUT LOW oder INPUT (Hi-Z) |
| GPIO35-37 gesperrt | Octal-PSRAM intern (N16R8) | Pins nicht verwenden |
| INA226 Shunt-Wert | Platzhalter 1mΩ | Nach Einbau messen und anpassen |
| BSEC State nach Sensortausch | Alter State macht IAQ-Kalibrierung kaputt | `SENSOR_BME680_BSEC_STATE_VERSION` hochzählen |

## Versionsverlauf (Highlights)

| Datum | Änderung |
|---|---|
| 2026-05-16 | INA226 integriert, Web-Dashboard, Memory-Bank angelegt |
| 2026-05-03 | Batterie-ADC Neukalibrierung nach Q3-Platinentausch |
| 2026-02-13 | HX711 Kalibrierung (18kg Referenz) |
| 2026-02 | RS485-Protokoll v2 (Topic-basiert), Deep-Sleep, Touch-Wakeup |
