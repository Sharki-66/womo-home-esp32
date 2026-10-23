# Tech Context — WoMoHome Display

## Hardware
- **MCU**: ESP32-S3-Touch-LCD-7 (ESP32-S3, Dual-Core 240 MHz, 16 MB Flash, 8 MB PSRAM)
- **Display**: RGB-Interface 800×480 px, 16 MHz PCLK
- **Touch**: GT911 (kapazitiv, I2C), Bibliothek `esp_lcd_touch_gt911`
- **Sensorboard-Link**: ESP-NOW (seit 2026-06, ersetzt RS485; RS485-Klemme des Boards ungenutzt)

## Stack

| Komponente | Version / Details |
|---|---|
| ESP-IDF | v5.5.2 (`source /home/hajo/esp/v5.5.2/esp-idf/export.sh`) |
| LVGL | v9.5.0 (Managed Component `lvgl/lvgl: ^9.2.0`, laut `dependencies.lock`) |
| esp_lvgl_port | v2.7.2 (`espressif/esp_lvgl_port: ^2`) |
| Sprache | C (C17) |
| Build | CMake via `idf.py build` |

## LVGL-Konfiguration

```
Display:     RGB 800×480, 2 Framebuffer in PSRAM (Direct Mode + Avoid-Tearing)
Bounce-Buffer: 4 Zeilen × 800 px × 2 Byte × 2 = 12,8 KB DMA-SRAM
LVGL-Task:   Core 1, Stack 16 KB intern (MALLOC_CAP_INTERNAL, lvgl_port.c), Priorität 4
Fonts:       LVGL lv_font_montserrat_12/14/16/20/24/26 (sdkconfig) + eigene *_german 12/14/16/20/24 + Material 16/24
```

## Speicherstrategie

- **PSRAM**: Framebuffer, LVGL-Heap, Icon-Bilddaten (PNG-Decode-Buffer), Stacks von `router_poll_task` u. a. (`xTaskCreateWithCaps`, kein NVS darin)
- **SRAM**: DMA Bounce-Buffer, kritische ISR-Pfade, kleine statische Puffer
- Große `malloc`-Aufrufe → `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`

## Netzwerk-Komponenten

| Modul | Funktion |
|---|---|
| `womo_wifi` | ESP32-eigene STA-Verbindung, Scan, Known-Network-Pool (NVS `wifi_cfg`) |
| `womo_router_uci` | RUTX11 HTTP/UCI JSON-RPC: WiFi-, LTE-, AP-Status, Steuerung; `datapass.json` (Port 8080, Router-WebUI-Tool) |
| `womo_buzzer_http` | HTTP-Server zum Testen von Buzzer-Tönen (nur mit `WOMO_ENABLE_BUZZER_STUDIO_HTTP`) |

## Build & Flash

```bash
# Build (nur erklären, nicht ausführen — User startet über VS Code Task)
source /home/hajo/esp/v5.5.2/esp-idf/export.sh
cd firmware/display
idf.py build
```

Flash und Monitor: VS Code Tasks „Flash Display" / „Monitor Display".

## ESP-NOW-Protokoll (Display-Seite)

- **Transport**: `main/espnow/womo_espnow.c`, ein JSON-Frame pro Paket, max. 250 Byte (`WOMO_ESPNOW_MAX_PAYLOAD`), Kanal = WLAN-Kanal, sonst `WOMO_ESPNOW_CHANNEL_FALLBACK`; Sensorboard-MAC wird beim ersten Paket gelernt
- **Heartbeat**: Display sendet alle 2 s `hb`, damit das Sensorboard den Peer findet (ersetzt `display_ready`)
- **Empfang**: Topic-basiert, Display merged Teilupdates in `s_latest_data`
- **Senden**: Display schickt `cmd`-Objekte (z. B. `pwr_12v_on`, `radio_on`, `wifi_config`) mit `need_ack:true`
- **ACK**: Sensorboard antwortet mit `cmd_ack` (`ack_seq`), Display quittiert per `{ "type":"ack", "ack_seq":<seq>, "status":"ok"|"err" }`
- Details: `firmware/display/CLAUDE.md`, Abschnitt „ESP-NOW-Schnittstelle"

## Konfigurationsdateien

| Datei | Inhalt |
|---|---|
| `main/gui/womo_thresholds.h` | Konfigurierbare Schwellwerte (NVS-gespeichert) |
| `main/gui/womo_tariff.h` | Tarif-Defaults (Vorlauf 3 Tage, Volumen 10 %), Rhythmus-Enum |
| `main/gui/womo_theme.h` | `womo_status_level_t` Enum, Theme-Farben |
| `firmware/shared/womo_config.h` | Geteilte Konstanten (ESP-NOW-Kanal/Payload, Topic-Intervalle, Kommando-Timeout) |
| `sdkconfig` | ESP-IDF Konfiguration (PSRAM, WiFi-Stack) |
