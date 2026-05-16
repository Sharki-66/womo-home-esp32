# Tech Context — WoMoHome Display

## Hardware
- **MCU**: ESP32-S3-Touch-LCD-7 (ESP32-S3, Dual-Core 240 MHz, 16 MB Flash, 8 MB PSRAM)
- **Display**: RGB-Interface 800×480 px, 16 MHz PCLK
- **Touch**: GT911 (kapazitiv, I2C), Bibliothek `esp_lcd_touch_gt911`
- **RS485**: UART2, 57600 8N1, Half-Duplex (DE/RTS-Pin)

## Stack

| Komponente | Version / Details |
|---|---|
| ESP-IDF | v5.5.2 (`source /home/hajo/esp/v5.5.2/esp-idf/export.sh`) |
| LVGL | v9.2.0 (Managed Component `lvgl/lvgl: ^9.2.0`) |
| esp_lvgl_port | v2 (`espressif/esp_lvgl_port: ^2`) |
| Sprache | C (C17) |
| Build | CMake via `idf.py build` |

## LVGL-Konfiguration

```
Display:     RGB 800×480, 2 Framebuffer in PSRAM (Direct Mode + Avoid-Tearing)
Bounce-Buffer: 4 Zeilen × 800 px × 2 Byte × 2 = 12,8 KB DMA-SRAM
LVGL-Task:   Core 1, Stack 16 KB in PSRAM, Priorität 4
Fonts:       lv_font_montserrat_12/14/16/20/24/28/32/48 (alle aktiviert)
```

## Speicherstrategie

- **PSRAM**: Framebuffer, LVGL-Heap, Icon-Bilddaten (PNG-Decode-Buffer), LVGL-Task-Stack
- **SRAM**: DMA Bounce-Buffer, kritische ISR-Pfade, kleine statische Puffer
- Große `malloc`-Aufrufe → `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`

## Netzwerk-Komponenten

| Modul | Funktion |
|---|---|
| `womo_wifi` | ESP32-eigene STA-Verbindung, Scan, Known-Network-Pool (NVS `wifi_cfg`) |
| `womo_router_uci` | RUTX11 HTTP/UCI JSON-RPC: WiFi-, LTE-, AP-Status, Steuerung |
| `womo_buzzer_http` | HTTP-Request an Sensorboard-Buzzer-Endpoint |

## Build & Flash

```bash
# Build (nur erklären, nicht ausführen — User startet über VS Code Task)
source /home/hajo/esp/v5.5.2/esp-idf/export.sh
cd firmware/display
idf.py build
```

Flash und Monitor: VS Code Tasks „Flash Display" / „Monitor Display".

## RS485-Protokoll (Display-Seite)

- **Physik**: UART2, 57600 8N1, Half-Duplex (115200 verursacht Framing-Fehler durch DE-Toggle)
- **Format**: CRLF-terminierte ASCII-JSON-Zeilen
- **Empfang**: Topic-basiert, Display merged Teilupdates in `s_latest_data`
- **Senden**: Display schickt `cmd`-Objekte (z.B. `display_ready`, `pwr_12v_on`, `radio_on`)
- **ACK**: Display quittiert per `{ "type":"ack", "ack":<seq>, "status":"ok"|"err" }`

## Konfigurationsdateien

| Datei | Inhalt |
|---|---|
| `main/gui/womo_thresholds.h` | Konfigurierbare Schwellwerte (NVS-gespeichert) |
| `main/gui/womo_theme.h` | `womo_status_level_t` Enum, Theme-Farben |
| `firmware/shared/womo_config.h` | Geteilte Konstanten (RS485-Baudrate, Topics) |
| `sdkconfig` | ESP-IDF Konfiguration (PSRAM, UART, WiFi-Stack) |
