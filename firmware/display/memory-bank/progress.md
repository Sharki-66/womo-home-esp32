# Progress — WoMoHome Display

## Fertig / funktioniert

### ESP-NOW-Protokoll (Display-Empfänger, seit 2026-06 statt RS485)
- Topic-Parser für alle Topics: `bat`, `tank`, `gas`, `imu`, `bme_in`, `bme_out`, `ctrl`, `elec`, `hx` (+ `ruuvi_scan`, `gasbee_scan`, `gasbee_cal_result` auf Anfrage)
- ACK-Mechanismus (Display quittiert Sensorboard-Pakete mit `need_ack=true`)
- Merge-Logik: jedes Topic aktualisiert nur seinen Teil in `s_latest_data`
- Peer-Discovery per `hb` alle 2 s (ersetzt `display_ready`)

### GUI-Widgets (Hauptscreen)
- **Batterie**: 2 Widgets (Board/Kfz), Volt-Anzeige, farbkodiert per Schwellwert
- **Tanks**: Frischwasser + Grauwasser, Prozent-Balken, farbkodiert
- **Gas**: HX711-Gewicht + Gaslogik (Restlaufzeit, Verbrauchsrate), aktive Flasche
- **Strom (elec)**: INA226 — Spannung, Strom, Leistung zwischen Batterie-Widgets
- **IMU**: BNO055 Neigungswinkel (Pitch/Roll), Kalibrierungsanzeige
- **Wetter/Klima**: BME680 (innen, IAQ) + BME280 (außen, Drucktrend)
- **Online-Wetter**: Open-Meteo (aktuell, 5 Tage, 24 h Stundenwerte); Unwetter als eigene Auswertung nach DWD-Kriterien (`womo_weather_warn`), Meteoalarm entfernt (2026-10-20, auf Hardware getestet)
- **GPS**: Koordinaten + GNSS-Status vom RUTX11 (HTTP-Poll)
- **Systemstatus-Label**: 2-Stufen (MANUAL, SENSOR), Acknowledge per Touch

### Error-Stack
- 9 Fehlerquellen (Gas, Frisch, Grau, Batterie, Sensorboard-Link, IAQ, Tarif, Daten, GasBee-Akku) mit Latching, Debounce (ok_streak 6 = 3 s) und Acknowledge
- Buzzer-Triggerung bei neuen unacknowledged Alarmen
- Widget-Farben folgen `.current` (Echtzeit, unabhängig von Latch/Ack)

### Konnektivität
- **WiFi/LTE-Pill-Button** (oben links, 118×44 px): AP-Sektion (grün/grau), WiFi-Icon, LTE-Icon, zwei schwarze Divider
- **Konnektivitäts-Modal**: 3 Spalten — ESP32-WiFi (AP-Status), RUTX11-WLAN (mit Scan+Switch), LTE (mit Switch)
- `s_router_reachable` korrekt gesetzt (nur via WiFi-UCI/HTTP, nicht LTE/AP-Abfragen)
- Router-LED-Button: blau wenn RUTX11 erreichbar, rot + nicht klickbar wenn nicht
- WLAN/LTE-Switch deaktiviert + Badge wenn RUTX11 nicht erreichbar
- Known-Network-Pool: NVS `rtr_wifi` (max. 20 MRU), Passwort-Vorausfüllung
- Mobilfunk-Tarif: Warnung vor Ablauf + bei wenig Restvolumen (Fehler-Stack, Tab LTE, `womo_tariff`), automatisch über pass.telekom.de oder manueller Termin (2026-10-21, auf Hardware getestet)
- Router-WebUI (`tools/router-webui/`): Modem-Schutz gegen gestapelte `gsmctl`-Aufrufe, Log-Schnappschuss bei Modem-Ausfall (2026-10-20, auf Router geprüft)

### Settings-Modal (5 Tabs: AP | Sensoren | Grenzwerte | LTE | System)
- AP-Konfiguration: ESP32-eigene WiFi-Credentials (NVS `womo_ap_cfg`)
- Sensoren: Ruuvi-Tags, GasBee-Einrichten-Assistent
- Grenzwerte (Schwellwerte für Tanks/Gas, NVS `womo_thr`)
- LTE: Mobilfunk-Tarif (Vorlauf, Volumenschwelle, manueller Termin, Rhythmus; NVS `womo_tariff`)
- System: Töne, Sprache (DE/EN), Zeit / RTC

### Boot-Verhalten
- WiFi-Autoretry bevorzugt gespeicherte AP-Konfiguration aus NVS
- Fallback auf Kconfig-SSID wenn kein gespeicherter AP

### Steuerung
- **12V-Bordnetz**: Long-Press auf Blitz-Button (unten links), ESP-NOW-Kommando `pwr_12v_on/off`
- **Radio/Multimedia**: Click auf Noten-Button (oben links), ESP-NOW-Kommando `radio_on/off`
- **Einstellungen**: Click auf Zahnrad-Button

## Offene Hardware-Tests

**WLAN-Wechsel Einstellungen > AP (2026-10-25, gebaut, nicht getestet).** Display und Sensorboard gemeinsam flashen (`womo_config.h` geändert).
- [ ] AP-Tab rechts oben: „Sensorboard: <SSID> (… dBm)“ grün; Sensorboard aus → nach 10 s „keine Daten“
- [ ] Richtiges Passwort, neues Netz: Sensorboard-Log `Neue Zugangsdaten bestätigt`, Label zeigt das neue Netz; wenn möglich Netz auf **anderem Kanal**
- [ ] Falsches Passwort: Sensorboard-Log `… nicht erreichbar – zurück auf …`, wieder im alten Netz; Display fällt per Autoretry zurück
- [ ] Sensorboard aus beim Speichern: Statuszeile „– Sensorboard nicht erreicht“ nach ca. 9 s, Display wechselt trotzdem
- [ ] Display-Log `ap_cfg_conn: Stack-Reserve … Byte` – unter ca. 1 KB `AP_TASK_STACK` (4096) erhöhen
- [ ] Offenes Netz in der Scan-Liste grau mit „(offen – nicht zulässig)“, nicht wählbar; Speichern ohne Passwort bzw. mit < 8 Zeichen abgelehnt
- [ ] Sicherheitsabfrage: anderes Netz speichern → Popup „Passwort von <alt> eingeben“; 3× falsch → „gesperrt, noch N s“; nach 60 s richtiges Passwort → Wechsel läuft
- [ ] Gleiches Netz mit gleichem Passwort erneut speichern → keine Abfrage
- [ ] Tastatur verdeckt das Popup nicht; Texte DE/EN mit Umlauten korrekt

## Backlog / noch nicht implementiert

- **Sicherheit (nächstes Thema, 2026-10-25):** `POST /api/cmd` des Sensorboards führt jedes Kommando ohne Anmeldung aus (`sensor_http.c`); ESP-NOW unverschlüsselt (PMK/LMK). Erst Optionen vorlegen.

- **Kalibrierungs-UI**: NVS-Layer + Web-UI für HX711/Batterie/Tank-Kalibrierung (heute alles hardcodiert in `sensor_config.h`)
- **GasBee-Status im Hauptbildschirm**: Akku/Quelle bisher nur im Einstellungs-Tab „Sensoren“ sichtbar (2026-10-17), nicht am Gas-Widget
- **Drucktrend-Anzeige**: BME280 Drucktrend visuell (aktuell nur Text)
- **OTA-Update**: Firmware-Update über WLAN
- **LTE-Modemstatus im Display**: `lte.modem` (up/down aus `status.json`) wird noch nicht ausgewertet, z. B. „Modem nicht erreichbar“ im Konnektivitäts-Modal

## Bekannte Probleme / Einschränkungen

- LVGL Font `lv_font_montserrat_8` nicht aktiviert (kleinste verfügbare Schrift: 12 px)
- Icons/Hintergründe werden als PNG von der SD-Karte geladen (`/sdcard/images/...`) → Ladezeit beim Boot, Fehler bei fehlender/defekter Karte (siehe `NOTES.md`)
- RUTX11: pausierter congstar-Tarif kann Modem-Neustarts auslösen; Ursache landet künftig in `/etc/womo/log/modem_down_*.log`
- RUTX11: `simslot` in `status.json` leer (`AT+QUIMSLOT?` ohne Ziffer)
