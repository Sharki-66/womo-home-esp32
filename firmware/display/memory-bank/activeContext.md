# Active Context — WoMoHome Display

## Bugfix: Tag-Hintergrund bei Nacht-Schrift (2026-10-21, gebaut ungetestet)

Symptom: nachts helle Schrift, aber Tag-Hintergrund; nach Neustart weg, Ursache nicht per Log belegt. Härtung: `time_update_timer_cb` (`main.c`) merkt `last_applied_mode` erst nach erfolgreichem `load_background_image()` (sonst Retry jede Sekunde) und zieht Hintergrundbild + Bildschirmfarbe einmal pro Minute nach. `womo_theme_get_twilight_color()` prüft Jahr ≥ 2024 und liefert bei ungültiger Zeit die Farbe des aktuellen Modus statt pauschal Tag (ohne Cache). Außerdem: WLAN-Treiber-Tag `wifi` auf `LOG_LEVEL_WIFI` (DELBA-Infos weg).

## RUTX11-Modem-Ausfall + Router-WebUI-Härtung (2026-10-20, deployt, auf Router geprüft)

Symptom: Display/WebUI ohne LTE, `gsmctl` „Couldn't retrieve data“. Befund: Modem in GPIO-Neustartschleife (`modem_trackd`), Ports als `ttyUSB37–39` statt `ttyUSB1–3` → `gsmd` scheiterte; dazu ~90 Zombie-`gsmctl` und gestapelte `poll.sh` (keine Sperre). Wahrscheinlicher Auslöser: congstar-Tarif war pausiert (keine Datenverbindung → RutOS startet Modem neu), unser Polling verhinderte die Erholung. Nicht belegbar, RAM-Log durch Reboot verloren. Reboot + Aufladung behoben.

Fix in `tools/router-webui/deploy.sh` (Commit 857ae71, gepusht auf `openwiki-test`): Sperre `/tmp/womo_poll.lock`, `gsmctl` mit `timeout 8`, kein Modemzugriff ohne ubus `gsm.modem0` (`lte.modem` = up/down in `status.json`), `cgi-bin/lte` liest nur Cache, Log-Schnappschuss bei Modem-Ausfall nach `/etc/womo/log/` (max. 5). Geprüft: Sperre greift, `gsm.modem0` existiert im Normalbetrieb. Verbliebene 5 Zombie-`gsmctl` stammen von `rpcd` (RutOS), stabil.

Offen: `simslot` leer (`AT+QUIMSLOT?` liefert keine Ziffer). Display wertet `lte.modem` noch nicht aus (Option: „Modem nicht erreichbar“ im Konnektivitäts-Modal).

## Bugfix: Stack-Overflow om_task + lwIP-Assert (2026-10-20, auf Hardware getestet)

Log: `stack overflow in task om_task`, im nächsten Boot `assert tcp_slowtmr ... persist ticking with empty send buffer`. `om_task` (Open-Meteo, `womo_weather_http.c`) hatte nur 4096 Byte Stack für HTTPS/TLS; andere HTTPS-Tasks 6–8 KB. Auf 8192 (PSRAM) erhöht, Stack-Reserve wird nach jeder Abfrage gemessen (`ESP_LOGW` < 1 KB, sonst `ESP_LOGD`). Vermutung: lwIP-Assert ist Folgeschaden (unerkannter Überlauf, Canary-Check nur beim Taskwechsel, lwIP-Strukturen ebenfalls im PSRAM). Falls der Assert wiederkommt: `CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK` temporär aktivieren und weitersuchen.

Health-Log reaktiviert: `log_runtime_heap_stats()` (`main.c`, aus `router_poll_task`, alle `HEAP_LOG_POLL_CYCLES` ≈ 10 min) loggt per `ESP_LOGW` Uptime, internen/DMA-Heap (frei, größter Block, Minimum) und Stack-Reserve von `router_poll`/`wifi_autoretry`. Kein PSRAM-Abfragen (Flackern). `geocode` misst seine Stack-Reserve nach jeder HTTPS-Abfrage wie `om_task`. Erster Langzeitlauf (~4,2 h, 2026-10-20): interner Heap stabil bei ~71,5 KB frei / 31,7 KB größter Block. `gsmctl -o`-Fehler „Couldn't retrieve data“ geklärt, siehe RUTX11-Modem-Abschnitt.

## Unwetter: Meteoalarm ersetzt durch eigene Auswertung (2026-10-20, auf Hardware getestet)

Meteoalarm lieferte HTTP 404 (Endpunkt `for-coordinates` existiert nicht). MeteoGate-EDR (`api.meteogate.eu/warnings`) geprüft: nur Länder-IDs, Antworten bis ~850 KB (IT) → verworfen. Neu: `womo_weather_warn.c/.h` wertet 24 Open-Meteo-Stundenwerte (`hourly`, in `womo_weather_http.c` mit abgefragt) nach DWD-Warnkriterien aus (Wind, Starkregen, Dauerregen, Schnee nach Höhe, Gewitter, gefrierender Regen, Frost, Hitze). Ergebnis über `womo_weather_http_set_warn_callback()` → `weather_warn_update_cb` in `main.c`: Stufe 1 nur im Forecast-Modal, ab Stufe 2 Badge + Auto-Popup. Texte über `STR_WARN_*`. Regeln auf dem PC mit echten Open-Meteo-Daten und Testfällen geprüft. `womo_meteoalarm.*` und `harica_root_pem.h` gelöscht.

## Einstellungs-Modal umgebaut (2026-10-19, gebaut, noch nicht auf Hardware getestet)

Tabs jetzt AP | Sensoren | Grenzwerte | System (AP = Starttab, `enum TAB_*`). Sprache und RTC-Info sind in den System-Tab gewandert (zwei Spalten: Töne + Sprache | Zeit / RTC, `system_time_info_refresh()`). „RTC / Temp / Gas“ heißt jetzt „Sensoren“ (`build_tab_sensoren`, Ruuvi + GasBee nach oben gerückt, Platz für weitere Sensoren). Alle Texte der umgebauten Tabs über `womo_locale` (neue `STR_SET_TAB_*`, `STR_TIME_*`, `STR_SENS_*`, `STR_ON/OFF`), Live-Update in `settings_locale_cb()`. Die Ton-Buttons lesen ihren Zustand jetzt aus dem Buzzer-Modul statt aus dem Button-Text. Noch hart codiert: Statustexte Ruuvi/GasBee/Assistent, AP-Tab, Popups.

## GasBee-Einrichten-Assistent (2026-10-18, auf Hardware getestet, Ruuvi-Scan läuft ebenfalls)

Scan-Popup für GasBee ersetzt durch Assistenten `gbw_*` (`womo_settings_modal.c`): USB anstecken → gefunden → übernehmen → kalibrieren (Nullpunkt, Referenzgewicht in g, Prüfung ±1 %/±50 g nach ≥ 2 Messzyklen) → USB abziehen. Ruuvi- und GasBee-Ergebnisse gehen jetzt über ein Abholfach + `rtc_poll_timer_cb` (vorher `lvgl_port_lock(-1)` im RX-/esp_timer-Task). `make_cfg_row()` mit Button-Text/-Breite. Neue API in `womo_sensorboard.h`: `send_gasbee_setup_scan`, `send_gasbee_cal_session`, `send_gasbee_cal`, `set_gasbee_cal_result_callback`; `cfg`/`seq` in `womo_gasbee_scan_entry_t`, `config_mode` in `hx711.bee[]`.

## Scan-Listen ein Frame pro Gerät (2026-10-18, auf Hardware getestet)

`ruuvi_scan`/`gasbee_scan` kommen jetzt als ein Frame pro Gerät (`sid`, `idx`, `cnt`). `womo_sensorboard.c` setzt zusammen (`scan_asm_*`, esp_timer 1 s Timeout), GUI-Callbacks unverändert. Nur zusammen mit passender Sensorboard-Firmware lauffähig.

## GasBee-Zuordnung im Einstellungs-Tab (2026-10-17, auf Hardware getestet)

Tab „RTC / Ruuvi“ → „RTC / Temp / Gas“. Neuer Abschnitt „Gaswaage (GasBee)“
(Flasche A/B, Scan-Popup, NVS `womo_gasbee_cfg`, `cmd:"gasbee_config"`), Layout
des Tabs verdichtet (gemeinsame Zeilenfunktion `make_cfg_row()`, gemeinsame
Statuszeile `rtc_tab_set_status()`). `womo_sensorboard.c/h`: Parser für `hx.src`
+ Akku/Flags je Flasche, Topic `gasbee_scan`, Kommandos `gasbee_config`/
`gasbee_scan_request`. Gegenstück im Sensorboard: `gasbee_ble_scanner.c`,
`gas_source.c`. Scan + Zuordnung Flasche A funktioniert.

## Datenvolumen-Anzeige im LTE-Panel (2026-10-04)

Neue Funktion `womo_router_get_data_pass()` in `womo_router_uci.c/h`: liest
`http://<ROUTER_IP>:8080/datapass.json` (statische Datei der dedizierten
Router-WebUI-uhttpd-Instanz, kein ubus/Login nötig — siehe
`tools/router-webui/deploy.sh`, dort per Cron alle 30 min + einmalig nach
Boot aktualisiert). Liefert Restvolumen/Gesamtvolumen/Einheit/Gültigkeit
des aktuellen Congstar-Datenpasses (Quelle: pass.telekom.de).

Wichtig: Die Telekom-Seite nennt das Feld irreführend
"remaining-volume-value", zeigt aber tatsächlich das **Restvolumen**
(verifiziert gegen die Congstar-App) — NICHT den Verbrauch. In `main.c`
und `womo_connectivity_modal.c` entsprechend als "remaining" (nicht
"used") benannt.

Poll-Rhythmus in `router_poll_task` (main.c): eigener, viel selteneren
Zähler (`ROUTER_DATAPASS_POLL_CYCLES` = 30 min) statt der normalen 15s,
da sich der Wert serverseitig eh nur alle 30 min ändert — plus einmal
sofort nach Verbindungsaufbau (`poll_count == 1`).

Anzeige: zweite Zeile im LTE-Status-Label des Connectivity-Modals
(`update_lte_status_label()`), Format `"<Betreiber> (<Signal>%)\n<Rest>/<Gesamt>
<Einheit> - bis <Datum>"`. Noch nicht auf echtem Display geprüft, ob
zwei Zeilen im `LV_LABEL_LONG_DOT`-Modus sauber dargestellt werden.

**Voraussetzung:** Setzt voraus, dass `tools/router-webui/deploy.sh`
auf dem RUTX11 ausgeführt wurde (Port-8080-Instanz + `/etc/womo/`).
Ohne das liefert `womo_router_get_data_pass()` einfach ESP_FAIL,
Datenvolumen-Zeile bleibt im Display unsichtbar (kein harter Fehler).

## RUTX11 Dual-Radio-WLAN-Uplink: 2,4GHz + 5GHz (2026-09-17)

Ausgangsproblem: WLAN-STA-Uplink (Ziel „Solaris-Gast", 5 GHz) konnte am
RUTX11 nicht verbinden ("Verbindung wird gesucht"), obwohl der Router über
den AP erreichbar war. Root Cause: `default_radio1` (STA-Sektion) war auf
`device: radio0` konfiguriert — dieselbe physische 2,4-GHz-Funkeinheit wie
der AP „Malibu-622" (`default_radio0`). `iwinfo devices` zeigte deshalb nur
ein aktives Interface (den AP); für die STA-Sektion wurde nie ein eigenes
Interface hochgefahren.

Hardware-Fakt (per `iw phy phy0 info` verifiziert): radio0 (2,4 GHz)
unterstützt AP+STA gleichzeitig auf einem Chip, aber nur auf **demselben
Kanal** (`#channels <= 1`). radio1 (5 GHz, Kanal 36+, VHT80) ist komplett
eigenständig.

Architekturentscheidung (Option A, siehe Konversation): Zwei feste
STA-UCI-Sektionen für Dual-Radio-Uplink, beide auf `network: wan1` (mwan3
unverändert, kennt nur die logische Schnittstelle):
- `default_radio1` → `device: radio1` (5 GHz, eigenständig)
- `sta_radio0` (neu angelegt) → `device: radio0` (2,4 GHz, Kombi-Betrieb
  mit dem AP, gleicher Kanal)

Nur eine der beiden ist je `disabled=0`; welche, entscheidet der Kanal des
vom Nutzer gewählten Scan-Ergebnisses (≥36 → 5GHz-Sektion, sonst 2,4GHz).

**Code-Änderungen:**
- `womo_router_uci.h`: `womo_router_channel_is_5ghz()` (Band aus Kanal),
  `womo_router_wifi_set_sta()` um `channel`-Parameter erweitert.
- `womo_router_uci.c`: `find_sta_interface()` komplett umgebaut (prüft nur
  noch die zwei festen Sektionen, kein UCI-Full-Dump-Scan mehr, kein
  Caching mehr — Zustand kann sich durch Connect/Disconnect jederzeit
  ändern). `womo_router_wifi_set_sta()` wählt Ziel-Sektion nach Band,
  deaktiviert die jeweils andere. `womo_router_wifi_scan()` scannt jetzt
  **alle** vorhandenen Interfaces (beide Radios) statt nur eines;
  `device_is_sta()` → `device_scan_would_disrupt()` (schließt nur noch
  aktiv assoziierte STA-Interfaces aus, nicht mehr jedes im Client-Modus —
  ein noch unverbundenes STA-Interface darf gescannt werden).
- `womo_connectivity_modal.c`: Scan-Liste zeigt Band hinter der SSID
  („SSID (2,4 GHz) / (dBm / %)"). Ausgewählter Kanal wird durch den
  gesamten Connect-Flow gereicht (`selected_channel` im Modal-Context,
  `wifi_connect_params_t.channel`, Snapshot-Feld `wifi_channel` als
  Fallback beim Reconnect ohne neuen Scan).

**Router-seitig bereits live umgesetzt** (nicht im Git-Repo, reine
Config): `sta_radio0`-Sektion angelegt, `default_radio1.device` auf
`radio1` korrigiert, committed.

**Vorsicht bei künftigen `wifi reload`-Aufrufen an diesem Router:** Ein
`wifi reload` mit einer STA-Sektion mit leerer `ssid` hat den Router
einmal temporär komplett unresponsive gemacht (Reboot nötig). Sektionen
vor einem Reload immer mit gültiger SSID oder `disabled=1` belassen.

**Noch nicht getestet:** Echter Verbindungsaufbau zu einem 2,4-GHz-Netz
über die neue `sta_radio0`-Kombi-Sektion (AP+STA gleichzeitig auf
radio0) — nur die 5-GHz-Seite (Solaris-Gast) ist live verifiziert.

## RUTX11-LAN-Subnetz geändert: 192.168.10.1 → 192.168.55.1 (2026-09-17)

Ursache: Subnetz-Kollision zwischen dem Wohnmobil-RUTX11-LAN (`192.168.10.0/24`)
und dem Heimnetz „Solaris" (nutzt selbst `192.168.10.0/24`). Sichtbar geworden,
als über WireGuard-VPN (Laptop → Solaris-Heimnetz, per deSEC-DynDNS, wegen
E.ON-CGNAT nur über IPv6 erreichbar) bei aktivem Tunnel der Wohnmobil-RUTX11
nicht mehr erreichbar war — die vom WireGuard-Peer mitgebrachte Route für
`192.168.10.0/24` (Solaris-Heimnetz-VLAN) gewann gegen die lokale Route zum
Wohnmobil-Router.

Fix: RUTX11-LAN-IP am Router selbst auf `192.168.55.1` umgestellt (unüblicher
Bereich, keine Kollisionsgefahr mit typischen Heim-/Campingplatz-Subnetzen).
Code angepasst an 4 Stellen:
- `firmware/shared/womo_config.h` (`WOMO_ROUTER_IP`)
- `firmware/display/main/Kconfig.projbuild` (Default `WOMO_ROUTER_IP`)
- `firmware/display/main/network/womo_router_uci.h` (Fallback `CONFIG_WOMO_ROUTER_IP`)
- `firmware/display/sdkconfig` (aktueller Build-Wert)

Separat (reine Router-Konfiguration, kein Code-Bezug): LTE-Interface
(`mob1s1a1`) am RUTX11 von `pdptype: ip` auf `pdptype: ipv4v6` umgestellt,
damit die Telekom-APN `internet.telekom` auch IPv6 per DHCPv6-PD liefert
(Voraussetzung für den IPv6-only-VPN-Zugang zum Heimnetz über LTE).

## Aktueller Fokus

Branch `feature/sensorboard-webdashboard`. Zuletzt abgeschlossen: Vollständige Überarbeitung des Konnektivitäts-Modals und des WiFi/LTE-Pill-Buttons. Commit `a321dc8`.

## Zuletzt abgeschlossen (diese Session)

- [x] AP-Code aus `womo_connectivity_modal.c` entfernt (RUTX11-AP-Felder)
- [x] `womo_connectivity_snapshot_t` umgebaut: ESP32-eigene WiFi-Felder statt RUTX11-AP
- [x] `s_router_reachable` Fix: nur via `womo_router_get_wifi_status()` (HTTP/UCI) gesetzt
- [x] Early-return Bug in `update_wifi_status_label()` / `update_lte_status_label()`: `!router_reachable`-Block jetzt zuerst
- [x] Settings-Modal: 5 Tabs (Sprache, RTC, Grenzwerte, System, AP-Konfiguration)
- [x] `womo_ap_cfg_load()` in `womo_settings_modal.h` exportiert
- [x] Boot-WiFi auf gespeicherte AP-Konfiguration aus NVS umgestellt
- [x] WiFi/LTE-Pill-Button: AP-Sektion (grüne Mitte, 16×36 px) mit „A/P"-Label, zwei schwarze Divider bei CENTER ±9, Pill 118 px breit

## Bugfix: bme_in/bme_out „valid"-Erkennung (2026-08-24)
`womo_sensorboard.c`: `valid` wurde bisher gesetzt sobald `temp_c`/`rh_pct`/`press_hpa`
im JSON-Frame vorhanden waren ([:966](../main/sensorboard/womo_sensorboard.c#L966),
[:1008](../main/sensorboard/womo_sensorboard.c#L1008)) — das Sensorboard sendet diese
Felder aber immer, auch als `0.0`-Platzhalter wenn gar kein Sensor (weder verkabelt
noch Ruuvi) verfügbar ist. Folge: Außen-Sensor wurde fälschlich als „Bus-Sensor aktiv"
gemeldet, obwohl nichts angeschlossen war → Ruuvi-Scan-Button in den Settings blieb
disabled (`ruuvi_wired_active()` in `womo_settings_modal.c:922-927`).

Fix: Bedingung auf `cJSON_IsString(chip)` umgestellt — das `chip`-Feld sendet das
Sensorboard nur, wenn tatsächlich Daten vorliegen (verkabelt oder Ruuvi-Fallback),
siehe `sensorboard/main/network/espnow_modem.c`.

## Ruuvi-Scan-Fehlersuche (offen, 2026-08-25)
`ruuvi_scan_request` (Settings-Button) bekommt keine ACK-Antwort vom Sensorboard
("waiting for ACK for ~4s"), Popup bleibt leer. Bug gefunden+gefixt: `ruuvi_scan_btn_cb()`
in `womo_settings_modal.c:1150` brach bei `s_ruuvi_scan_in_progress` (bis zu 4s nach jedem
Versuch) komplett stillschweigend ab → wiederholtes Drücken zeigte keinerlei Reaktion.
Jetzt Statusmeldung "Scan läuft bereits - bitte warten" ergänzt.

Kompletter Code-Audit (`ruuvi_ble_scanner.c/.h`, `ble_manager.c` Sensorboard-seitig,
Frame-Parsing/Pending-Tracking hier) zeigt keinen Logikfehler, der erklärt warum auch
ein einzelner isolierter Tastendruck keine Antwort bekommt. `LOG_LEVEL_SENSORBOARD` hier
war auf `ESP_LOG_WARN` (`display_config.h:28`) → RX/ACK-Verarbeitungslogs für Tag
`sensorboard` unsichtbar, während sensorboard-seitig `LOG_LEVEL_SENSORBOARD` bereits auf
INFO stand — asymmetrisch, daher blind auf dieser Seite. Jetzt ebenfalls temporär auf
`ESP_LOG_INFO` gesetzt. Nach Abschluss der Fehlersuche beide Seiten zurück auf `WARN`.
Nächster Schritt: einzelnen isolierten Scan-Tastendruck testen, Sensorboard- UND
Display-Monitor parallel beobachten (RX/TX auf Sensorboard, Frame-Verarbeitung hier).

**Root Cause gefunden (Sensorboard-seitig):** Race Condition in
`ble_manager_init()` (`sensorboard/main/network/ble_manager.c`) — `s_ready`
wurde nach `nimble_port_freertos_init()` gesetzt, wodurch der allererste
Scan-Start-Versuch beim Boot lautlos verworfen wurde. NimBLE-Scan lief seitdem
nie. Fix angewendet (`s_ready` jetzt davor gesetzt). Danach: erster Scan
funktioniert, aber wiederholte Scans laufen wieder in "Keine Antwort vom
Sensorboard" (altes Timeout-Symptom, jetzt seltener/intermittierend statt
permanent). GasBee als Ursache ausgeschlossen (Hardware existiert noch nicht).

**Robustheit ergänzt:** `ruuvi_scan_btn_cb`/`ruuvi_scan_timeout_cb` in
`womo_settings_modal.c` hatten keinen Retry für den `ruuvi_scan_request`-
Kommando-Umlauf (im Gegensatz zu periodischen Topics, die sich durch
Wiederholung selbst heilen). Jetzt bis zu `RUUVI_SCAN_MAX_RETRY=2` automatische
Wiederholungen bei Timeout, Statusanzeige "Keine Antwort - Versuch X/Y"
während Retry. Retry-Zähler wird bei neuem manuellem Scan-Start zurückgesetzt.

**Stuck-State-Bug gefunden+behoben (2026-08-25):** Nutzer berichtete: nach
längerem Testen findet der Scan-Button irgendwann gar nichts mehr, aber
Schließen+Neuöffnen des Settings-Modals behebt es sofort. Ursache: Cleanup-
Code beim Modal-Schließen setzt `s_ruuvi_scan_in_progress = false` explizit
zurück — das ist die einzige Stelle außer Erfolg/finalem Timeout, die das tut.
Wenn `lv_timer_create()` für den 4s-Timeout je fehlschlägt (NULL, z. B. bei
LVGL-Timer-Pool-Erschöpfung durch nicht sauber aufgeräumte Timer über viele
Scan-Versuche), blieb die Flag für immer `true` hängen → Button reagierte
dauerhaft nur noch mit "Scan läuft bereits", bis Modal neu geöffnet wurde.
Fix: NULL-Check nach `lv_timer_create()` an beiden Stellen (`ruuvi_scan_btn_cb`
und Retry-Pfad in `ruuvi_scan_timeout_cb`) — bei Fehlschlag wird die Flag
sofort zurückgesetzt statt dauerhaft hängen zu bleiben.

## Bugfix: WLAN-Scan hängte Router-Uplink ein (2026-08-30)

Symptom: Scan-Button in der WLAN-Spalte des Konnektivitäts-Modals zeigte
inkonsistent mal 1 Netz ("Solaris"), mal alle ~9 Netze der Umgebung. Einmal
führte der Scan sogar dazu, dass die Router-WLAN-Verbindung komplett einfror
(LuCI-Weboberfläche nicht mehr erreichbar), bis der Nutzer manuell das
Uplink-Netz wechselte (Solaris → Malibu-622).

Root Cause: `womo_router_wifi_scan()` in `main/network/womo_router_uci.c`
nahm ungeprüft das erste von `iwinfo devices` gelieferte Interface für den
Scan — je nach Zustand/Timing konnte das auch das **aktive STA/Client-
Uplink-Interface** treffen. Ein `iwinfo scan` auf einem gerade verbundenen
Client-Radio zwingt es zum kanalübergreifenden Scannen und kann die
Verbindung einfrieren.

Verifiziert durch Nutzer direkt am Router: `radio0`/`wlan0` = AP,
`radio1`/`wlan1` = STA (Uplink zu Malibu-622).

Fix: Neue Hilfsfunktion `device_is_sta()` (analog zum bereits vorhandenen
Muster in `womo_router_get_wifi_status()`) prüft per `iwinfo info` den
`mode` jedes Kandidaten-Interfaces und schließt `mode=="Client"` sowohl bei
der `iwinfo devices`-Auswertung als auch in der Fallback-Liste aus. Damit
wird nie mehr auf dem aktiven Uplink-Interface gescannt — robust auch bei
Namensänderungen nach Router-Reset/Update, da kein Hardcoding auf
"wlan0"/"wlan1".

## Offene Fragen / nächste Schritte

- Kalibrierungs-Web-UI (HX711, Batterie, Tank) — größeres Feature, noch nicht begonnen
- GasBee BLE-Verbindungsstatus im Hauptscreen anzeigen

## Letzte Architekturentscheidungen

| Entscheidung | Begründung |
|---|---|
| `s_router_reachable` nur via WiFi-UCI | LTE/AP-Abfragen (gsmctl) geben `ESP_OK` mit leeren Daten auch ohne RUTX11 zurück |
| AP-Status als grüne Sektion im Pill-Button | Separater AP-Button visuell unpassend; Integration in bestehenden Button sauberer |
| `update_router_btn_overlay()` vereinfacht | Overlay-Approach (Linien, Labels über Icons) durch einfache Farbänderung ersetzt |
| Early-return Reihenfolge in Modal-Update | Task-/Cooldown-Returns dürfen `!router_reachable`-Block nicht überspringen |
