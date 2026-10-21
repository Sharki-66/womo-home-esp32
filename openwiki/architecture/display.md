---
type: firmware-node
title: Display-Knoten (ESP32-S3-Touch-LCD-7)
description: Architekturreferenz für die WoMoHome-Display-Firmware auf dem Waveshare ESP32-S3-Touch-LCD-7 — ihre LVGL-GUI, die Anbindung ans Sensorboard und ihre Rolle als Integrationspunkt des Fahrzeugs zu Router und externen Cloud-APIs.
tags: [firmware, display, esp32-s3, lvgl, gui, rs485, espnow, waveshare, gt911, ch422g, router-integration, womohome]
verified:
  - by: openwiki/0.7.2
    at: 2026-10-19T20:49:57.319Z
sources:
  - id: openwiki-source-475bf01eb454c193bd958980
    resource: repo://firmware/display/CLAUDE.md
  - id: openwiki-source-d59b1d08139611e4d608306d
    resource: repo://firmware/display/main/CMakeLists.txt
  - id: openwiki-source-9c792915dfc27f96da43d323
    resource: repo://firmware/display/main/espnow/womo_espnow.c
  - id: openwiki-source-27c674b0976a3f6b1809ec69
    resource: repo://firmware/display/main/gui/womo_battery.h
  - id: openwiki-source-1fb5d6eb30544f289df0489f
    resource: repo://firmware/display/main/gui/womo_forecast_modal.c
  - id: openwiki-source-054dc9b340a4df19d429ec5d
    resource: repo://firmware/display/main/gui/womo_gas_bottle.h
  - id: openwiki-source-208e6597c8247fe616bff531
    resource: repo://firmware/display/main/gui/womo_tank.h
  - id: openwiki-source-c69f62296db4da7712f506c7
    resource: repo://firmware/display/main/hardware/waveshare_rgb_lcd_port.c
  - id: openwiki-source-f202cfac176e33a9d2fddc21
    resource: repo://firmware/display/main/hardware/waveshare_rgb_lcd_port.h
  - id: openwiki-source-9174f3ba0dad0af6c113f52e
    resource: repo://firmware/display/main/idf_component.yml
  - id: openwiki-source-38b9eb6d2870818b1492c628
    resource: repo://firmware/display/main/main.c
  - id: openwiki-source-dc74fcc5a4676ce0b9d598d1
    resource: repo://firmware/display/main/network/womo_http_mutex.c
  - id: openwiki-source-1464e5717935d60dcbc05027
    resource: repo://firmware/display/main/network/womo_router_uci.h
  - id: openwiki-source-744e1ac518800108df2d4cea
    resource: repo://firmware/display/main/network/womo_weather_http.h
  - id: openwiki-source-6293666da42dd464af73c689
    resource: repo://firmware/display/main/sensorboard/womo_sensorboard.c
  - id: openwiki-source-8b9402f131c122686ab64214
    resource: repo://firmware/display/main/sensorboard/womo_sensorboard.h
  - id: openwiki-source-0711ccac3350a5b9e066cae5
    resource: repo://firmware/display/main/ui/CMakeLists.txt
  - id: openwiki-source-e513d5fe3145bbdfa5ec52c1
    resource: repo://firmware/display/main/ui/main.c
  - id: openwiki-source-3be7acf5d94a20da1070dcf2
    resource: repo://firmware/display/memory-bank/systemPatterns.md
  - id: openwiki-source-3fa3bc1259aa79979e696eba
    resource: repo://firmware/display/memory-bank/techContext.md
  - id: openwiki-source-0d02e9ed0072a954af38892f
    resource: repo://firmware/display/README.md
  - id: openwiki-source-7ff6287b239f90d6c9ccd298
    resource: repo://firmware/shared/womo_config.h
  - id: openwiki-source-23775c3de52f3ab95a13cb8b
    resource: repo://README.md
generated: { by: "openwiki/0.7.2", at: "2026-10-19T20:49:57.319Z" }
---

## Zweck

Der Display-Knoten ist der benutzerseitige „Bordcomputer" des WoMoHome-Systems. Er läuft auf einem Waveshare **ESP32-S3-Touch-LCD-7** und ist zuständig für:

- Die Darstellung aller Fahrzeugstatusdaten (Energie, Gas, Wasser, Klima, Navigation) auf einem 7"-Touchscreen über eine **LVGL**-GUI.
- Den Empfang der Sensor-Telemetrie vom **Sensorboard**-Knoten (einem zweiten ESP32-S3) über das projekteigene „RS485"-JSON-Topic-Protokoll, deren Zusammenführung in einem lokalen Snapshot und die Ansteuerung aller Bildschirm-Widgets daraus.
- Die Rolle als einziger **Integrationspunkt** des Systems nach außen: Er fragt beim Teltonika-**RUTX11-Router** WLAN/LTE/GPS-Status ab und steuert ihn, und er ist der einzige Knoten, der ausgehende HTTPS-Aufrufe an externe Cloud-APIs (Open-Meteo, Nominatim, Meteoalarm) tätigt. Das Sensorboard spricht niemals direkt mit dem Router oder dem Internet.
- Die Entgegennahme von Touch-Eingaben zur Steuerung des 12V-Bordnetzes und der Multimedia-/Radio-Schaltkreise, von WLAN/LTE/AP-Einstellungen und konfigurierbaren Schwellwerten sowie die Weiterleitung der resultierenden Befehle an das Sensorboard.

(Quelle: repo://firmware/display/README.md, repo://firmware/display/CLAUDE.md)

## Hardware

| Komponente | Beschreibung |
|---|---|
| Board | Waveshare ESP32-S3-Touch-LCD-7 (800×480, RGB565) |
| MCU | ESP32-S3-WROOM-1, 16 MB Flash, 8 MB PSRAM (Octal) |
| Display-Panel | 7" IPS, ST7701-RGB-Interface, Double-Buffer + Direct-Mode (Avoid-Tearing) |
| Touch | GT911-kapazitiver Touch-Controller über I2C, angesteuert über `esp_lcd_touch_gt911` mit eigenem Read-Callback |
| IO-Expander | CH422G über I2C — schaltet die Hintergrundbeleuchtung und legt die Chip-Select-Leitung der SD-Karte (`EXIO4`); er wird im aktuellen Treiber **nicht** für das Reset des Touch-Controllers verwendet |
| Sensorboard-Anbindung | JSON-„RS485"-Protokollframes, heute über eine ESP-NOW-Funkstrecke übertragen (siehe unten) |
| SD-Karte | Hintergrundbilder, Screenshots (optional) |

Die RGB-Timing-Pins (`EXAMPLE_LCD_IO_RGB_*`), die I2C-Bus-Pins (`I2C_MASTER_SCL_IO`=GPIO9, `I2C_MASTER_SDA_IO`=GPIO8) und der lokale Piezo-Summer (GPIO6, ein ansonsten ungenutzter ADC-Pin) sind alle in den Board-Port-Headern definiert.
(Quelle: repo://firmware/display/main/hardware/waveshare_rgb_lcd_port.h#L22-L66, repo://firmware/display/main/display_config.h#L59-L68, repo://firmware/display/main/hardware/waveshare_rgb_lcd_port.c#L126-L151)

Hintergrundbeleuchtung Ein/Aus und die Chip-Select-Leitung der SD-Karte werden über zwei CH422G-I2C-Gerätehandles (Konfigurations- + Datenregister) per Schreiben eines Ausgabebytes geschaltet; der aktuelle Backlight-Zustand wird in `s_ch422g_backlight_on` zwischengespeichert, um redundante I2C-Schreibzugriffe zu vermeiden.
(Quelle: repo://firmware/display/main/hardware/waveshare_rgb_lcd_port.c#L19-L151, repo://firmware/display/main/hardware/waveshare_rgb_lcd_port.c#L340-L380)

Schaltpläne und das zugrunde liegende KiCad-Projekt für diese Hardware finden sich unter [Boards & KiCad](../hardware/boards-and-kicad.md).

## Firmware-Stack

- **ESP-IDF** ≥ v5.1 (entwickelt gegen v5.5.2); **LVGL** `^9.2.0` und **esp_lvgl_port** `^2` als Managed Components (fixiert in `main/idf_component.yml`).
- RGB-Interface mit 800×480, 16 MHz Pixeltakt, zwei Framebuffern im PSRAM (Direct Mode + Avoid-Tearing), mit einem 4-Zeilen × 800 px × 2 B × 2-Buffer (12,8 KB) großen DMA-Bounce-Buffer im internen SRAM.
- Der LVGL-Task läuft auf Core 1 mit einem im PSRAM allozierten 16-KB-Stack bei Priorität 4; jede LVGL-Objektmanipulation muss in diesem Task oder unter `lvgl_port_lock()` erfolgen.
- Ein eigenständiges `main/ui/`-Verzeichnis enthält eine von EEZ Studio generierte Referenz-UI (`ui/ui.h`, eigene `CMakeLists.txt`); sie ist **nicht** in die ausgelieferte App eingebunden — `main/CMakeLists.txt` registriert nur die Dateien unter `main/gui`, `main/network`, `main/sensorboard`, `main/espnow`, `main/hardware`, `main/storage` und `main/time`.
(Quelle: repo://firmware/display/main/idf_component.yml, repo://firmware/display/CLAUDE.md#L63-L69, repo://firmware/display/main/CMakeLists.txt, repo://firmware/display/main/ui/CMakeLists.txt)

Hinweis zur Versionsangabe: `main/idf_component.yml` fixiert LVGL auf `^9.2.0` (mit `esp_lvgl_port` `^2`); die Kopfzeile von `firmware/display/README.md` nennt dagegen „LVGL v9.5" — diese Versionsangabe stimmt nicht mit der fixierten Abhängigkeit überein.
(Quelle: repo://firmware/display/CLAUDE.md#L65-L67, repo://firmware/display/README.md#L3-L3, repo://firmware/display/main/idf_component.yml)

## GUI-Modulstruktur

`main/main.c` (~5100 Zeilen) ist der zentrale Orchestrator: Er baut den Hauptbildschirm auf, besitzt die globalen LVGL-Objekte, betreibt den periodischen `ui_update_timer_cb` (500 ms), der den jeweils aktuellen gemergten Sensor-Snapshot liest und jedes Widget aktualisiert, und verdrahtet sämtliche GUI-/Netzwerk-/Sensorboard-Callbacks miteinander.

Der GUI-Code ist unter `main/gui/` in wiederverwendbare Widgets und vollflächige Modals aufgeteilt:

| Modul | Rolle |
|---|---|
| `womo_theme` | Tag/Nacht-Themenumschaltung (gesteuert durch Sonnenauf-/-untergang), gemeinsame Status-Level-Farben |
| `womo_locale` | Deutsche Lokalisierung (Datum, Wochentagsnamen); siehe `README_LOCALE.md` für das Muster zur Erweiterung der Locale |
| `womo_fonts_german` | Montserrat-Schriftsatz mit deutschen Sonderzeichen plus eingebetteten Material-Icon-Fonts |
| `womo_battery`, `womo_tank`, `womo_gas_bottle` | Anzeige-Widgets des Hauptbildschirms (Batteriebalken, Tankfüllstände, Gasflaschen-Füllstand) |
| `womo_weather` | Widget für aktuelles Wetter-Icon/Temperatur, Mapping WMO-Code → LVGL-Icon |
| `womo_forecast_modal` | 5-Tage-Vorhersage-Modal (Open-Meteo-Tagesdaten) und Banner für Unwetterwarnungen |
| `womo_connectivity_modal` | 3-spaltiges (Hotspot/AP · WLAN · LTE) Bedienpanel für Konnektivität |
| `womo_router_leds_modal` | Zusätzliches Modal mit Router-WLAN/LTE/GPS-Signal-LEDs |
| `womo_settings_modal` | Tabbed-Einstellungsmodal (Sprache · RTC/Temp/Gas · Schwellwerte · System · AP) |
| `womo_thresholds` | NVS-gestützte Schwellwertspeicherung für Tank-/Batterie-/Gas-Warnungen |

(Quelle: repo://firmware/display/README.md#L41-L63, repo://firmware/display/main/gui, repo://firmware/display/main/main.c#L281-L300)

### Bildschirme und Modals

- **Hauptbildschirm**: Innen-/Außentemperatur, Luftdruck mit Trendpfeil, Luftfeuchte, Kompass/Neigung (vom BNO055 des Sensorboards), zwei Batteriebalken, Frisch-/Grauwassertank mit Verbrauchsrate und Restzeitschätzung, zwei Gasflaschenanzeigen mit aktuellem Verbrauch und Restzeit, aktuelles Wetter-Icon, Sonnenauf-/-untergang (lokal aus GPS-Position über `womo_sun_calc` berechnet) sowie ein optionales graues „Elec"-Panel (INA226-Spannung/-Strom/-Leistung, per Long-Press ein-/ausblendbar).
- **Konnektivitäts-Modal**: ein 640×420 px großes 3-Spalten-Panel. Die AP-Spalte ist rein informativ (SSID, Anzahl/Liste verbundener Clients), und der AP kann softwareseitig niemals deaktiviert werden, da er der einzige Kommunikationskanal zwischen ESP32 und Router ist; die WLAN- und LTE-Spalten zeigen jeweils eine Status-LED, Verbindungs-/Signalinformationen sowie einen Schalter, der `womo_router_wifi_set_sta()`/`womo_router_wifi_enable_sta()` bzw. `womo_router_lte_enable()` über die UCI-JSON-RPC-API des Routers ansteuert. Ein WLAN-Scan zielt bewusst nur auf die AP-/Master-Funkschnittstellen des Routers, niemals auf das aktive STA-Uplink-Funkmodul, da ein Scan des verbundenen Uplink-Funkmoduls nachweislich diese Verbindung einfrieren lässt.
- **Einstellungs-Modal**: Tabs für Sprache, RTC/Temp/Gas, Schwellwerte, System und AP (640×420 px). Der Tab RTC/Temp/Gas beherbergt die Ruuvi-BLE-Tag-Konfiguration (Innen-/Außen-Ersatzsensoren) und einen geführten Einrichtungs-/Kalibrierungsassistenten für die GasBee-BLE-Waage; siehe [Gaskalibrierungs-Workflow](../workflows/gas-calibration.md) für den Ablauf.
- **Fehler-/Statusbehandlung**: ein Fehler-Stack mit 6 Quellen (Gas, Frischwasser, Grauwasser, Batterie, Sensorlink-Timeout, Innenraumluftqualität) mit Neubewertung pro Tick (`current`), klebrigem Latching (`latched`), das erst nach Quittierung plus einer stabilen Erholungsserie zurückgesetzt wird, sowie Touch-basierter Quittierung (`acked`); nicht quittierte, gelatchte Fehler lösen einen Summeralarm/-warnton aus.
- **Weitere Funktionen**: Long-Press-Screenshot auf SD-Karte (PNG), automatische Themen-/Backlight-Umschaltung bei Sonnenauf-/-untergang sowie ein lokaler Piezo-Summer mit eigenen Klick-/Warn-/Alarm-/Start-/Erfolgs-/Fehlertonfolgen.
(Quelle: repo://firmware/display/CLAUDE.md#L44-L62, repo://firmware/display/main/main.c#L226-L279, repo://firmware/display/main/hardware/buzzer.h)

Die Wetterabdeckung geht über das Top-Level-Projekt-README hinaus, das „Wettervorhersage" noch als `(todo)`-Punkt führt: Die Display-Firmware samt eigenem README/CLAUDE.md beschreibt (und der Code implementiert) die Anzeige des aktuellen Wetters, ein 5-Tage-Vorhersage-Modal und ein automatisches Unwetter-Popup aus Meteoalarm-Daten — d. h. dieses Feature ist auf dem Display-Knoten bereits umgesetzt, auch wenn es auf Gesamtprojektebene noch als geplant erscheinen mag.
(Quelle: repo://README.md#L27-L31, repo://firmware/display/README.md#L18-L31, repo://firmware/display/main/gui/womo_forecast_modal.c, repo://firmware/display/main/network/womo_weather_http.h)

## Sensorboard-Anbindung: „RS485"-Topic-Protokoll und Merge-Logik

Die Protokolldokumentation des Projekts (`firmware/display/README.md`, `firmware/display/CLAUDE.md`) beschreibt die Verbindung Display↔Sensorboard als RS485:

- Physikalische Schicht (laut Dokumentation): UART2, Half-Duplex-RS485, **57600 8N1** mit automatischem DE/RTS-Toggle (115200 verursachte nachweislich Framing-Fehler durch das DE-Toggle-Timing); die Frames sind CRLF-terminierte ASCII-JSON-Zeilen (nur druckbares ASCII 0x20–0x7E, kein Binärformat).
- Auf Sensorboard-Seite genannte Pins: TX=GPIO12, RX=GPIO11, DE/RTS=GPIO21 (`modem_config.h`). Diese GPIO-Nummern sind die eigene lokale Pinbelegung des Sensorboards für sein Ende desselben physischen RS485-Leitungspaars; die entsprechende UART-Pinbelegung auf der Display-Seite ist eine separate, board-lokale Zuordnung — die GPIO-Nummern beider Boards müssen nicht übereinstimmen.
- Jeder Frame ist ein JSON-Objekt mit `type` oder `cmd`, immer mit `seq` (u32) und `ts` (ms seit Epoch); `need_ack` steuert, ob die Gegenseite mit `{"type":"ack","ack":<seq>,"status":"ok"|"err","cmd":"<label>","err":"<text>"}` antworten muss.
- Das Sensorboard sendet periodisch `hello`/`hb`-Heartbeat-Frames sowie kleine Topic-Pakete nach einem Round-Robin-Scheduler (max. ein Topic pro 100-ms-Zyklus): `ctrl` (2 s; 12V/Radio/AC-vorhanden), `imu` (1 s; BNO055-Heading/Pitch/Roll + Kalibrierung), `bat` (1 s), `tank` (10 s), `hx` (10 s; Wägezellen- oder GasBee-BLE-Waagengewichte), `gas` (10 s; abgeleiteter Gasverbrauch/Reichweite), `bme_in`/`bme_out` (10 s; Innen-/Außenklima, verkabeltes BME680/BME280 oder Ruuvi-BLE-Tag als Ersatz) und `elec` (1 s; INA226).
- Das Display sendet `cmd`-Objekte zurück: `display_ready`, `level_start`/`level_stop`, `tare_a`/`tare_b`, `gas_bottle_replace`, `pwr_12v_on`/`pwr_12v_off`, `radio_on`/`radio_off`, `ruuvi_config`, `ruuvi_scan_request`, `gasbee_config`, `gasbee_scan_request`, `gasbee_cal_start`/`gasbee_cal_end`, `gasbee_cal`. Siehe [RS485-Protokoll-Workflow](../workflows/rs485-protocol.md) für den vollständigen Frame-Katalog.
- GPS und LTE werden über diese Verbindung ausdrücklich **nicht** übertragen — das Display fragt dafür direkt den Router ab.
- **Merge-Semantik**: Jedes empfangene Topic aktualisiert nur seinen eigenen Ausschnitt eines gemeinsamen `s_latest_data`/`latest_sensor_data`-Zustands; alle anderen Felder bleiben erhalten. Ein Callback übergibt `main.c` bei jeder Aktualisierung einen vollständigen gemergten Snapshot, und unbekannte/fehlende JSON-Felder werden toleriert statt als Fehler behandelt.
(Quelle: repo://firmware/display/CLAUDE.md#L17-L41, repo://firmware/display/README.md#L5-L13)

### Aktueller Transport: ESP-NOW statt physischem RS485-Bus

Die eigene Quelle der Display-Firmware widerspricht der oben beschriebenen UART/RS485-Physik: Der gemeinsame Konfigurationsheader dokumentiert ausdrücklich „ESP-NOW Transport (ersetzt RS485)" — d. h. **ESP-NOW ersetzt RS485** als lebenden Transport —, und `womo_sensorboard_init()` ist als „Sensorboard-Kommunikation initialisieren (ESP-NOW)" kommentiert. Das Display sendet JSON-Frames über `womo_espnow_send()` und empfängt sie über einen ESP-NOW-Empfangs-Callback/-Queue (`main/espnow/womo_espnow.c`), mit Peer-Discovery per Broadcast und anschließendem Erlernen der MAC-Adresse des Sensorboards für Unicast. Auch Log-Meldungen in `main.c` sprechen von einem „ESP-NOW Handshake" statt von einem UART-Link-up. Anwendungscode und GUI-Beschriftungen verwenden „RS485" weiterhin als historischen Namen für dieses logische Topic-Protokoll sowie für Konfigurationskonstanten (Heartbeat-Intervall, Kommando-Timeout, Topic-Intervalle), aber der physische Träger im aktuellen Codestand ist eine WiFi-Kanal-ESP-NOW-Verbindung, keine RS485-UART-Busleitung.
(Quelle: repo://firmware/shared/womo_config.h#L25-L73, repo://firmware/display/main/sensorboard/womo_sensorboard.h#L240-L241, repo://firmware/display/main/espnow/womo_espnow.c, repo://firmware/display/main/main.c#L5070-L5096)

Das bedeutet: Leser sollten den oben dokumentierten JSON-Topic-/Kommando-Katalog, die Baudraten-/Pin-Angaben und die Frame-Semantik als die maßgebliche **Protokollebenen**-Beschreibung ansehen (gemäß dem projekteigenen README/CLAUDE.md), während der tatsächliche physische Transport in der aktuellen Firmware-Revision ESP-NOW ist — eine Doku-/Code-Diskrepanz, die man beim Debuggen der Konnektivität oder bei Hardware-Planungen im Hinterkopf behalten sollte.

Ein eigenständiger RS485-UART-Bring-up-Test existiert außerhalb des Haupt-Firmware-Baums (`tests/02_RS485_Test/`), getrennt von dieser produktiven ESP-NOW-Verbindung.
(Quelle: repo://tests/02_RS485_Test/README.md)

### Link-Überwachung und Kommandos

- `womo_sensorboard.c` verfolgt ausstehende Kommandos (`rs485_pending_cmd_t`-Slots), gleicht ACKs anhand von Sequenznummer und Label ab und lässt sie nach `WOMO_SENSOR_COMMAND_TIMEOUT_MS` (3 s) timeouten.
- `main.c` betreibt einen 5-Minuten-RS485-Timeout-Watchdog (`RS485_TIMEOUT_US`); trifft innerhalb dieses Fensters kein Paket ein, wird der Eintrag `SENSOR_ERR_SRC_RS485` im Fehler-Stack gesetzt, sodass veraltete Sensordaten sichtbar auf dem Bildschirm markiert werden.
- GPS-/LTE-Felder im gemergten Snapshot werden vom Sensorboard-Callback bewusst nicht angefasst und stattdessen von einem separaten 15-Sekunden-`router_poll_task` geschrieben, sodass sich die beiden Datenquellen nicht gegenseitig überschreiben können.
(Quelle: repo://firmware/display/main/sensorboard/womo_sensorboard.c#L49-L261, repo://firmware/display/main/main.c#L219-L222, repo://firmware/display/main/main.c#L5113-L5127, repo://firmware/shared/womo_config.h#L29-L37)

## Integrationspunkt: Router und externe APIs

Das Display ist der einzige netzwerkseitig aktive Knoten des Systems:

<!-- openwiki: broken internal link [../router-and-connectivity.md] file "../router-and-connectivity.md" does not exist. Fix the href or restore the target, then delete this comment. -->
- **Router (Teltonika RUTX11)**: `network/womo_router_uci.c` spricht per UCI-JSON-RPC/ubus über HTTP mit dem Router (`CONFIG_WOMO_ROUTER_IP`, Standard `192.168.55.1`), um WLAN-Client-Status, LTE-/Modem-Status, GPS-Position und WLAN-Scan-Ergebnisse auszulesen sowie WLAN-STA/LTE zu schalten. Ein `router_poll_task` aktualisiert AP-/WLAN-/LTE-/GPS-Zustand alle 15 s. Siehe [Router & Connectivity](../router-and-connectivity.md) für die router-seitige Architektur.
- **ESP32-eigenes WLAN**: `network/womo_wifi.c` verwaltet die eigene STA-Verbindung des ESP32 (unabhängig von den Funkmodulen des Routers), mit einem Pool bekannter Netzwerke und Auto-Retry, genutzt für den AP-/Hotspot-Pfad und den eigenen Internetzugang des Displays.
- **Externe Cloud-Dienste**, allesamt nur vom Display und nur dann erreicht, wenn der ESP32 über Router/Hotspot Internetzugang hat:

| Dienst | Zweck | TLS |
|---|---|---|
| Open-Meteo | Aktuelles Wetter + 5-Tage-Vorhersage | ISRG Root X1 (eingebettetes PEM) |
| Nominatim | Reverse Geocoding (Ortsname aus GPS) | ISRG Root X1 (eingebettetes PEM) |
| Meteoalarm | Unwetterwarnungen (CAP) | GEANT TLS RSA 1 (eingebettetes PEM) |
| Teltonika RUTX11 | WLAN-/LTE-/GPS-Steuerung | einfaches HTTP (nur LAN) |

`network/womo_http_mutex.c` serialisiert TLS-Sitzungen, damit gleichzeitige HTTPS-Aufrufe an diese Dienste die mbedTLS-Ressourcen des ESP32-S3 nicht erschöpfen. Siehe [External Services](../integrations/external-services.md) für den API-seitigen Vertrag jeder Integration.
(Quelle: repo://firmware/display/README.md#L32-L40, repo://firmware/display/main/network/womo_router_uci.h#L1-L50, repo://firmware/display/main/network/womo_http_mutex.c, repo://firmware/display/memory-bank/techContext.md#L34-L40)

Das Sensorboard führt nichts davon selbst aus: WLAN-/LTE-Status und sämtliche Cloud-Daten erreichen das Fahrzeug-Display ausschließlich dadurch, dass das Display Router und Internet direkt abfragt, unabhängig von der Sensorboard-Telemetrieverbindung.

## Konfiguration und Persistenz

- `main/display_config.h` bündelt die `ESP_LOG_*`-Pegel je Modul und Board-Konstanten (Summer-GPIO/LEDC-Einstellungen, Material-Icon-Codepoints), sodass die Log-Ausführlichkeit ohne Eingriff in den Modulcode angepasst werden kann.
- `firmware/shared/womo_config.h` enthält Werte, die zwischen Display- und Sensorboard-Builds **übereinstimmen müssen**: Heartbeat-/Kommando-Timeout-Intervalle, Aktualisierungsintervalle je Topic, die Router-IP sowie die ESP-NOW-Fallback-Konstanten für Kanal/Payload-Größe/Queue-Tiefe — beide Firmware-Images müssen nach jeder Änderung hier neu gebaut und geflasht werden.
- NVS wird verwendet für: WLAN-Zugangsdaten der Router-Netzwerke (Namespace `rtr_wifi`, MRU-Liste, max. 20 Einträge), den eigenen Pool bekannter WLANs des ESP32 (`wifi_cfg`/`known_list`), AP-Hotspot-Zugangsdaten (`womo_ap_cfg`), konfigurierbare Schwellwerte (`womo_cfg`/`womo_thresholds`) sowie Ruuvi/GasBee-Sensor-zu-Slot-Zuordnungen (`womo_ruuvi_cfg`, `womo_gasbee_cfg`).
(Quelle: repo://firmware/display/main/display_config.h, repo://firmware/shared/womo_config.h, repo://firmware/display/CLAUDE.md#L53-L63, repo://firmware/display/memory-bank/systemPatterns.md#L58-L62)

## Invarianten und Betriebshinweise

- Jede LVGL-Objektmanipulation muss im LVGL-Task (Core 1) oder unter dessen Lock erfolgen; Sensorboard- und Router-Polling-Tasks schreiben nur in mutex-/spinlock-geschützten Snapshot-Zustand und dürfen LVGL niemals direkt aufrufen.
- Der UI-Update-Code liest stets einen vollständigen Snapshot des gemergten Sensorzustands statt einzelner Felder, um zerrissene Lesezugriffe über den Multi-Topic-Merge hinweg zu vermeiden.
- Die AP-Spalte des Konnektivitäts-Modals darf nie eine Deaktivierungsmöglichkeit anbieten, da der ESP32-AP der einzige Weg zurück zum Router ist, solange WLAN-/LTE-Zustandsänderungen unterwegs sind.
- Laut Projektnotizen hat das Herausziehen der RS485-/ESP-NOW-Initialisierung aus dem kritischen Boot-Pfad die Bootzeit messbar verbessert, und die SD-Karten-Initialisierung läuft asynchron, da eine synchrone frühe SD-Initialisierung sich nach monitor-ausgelösten Resets als weniger zuverlässig erwiesen hat.
(Quelle: repo://firmware/display/memory-bank/systemPatterns.md#L58-L68, repo://firmware/display/NOTES.md)

## Verwandte Seiten

<!-- openwiki: broken internal link [../router-and-connectivity.md] file "../router-and-connectivity.md" does not exist. Fix the href or restore the target, then delete this comment. -->
- [Router & Connectivity](../router-and-connectivity.md) — RUTX11-Router-Architektur und die hier genutzte UCI-Steuerschnittstelle.
- [Boards & KiCad](../hardware/boards-and-kicad.md) — Schaltpläne für die Waveshare-Board-Verdrahtung und die kundenspezifische Trägerhardware.
- [External Services](../integrations/external-services.md) — API-Verträge für Open-Meteo, Nominatim und Meteoalarm.
- [Gaskalibrierungs-Workflow](../workflows/gas-calibration.md) — der vom Einstellungs-Modal angestoßene Einrichtungs-/Kalibrierungsassistent für die GasBee-BLE-Waage.
- [RS485-Protokoll](../workflows/rs485-protocol.md) — vollständiger JSON-Topic-/Kommando-Katalog, der mit dem Sensorboard-Knoten geteilt wird.
