# Progress — WoMoHome Display

## Fertig / funktioniert

### RS485-Protokoll (Display-Empfänger)
- Topic-Parser für alle Topics: `bat`, `tank`, `gas`, `imu`, `bme`, `ctrl`, `elec`, `hx`, `hello`, `hb`
- ACK-Mechanismus (Display quittiert Sensorboard-Pakete mit `need_ack=true`)
- Merge-Logik: jedes Topic aktualisiert nur seinen Teil in `s_latest_data`
- `display_ready`-Kommando beim Start, danach periodische `ctrl`/`pwr`-Kommandos

### GUI-Widgets (Hauptscreen)
- **Batterie**: 2 Widgets (Board/Kfz), Volt-Anzeige, farbkodiert per Schwellwert
- **Tanks**: Frischwasser + Grauwasser, Prozent-Balken, farbkodiert
- **Gas**: HX711-Gewicht + Gaslogik (Restlaufzeit, Verbrauchsrate), aktive Flasche
- **Strom (elec)**: INA226 — Spannung, Strom, Leistung zwischen Batterie-Widgets
- **IMU**: BNO055 Neigungswinkel (Pitch/Roll), Kalibrierungsanzeige
- **Wetter/Klima**: BME680 (innen, IAQ) + BME280 (außen, Drucktrend)
- **GPS**: Koordinaten + GNSS-Status vom RUTX11 (HTTP-Poll)
- **Systemstatus-Label**: 2-Stufen (MANUAL, SENSOR), Acknowledge per Touch

### Error-Stack
- 6 Fehlerquellen mit Latching, Debounce (ok_streak 6 = 3 s) und Acknowledge
- Buzzer-Triggerung bei neuen unacknowledged Alarmen
- Widget-Farben folgen `.current` (Echtzeit, unabhängig von Latch/Ack)

### Konnektivität
- **WiFi/LTE-Pill-Button** (oben links, 118×44 px): AP-Sektion (grün/grau), WiFi-Icon, LTE-Icon, zwei schwarze Divider
- **Konnektivitäts-Modal**: 3 Spalten — ESP32-WiFi (AP-Status), RUTX11-WLAN (mit Scan+Switch), LTE (mit Switch)
- `s_router_reachable` korrekt gesetzt (nur via WiFi-UCI/HTTP, nicht LTE/AP-Abfragen)
- Router-LED-Button: blau wenn RUTX11 erreichbar, rot + nicht klickbar wenn nicht
- WLAN/LTE-Switch deaktiviert + Badge wenn RUTX11 nicht erreichbar
- Known-Network-Pool: NVS `rtr_wifi` (max. 20 MRU), Passwort-Vorausfüllung

### Settings-Modal (5 Tabs)
- Sprache (DE/EN)
- RTC (Uhrzeit/Datum setzen)
- Grenzwerte (Schwellwerte für alle Sensoren, NVS-gespeichert)
- System (Helligkeit, Buzzer-Lautstärke)
- AP-Konfiguration: ESP32-eigene WiFi-Credentials (NVS `womo_ap_cfg`)

### Boot-Verhalten
- WiFi-Autoretry bevorzugt gespeicherte AP-Konfiguration aus NVS
- Fallback auf Kconfig-SSID wenn kein gespeicherter AP

### Steuerung
- **12V-Bordnetz**: Long-Press auf Blitz-Button (unten links), RS485-Kommando `pwr_12v_on/off`
- **Radio/Multimedia**: Click auf Noten-Button (oben links), RS485-Kommando `radio_on/off`
- **Einstellungen**: Click auf Zahnrad-Button

## Backlog / noch nicht implementiert

- **Kalibrierungs-UI**: NVS-Layer + Web-UI für HX711/Batterie/Tank-Kalibrierung (heute alles hardcodiert in `sensor_config.h`)
- **GasBee-Detailstatus**: BLE-Verbindungsstatus der Gaswaage im UI anzeigen
- **Drucktrend-Anzeige**: BME280 Drucktrend visuell (aktuell nur Text)
- **OTA-Update**: Firmware-Update über WLAN

## Bekannte Probleme / Einschränkungen

- LVGL Font `lv_font_montserrat_8` nicht aktiviert (kleinste verfügbare Schrift: 12 px)
- Icons werden als PNG geladen aus SPIFFS → Ladezeit beim Bootup
- RS485 bei 115200 Baud: Framing-Fehler durch DE/RTS-Toggle-Timing → fest auf 57600
