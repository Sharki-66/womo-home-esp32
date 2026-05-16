# Claude Code — project context

# Instructions (WoMoHome Sensor)

## Build & Flash
- Build, Flash und Monitor werden vom Nutzer gestartet (siehe VS Code Tasks „Build/Flash/Monitor Display Firmware"). Nur erklären, keine Befehle ausführen.

## Projektüberblick
- Ziel: Anzeige und Steuerung von Wohnmobil-Daten. Router liefert WLAN/LTE/GNSS; Display visualisiert; Sensorboard sammelt analoge/digitale Messwerte. Kommunikation zwischen Display und Sensorboard per RS485.

## Hardware
- Router: Teltonika RUTX11 (OpenWRT-basiert) für WLAN/Netz/Hotspot/LTE/GNSS. Anschluss ans Wohnmobil über 2 Relais mit Freilaufdiode und Nachlaufzeit gemäß RUTX11-Doku. Dachantenne (Teltonika) mit 2× WLAN, 2× LTE, 1× GNSS.
- Display: ESP32-S3-Touch-LCD-7, übernimmt Visualisierung aller Sensor- und Statusdaten im Wohnmobil.
- Sensorboard: Heemol ESP32 S3 N16R8 DevKitC-1 (ESP32-S3-DevKitC-1), erfasst analoge/digitale Sensoren und bedient E/A Richtung EBL/Anzeigepanel.
- Sensorik: BNO055, BME680 (innen, 0x76), BME280/BME260 (außen, 0x77), HX711 (Gasfüllstände), 2× Batterien (Board/Kfz), 2× Tanksensoren Votronic (Frisch/Grau, kapazitiv), Steuerung Ein-/Ausgänge EBL/Panel.

## RS485-Schnittstelle (Sensorboard → Display) – Protokoll v2 (Topic-basiert)
- Physik: UART2 RS485 Half-Duplex, **57600** 8N1, DE/RTS automatisch (115200 verursacht Framing-Fehler durch DE-Toggle). Leitungsende per CRLF-terminierter ASCII-JSON-Zeilen (kein Binary, nur 0x20–0x7E).
- Pins (Sensorboard): TX=GPIO9, RX=GPIO10, DE/RTS=GPIO8 (siehe `sensor_config.h`).
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
- `bme` (15 s): `{ type:"bme", "0x76":{temp_c,rh_pct,press_hpa,gas_kohm?,iaq?,iaq_acc?,eco2_ppm?,bvoc_ppm?}, "0x77":{…} }` – 0x76=indoor (BME680), 0x77=outdoor (BME280/BME260)
- `elec` (5 s): `{ type:"elec", v_bus, i_a, p_w, v_shunt_mv, ts }` – INA226 Hauptleitung (Spannung/Strom/Leistung); `nc=true` wenn Sensor fehlt
- Sofort-Ctrl: Nach Ausführung von `pwr_12v_on/off` oder `radio_on/off` wird `ctrl`-Topic sofort gesendet (`s_ctrl_immediate`-Flag), damit Display den neuen Zustand schnell anzeigt.
- GPS/LTE: Werden **nicht** über RS485 übertragen. Display pollt den Router direkt (HTTP/SSH).
- Display → Sensorboard (Kommandos): `cmd` Feld mit JSON-Objekt. Unterstützt: `display_ready`, `level_start|level_stop`, `tare_a|tare_b`, `gas_bottle_replace` (optional `slot`, `channel`), `pwr_12v_on|pwr_12v_off` (12V Bordnetz), `radio_on|radio_off` (Multimedia, nur wenn 12V aktiv). Sensorboard quittiert per ACK.
- Display-Merge: Jedes empfangene Topic aktualisiert nur seinen Teil in `s_latest_data`; alle anderen Felder bleiben erhalten. Callback an main.c liefert vollständigen Merge-Snapshot. Fehlende/unbekannte Felder tolerant ignorieren.
>- Keine Zusatz-Protokolländerungen ohne diese Datei zu aktualisieren.
