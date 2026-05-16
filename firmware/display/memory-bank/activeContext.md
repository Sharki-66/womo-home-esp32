# Active Context — WoMoHome Display

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
