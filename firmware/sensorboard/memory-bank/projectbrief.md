# Project Brief – WoMoHome Sensorboard

## Ziel
Das Sensorboard ist das sensorische Herzstück des digitalen Wohnmobil-Cockpits. Es erfasst alle analogen und digitalen Messwerte, steuert das 12V-Bordnetz und leitet Daten per RS485 an das Display weiter. Zusätzlich bietet es ein WiFi-Web-Dashboard für direkten Browserzugriff.

## MCU
**Heemol ESP32-S3-WROOM-1 N16R8 DevKitC-1**
- 16 MB Flash, 8 MB Octal-PSRAM (intern, GPIO35–37 gesperrt!)
- Betrieb: 3,3V, 12V Bordnetz per bistabilem Relais

## Scope

| Aufgabe | Status |
|---|---|
| Sensordaten erfassen (Klima, IMU, Gas, Batterie, Tank, Strom) | ✅ |
| RS485 → Display (JSON Topic-basiert, v2) | ✅ |
| Steuerung 12V Bordnetz + Multimedia-Relais | ✅ |
| Web-Dashboard (HTTP, SPIFFS, mDNS) | ✅ (Feature-Branch) |
| GasBee BLE-Client (NimBLE Central, Auto-Reconnect) | ✅ |
| Deep-Sleep + Touch-Wakeup | ✅ |
| NTP + RTC-Sync (PCF8523) | ✅ |
| INA226 Strom-/Leistungsmessung Hauptleitung | ✅ (neu) |
| Original WoMo-Tanksensoren (4-Stufen Reed-Schwimmer) | ❌ TODO |
| NVS-Layer für Kalibrierung (HX711/Bat/Tank per Web-UI) | ❌ TODO |

## Hardware-Übersicht

| Komponente | Beschreibung | Bus / Pin |
|---|---|---|
| IMU | BNO055 – Lage, Kompass, Kalibrierung | I2C 0x28 |
| Klima innen | BME680 – Temp, Feuchte, Druck, IAQ (BSEC) | I2C 0x76 |
| Klima außen | BME280 / BME260 – Temp, Feuchte, Druck, Drucktrend | I2C 0x77 |
| Gasfüllstand | HX711 – 2× Wägezelle (Kanal A + B) | DOUT=GPIO47, SCK=GPIO45 |
| GasBee | BLE-Gaswaage (ESP32-C3), NimBLE Central | BLE |
| Strom/Leistung | INA226 – Vbus 0–36V, ±81,92mV Shunt | I2C 0x40 |
| Batterien | 2× ADC + Spannungsteiler | Kfz=GPIO4, Board=GPIO5 |
| Tanks | 2× Votronic kapazitiv (0–1V) | Frisch=GPIO1, Grau=GPIO2 |
| RTC | PCF8523 – Echtzeituhr | I2C 0x68 |
| RS485 | MAX3485, Half-Duplex, 57600 8N1 | TX=GPIO9, RX=GPIO10, DE=GPIO8 |
| I2C-Bus | 100 kHz, interne Pull-ups | SDA=GPIO16, SCL=GPIO15 |
| RGB-LED | WS2812 onboard | GPIO48 |

## Eingebettetes System
Kein RTOS-Shell, keine Konsole. Alle Konfiguration über `sensor_config.h` und NVS. Flashing über VS Code Tasks.
