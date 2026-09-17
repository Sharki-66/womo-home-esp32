# Active Context — WoMoHome Display

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
