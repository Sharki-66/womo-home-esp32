# WoMoHome Display Firmware

ESP-IDF v5.5.2 · LVGL v8.4 · ESP32-S3 (Waveshare ESP32-S3-Touch-LCD-7)

## Hardware

| Komponente | Beschreibung |
|---|---|
| **Board** | Waveshare ESP32-S3-Touch-LCD-7 (800×480, RGB565) |
| **MCU** | ESP32-S3-WROOM-1, 16 MB Flash, 8 MB PSRAM (Octal) |
| **Display** | 7" IPS, ST7701 RGB-Interface, Double-Buffer + Direct-Mode |
| **Touch** | GT911 (I2C), CH422G GPIO-Expander für Backlight/Reset |
| **RS485** | RX vom Sensorboard (JSON, 115200 8N1) |
| **SD-Karte** | Hintergrundbilder, optional |

## Funktionen

- **Hauptscreen**: Temperatur (innen/außen), Luftdruck + Trend-Pfeil, Feuchte, Kompass, Neigung
- **Batterien**: 2× Balken mit Spannung, Warnung bei Unterspannung
- **Tanks**: Frischwasser + Grauwasser mit Füllstand, Verbrauchsrate, Restdauer
- **Gas**: 2× Flaschengewicht, aktiver Verbrauch, Restdauer
- **Wetter**: Aktuelles WMO-Symbol + Temperatur (Open-Meteo), Unwetterwarnungen (Meteoalarm)
- **Konnektivität**: Modal mit AP-Status, WLAN-Scan/Connect, LTE Ein/Aus (Router UCI)
- **Steuerung**: 12V Bordnetz + Multimedia per RS485-Kommandos ans Sensorboard
- **Tag/Nacht**: Automatischer Themenwechsel, Backlight-Steuerung

## Externe Anbindungen

| Dienst | Zweck | TLS |
|---|---|---|
| Open-Meteo | Aktuelles Wetter | ISRG Root X1 (PEM) |
| Nominatim | Reverse Geocoding | ISRG Root X1 (PEM) |
| Meteoalarm | Unwetterwarnungen | ESP TLS Bundle (HARICA) |
| Teltonika RUTX11 | WLAN/LTE/GPS-Steuerung | HTTP (LAN) |

## Projektstruktur

```
main/
├── main.c                      Startup, GUI-Update-Loop, Task-Orchestrierung
├── gui/
│   ├── womo_theme              Tag/Nacht-Umschaltung
│   ├── womo_locale             Deutsche Lokalisierung (Datum, Wochentage)
│   ├── womo_fonts_german       6× Montserrat mit dt. Sonderzeichen + Icons
│   ├── womo_battery            Batterie-Widget
│   ├── womo_tank               Tank-Widget
│   ├── womo_gas_bottle         Gas-Flaschen-Widget
│   ├── womo_weather            Wetter-Icon-Mapping (WMO → LVGL)
│   └── womo_connectivity_modal WLAN/LTE/AP-Steuerung
├── hardware/
│   ├── waveshare_rgb_lcd_port  LCD + Touch + CH422G Init
│   └── lvgl_port               LVGL Flush, Buffer, Task, Indev
├── network/
│   ├── womo_wifi               STA-Verbindung (RUTX11)
│   ├── womo_weather_http       Open-Meteo API
│   ├── womo_geocode            Nominatim Reverse Geocoding
│   ├── womo_meteoalarm         Meteoalarm CAP-Feed
│   ├── womo_router_uci         RUTX11 UCI JSON-RPC
│   ├── womo_http_mutex         TLS-Session-Serialisierung
│   └── isrg_root_x1_pem.h     Let's Encrypt Root-Zertifikat
├── rs485/
│   └── womo_rs485_display      RS485-Empfang, JSON-Parsing, Merge-Snapshot
├── storage/
│   └── womo_sd                 SD-Karten-Zugriff (Hintergrundbilder)
└── time/
    └── womo_time               SNTP + RTC-Sync
```
