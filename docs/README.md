# Dokumentation – WoMo Home ESP32

## Hardware

| Dokument | Beschreibung |
|----------|-------------|
| [ESP32-S3-Touch-LCD-7](hardware/esp32-s3-touch-lcd-7.md) | Waveshare Display-Board (800×480, GT911 Touch) |
| [LCD-Spezifikationen](hardware/WAVESHARE_ESP32_S3_LCD_SPECS.md) | Display-Panel, RGB-Interface, Timing |
| [ESP32-S3 Modul-Specs](hardware/WAVESHARE_ESP32_S3_SPECS.md) | ESP32-S3-WROOM-1, Pinout, PSRAM |
| [Verifizierte Specs](hardware/WAVESHARE_ESP32_S3_VERIFIED_SPECS.md) | Aus Demo-Code bestätigte Werte |

## Firmware

| Dokument | Beschreibung |
|----------|-------------|
| [Sensorboard README](../firmware/sensorboard/README.md) | Hardware, Pins, RS485-Protokoll, NVS, Projektstruktur |
| [Display README](../firmware/display/README.md) | Hardware, Funktionen, Externe APIs, Projektstruktur |

## System

| Komponente | Hardware | Funktion |
|---|---|---|
| **Display** | Waveshare ESP32-S3-Touch-LCD-7 | LVGL-GUI, Touch, RS485-Empfang, Router-Anbindung |
| **Sensorboard** | Heemol ESP32-S3 N16R8 DevKitC-1 | Sensoren (BME680, BNO055, HX711, ADC), RS485-Sender |
| **Router** | Teltonika RUTX11 | WLAN, LTE, GNSS, Hotspot |

Kommunikation: Display ↔ Sensorboard per RS485 (JSON, 115200 8N1).
Router wird vom Display per HTTP/UCI angesprochen.
