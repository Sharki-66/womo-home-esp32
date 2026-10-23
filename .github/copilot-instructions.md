# Copilot Instructions (WoMoHome Sensor)

## Kommunikationsregeln
- Antworten immer auf Deutsch, inkl. Code-Reviews und Commit-Beschreibungen.
- Hardwarezugriffe ausschließlich über die vorhandenen komponentenspezifischen Bibliotheken (z. B. LVGL, esp_lcd). Keine Eigenimplementierungen solange eine Bibliothek die Funktion bereits anbietet.
- Build, Flash und Monitor werden vom Nutzer gestartet (siehe VS Code Tasks "Build/Flash/Monitor Display Firmware"). Nur erklären, keine Befehle ausführen.
Bei Änderungen am Code die aktuelle README.md (*.md) prüfen / gegebenenfalls bearbeiten.
Git regelmäsig updaten -> Nutzer fragen.

## Projektüberblick
- Ziel: Anzeige und Steuerung von Wohnmobil-Daten. Router liefert WLAN/LTE/GNSS; Display visualisiert; Sensorboard sammelt analoge/digitale Messwerte. Kommunikation zwischen Display und Sensorboard per ESP-NOW (bis 2026-06 RS485).

## Hardware
- Router: Teltonika RUTX11 (OpenWRT-basiert) für WLAN/Netz/Hotspot/LTE/GNSS. Anschluss ans Wohnmobil über 2 Relais mit Freilaufdiode und Nachlaufzeit gemäß RUTX11-Doku. Dachantenne (Teltonika) mit 2× WLAN, 2× LTE, 1× GNSS.
- Display: ESP32-S3-Touch-LCD-7, übernimmt Visualisierung aller Sensor- und Statusdaten im Wohnmobil.
- Sensorboard: Heemol ESP32 S3 N16R8 DevKitC-1 (ESP32-S3-DevKitC-1), erfasst analoge/digitale Sensoren und bedient E/A Richtung EBL/Anzeigepanel.
- Sensorik: BNO055, 2× BME680 (außen/innen), HX711 (Gasfüllstände), 2× Batterien (Board/Kfz), 2× Tanksensoren Votronic (Frisch/Grau, kapazitiv), Steuerung Ein-/Ausgänge EBL/Panel.

## ESP-NOW-Schnittstelle (Sensorboard ↔ Display)
- Seit 2026-06 ESP-NOW statt RS485. Aktuelle Protokollbeschreibung (Transport, Topics, Kommandos): `firmware/display/CLAUDE.md`, Abschnitt „ESP-NOW-Schnittstelle“, und `firmware/sensorboard/CLAUDE.md`.

## Konnektivitäts-Modal (Display)
- 3-Spalten-Layout: HotSpot/AP | WLAN | LTE nebeneinander in einem 640×420 px Panel.
- WLAN-/LTE-Spalte: Titel + farbige Status-LED (grün=aktiv, rot=inaktiv), Verbindungsname (SSID/Provider + Signal%), Switch zum Ein-/Ausschalten.
- AP-Spalte: Rein informativ (kein Switch!). Zeigt SSID, Anzahl verbundener Clients und Geräte-Liste (Hostname aus DHCP-Lease, Fallback MAC). AP darf **niemals** per Software deaktiviert werden, da er der einzige Kommunikationskanal zwischen ESP32 und Router ist.
- WLAN-Spalte: Status + Switch + Scan-Button + Dropdown + Passwortfeld (erst nach Netzwerkauswahl sichtbar). Switch steuert `womo_router_wifi_set_sta()` / `womo_router_wifi_enable_sta()`. QWERTZ-Tastatur für Passwort.
- LTE-Spalte: Provider + Signalstärke + Switch. Switch steuert `womo_router_lte_enable()`.
- WLAN und LTE werden über UCI JSON-RPC am RUTX11 gesteuert (nicht über ESP-NOW).
- Router-WiFi-Zugangsdaten werden im NVS gespeichert (Namespace `rtr_wifi`, max. 20 Einträge, MRU-Reihenfolge). Bei Netzwerkauswahl aus Dropdown wird gespeichertes Passwort vorausgefüllt.
- Router-Poll-Task (`router_poll_task` in main.c) aktualisiert AP/WiFi/LTE/GPS alle 15 s und füllt `womo_connectivity_snapshot_t` mit AP-Feldern (ap_enabled, ap_ssid, ap_clients, ap_client_list).

## Geplante Migration: LVGL v8 → v9

> Details, Phasen, betroffene Dateien und Risiken: siehe [.github/lvgl-v8-to-v9-migration.md](lvgl-v8-to-v9-migration.md)

- Branch: `feature/lvgl9-migration`
- Aktueller LVGL-Stand: **8.4.0** (lokale Kopie in `components/lvgl__lvgl/`)
- Migrationsziel: LVGL **v9** + `espressif/esp_lvgl_port ^2` als Managed Component
- Startpunkt: Phase 1 – `lvgl_port.c` Rewrite + `idf_component.yml` umstellen