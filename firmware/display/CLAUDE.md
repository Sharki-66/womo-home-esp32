# Claude Code — project context

# Instructions (WoMoHome Display)

## Build & Flash
- Build, Flash und Monitor werden vom Nutzer gestartet (siehe VS Code Tasks „Build/Flash/Monitor Display Firmware"). Nur erklären, keine Befehle ausführen.

## Projektüberblick
- Ziel: Anzeige und Steuerung von Wohnmobil-Daten. Router liefert WLAN/LTE/GNSS; Display visualisiert; Sensorboard sammelt analoge/digitale Messwerte. Kommunikation zwischen Display und Sensorboard per ESP-NOW (bis 2026-06 RS485).

## Hardware
- Router: Teltonika RUTX11 (OpenWRT-basiert) für WLAN/Netz/Hotspot/LTE/GNSS. Anschluss ans Wohnmobil über 2 Relais mit Freilaufdiode und Nachlaufzeit gemäß RUTX11-Doku. Dachantenne (Teltonika) mit 2× WLAN, 2× LTE, 1× GNSS.
- Display: ESP32-S3-Touch-LCD-7, übernimmt Visualisierung aller Sensor- und Statusdaten im Wohnmobil.
- Sensorboard: Heemol ESP32 S3 N16R8 DevKitC-1 (ESP32-S3-DevKitC-1), erfasst analoge/digitale Sensoren und bedient E/A Richtung EBL/Anzeigepanel.
- Sensorik: BNO055, BME680 (innen), BME280 (außen), HX711 (Gasfüllstände), 2× Batterien (Board/Kfz), 2× Tanksensoren Votronic (Frisch/Grau, kapazitiv), Steuerung Ein-/Ausgänge EBL/Panel.

## ESP-NOW-Schnittstelle (Sensorboard ↔ Display) – Topic-basiertes JSON-Protokoll
- Seit 2026-06 (Commit `e1eae83`) ESP-NOW statt RS485, der RS485-Transceiver ist aus dem Schaltplan entfernt.
- Transport: Display `main/espnow/womo_espnow.c`, Sensorboard `main/network/espnow_transport.c`. Ein Frame = ein ESP-NOW-Paket mit unformatiertem JSON, max. `WOMO_ESPNOW_MAX_PAYLOAD` (250 Byte) → große Topics aufteilen (deshalb `bme_in`/`bme_out`). Kanal = Kanal der aktiven WLAN-Verbindung, sonst `WOMO_ESPNOW_CHANNEL_FALLBACK` (6), beide Boards müssen auf demselben Kanal sein. TX-Leistung 10 dBm, verschlüsselt (siehe Kopplung).
- Kopplung + Verschlüsselung (seit 2026-10-25): Jedes Board speichert die MAC der Gegenstelle im NVS (`espnow_peer`/`mac`) und nimmt Nutzdaten **nur per verschlüsseltem Unicast** (CCMP, `WOMO_ESPNOW_PMK`/`LMK` aus `womo_credentials.h`) von ihr an. Broadcast ist nicht verschlüsselbar und trägt nur den Kopplungs-Beacon `{"type":"pair"}` (alle 1 s), Nutzdaten ohne Kopplung werden nicht gesendet (`ESP_ERR_INVALID_STATE`). Kopplungsfenster: beide, solange keine Gegenstelle gespeichert ist; Display 60 s nach „Neu koppeln“ (Einstellungen > Service), schließt beim ersten verschlüsselten Frame; Sensorboard, solange seit `WOMO_ESPNOW_LINK_DEAD_MS` (10 s) nichts vom Display kam. Im Fenster wird ein fremder Beacon als neue Gegenstelle übernommen. Konstanten in `firmware/shared/womo_config.h`.
- Display sendet alle `WOMO_SENSOR_HEARTBEAT_INTERVAL_MS` (2 s) ein `hb` (`uptime`, optional `rx_seq`). Sobald das Sensorboard seit dem Boot einen Frame vom gekoppelten Display hatte (`espnow_transport_peer_known()`), schaltet es die Topics frei und sendet einen Initial-Burst aller Topics. `hello` und der Kommando-Handshake `display_ready` gibt es nicht mehr.
- Grundstruktur jedes Frames: JSON-Objekt mit `type` oder `cmd`, immer `seq` (u32). `ts` (ms seit Epoch) ist kein Kopffeld mehr, sondern nur der Messzeitpunkt in einzelnen Topics (`imu`, `hx`, `elec`, `bme_*`). `need_ack` bool steuert, ob das Gegenüber ein ACK schickt (Standard `false`; Kommandos vom Display setzen `true`, Timeout `WOMO_SENSOR_COMMAND_TIMEOUT_MS` 3 s).
- ACK vom Sensorboard: `{ "type":"cmd_ack", "ack_seq":<seq>, "status":"ok"|"error", "cmd":"<label>", "err":"<text>" }`. ACK vom Display: `{ "type":"ack", "ack_seq":<seq>, "status":"ok"|"err", ... }`, `need_ack=false`.
- **Topic-Pakete**: Jedes Topic wird als eigenständiges kleines JSON-Paket gesendet. Round-Robin-Scheduler, max. 1 Topic pro 100 ms-Zyklus.
- Topic-Intervalle (definiert in `firmware/shared/womo_config.h`, `WOMO_TOPIC_*_INTERVAL_MS`):
- `ctrl` (2 s): `{ type:"ctrl", pwr_on, radio_on, ac_present, rtc_bat_low?, rtc_bat_switched?, ssid, rssi?, wifi_trial?, uptime, heap }` – Steuer-/Power-Zustände plus Diagnose (ehemals `hb`). `ssid` = WLAN des Sensorboards (leer = nicht verbunden), `rssi` nur wenn verbunden, `wifi_trial:true` solange neue Zugangsdaten getestet werden → `sb_wifi` im Display, Anzeige in Einstellungen > AP. WiFi/LTE-Zustand kommt **nicht** vom Sensorboard – Display pollt Router direkt.
- `imu` (2 s): `{ type:"imu", yaw_deg, pitch_deg, roll_deg, hdg, cal:{sys,gyro,acc,mag}, calibrated }` – BNO055
- `bat` (5 s): `{ type:"bat", b1, b2, nc1, nc2 }` – Batterien (Board/Kfz in Volt, kein SoC)
- `tank` (10 s): `{ type:"tank", t1, t2, nc1, nc2 }` – Tanksensoren (Prozent)
- `hx` (10 s): `{ type:"hx", src, a, b, sum, nc }` – Flaschengewichte (kg). `src`: `"hx"` = Kabel-HX711 (Vorrang), `"ble"` = GasBee-Waagen, `"none"`. Bei `"ble"` zusätzlich je Flasche `batt_a_mv`, `batt_a_pct`, `chg_a`, `cal_a` (analog `_b`) → `hx711.is_gasbee`/`bee[0..1]`.
- `gas` (10 s): `{ type:"gas", src, active, net, rate1h, rate2h, rest_h, net_a, net_b, cap_kg, pct, pct_a, pct_b }` – Gaslogik
- `bme_in` (10 s): `{ type:"bme_in", temp_c, rh_pct, press_hpa, gas_kohm?, iaq?, iaq_acc?, eco2_ppm?, bvoc_ppm?, press_trend_state?, press_trend_hpa_h?, chip?, batt_mv? }` – Innensensor. Entweder verkabeltes BME680 (mit IAQ) **oder** Ruuvi-BLE-Tag als Ersatz, je nachdem was verbaut ist – nie beides gleichzeitig. Bei Ruuvi: `chip:"ruuvi"` und `batt_mv` (Batteriespannung in mV) gesetzt, IAQ-Felder entfallen.
- `bme_out` (10 s): `{ type:"bme_out", temp_c, rh_pct, press_hpa, gas_kohm?, press_trend_state?, press_trend_hpa_h?, chip?, batt_mv? }` – Außensensor. Analog: verkabeltes BME280 **oder** Ruuvi-BLE-Tag (`chip:"ruuvi"`, `batt_mv`).
- `elec` (1 s): `{ type:"elec", nc:bool, v_bus, i_a, p_w, v_shunt_mv }` – INA226 Strom-/Leistungsmessung Hauptleitung. `nc:true` wenn Sensor nicht eingebaut. Display zeigt Spannung/Strom(mA)/Leistung als graues Panel rechts mitte (Long Press zum Ein-/Ausblenden).
- Sofort-Ctrl: Nach Ausführung von `pwr_12v_on/off` oder `radio_on/off` wird `ctrl`-Topic sofort gesendet (`s_ctrl_immediate`-Flag), damit Display den neuen Zustand schnell anzeigt.
- GPS/LTE: Werden **nicht** über ESP-NOW übertragen. Display pollt den Router direkt (HTTP/SSH).
- Display → Sensorboard (Kommandos): `cmd` Feld mit JSON-Objekt. Unterstützt: `tare_a|tare_b`, `imu_zero` (Pitch/Roll nullen), `wifi_config` (`ssid`, `pass` mit ≥ `WOMO_WIFI_PASS_MIN_LEN` Zeichen, sonst Fehler-ACK – Sensorboard testet die Daten: erst nach erfolgreicher Verbindung ins NVS, nach `SENSOR_WIFI_RETRY_MAX` Fehlversuchen zurück auf die alten; `cmd_ack` heißt nur „empfangen“, Ergebnis über `ssid`/`wifi_trial` im `ctrl`), `gas_bottle_replace` (optional `slot`, `channel`), `pwr_12v_on|pwr_12v_off` (12V Bordnetz), `radio_on|radio_off` (Multimedia, nur wenn 12V aktiv), `ruuvi_config` (`indoor_mac`, `outdoor_mac` – MAC-Filter für den Ruuvi-BLE-Scanner, in Sensorboard-NVS persistiert, leer = Slot deaktiviert), `ruuvi_scan_request` (fordert einmalig das `ruuvi_scan`-Antwort-Topic an), `gasbee_config` (`bottle_a_mac`, `bottle_b_mac` – Zuordnung GasBee → Flasche, Sensorboard-NVS), `gasbee_scan_request` (fordert `gasbee_scan` an, mit `cfg_only:true` nur GasBees an USB), `gasbee_cal_start`/`gasbee_cal_end` (`slot`), `gasbee_cal` (`mac`, `step` `zero`/`ref`, `g`; Ergebnis kommt als Frame `gasbee_cal_result` `{step, ok, err, msg}`). Sensorboard quittiert per `cmd_ack`. Umgekehrt fragt das Sensorboard bei WLAN-Authentifizierungsfehler per `wifi_pass_request` (`need_ack:true`) nach den Zugangsdaten, Display antwortet mit `wifi_config` aus seiner Known-List.
- `ruuvi_scan` (nur auf Anfrage, kein periodisches Topic): ein Frame pro Tag `{ type:"ruuvi_scan", sid, idx, cnt, mac, rssi, temp_c, has_pressure, age_s }` (`cnt:0` ohne Eintrag = nichts gefunden) – alle zuletzt gesehenen Ruuvi-Tags (unabhängig vom konfigurierten Filter), für die Auswahlliste im Display (Einstellungen > Sensoren).
- `gasbee_scan` (nur auf Anfrage): ein Frame pro GasBee `{ type:"gasbee_scan", sid, idx, cnt, mac, rssi, weight_kg?, batt_mv?, batt_pct?, chg, cal, cfg, bee_seq, age_s }` – alle zuletzt gesehenen GasBees. GasBee sendet nur alle 5 min (10 s lang) → Liste kann kurz nach Boot leer sein. Zusammengesetzt in `womo_sensorboard.c` (`scan_asm_*`): Auslieferung an die GUI, sobald alle `cnt` Einträge einer `sid` da sind, spätestens 1 s nach dem letzten Frame mit dem Empfangenen.
- Display-Merge: Jedes empfangene Topic aktualisiert nur seinen Teil in `s_latest_data`; alle anderen Felder bleiben erhalten. Callback an main.c liefert vollständigen Merge-Snapshot. Fehlende/unbekannte Felder tolerant ignorieren.
>- Keine Zusatz-Protokolländerungen ohne diese Datei zu aktualisieren.

## Konnektivitäts-Modal (Display)

- 3-Spalten-Layout: HotSpot/AP | WLAN | LTE nebeneinander in einem 640×420 px Panel.
- WLAN-/LTE-Spalte: Titel + farbige Status-LED (grün=aktiv, rot=inaktiv), Verbindungsname (SSID/Provider + Signal%), Switch zum Ein-/Ausschalten.
- AP-Spalte: Rein informativ (kein Switch!). Zeigt SSID, Anzahl verbundener Clients und Geräte-Liste (Hostname aus DHCP-Lease, Fallback MAC). AP darf **niemals** per Software deaktiviert werden, da er der einzige Kommunikationskanal zwischen ESP32 und Router ist.
- WLAN-Spalte: Status + Switch + Scan-Button + Dropdown + Passwortfeld (erst nach Netzwerkauswahl sichtbar). Switch steuert `womo_router_wifi_set_sta()` / `womo_router_wifi_enable_sta()`. QWERTZ-Tastatur für Passwort.
  - `womo_router_wifi_scan()` scannt gezielt nur AP/Master-Interfaces des Routers (per `iwinfo info`-Moduscheck), niemals das aktive STA/Client-Uplink-Interface: Ein Scan auf dem verbundenen Uplink-Radio kann dessen Verbindung einfrieren (beobachtet 2026-08-30 bei RUTX11, Radio0/wlan0=AP, Radio1/wlan1=STA).
- LTE-Spalte: Provider + Signalstärke + Switch. Switch steuert `womo_router_lte_enable()`.
- WLAN und LTE werden über UCI JSON-RPC am RUTX11 gesteuert (nicht über ESP-NOW).
- Router-WiFi-Zugangsdaten werden im NVS gespeichert (Namespace `rtr_wifi`, max. 20 Einträge, MRU-Reihenfolge). Bei Netzwerkauswahl aus Dropdown wird gespeichertes Passwort vorausgefüllt.
- Router-Poll-Task (`router_poll_task` in main.c) aktualisiert AP/WiFi/LTE/GPS alle 15 s und füllt `womo_connectivity_snapshot_t` mit AP-Feldern (ap_enabled, ap_ssid, ap_clients, ap_client_list).
- **ESP32-WLAN-Konfiguration**: Die AP-Spalte enthält einen "WLAN konfigurieren"-Button, der einen ESP32-eigenen WiFi-Scan (`womo_wifi_scan_async`) startet. Nach Auswahl + Passwort-Eingabe verbindet der ESP32 sich mit dem gewählten Netzwerk (`womo_wifi_connect`). Credentials werden in NVS (`wifi_cfg` / `known_list`) gespeichert. Reihenfolge in `ap_connect_task_fn` (Einstellungen > AP): erst `womo_sensorboard_send_wifi_credentials_wait()` (`cmd:"wifi_config"`, bis zu 3 Versuche à 3 s bis zum `cmd_ack`), dann `womo_wifi_connect()` – nach dem eigenen Wechsel ist das Sensorboard bei anderem Kanal per ESP-NOW nicht mehr erreichbar. Das Sensorboard prüft die Daten selbst und fällt bei Fehlschlag zurück. **Offene Netze sind nicht zulässig**: In der Scan-Liste sind offene Netze (`WIFI_AUTH_OPEN`/`OWE`) gesperrt, Speichern verlangt ein Passwort mit ≥ `WOMO_WIFI_PASS_MIN_LEN` (8) Zeichen (`firmware/shared/womo_config.h`). **Sicherheitsabfrage**: Ist in `womo_ap_cfg` ein Netz mit Passwort gespeichert und ändert sich SSID oder Passwort, muss vorher das bisherige Passwort eingegeben werden (`ap_pwchk_*`, Popup oben mit QWERTZ-Tastatur), alternativ die Service-PIN. Nach `AP_PWCHK_MAX_FAILS` (3) Fehlversuchen ist das für `AP_PWCHK_LOCK_MS` (60 s) gesperrt. Die Restzeit zählt im Popup sekündlich herunter (`ap_pwchk_tick()` aus `rtc_poll_timer_cb`), „Bestätigen“ ist solange deaktiviert, danach „Wieder möglich“. Zähler und Sperre bleiben beim Schließen des Modals erhalten, nicht über einen Neustart. Der AP-Tab zeigt rechts oben das WLAN des Sensorboards (grün = gleiches Netz wie Display, orange = anderes Netz, „keine Daten“ = kein `ctrl` seit 10 s). Der WiFi-Autoretry (`wifi_autoretry_task`) bevorzugt `womo_wifi_connect_best_known()` mit Fallback auf Kconfig-SSID.

## Einstellungen-Modal (Display)

- Tabs: Grenzwerte | Sensoren | LTE | AP | System (Reihenfolge `enum TAB_*`, Grenzwerte ist Starttab), 640×420 px Panel (`womo_settings_modal.c`).
- **Servicebereich**: Titel „Einstellungen“ `SVC_LONGPRESS_MS` (5 s) drücken → Dialog „Service-PIN“ (Ziffernfeld, `WOMO_SERVICE_PIN` aus `womo_credentials.h`, 4 Zeichen, sonst Build-Fehler; 3 Fehlversuche → 60 s Sperre, eigene Sperre je Abfrage). Danach wird der Tab „Service“ als Index `TAB_COUNT` angehängt (`svc_open()`), mit dem Schließen des Modals ist er wieder weg. Inhalt: Sensorboard-Kopplung (gekoppelte MAC bzw. Restzeit, „Neu koppeln“ → `womo_espnow_pair_start()`, Hinweis zum Tauschablauf). Der AP-Tab bleibt sichtbar (Kundenfunktion); in seiner Sicherheitsabfrage gilt die Service-PIN als Ausweg bei vergessenem Passwort. Beide Abfragen nutzen dasselbe Popup (`ap_pwchk_*`, Modus `PWCHK_AP`/`PWCHK_SVC`).
- **Alle Texte über `womo_locale`** (`womo_locale_get_string`, DE/EN), auch Tab-Titel. Bei Sprachwechsel aktualisiert `settings_locale_cb()` alle gemerkten Labels (Tab-Texte per `lv_tabview_set_tab_text`). Titel/Labels mit Umlauten in den `*_german`-Fonts. Neue Texte immer als `STR_*` in `womo_locale.h/.c` anlegen, nie hart codieren.
- **System**: Drei Spalten (`SYS_COL_*`, je 190 px): Töne (Systemtöne, Touch-Klick) | Sprache (Deutsch/English untereinander) | Zeit / RTC (Zeitquelle, letzte Sync, Batteriestatus der Display-RTC-Knopfzelle; `system_time_info_refresh()`, Zeilen brechen um). Darunter ab `SYS_DISP_Y` über die ganze Breite „Display“: Display-Aus-Automatik −/+ (deaktiviert, nach 1–5 min) und „Bei Meldung einschalten“ (Nie | Nur Alarm | Warnung + Alarm), siehe Abschnitt Hintergrundbeleuchtung.
- **Sensoren** (früher „RTC / Temp / Gas“, RTC-Teil liegt jetzt im System-Tab): Ruuvi wie unten beschrieben, zusätzlich Abschnitt „Gaswaage (GasBee)“ mit Zeilen Flasche A/B: Akku-% (rot < `THRESH_GASBEE_BATT_LOW_PCT` = 10 % ohne Laden oder „nicht kalibriert“, „(USB)“ im Konfigurationsmodus), Button **„Einrichten“** startet den geführten Assistenten (`gbw_*` in `womo_settings_modal.c`): GasBee an USB stecken → genau eine mit `cfg` gefunden → übernehmen (Display-NVS `womo_gasbee_cfg` + `cmd:"gasbee_config"`) → optional kalibrieren (Waage leer → Gewicht in Gramm eingeben → Prüfung am Advertising) → „USB abziehen“. Keine MAC-Auswahl für den Nutzer. Ergebnisse aus RX-/Timer-Task landen in einem Abholfach und werden im LVGL-Timer `rtc_poll_timer_cb` (100 ms) ausgewertet – kein `lvgl_port_lock()` in fremden Tasks. Bei `hx711.is_wired` (Kabelwaage aktiv) sind die Buttons gesperrt. Gemeinsame Statuszeile für Ruuvi und GasBee.
- Ruuvi-Tag-Konfiguration (Abschnitt im Tab Sensoren). Pro Slot (Innen/Außen): MAC-Anzeige + Batteriespannung als `Batt 3.00V (min 2.5V)` (falls Ruuvi statt verkabeltem BME aktiv; Warnschwelle temperaturabhängig wie Ruuvi Station: < −20 °C 2,0 V, −20…0 °C 2,3 V, sonst 2,5 V; Quelle ruuvi.com/battery-usage; kein Prozent, da Knopfzelle CR2477 flache Kennlinie hat), "Scan"-Button fordert per `ruuvi_scan_request` die aktuelle Tag-Liste vom Sensorboard an und zeigt sie zur Auswahl in einem Popup (analog zum WLAN-Scan im AP-Tab). Auswahl/Löschen speichert sofort in Display-NVS (`womo_ruuvi_cfg`) und sendet `cmd:"ruuvi_config"` ans Sensorboard.

- **LTE** (Mobilfunk-Tarif, Logik in `gui/womo_tariff.c/h`, NVS `womo_tariff`): Hinweis vor Ablauf (0–14 Tage, 0 = aus, Standard 3), Warnung Restvolumen (0–50 % in 5er-Schritten, 0 = aus, Standard 10), manueller Termin (−/+ Tag, „Heute aufgeladen“ = heute + Rhythmus, X löscht) mit Rhythmus 4 Wochen / 30 Tage / 1 Monat. Darunter der automatische Termin aus pass.telekom.de (X verwirft ihn, z. B. nach Tarifwechsel) und „Nächster Ablauf“.

## Hintergrundbeleuchtung (Display)

- Nur Ein/Aus über CH422G (`wavesahre_rgb_lcd_bl_on/off()`), kein Dimmen im Code. Logik in `main.c` (`backlight_*`), Einstellungen in `gui/womo_display_cfg.c/h` (NVS `womo_disp`: `auto_off` 0–5 min, `wake` Nie/Alarm/alle; Standard deaktiviert bzw. Warnung + Alarm).
- Ein Inaktivitäts-Timer `backlight_idle_timer`: Zeit aus `backlight_idle_timeout_ms()` – Ruhezeit 22–8 Uhr `QUIET_TOUCH_TIMEOUT_MS` (5 min) oder die kürzere Display-Aus-Automatik, tagsüber die Display-Aus-Automatik (0 = kein Timer). Jede Berührung (`touch_wake_cb`, auch in Modals) startet ihn neu; die erste Berührung bei dunklem Display weckt nur.
- Backlight-Taste: von Hand aus bleibt aus (kein Timer), von Hand an startet den Timer.
- Neue Meldung im Fehler-Stapel (steigende Flanke wie beim Buzzer, Alarm = `>= WOMO_STATUS_ERROR`) schaltet je nach `wake` ein (`backlight_wake_for_message`); in der Ruhezeit weckt nur ein Alarm.

## Mobilfunk-Tarif (Display)

- Quelle automatisch: `valid_until_iso` aus `datapass.json` (Router wandelt „16. November 2026“ in `datapass.sh` um, siehe `tools/router-webui/deploy.sh`). `router_poll_task` → `womo_tariff_set_auto_due()` + `womo_tariff_set_volume()`. Letzter automatischer Termin bleibt im NVS, damit „abgelaufen“ auch ohne Datenverbindung (pausierter Tarif) erkannt wird. Hat Vorrang vor dem manuellen Termin.
- Quelle manuell (Tarife ohne pass.telekom.de): Termin + Rhythmus, rollt nach Ablauf selbst weiter (ob verlängert wurde ist unbekannt → kein „abgelaufen“).
- Auswertung in `ui_update_timer_cb` nur mit plausibler Uhrzeit (Jahr ≥ 2024). Zwei Quellen im Fehler-Stapel, beide nur WARNING: `SENSOR_ERR_SRC_TARIFF` („Tarif bis 16.11.“ / „Tarif abgelaufen“, Übergang läuft ab → abgelaufen meldet trotz Quittierung erneut) und `SENSOR_ERR_SRC_DATA` („Daten 8%“). Texte über `STR_TARIFF_SRC_*`.
- Datum intern als JJJJMMTT (`uint32_t`), Rechnung in ganzen Tagen ohne Zeitzone. „Gültig bis“ schließt den Tag ein.
- **Kein NVS aus `router_poll_task`** (Stack im PSRAM, NVS schaltet den Flash-Cache ab → Assert `s_task_stack_is_sane_when_cache_frozen`). Gleiches gilt für `sensorboard_init_task` (PSRAM-Stack): `womo_espnow` lädt/speichert die Kopplung deshalb nur in `espnow_rx_task` (interner Stack). Boot-Schleife mit Assert `esp_task_stack_is_sane_cache_disabled` am 2026-10-25, als `peer_load` noch in `womo_espnow_init()` lief. `womo_tariff_set_auto_due()` setzt nur RAM + Dirty-Flag, `womo_tariff_flush()` schreibt am Anfang von `ui_update_timer_cb` (LVGL, interner Stack).

## GasBee-Akku-Warnung (Display)

- Fehlerquelle `SENSOR_ERR_SRC_GASBEE` (nur WARNING, Text über `STR_GASBEE_SRC_*`), ausgewertet in `ui_update_timer_cb` aus `hx711.bee[0..1]` (nur bei `is_gasbee` und `present`):
  - „Akku A 8 %“: `batt_pct < THRESH_GASBEE_BATT_LOW_PCT` (10 %) und nicht am Laden.
  - „Laden <0°C“: `charging` und Außentemperatur (`bme680.temperature_c`, also `bme_out`: BME280 oder Ruuvi außen) `< THRESH_GASBEE_CHARGE_MIN_C` (0 °C). Grund: Der GasBee-Lader BQ25101 hat keine Temperaturüberwachung (`firmware/gasbee_nrf/README.md`). Ohne Außensensor kein Kälte-Hinweis.
  - Neuer Grund (andere Flasche oder Laden bei Kälte) setzt die Quittierung zurück.
- Konstanten fest in `gui/womo_thresholds.h` (nicht im Tab Grenzwerte). Einrichten-Assistent zeigt beim Schritt „USB anstecken“ den Hinweis `STR_GBW_COLD_HINT`.
- Chiptemperatur der GasBee (nRF52840 TEMP) als Alternative geprüft und verworfen: Genauigkeit laut Datenblatt ±5 °C, beim Laden zusätzlich Eigenerwärmung (Details: `memory-bank/activeContext.md`).

## Wetter / Unwetter (Display)

- Wetter nur von Open-Meteo (`womo_weather_http.c`, Task `om_task`, alle 5 min): `current`, `daily` (5 Tage) und `hourly` (24 h: Böen, Niederschlag, Schnee, Temperatur, gefühlte Temperatur, WMO-Code).
- Unwetter: **eigene Auswertung** der Stundenwerte in `womo_weather_warn.c` nach den DWD-Warnkriterien (Schwellwerte als `WW_*` in `womo_weather_warn.h`, Quelle dort verlinkt). Keine amtliche Warnung, Hinweis wird im Modal/Popup angezeigt. Stufe 1 nur im Forecast-Modal, ab Stufe 2 Badge + Auto-Popup.
- Meteoalarm wurde 2026-10-20 entfernt (alter Endpunkt `for-coordinates` existierte nicht → HTTP 404; MeteoGate-EDR liefert nur ganze Länder, bis ~850 KB pro Abruf).

## Display-Stack (LVGL)

- LVGL **v9.x** als Managed Component (`lvgl/lvgl: ^9.2.0`, aktuell aufgelöst auf 9.5.0, `espressif/esp_lvgl_port: ^2`)
- RGB-Interface 800×480, 16 MHz PCLK, 2 Framebuffer in PSRAM (Direct Mode + Avoid-Tearing)
- Bounce-Buffer: 4 Zeilen × 800 px × 2 Byte × 2 Buffer = 12,8 KB DMA-SRAM
- LVGL-Task auf Core 1, Stack 16 KB intern (`MALLOC_CAP_INTERNAL`, `lvgl_port.c`), Priorität 4
- Touch: GT911 via `esp_lcd_touch_gt911`, eigener Read-Callback in `lvgl_port.c`

# Plan / Act Arbeitsablauf (Cursor-Stil)

Sofern der Nutzer nicht explizit darauf verzichtet (z. B. **„Plan überspringen, direkt umsetzen"** oder **„einfach fixen"** ohne Mehrdeutigkeit), werden **zwei Modi** verwendet. Dies entspricht dem Cursor-Workflow: PLAN → freigeben → ACT.

## Plan-Modus (Standard)

- **Erste Zeile jeder Plan-Modus-Antwort muss exakt lauten:** `# Mode: PLAN`
- **Das Repository darf in keiner Weise verändert werden**, einschließlich:
  - Keine Dateien anlegen, bearbeiten oder löschen (Quellcode, Konfiguration, Docs, **einschließlich `./memory-bank/**`-Dateien**).
  - Keine Multi-Datei-Edits, Schnellkorrekturen oder Patch-Änderungen.
  - Keine Terminal-Befehle, die den Workspace verändern (Installationen, Builds mit zu übernehmenden Ausgaben, `git`-Schreibbefehle usw.).
- **Erlaubt im Plan-Modus:** Dateien lesen/durchsuchen, Fragen beantworten, Schritte auflisten, Risiken benennen und einen **schriftlichen Plan** (Markdown) erstellen.
- **Plan-Modus-Antworten enden** mit einem Hinweis zum weiteren Vorgehen, z. B. **`ACT` eingeben, wenn der Plan freigegeben wird** (oder den Plan zuerst verfeinern).

## Act-Modus

- **Nur betreten**, wenn die Nutzernachricht die Umsetzung **klar freigibt**, z. B. **`ACT`**, **`act`** oder Formulierungen wie **„mach das"**, **„Plan umsetzen"**, **„freigegeben"** direkt nach einem Plan – oder wenn explizit auf die Planungsphase verzichtet wurde.
- **Erste Zeile jeder Act-Modus-Antwort muss exakt lauten:** `# Mode: ACT`
- **Danach** dürfen Dateien bearbeitet, Befehle ausgeführt und **`./memory-bank/`** bei Bedarf aktualisiert werden.
- Nach dem Ende eines Act-Modus-Durchgangs wird die nächste Nutzernachricht wieder im **Plan-Modus** behandelt, es sei denn, der Nutzer gibt erneut mit **`ACT`** (oder gleichwertig) frei.

## Wenn der Nutzer Code-Änderungen im Plan-Modus anfordert

- **Nicht umsetzen.** Mit `# Mode: PLAN` antworten, den Plan kurz wiederholen oder anpassen und den Nutzer bitten, **`ACT`** einzugeben, wenn die Änderungen angewendet werden sollen.

---

# Memory Bank (persistenter Kontext)

Dieses Repository nutzt eine **Memory Bank** unter `./memory-bank/` – strukturiertes Markdown, das Sessions überlebt, ähnlich dem Cursor-Workflow.

Kontextebenen (tiefere Dateien nach den Grundlagen lesen): **projectbrief** → **productContext** / **systemPatterns** / **techContext** → **activeContext** → **progress**.

## Was Claude tun soll

1. **Vor substanzieller Arbeit** alle folgenden Dateien unter `./memory-bank/` lesen, wenn die Aufgabe vom Projektstand abhängt (bei nicht-trivialer Arbeit keine Option). Im **Plan-Modus** ist Lesen für die Planung erlaubt; **Dateien erst im Act-Modus bearbeiten**, es sei denn, der Nutzer bat ausdrücklich nur um ein Dokumentations-/Memory-Update ohne Code-Änderung.
   - `projectbrief.md` – Umfang und Ziele
   - `productContext.md` – Produktabsicht und UX
   - `systemPatterns.md` – Architektur und Konventionen
   - `techContext.md` – Stack und Einschränkungen
   - `progress.md` – Erledigt / Ausstehend / Bekannte Probleme
   - `activeContext.md` – Aktuelle Aufgabe und Entscheidungen

2. **Während der Act-Modus-Arbeit** `activeContext.md` mit der aktuellen Aufgabe synchron halten (aktualisieren, wenn sich der Fokus verschiebt).

3. **Nach bedeutenden Meilensteinen** (im Act-Modus) `progress.md` und betroffene Docs in `./memory-bank/` aktualisieren.

4. Wenn der Nutzer die **Memory Bank aktualisieren** (oder ähnliches) möchte, **jede** Datei in `./memory-bank/` öffnen und prüfen, dann das Geänderte aktualisieren – insbesondere `activeContext.md` und `progress.md`, auch wenn andere Dateien unverändert bleiben. Umfangreiche Memory-Bank-Schreibvorgänge bevorzugt im **Act-Modus** durchführen, es sei denn, der Nutzer bat nur um ein Dokumentations-Update.

5. **Kurze, sachliche Updates** gegenüber langen Texten bevorzugen. Dateien, Symbole und Tickets referenzieren statt Code zu duplizieren.

Diese Dateien nicht löschen; sie mit dem Projekt weiterentwickeln.