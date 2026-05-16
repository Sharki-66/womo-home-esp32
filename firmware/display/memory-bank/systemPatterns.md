# System Patterns — WoMoHome Display

## High-Level Architektur

```
RS485-UART  ──►  rs485_parser_task  ──►  s_latest_data (Mutex)
                                               │
                                    ui_update_timer_cb (500 ms)
                                               │
                          ┌────────────────────┼────────────────────┐
                          ▼                    ▼                    ▼
                    Widget-Update        Error-Stack          Router-Overlay
                    (bat/tank/gas…)      (6 Quellen)          (WiFi-Pill)

router_poll_task (15 s)  ──►  s_router_ap / s_router_wifi / s_router_lte
                                               │
                                    ui_update_timer_cb liest s_router_reachable
```

## Modulstruktur

| Verzeichnis | Inhalt |
|---|---|
| `main/main.c` | Zentrales Orchestrierung (~3500 Zeilen): Tasks, Timer, UI-Build, Callbacks |
| `main/gui/` | LVGL-Modale und Widgets: `womo_connectivity_modal`, `womo_settings_modal`, `womo_weather`, `womo_thresholds`, `womo_theme` |
| `main/network/` | HTTP-Clients: `womo_wifi` (ESP32-STA), `womo_router_uci` (RUTX11), `womo_buzzer_http` |
| `main/rs485/` | Parser und Sender für das RS485-Protokoll |

## Datenpfad RS485 → GUI

1. Sensorboard sendet JSON-Topics über RS485 (57600 8N1)
2. `rs485_parser_task` parsed Topics und schreibt in `s_latest_data` (geschützt per Mutex)
3. `ui_update_timer_cb` läuft alle 500 ms auf dem LVGL-Task (Core 1)
4. Timer liest `s_latest_data` snapshot, aktualisiert alle Widgets und Error-Stack
5. Router-Daten kommen separat aus `router_poll_task` (alle 15 s) → `s_router_reachable`, `s_router_ap`, `s_router_wifi`, `s_router_lte`

## Konnektivitäts-Logik

- `s_router_reachable`: **nur** `true` wenn `womo_router_get_wifi_status()` (HTTP/UCI) `ESP_OK` zurückgibt. LTE- und AP-Abfragen liefern `ESP_OK` auch ohne Router → nicht für Reachability verwenden.
- WiFi/LTE-Pill-Button: zeigt AP-Status (grüne Mittelsektion = ESP32 mit WLAN verbunden), WiFi-Icon links, LTE-Icon rechts
- `update_router_btn_overlay()`: färbt `wifi_ap_section` grün/grau, wird in `ui_update_timer_cb` aufgerufen

## Error-Stack (6 Quellen)

Quellen: Gas, Frischwasser, Grauwasser, Batterie, RS485-Timeout, IAQ (Luftqualität)

```c
sensor_err_state_t:
  .current   – aktuell aus Schwellwert-Vergleich
  .latched   – sticky (bleibt bis ack + 3 s stabil OK)
  .acked     – durch Touch auf Status-Label gesetzt
  .ok_streak – Zähler für Debounce (Schwelle: 6 = 3 s)
```

- Widget-Farben folgen `.current` (Echtzeit)
- Buzzer löst auf steigender Flanke von unacknowledged `stack_max` aus

## Patterns

- **Snapshot-Prinzip**: UI-Callbacks lesen immer einen vollständigen Snapshot, nie Teilfelder direkt aus geteiltem State
- **Modale**: `womo_*_modal_show()` erzeugt Modal neu, `_refresh()` aktualisiert es, `_is_open()` für Guard
- **NVS-Namespaces**: `womo_ap_cfg` (AP-Credentials), `rtr_wifi` (Router-WiFi-Pool, max. 20 MRU), `womo_cfg` (Schwellwerte)
- **LVGL-Thread-Safety**: Alle LVGL-Aufrufe nur im LVGL-Task (Core 1) oder unter `lv_lock()`

## Patterns vermeiden

- Keine LVGL-Aufrufe aus `rs485_parser_task` oder `router_poll_task` direkt
- Keine blockierenden HTTP-Calls im LVGL-Task
- Kein Polling in Busy-Loops — immer Tasks mit `vTaskDelay` oder Event-Groups
