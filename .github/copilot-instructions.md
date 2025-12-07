# Copilot Instructions

## Kommunikationsregeln
- Antworten immer auf Deutsch, inkl. Code-Reviews und Commit-Beschreibungen.
- Flash/Monitor/Build-Aufgaben startet ausschließlich der Nutzer (vorhandene VS-Code-Tasks nutzen). Wir beschreiben nur die Schritte.
- Hardwarezugriffe stets über vorhandene Bibliotheken (`womo_*`, `lvgl`, `esp_lcd`, `womo_rs485`, `womo_wifi` usw.); keine Eigenimplementierungen, solange es passende Wrapper gibt.

## Architektur & Datenfluss
- Das Repo enthält zwei ESP-IDF-Projekte:
  - `firmware/`: Display-Controller mit LVGL-Oberfläche auf ESP32-S3 + ST7701/GT911.
  - `firmware-walter/`: Sensor-Hub („Walter“) der alle Sensoren (BNO055, BME680, HX711, Analog, LTE) ausliest und Messwerte via RS485 + JSON liefert.
- Datenfluss: Walter sammelt Werte → `rs485_tx_task` sendet zyklische "full"-Pakete → Display nimmt UART1-Frames über `main/rs485/womo_rs485_display.c` an → `rs485_data_received()` (Display `main.c`) aktualisiert `latest_sensor_data` unter `display_data_spinlock` → Timercallbacks/UI-Tasks rendern Widgets.

## Display-Firmware (firmware)
- Einstieg: `main/main.c` (UI-Aufbau, RS485-Callback, Timer) und `main/rs485/womo_rs485_display.c` (UART + cJSON-Parsen). Immer `lvgl_port_lock(-1)`/`_unlock()` verwenden, außer innerhalb der LVGL-Task oder Timer.
- Neue Widgets folgen bestehenden Dateien unter `main/gui/*` (z. B. `womo_attitude`, `womo_battery`). Fonts liegen unter `main/gui/fonts`.
- Lokalisierte Strings ausschließlich über `womo_locale_get_string()` beziehen; Platzhalter-/Fallback-Konstanten (`PLACEHOLDER_*`) beachten.
- Timerkonzept: `ui_update_timer_cb()` (500 ms) aktualisiert Widgets anhand von `latest_sensor_data` und `latest_missing_snapshot`; `time_update_timer_cb()` pflegt Uhr/Datumsanzeige und triggert Theme-Updates.

## Walter-Firmware (firmware-walter)
- Zentrales File: `main/main.cpp`. Sensoraufgaben (BNO055, HX711, Analog, BME680, LTE) laufen als eigene FreeRTOS-Tasks; gemeinsam genutzte Werte landen in `sensor_shared_state_t` via `sensor_state_publish_*()` (Mutex `s_sensor_state_mux`).
- Konfiguration erfolgt über `components/womo_common/include/walter_config.h` (GPIOs, Poll-Intervalle, RS485/LTE/WiFi-Einstellungen). Änderungen dort vermeiden Menüconfig-Rebuilds.
- `rs485_tx_task()` erzeugt JSON mit `cJSON` und nutzt `womo_rs485_write()`. Pakettypen (`type":"full"`, optional `"imu"`) enthalten strukturierte Teilobjekte (`hx`, `imu`, `bme`, `bat`, `tank`, `wifi`, `lte`). Anpassungen hier müssen mit dem Display-Parser synchron bleiben.
- Logging: Sensor-spezifische `WALTER_SENSOR_LOG_*` Flags steuern die Frequenz; teure Logs (z. B. BNO055) sollten an die anderen Sensorintervalle gekoppelt werden, damit die Konsole nicht überläuft.

## Build- & Debug-Workflow
- ESP-IDF Ziel ist überall `esp32s3`. Typische Befehle (vom Nutzer auszuführen): `idf.py set-target esp32s3`, `idf.py build`, `idf.py -p <COM> flash`, `idf.py -p <COM> monitor`. Für Walter und Display jeweils im passenden Ordner ausführen.
- Komponenten-Abhängigkeiten liegen unter `managed_components/` und werden über `dependencies.lock` verwaltet. Keine manuelle Bearbeitung – falls nötig `idf.py add-dependency` verwenden.
- Für UART/RS485-Debugging `ESP_LOGI/W/D` nutzen. Bei hoher Frequenz (IMU) nur `ESP_LOGD` und optional Log-Takt senken, um Watchdog-Timeouts zu vermeiden.

## Tests & Erweiterungen
- Automatisierte Tests existieren nicht; Änderungen an Parsern sollten über gezielte Unit-Tests (z. B. cJSON) vorbereitet werden, ansonsten Hardwaretests.
- Neue Sensoren am Display immer zuerst über Walter einspeisen (JSON erweitern, `womo_rs485_display` anpassen, danach LVGL-Widgets). Direkter Hardwarezugriff am Display ist nur letzter Ausweg.
- Bei UI-Erweiterungen prüfen, ob `latest_missing_snapshot` korrekt gesetzt wird, damit Platzhalter/Warnungen konsistent bleiben.

> Fehlen dir wichtige Abläufe oder Projektkonventionen? Kurze Rückmeldung reicht, dann ergänze ich die Doku.
