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
- **Wetter**: Aktuelles WMO-Symbol + Temperatur (Open-Meteo), Tipp auf Icon → 5-Tage-Vorhersage-Modal; Auto-Popup bei Unwetterwarnungen (Meteoalarm)
- **Konnektivität**: Modal mit AP-Status, WLAN-Scan/Connect, LTE Ein/Aus (Router UCI)
- **Steuerung**: 12V Bordnetz + Multimedia per RS485-Kommandos ans Sensorboard
- **Tag/Nacht**: Automatischer Themenwechsel, Backlight-Steuerung
- **Optionales Buzzer-Studio (HTTP)**: Live-Tonvorschau am Display-Piezo via `/buzzer_studio.html` (per `#define` schaltbar, Seite ist im Firmware-Image eingebettet)
- **Web-Dashboard (HTTP, Port 8080)**: Spiegelt das Display-Layout 1:1 als 800×480-Webseite. Alle Sensordaten + Router-Status werden alle 5 Sekunden vom Endpunkt `/api/status` abgerufen. Erreichbar unter `http://<display-ip>:8080/`

## Buzzer-Studio (Optional)

- Schalter: `WOMO_ENABLE_BUZZER_STUDIO_HTTP` in `main/display_config.h`
- `1`: HTTP-Endpunkte + Studio-Seite aktiv
- `0`: Feature komplett auskompiliert
- Endpunkte:
    - `GET /api/buzzer/ping`
    - `POST /api/buzzer/play` (`preset` oder `notes[]`)
    - `POST /api/buzzer/stop`
- Studio-Seite: `http://<display-ip>/buzzer_studio.html`

## Web-Dashboard (Port 8080)

Spiegelt das Display-Layout **1:1** als 800×480-Webseite im Browser.

- Startet automatisch beim Boot (kein Konfigurationsschalter nötig)
- Port: **8080** (unabhängig vom Buzzer-Studio auf Port 80)
- Immer aktiv, sobald WiFi verbunden
- Endpunkte:
    - `GET /` oder `GET /display` → `womo_display.html` (800×480 Dashboard)
    - `GET /api/status` → JSON-Snapshot mit Sensordaten, Router-Status, GPS, Uhrzeit, Theme
- URL: `http://<display-ip>:8080/`
- Das Dashboard pollt `/api/status` alle **5 Sekunden** und aktualisiert alle Widgets live
- Tag/Nacht-Theme wird automatisch per Uhrzeit (07–20 Uhr = Tag) umgeschaltet
- Ducato-Silhouette als CSS/SVG-Overlay, Farben identisch mit dem LVGL-Display

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
│   ├── womo_forecast_modal     5-Tage-Vorhersage Modal (Open-Meteo daily)
│   └── womo_connectivity_modal WLAN/LTE/AP-Steuerung
├── hardware/
│   ├── waveshare_rgb_lcd_port  LCD + Touch + CH422G Init
│   └── lvgl_port               LVGL Flush, Buffer, Task, Indev
├── network/
│   ├── womo_wifi               STA-Verbindung (RUTX11)
│   ├── womo_weather_http       Open-Meteo API (aktuell + 5-Tage daily)
│   ├── womo_geocode            Nominatim Reverse Geocoding
│   ├── womo_meteoalarm         Meteoalarm CAP-Feed
│   ├── womo_router_uci         RUTX11 UCI JSON-RPC
│   ├── womo_http_mutex         TLS-Session-Serialisierung
│   ├── womo_buzzer_http        Optionales Buzzer-Studio HTTP-API + Seite (Port 80)
│   ├── womo_display_http       Web-Dashboard HTTP-Server (Port 8080, /api/status)
│   └── isrg_root_x1_pem.h     Let's Encrypt Root-Zertifikat
├── rs485/
│   └── womo_rs485_display      RS485-Empfang, JSON-Parsing, Merge-Snapshot
├── storage/
│   └── womo_sd                 SD-Karten-Zugriff (Hintergrundbilder)
└── time/
    └── womo_time               SNTP + RTC-Sync

spiffs/
├── buzzer_studio.html          Quell-Datei der eingebetteten Web-UI fuer Live-Piezo-Test am Display
└── womo_display.html           800×480 Web-Dashboard (spiegelt Display-Layout, /api/status-Polling)
```
