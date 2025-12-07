# Copilot Instructions

## Kommunikationsregeln
- Antworten immer auf Deutsch, inkl. Code-Reviews und Commit-Beschreibungen.
- Hardwarezugriffe ausschließlich über die vorhandenen `womo_*` bzw. komponentenspezifischen Bibliotheken (z. B. LVGL, esp_lcd, womo_rs485). Keine Eigenimplementierungen solange eine Bibliothek die Funktion bereits anbietet.
- Build, Flash und Monitor werden vom Nutzer gestartet (siehe VS Code Tasks "Build/Flash/Monitor Display Firmware"). Nur erklären, keine Befehle ausführen.

## Projektüberblick
- `firmware/` enthält die Display-Firmware für das ESP32-S3 RGB-Panel (ST7701 + GT911). `firmware-walter/` ist read-only Referenz und liefert RS485-Sensordaten.
- Zentrale Einstiegspunkte liegen in `main/main.c` (UI-Aufbau, Timer, LVGL-Lock/Unlock) und `main/rs485/womo_rs485_display.c` (UART1 + JSON → `womo_sensor_data_t`).
- Eigene GUI-Widgets, Themen und Lokalisierung befinden sich in `main/gui/*` (z. B. `womo_theme`, `womo_locale`, `womo_battery`). Netzwerk- und Zeitsynchronisation steckt unter `main/network` und `main/time`.

## Datenfluss & Threads
- RS485: `womo_rs485_display_init()` startet eine FreeRTOS-RX-Task, die JSON-Pakete parst und über `rs485_data_received()` (in `main.c`) Snapshots aktualisiert. Synchronisation erfolgt per Spinlock `display_data_spinlock`; neue Daten müssen darin geschützt werden.
- UI-Refresh: `ui_update_timer_cb()` läuft alle 500 ms, liest `latest_sensor_data` + Fehlzähler und aktualisiert LVGL-Widgets. Platzhalter werden erst nach `RS485_MISSING_THRESHOLD` Paketen ohne Daten gesetzt.
- Zeit/Theme: `time_update_timer_cb()` pflegt Zeit/Datum, zieht `womo_time` + `womo_locale` heran und ruft periodisch `womo_theme_update()` und `apply_text_theme_colors()`.

## UI- & Theme-Konventionen
- Vor jedem LVGL-Aufruf außerhalb von Timer/ISR `lvgl_port_lock(-1)` verwenden und danach `lvgl_port_unlock()`. Neue Widgets folgen dem Muster aus `app_main()` und nutzen Fonts aus `gui/fonts`.
- Texte stets über `womo_locale_get_string()` lokalisieren, wenn sie statisch angezeigt werden. Sensor-Platzhalter folgen den Konstanten in `main.c` (`PLACEHOLDER_*`).
- Theme-Farben und Transparenzen liegen in `womo_theme.c`; neue UI-Elemente sollten dortige Helper (`womo_theme_is_daytime`, `womo_theme_apply_to_screen`) berücksichtigen.

## Netzwerk, Speicher & externe Dienste
- WLAN-Handling läuft über `womo_wifi_*` (unter `main/network`). RSSI wird zu Prozentwerten mit `wifi_rssi_to_percent()` gemappt, LTE-Status via RS485 gepflegt – neue Verbraucher müssen beide Informationsquellen kombinieren wie `update_connectivity_label()`.
- SD-Karten-Zugriff erfolgt über `storage/womo_sd`. Für Hintergrundbilder immer `womo_sd_is_mounted()` prüfen (siehe `load_background_image`).
- Wetterdaten kommen über `womo_weather_http_start()` in `openweather_update_cb()`. Bei neuen Online-Quellen erst prüfen, ob bestehende Widgets (z. B. `womo_weather`) erweitert werden können.

## Build- & Debug-Workflow
- Zielplattform ist `esp32s3`; set-target wird im Repo bereits konfiguriert. Übliche Befehle: `idf.py build`, `idf.py -p COM5 flash`, `idf.py -p COM5 monitor` (vom Nutzer ausführen). Menüconfig: `idf.py menuconfig`.
- Component-Abhängigkeiten liegen im `managed_components/` Ordner und werden automatisch über `dependencies.lock` gepflegt. Keine manuellen Änderungen, stattdessen `idf.py add-dependency` nutzen, falls nötig.
- Für UART/RS485-Debugging bevorzugt `ESP_LOGI/W` nutzen, wie in `womo_rs485_display.c`; hohe Frequenzen → `ESP_LOGD` mit aktiviertem `CONFIG_LOG_DEFAULT_LEVEL_DEBUG`.

## Erweiterungen & Tests
- Neue Sensoren sollten ihre Daten über RS485 vom Walter beziehen; auf Display-Seite reichen Parser + UI-Erweiterung. Direkte Hardware-Neuanbindungen am Display vermeiden, solange Walter die Daten liefern kann.
- Beim Hinzufügen von UI-Elementen immer prüfen, ob `latest_missing_snapshot` genutzt werden muss, damit Platzhalter sinnvoll reagieren.
- Tests erfolgen hauptsächlich manuell auf Hardware; für reine Parser-Änderungen können `cJSON`-Unit-Tests in separaten Komponenten ergänzt werden, jedoch existieren derzeit keine automatisierten Tests im Repo.

> Rückmeldung willkommen: Fehlt ein Workflow oder eine Projektkonvention, die für neue Beiträge kritisch ist? Bitte kurz bescheid geben.
