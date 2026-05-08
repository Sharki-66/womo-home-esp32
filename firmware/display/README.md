# WoMoHome Display Firmware

ESP-IDF v5.5.2 · LVGL v9.5 · ESP32-S3 (Waveshare ESP32-S3-Touch-LCD-7)

## Hardware

| Komponente | Beschreibung |
|---|---|
| **Board** | Waveshare ESP32-S3-Touch-LCD-7 (800×480, RGB565) |
| **MCU** | ESP32-S3-WROOM-1, 16 MB Flash, 8 MB PSRAM (Octal) |
| **Display** | 7" IPS, ST7701 RGB-Interface, Double-Buffer + Direct-Mode |
| **Touch** | GT911 (I2C), CH422G GPIO-Expander für Backlight/Reset |
| **RS485** | RX/TX Sensorboard (JSON, 57600 8N1, Half-Duplex) |
| **SD-Karte** | Hintergrundbilder, optional |

## Funktionen

- **Hauptscreen**: Temperatur (innen/außen), Luftdruck + Trend-Pfeil, Feuchte, Kompass, Neigung
- **Batterien**: 2× Balken mit Spannung, Warnung bei Unterspannung
- **Tanks**: Frischwasser + Grauwasser mit Füllstand, Verbrauchsrate, Restdauer
- **Gas**: 2× Flaschengewicht, aktiver Verbrauch, Restdauer
- **Wetter**: Aktuelles WMO-Symbol + Temperatur (Open-Meteo), Tipp auf Icon → 5-Tage-Vorhersage-Modal; Auto-Popup bei Unwetterwarnungen (Meteoalarm)
- **Sonnenauf-/-untergang**: Berechnung aus GPS-Position (womo_sun_calc), Anzeige im Hauptscreen
- **Konnektivität**: Modal mit AP-Status, WLAN-Scan/Connect, LTE Ein/Aus (Router UCI)
- **Router-LEDs**: Separate Statusanzeige für WLAN/LTE/GPS-Empfang des Routers
- **Steuerung**: 12V Bordnetz + Multimedia per RS485-Kommandos ans Sensorboard
- **Fehler-Management**: Fehler-Stack mit Latching, Quittierung per Touch, Buzzer-Alarm (Warn/Alarm-Ton bei neuen unquittier­ten Fehlern)
- **Einstellungs-Modal**: Schwellwerte für Tank/Batterie/Gas konfigurierbar, Speicherung in NVS
- **Screenshot**: Long-Press unten links → PNG auf SD-Karte
- **Tag/Nacht**: Automatischer Themenwechsel (Sonnenauf-/-untergang), Backlight-Steuerung

## Externe Anbindungen

| Dienst | Zweck | TLS |
|---|---|---|
| Open-Meteo | Aktuelles Wetter + 5-Tage-Vorhersage | ISRG Root X1 (PEM, eingebettet) |
| Nominatim | Reverse Geocoding | ISRG Root X1 (PEM, eingebettet) |
| Meteoalarm | Unwetterwarnungen | GEANT TLS RSA 1 (PEM, eingebettet) |
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
│   ├── womo_forecast_modal     5-Tage-Vorhersage Modal (Open-Meteo daily)
│   ├── womo_connectivity_modal WLAN/LTE/AP-Steuerung
│   ├── womo_router_leds_modal  Router-LED-Statusanzeige
│   ├── womo_settings_modal     Einstellungs-Modal (Schwellwerte, NVS)
│   └── womo_thresholds         Schwellwert-Verwaltung (NVS-Persistenz)
├── hardware/
│   ├── waveshare_rgb_lcd_port  LCD + Touch + CH422G Init
│   ├── lvgl_port               LVGL Flush, Buffer, Task, Indev
│   └── buzzer                  Piezo-Buzzer (Warn/Alarm-Töne)
├── network/
│   ├── womo_wifi               STA-Verbindung + NVS-Passwort-Speicher (RUTX11)
│   ├── womo_weather_http       Open-Meteo API (aktuell + 5-Tage daily)
│   ├── womo_geocode            Nominatim Reverse Geocoding
│   ├── womo_meteoalarm         Meteoalarm CAP-Feed
│   ├── womo_router_uci         RUTX11 UCI JSON-RPC
│   ├── womo_buzzer_http        HTTP-Endpunkt für externen Buzzer-Trigger
│   ├── womo_http_mutex         TLS-Session-Serialisierung
│   ├── isrg_root_x1_pem.h     Let's Encrypt Root-Zertifikat
│   └── harica_root_pem.h       GEANT TLS RSA 1 (Meteoalarm)
├── rs485/
│   └── womo_rs485_display      RS485-Empfang/Senden, JSON-Parsing, Merge-Snapshot
├── storage/
│   └── womo_sd                 SD-Karten-Zugriff (Hintergrundbilder, Screenshots)
└── time/
    ├── womo_time               SNTP + RTC-Sync
    └── womo_sun_calc           Sonnenauf-/-untergang aus GPS-Koordinaten
```
