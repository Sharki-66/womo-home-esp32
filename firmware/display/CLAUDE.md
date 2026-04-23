# Claude Code — project context

# Copilot Instructions (WoMoHome Sensor)

## Kommunikationsregeln
- Antworten immer auf Deutsch, inkl. Code-Reviews und Commit-Beschreibungen.
- Hardwarezugriffe ausschließlich über die vorhandenen komponentenspezifischen Bibliotheken (z. B. LVGL, esp_lcd). Keine Eigenimplementierungen solange eine Bibliothek die Funktion bereits anbietet.
- Build, Flash und Monitor werden vom Nutzer gestartet (siehe VS Code Tasks "Build/Flash/Monitor Display Firmware"). Nur erklären, keine Befehle ausführen.
Bei Änderungen am Code die aktuelle README.md (*.md) prüfen / gegebenenfalls bearbeiten.
Git regelmäsig updaten -> Nutzer fragen.

## Projektüberblick
- Ziel: Anzeige und Steuerung von Wohnmobil-Daten. Router liefert WLAN/LTE/GNSS; Display visualisiert; Sensorboard sammelt analoge/digitale Messwerte. Kommunikation zwischen Display und Sensorboard per RS485.

## Hardware
- Router: Teltonika RUTX11 (OpenWRT-basiert) für WLAN/Netz/Hotspot/LTE/GNSS. Anschluss ans Wohnmobil über 2 Relais mit Freilaufdiode und Nachlaufzeit gemäß RUTX11-Doku. Dachantenne (Teltonika) mit 2× WLAN, 2× LTE, 1× GNSS.
- Display: ESP32-S3-Touch-LCD-7, übernimmt Visualisierung aller Sensor- und Statusdaten im Wohnmobil.
- Sensorboard: Heemol ESP32 S3 N16R8 DevKitC-1 (ESP32-S3-DevKitC-1), erfasst analoge/digitale Sensoren und bedient E/A Richtung EBL/Anzeigepanel.
- Sensorik: BNO055, 2× BME680 (außen/innen), HX711 (Gasfüllstände), 2× Batterien (Board/Kfz), 2× Tanksensoren Votronic (Frisch/Grau, kapazitiv), Steuerung Ein-/Ausgänge EBL/Panel.

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

## Geplante Migration: LVGL v8 → v9

> Details, Phasen, betroffene Dateien und Risiken: siehe [.github/lvgl-v8-to-v9-migration.md](lvgl-v8-to-v9-migration.md)

- Branch: `feature/lvgl9-migration`
- Aktueller LVGL-Stand: **8.4.0** (lokale Kopie in `components/lvgl__lvgl/`)
- Migrationsziel: LVGL **v9** + `espressif/esp_lvgl_port ^2` als Managed Component
- Startpunkt: Phase 1 – `lvgl_port.c` Rewrite + `idf_component.yml` umstellen

# Plan / Act workflow (Cursor-style)

Unless the user clearly opts out (e.g. **"skip plan, implement now"** or **"just fix it"** with no ambiguity), use **two modes**. This matches Cursor’s PLAN → approve → ACT flow.

## Plan mode (default)

- **First line of every Plan-mode response MUST be exactly:** `# Mode: PLAN`
- **Do not modify the repository in any way**, including:
  - No creating, editing, or deleting files (source, config, docs, **including `./memory-bank/**` memory-bank files**).
  - No applying multi-file edits, quick fixes, or patch-style changes.
  - No terminal commands that change the workspace (installs, builds that write outputs you were asked to apply, `git` writes, etc.).
- **Allowed in Plan mode:** Read/search files to understand the codebase, answer questions, list steps, identify risks, and produce a **written plan** (markdown).
- **End Plan-mode responses** by telling the user how to proceed, e.g. **Type `ACT` when you approve this plan** (or ask them to refine the plan first).

## Act mode

- Enter **only** when the user’s message **clearly approves implementation**, e.g. they send **`ACT`**, **`act`**, or phrases like **"go ahead"**, **"implement the plan"**, **"approved"** right after a plan—or they explicitly told you to skip planning and implement.
- **First line of every Act-mode response MUST be exactly:** `# Mode: ACT`
- **Then** you may edit files, run commands, and update **`./memory-bank/`** when appropriate.
- After you finish an Act-mode turn, assume the next user message starts in **Plan mode** again unless they again approve with **`ACT`** (or equivalent) for further edits.

## If the user asks for code changes while you are in Plan mode

- **Do not implement.** Respond with `# Mode: PLAN`, briefly restate or adjust the plan, and ask them to type **`ACT`** when they want you to apply changes.

---

# Memory bank (persistent context)

This repository uses a **memory bank** under `./memory-bank/` — structured markdown that survives sessions, similar to Cursor-style workflows.

Context layers (read deeper files after foundations): **projectbrief** → **productContext** / **systemPatterns** / **techContext** → **activeContext** → **progress**.

## What Claude should do

1. **Before substantive work**, read **all** of the following under `./memory-bank/` when the task depends on project state (not optional for non-trivial work). In **Plan mode**, reading for the plan is allowed; **do not edit** these files until **Act mode** unless the user only asked for a documentation/memory update with no code change.
   - `projectbrief.md` — scope and goals
   - `productContext.md` — product intent and UX
   - `systemPatterns.md` — architecture and conventions
   - `techContext.md` — stack and constraints
   - `progress.md` — done / pending / known issues
   - `activeContext.md` — current task and decisions

2. **During Act-mode work**, keep `activeContext.md` aligned with the current task (update when focus shifts).

3. **After meaningful milestones** (in Act mode), update `progress.md` and any affected docs in `./memory-bank/`.

4. When the user asks to **update memory bank** (or similar), **open and review every** file in `./memory-bank/`, then update what changed — especially `activeContext.md` and `progress.md`, even if other files are unchanged. Prefer doing heavy memory-bank writes in **Act mode** unless the user asked for documentation-only updates.

5. Prefer **short, factual updates** over long prose. Reference files, symbols, and tickets instead of duplicating code.

Do not delete these files; evolve them as the project changes.