# Claude Code — WoMoHome Sensorboard

## Vor substanzieller Arbeit
Alle Dateien unter `memory-bank/` lesen (Reihenfolge: projectbrief → productContext / techContext / systemPatterns → activeContext → progress). Erst dann planen oder umsetzen.

## Build & Flash
Build, Flash und Monitor werden vom Nutzer über VS Code Tasks gestartet — nur erklären, keine Befehle ausführen.

## Kritische Hardware-Constraints

| Constraint | Grund |
|---|---|
| GPIO8 (ehemals RS485-DE, `SENSOR_ESPNOW_DE_LEGACY_GPIO`): im Deep-Sleep **LOW** halten | Altlast aus RS485-Zeit, Transceiver ist seit der ESP-NOW-Migration aus dem Schaltplan entfernt |
| GPIO7 (Display 5V): **HIGH=EIN, LOW/Hi-Z=AUS** | Neue Platine: Q5 (2N7002) als Treiber vor Q4 (AO3401A) — OUTPUT HIGH jetzt sicher |
| GPIO35, GPIO36, GPIO37: **nicht verwenden** | Intern vom Octal-PSRAM (N16R8) belegt |
| GPIO11/12 (12V-Relais): im Ruhezustand **LOW** | Bistabiles Relais — versehentlicher Puls schaltet Bordnetz |
| Relais-Puls: `SENSOR_PWR_12V_PULSE_MS` (500 ms), eine Konstante für alle 3 Puls-Stellen | 100 ms reichten dem Relais nicht zum Umschalten |
| GPIO14 (12V-Sense): Eingang, aktiver Pegel per `SENSOR_PWR_12V_SENSE_ACTIVE_HIGH` | **Bekannter Hardware-Bug (2026-09):** J7-Treiber Q1/Q2 für 12V-EIN/AUS sind P-Kanal-FETs (High-Side, +12V) statt N-Kanal (Low-Side, gegen GND) — EBL erwartet Pull-to-GND. Relais schaltet dadurch nicht, unabhängig von Pulslänge/Sense. Fix: IRLML2244→IRLML6244 auf Q1/Q2 (gleiches Footprint). TODO(hardware): Fix verifizieren. |

## ESP-NOW-Protokoll (Kurzfassung)
Seit 2026-06 ESP-NOW statt RS485 (Commit `e1eae83`). Transport `network/espnow_transport.c`, Topics/Kommandos `network/espnow_modem.c` (internes Präfix `modem_*`).
Ein Frame = ein ESP-NOW-Paket mit unformatiertem JSON, max. `WOMO_ESPNOW_MAX_PAYLOAD` (250 Byte). Kanal = WLAN-Kanal, sonst `WOMO_ESPNOW_CHANNEL_FALLBACK`. Kopplung + Verschlüsselung (seit 2026-10-25): Jedes Board speichert die MAC der Gegenstelle im NVS (`espnow_peer`/`mac`) und nimmt Nutzdaten **nur per verschlüsseltem Unicast** (CCMP, `WOMO_ESPNOW_PMK`/`LMK` aus `womo_credentials.h`) von ihr an. Broadcast ist nicht verschlüsselbar und trägt nur den Kopplungs-Beacon `{"type":"pair"}` (alle 1 s), Nutzdaten ohne Kopplung werden nicht gesendet (`ESP_ERR_INVALID_STATE`). Kopplungsfenster: beide, solange keine Gegenstelle gespeichert ist; Display 60 s nach „Neu koppeln“ (Einstellungen > System), schließt beim ersten verschlüsselten Frame; Sensorboard, solange seit `WOMO_ESPNOW_LINK_DEAD_MS` (10 s) nichts vom Display kam. Im Fenster wird ein fremder Beacon als neue Gegenstelle übernommen. Konstanten in `firmware/shared/womo_config.h`. Display sendet alle 2 s `hb`. Sobald seit Boot ein Frame vom gekoppelten Display kam (`espnow_transport_peer_known()`), werden die Topics freigeschaltet und einmal komplett gesendet. `hello`/`hb` sendet das Sensorboard nicht mehr, `display_ready` kommt vom Display nicht mehr.
Jedes Frame: `{ type|cmd, seq, need_ack?, … }`. ACK vom Sensorboard: `{ type:"cmd_ack", ack_seq, status:"ok"|"error", cmd, err? }`, ACK vom Display: `{ type:"ack", ack_seq, status }`.

Topics (Round-Robin, max. 1 pro 100 ms, Intervalle aus `firmware/shared/womo_config.h`):

| Topic | Intervall | Kernfelder |
|---|---|---|
| `ctrl` | 2 s | `pwr_on` (12V-Sense GPIO14, s. u.), `radio_on`, `ac_present`, `rtc_bat_low`, `rtc_bat_switched`, `ssid` (verbundenes WLAN, leer = keins), `rssi` (nur wenn verbunden), `wifi_trial` (nur `true` beim Testen neuer Zugangsdaten), `uptime`, `heap` (ehemals `hb`) |
| `imu` | 2 s | `yaw_deg`, `pitch_deg`, `roll_deg`, `hdg`, `cal`, `calibrated` |
| `elec` | 1 s | `v_bus`, `i_a`, `p_w`, `v_shunt_mv` |
| `bat` | 5 s | `b1`, `b2` (V), `nc1`, `nc2` |
| `tank` | 10 s | `t1`, `t2` (%), `t1_l`, `t2_l` (L), Rate-Felder |
| `hx` | 10 s | `src` (`hx`/`ble`/`none`), `a`, `b` (kg), `sum`, `nc`; bei `src:"ble"` `batt_a_mv`, `batt_a_pct`, `chg_a`, `cal_a`, `cfg_a` (nur wenn an USB/Konfigurationsmodus, analog `_b`) |
| `gas` | 10 s | `src`, `active`, `net`, `rate1h`, `rate2h`, `rest_h`, … |
| `bme_in` | 10 s | `chip`, `addr`, `temp_c`, `rh_pct`, `press_hpa`, `ts`; nur BME680: `gas_kohm` sowie – erst nach BSEC-Burn-in – `iaq`, `iaq_acc` (0–3), `eco2_ppm`, `bvoc_ppm`; Ruuvi-Fallback zusätzlich `batt_mv` |
| `bme_out` | 10 s | `chip`, `addr`, `temp_c`, `rh_pct`, `press_hpa`, `ts`; bei Trend `press_trend_state` (`fall_fast`/`fall_slow`/`steady`/`rise_slow`/`rise_fast`) + `press_trend_hpa_h`; nur BME680: `gas_kohm`; kein IAQ/BSEC außen; Ruuvi-Fallback zusätzlich `batt_mv` |
| `ruuvi_scan` | nur auf Anfrage | ein Frame pro Tag: `sid`, `idx`, `cnt` (0 = keiner) + `mac`, `rssi`, `temp_c`, `has_pressure`, `age_s` – alle zuletzt gesehenen Ruuvi-Tags (unabhängig vom Filter), Antwort auf `ruuvi_scan_request` |
| `gasbee_scan` | nur auf Anfrage | ein Frame pro GasBee: `sid`, `idx`, `cnt` (0 = keine) + `mac`, `rssi`, `weight_kg`, `batt_mv`, `batt_pct`, `chg`, `cal`, `cfg`, `bee_seq`, `age_s` – alle gesehenen GasBees, Antwort auf `gasbee_scan_request` |
| `gasbee_cal_result` | nach `gasbee_cal` | `step` (`zero`/`ref`), `ok`, `err` (0 ok, > 0 ATT-Fehler der GasBee, < 0 Sensorboard-Fehler, s. `gasbee_gatt_cal.h`), `msg` |

Display → Sensorboard (Kommandos per `cmd`): `pwr_12v_on/off`, `radio_on/off`, `tare_a/b`, `imu_zero`, `gas_bottle_replace`, `wifi_config` (`ssid`, `pass` mit ≥ `WOMO_WIFI_PASS_MIN_LEN` Zeichen, sonst Fehler-ACK – Sensorboard testet die Daten: erst nach erfolgreicher Verbindung ins NVS, nach `SENSOR_WIFI_RETRY_MAX` Fehlversuchen zurück auf die alten; `cmd_ack` heißt nur „empfangen“, Ergebnis über `ssid`/`wifi_trial` im `ctrl`), `ruuvi_config` (`indoor_mac`, `outdoor_mac` – MAC-Filter für Ruuvi-BLE-Scanner, NVS `ruuvi_cfg`, leer = Slot deaktiviert), `ruuvi_scan_request` (fordert `ruuvi_scan`-Antwort an), `gasbee_config` (`bottle_a_mac`, `bottle_b_mac` – Zuordnung GasBee → Flasche, NVS `gasbee_cfg`), `gasbee_scan_request` (optional `cfg_only:true` = nur GasBees an USB, zuletzt ≤ `SENSOR_GASBEE_CFG_MAX_AGE_S` gesehen), `gasbee_cal_start`/`gasbee_cal_end` (`slot` 0/1 – Kalibrier-Sitzung: Flasche solange `nc`, Rate ruht, max. `SENSOR_GASBEE_CAL_SESSION_MAX_S`), `gasbee_cal` (`mac`, `step` `zero`/`ref`, `g` – GATT-Write an die GasBee, Ergebnis als `gasbee_cal_result`).

**Gas-Quelle:** HX711-Modul am Kabel hat Vorrang; ohne Modul kommen die Gewichte von den GasBees (BLE-Advertising v1, kein Connect, Format in `README.md`). GasBee unter 1 kg = leer (0 %), `nc` nur ohne Werte bzw. unkalibriert; HX711 unter 1 kg = `nc`. Details: `memory-bank/systemPatterns.md`.

**Protokolländerungen immer in `memory-bank/systemPatterns.md` und `README.md` nachpflegen.**

## Dokumentation mitpflegen
Bei Protokoll- oder Hardware-Änderungen aktualisieren:
- `memory-bank/systemPatterns.md` (Protokoll, Architektur)
- `memory-bank/activeContext.md` (laufende Arbeit, Kalibrierungen)
- `memory-bank/progress.md` (Meilensteine, offene TODOs)
- `README.md` (öffentliche Referenz)
- `sensor_config.h` (Pin-/Kalibrierungskonstanten)
