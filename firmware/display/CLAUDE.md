# Claude Code — project context

# Instructions (WoMoHome Display)

## Build & Flash
- Build, Flash und Monitor werden vom Nutzer gestartet (siehe VS Code Tasks „Build/Flash/Monitor Display Firmware"). Nur erklären, keine Befehle ausführen.

## Projektüberblick
- Ziel: Anzeige und Steuerung von Wohnmobil-Daten. Router liefert WLAN/LTE/GNSS; Display visualisiert; Sensorboard sammelt analoge/digitale Messwerte. Kommunikation zwischen Display und Sensorboard per RS485.

## Hardware
- Router: Teltonika RUTX11 (OpenWRT-basiert) für WLAN/Netz/Hotspot/LTE/GNSS. Anschluss ans Wohnmobil über 2 Relais mit Freilaufdiode und Nachlaufzeit gemäß RUTX11-Doku. Dachantenne (Teltonika) mit 2× WLAN, 2× LTE, 1× GNSS.
- Display: ESP32-S3-Touch-LCD-7, übernimmt Visualisierung aller Sensor- und Statusdaten im Wohnmobil.
- Sensorboard: Heemol ESP32 S3 N16R8 DevKitC-1 (ESP32-S3-DevKitC-1), erfasst analoge/digitale Sensoren und bedient E/A Richtung EBL/Anzeigepanel.
- Sensorik: BNO055, BME680 (innen), BME280 (außen), HX711 (Gasfüllstände), 2× Batterien (Board/Kfz), 2× Tanksensoren Votronic (Frisch/Grau, kapazitiv), Steuerung Ein-/Ausgänge EBL/Panel.

## RS485-Schnittstelle (Sensorboard → Display) – Protokoll v2 (Topic-basiert)
- Physik: UART2 RS485 Half-Duplex, **57600** 8N1, DE/RTS automatisch (115200 verursacht Framing-Fehler durch DE-Toggle). Leitungsende per CRLF-terminierter ASCII-JSON-Zeilen (kein Binary, nur 0x20–0x7E).
- Pins (Sensorboard): TX=GPIO12, RX=GPIO11, DE/RTS=GPIO21 (siehe modem_config.h). Display RS485 Buchse (A / B).
- Grundstruktur jedes Frames: JSON-Objekt mit `type` oder `cmd`, immer `seq` (u32) und `ts` (ms seit Epoch). `need_ack` bool steuert, ob das Gegenüber ein ACK schickt (Sensorboard fordert i. d. R. ACK, sobald Display "ready" ist).
- ACK vom Display: `{ "type":"ack", "ack":<seq>, "status":"ok"|"err", "cmd":"<label>", "err":"<text>" }`, `need_ack=false`.
- Periodik vom Sensorboard:
- `hello`: `type="hello"`, Felder `fw`, `uptime` (s), optional `rx_seq`, `last_ack`.
- `hb`: `type="hb"`, Felder `uptime`, optional `rx_seq`, `last_ack`.
- **Topic-Pakete** (ersetzt das alte monolithische `full`-Paket): Jedes Topic wird als eigenständiges kleines JSON-Paket gesendet. Round-Robin-Scheduler, max. 1 Topic pro 100 ms-Zyklus.
- Topic-Intervalle:
- `ctrl` (2 s): `{ type:"ctrl", pwr_on, radio_on, ac_present }` – Steuer-/Power-Zustände. WiFi/LTE-Zustand kommt **nicht** vom Sensorboard – Display pollt Router direkt.
- `imu` (5 s): `{ type:"imu", yaw_deg, pitch_deg, roll_deg, hdg, cal:{sys,gyro,acc,mag}, calibrated }` – BNO055
- `bat` (10 s): `{ type:"bat", b1, b2, nc1, nc2 }` – Batterien (Board/Kfz in Volt, kein SoC)
- `tank` (10 s): `{ type:"tank", t1, t2, nc1, nc2 }` – Tanksensoren (Prozent)
- `hx` (10 s): `{ type:"hx", a, b, sum, nc }` – HX711 Wägezellen (kg)
- `gas` (10 s): `{ type:"gas", active, net, rate1h, rate2h, rest_h, net_a, net_b, cap_kg, pct, pct_a, pct_b }` – Gaslogik
- `bme` (15 s): `{ type:"bme", "0x76":{chip,addr,temp_c,rh_pct,press_hpa,gas_kohm?,iaq?,iaq_acc?,eco2_ppm?,bvoc_ppm?,ts}, "0x77":{chip,addr,temp_c,rh_pct,press_hpa,press_trend_state?,press_trend_hpa_h?,ts} }` – 0x76=BME680 innen (IAQ optional), 0x77=BME280 außen (kein IAQ, mit Drucktrend)
- `elec` (5 s): `{ type:"elec", nc:bool, v_bus, i_a, p_w, v_shunt_mv }` – INA226 Strom-/Leistungsmessung Hauptleitung. `nc:true` wenn Sensor nicht eingebaut. Display zeigt Titel "Strom" + Spannung/Strom + Leistung zwischen den beiden Batterie-Widgets.
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

## Display-Stack (LVGL)

- LVGL **v9.2.0** als Managed Component (`lvgl/lvgl: ^9.2.0`, `espressif/esp_lvgl_port: ^2`)
- RGB-Interface 800×480, 16 MHz PCLK, 2 Framebuffer in PSRAM (Direct Mode + Avoid-Tearing)
- Bounce-Buffer: 4 Zeilen × 800 px × 2 Byte × 2 Buffer = 12,8 KB DMA-SRAM
- LVGL-Task auf Core 1, Stack 16 KB in PSRAM, Priorität 4
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