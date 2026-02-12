# Copilot Instructions (WoMoHome Sensor)

## Kommunikationsregeln
- Antworten immer auf Deutsch, inkl. Code-Reviews und Commit-Beschreibungen.
- Hardwarezugriffe ausschließlich über die vorhandenen komponentenspezifischen Bibliotheken (z. B. LVGL, esp_lcd). Keine Eigenimplementierungen solange eine Bibliothek die Funktion bereits anbietet.
- Build, Flash und Monitor werden vom Nutzer gestartet (siehe VS Code Tasks "Build/Flash/Monitor Display Firmware"). Nur erklären, keine Befehle ausführen.

## Projektüberblick
- Ziel: Anzeige und Steuerung von Wohnmobil-Daten. Router liefert WLAN/LTE/GNSS; Display visualisiert; Sensorboard sammelt analoge/digitale Messwerte. Kommunikation zwischen Display und Sensorboard per RS485.

## Hardware
- Router: Teltonika RUTX11 (OpenWRT-basiert) für WLAN/Netz/Hotspot/LTE/GNSS. Anschluss ans Wohnmobil über 2 Relais mit Freilaufdiode und Nachlaufzeit gemäß RUTX11-Doku. Dachantenne (Teltonika) mit 2× WLAN, 2× LTE, 1× GNSS.
- Display: ESP32-S3-Touch-LCD-7, übernimmt Visualisierung aller Sensor- und Statusdaten im Wohnmobil.
- Sensorboard: Heemol ESP32 S3 N16R8 DevKitC-1 (ESP32-S3-DevKitC-1), erfasst analoge/digitale Sensoren und bedient E/A Richtung EBL/Anzeigepanel.
- Sensorik: BNO055, 2× BME680 (außen/innen), HX711 (Gasfüllstände), 2× Batterien (Board/Kfz), 2× Tanksensoren Votronic (Frisch/Grau, kapazitiv), Steuerung Ein-/Ausgänge EBL/Panel.

## RS485-Schnittstelle (Sensorboard → Display) – Protokoll v2 (Topic-basiert)
- Physik: UART2 RS485 Half-Duplex, 115200 8N1, DE/RTS automatisch. Leitungsende per CRLF-terminierter ASCII-JSON-Zeilen (kein Binary, nur 0x20–0x7E).
- Pins (Sensorboard): TX=GPIO12, RX=GPIO11, DE/RTS=GPIO21 (siehe modem_config.h). Display RS485 Buchse (A / B).
- Grundstruktur jedes Frames: JSON-Objekt mit `type` oder `cmd`, immer `seq` (u32) und `ts` (ms seit Epoch). `need_ack` bool steuert, ob das Gegenüber ein ACK schickt (Sensorboard fordert i. d. R. ACK, sobald Display "ready" ist).
- ACK vom Display: `{ "type":"ack", "ack":<seq>, "status":"ok"|"err", "cmd":"<label>", "err":"<text>" }`, `need_ack=false`.
- Periodik vom Sensorboard:
- `hello`: `type="hello"`, Felder `fw`, `uptime` (s), optional `rx_seq`, `last_ack`.
- `hb`: `type="hb"`, Felder `uptime`, optional `rx_seq`, `last_ack`.
- **Topic-Pakete** (ersetzt das alte monolithische `full`-Paket): Jedes Topic wird als eigenständiges kleines JSON-Paket gesendet. Round-Robin-Scheduler, max. 1 Topic pro 100 ms-Zyklus.
- Topic-Intervalle:
- `ctrl` (2 s): `{ type:"ctrl", pwr_on, radio_on, ac_present, wifi_target, wifi_active, lte_target, lte_active }` – Steuer-/Power-Zustände
- `imu` (5 s): `{ type:"imu", yaw_deg, pitch_deg, roll_deg, hdg, cal:{sys,gyro,acc,mag}, calibrated }` – BNO055
- `bat` (10 s): `{ type:"bat", b1, b2, nc1, nc2, soc? }` – Batterien (Board/Kfz in Volt)
- `tank` (10 s): `{ type:"tank", t1, t2, nc1, nc2 }` – Tanksensoren (Prozent)
- `hx` (10 s): `{ type:"hx", a, b, sum, nc }` – HX711 Wägezellen (kg)
- `gas` (10 s): `{ type:"gas", active, net, rate1h, rate2h, rest_h, net_a, net_b, cap_kg, pct, pct_a, pct_b }` – Gaslogik
- `bme` (15 s): `{ type:"bme", "0x76":{temp_c,rh_pct,press_hpa,gas_kohm?,iaq?,iaq_acc?,eco2_ppm?,bvoc_ppm?}, "0x77":{…} }` – 0x76=indoor, 0x77=outdoor
- Sofort-Ctrl: Nach Ausführung von `pwr_12v_on/off` oder `radio_on/off` wird `ctrl`-Topic sofort gesendet (`s_ctrl_immediate`-Flag), damit Display den neuen Zustand schnell anzeigt.
- GPS/LTE: Werden **nicht** über RS485 übertragen. Display pollt den Router direkt (HTTP/SSH).
- Display → Sensorboard (Kommandos): `cmd` Feld mit JSON-Objekt. Unterstützt: `display_ready`, `level_start|level_stop`, `tare_a|tare_b`, `gas_bottle_replace` (optional `slot`, `channel`), `pwr_12v_on|pwr_12v_off` (12V Bordnetz), `radio_on|radio_off` (Multimedia, nur wenn 12V aktiv). Sensorboard quittiert per ACK.
- Display-Merge: Jedes empfangene Topic aktualisiert nur seinen Teil in `s_latest_data`; alle anderen Felder bleiben erhalten. Callback an main.c liefert vollständigen Merge-Snapshot. Fehlende/unbekannte Felder tolerant ignorieren.
>- Keine Zusatz-Protokolländerungen ohne diese Datei zu aktualisieren.

## Konnektivitäts-Modal (Display)
- 3-Spalten-Layout: HotSpot/AP | WLAN | LTE nebeneinander in einem 640×420 px Panel.
- WLAN-/LTE-Spalte: Titel + farbige Status-LED (grün=aktiv, rot=inaktiv), Verbindungsname (SSID/Provider + Signal%), Switch zum Ein-/Ausschalten.
- AP-Spalte: Rein informativ (kein Switch!). Zeigt SSID, Anzahl verbundener Clients und Geräte-Liste (Hostname aus DHCP-Lease, Fallback MAC). AP darf **niemals** per Software deaktiviert werden, da er der einzige Kommunikationskanal zwischen ESP32 und Router ist.
- WLAN-Spalte: Status + Switch + Scan-Button + Dropdown + Passwortfeld (erst nach Netzwerkauswahl sichtbar). Switch steuert `womo_router_wifi_set_sta()` / `womo_router_wifi_enable_sta()`. QWERTZ-Tastatur für Passwort.
- LTE-Spalte: Provider + Signalstärke + Switch. Switch steuert `womo_router_lte_enable()`.
- WLAN und LTE werden über UCI JSON-RPC am RUTX11 gesteuert (nicht über RS485).
- Router-WiFi-Zugangsdaten werden im NVS gespeichert (Namespace `rtr_wifi`, max. 20 Einträge, MRU-Reihenfolge). Bei Netzwerkauswahl aus Dropdown wird gespeichertes Passwort vorausgefüllt.
- Router-Poll-Task (`router_poll_task` in main.c) aktualisiert AP/WiFi/LTE/GPS alle 15 s und füllt `womo_connectivity_snapshot_t` mit AP-Feldern (ap_enabled, ap_ssid, ap_clients, ap_client_list).

## Geplante Migration: LVGL v8 → v9 (noch nicht gestartet)
### Ist-Zustand
- LVGL **8.4.0** als lokale Kopie in `components/lvgl__lvgl/`, konfiguriert über **Kconfig** (`LV_CONF_SKIP=y`, kein `lv_conf.h`).
- Display: RGB LCD 800×480, 16-bit RGB565, Tear-Avoidance Mode 3 (Double-Buffer + Direct-Mode), LVGL-Task Core 1, Prio 4, 4–12 ms Delay.
- Touch: GT911 (I2C) via `esp_lcd_touch`.
- Custom `lvgl_port.c` (640 Zeilen): eigener Flush-Callback (5 Varianten), Dirty-Area-Tracking mit internen LVGL-Strukturen (`disp->inv_p`, `_lv_refr_get_disp_refreshing()`), eigener LVGL-Task, Indev-Driver.
- Custom Fonts: 6× Montserrat-German (12/14/16/20/24 px) in `main/gui/fonts/`, v8-Format.

### Ziel
- LVGL **v9** + **`espressif/esp_lvgl_port` v2** (aktuell v2.7.1) als Managed Component.
- `esp_lvgl_port` ersetzt den kompletten custom `lvgl_port.c` → ~640 Zeilen werden zu ~20 Zeilen Config.
- Referenz-Beispiel: `esp-bsp/components/esp_lvgl_port/examples/rgb_lcd/` (800×480 + GT1151, nahezu identisch zu unserem Setup).

### Aufwand (geschätzt 3–4 Tage)
1. **Dependencies umstellen** (0,5 Tage): `lvgl ^9` + `esp_lvgl_port ^2` in `idf_component.yml`, lokale LVGL-Kopie entfernen.
2. **Driver-Layer ersetzen** (1 Tag): `lvgl_port.c` durch `esp_lvgl_port` Config ersetzen (`lvgl_port_add_disp_rgb()` + `lvgl_port_add_touch()`). CH422G-Backlight + Touch-Wake-Callback separat halten.
3. **Custom Fonts neu generieren** (0,5 Tage): 6 Montserrat-German-Fonts mit v9-Font-Converter neu bauen.
4. **Widget-Umbenennungen** (1 Tag): ~100 Stellen, mechanisch:
   - `lv_btn_create` → `lv_button_create` (9×)
   - `lv_img_*` → `lv_image_*` (5×), `lv_img_dsc_t` → `lv_image_dsc_t`, `LV_IMG_CF_RAW_ALPHA` → `LV_COLOR_FORMAT_*`
   - `lv_obj_del` → `lv_obj_delete` (10×), `lv_obj_clear_flag` → `lv_obj_remove_flag` (15×)
   - `LV_BTNMATRIX_CTRL_*` → `LV_BUTTONMATRIX_CTRL_*` (17×)
   - `lv_coord_t` → `int32_t` (18×), `lv_scr_act()` → `lv_screen_active()` (6×)
   - `LV_LABEL_LONG_DOT` → `LV_LABEL_LONG_DOTS` (3×)
   - `lv_disp_get_hor_res` → `lv_display_get_horizontal_resolution` (2×)
   - `lv_indev_get_act` → `lv_indev_active` (1×)
   - `lv_timer_del` → `lv_timer_delete`, `lv_obj_set_style_img_opa` → `lv_obj_set_style_image_opa`
   - `lv_event_get_target` → `lv_event_get_target_obj`
5. **Spezialfälle** (0,5 Tage):
   - `lv_spinner_create(parent, speed, arc)` → `lv_spinner_create(parent)` + `lv_spinner_set_anim_params()`
   - `lv_msgbox` API komplett überarbeitet
   - Keyboard-Map: `lv_keyboard_set_map()` Signatur geändert
   - Flush-Callback Signatur: `lv_color_t *color_map` → `uint8_t *px_map`
   - `lv_color_t` intern jetzt RGB888 (3 Byte), Farbformat per `lv_display_set_color_format()`
6. **Kconfig + Test** (0,5 Tage): LVGL-v9-Menuconfig, Farbformat, Performance-Tuning.

### Risiken / Hinweise
- Touch-Wake-Callback (`lvgl_touch_set_wake_cb`): greift aktuell auf `indev->driver->read_timer` zu – Pfad existiert in v9 nicht mehr, muss anders gelöst werden.
- LVGL v9 hat Kompatibilitäts-Layer (`lv_api_map_v8.h`) für die meisten Umbenennungen – für schnellen Start nutzbar, für sauberen Code schrittweise ersetzen.
- Kein bestehendes v9-Beispiel speziell für Waveshare ESP32-S3-Touch-LCD-7, aber das Espressif RGB-LCD-Beispiel ist nahezu identisch (800×480, GT1151, ESP32-S3-N16R8).
- `esp_lvgl_port` übernimmt: LVGL-Task, Timer, Mutex, Flush, Buffer-Allokation, VSync-Sync. Übernimmt NICHT: RGB-Panel-Init, Touch-HW-Init, Backlight (CH422G).
