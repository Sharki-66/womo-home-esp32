# Claude Code — WoMoHome Sensorboard

## Vor substanzieller Arbeit
Alle Dateien unter `memory-bank/` lesen (Reihenfolge: projectbrief → productContext / techContext / systemPatterns → activeContext → progress). Erst dann planen oder umsetzen.

## Build & Flash
Build, Flash und Monitor werden vom Nutzer über VS Code Tasks gestartet — nur erklären, keine Befehle ausführen.

## Kritische Hardware-Constraints

| Constraint | Grund |
|---|---|
| RS485: **57600** Baud, nicht 115200 | 115200 verursacht Framing-Fehler durch DE-Toggle |
| GPIO7 (Display 5V): **HIGH=EIN, LOW/Hi-Z=AUS** | Neue Platine: Q5 (2N7002) als Treiber vor Q4 (AO3401A) — OUTPUT HIGH jetzt sicher |
| GPIO35, GPIO36, GPIO37: **nicht verwenden** | Intern vom Octal-PSRAM (N16R8) belegt |
| GPIO11/12 (12V-Relais): im Ruhezustand **LOW** | Bistabiles Relais — versehentlicher Puls schaltet Bordnetz |
| Relais-Puls: `SENSOR_PWR_12V_PULSE_MS` (500 ms), eine Konstante für alle 3 Puls-Stellen | 100 ms reichten dem Relais nicht zum Umschalten |
| GPIO14 (12V-Sense): Eingang, aktiver Pegel per `SENSOR_PWR_12V_SENSE_ACTIVE_HIGH` | **Bekannter Hardware-Bug (2026-09):** J7-Treiber Q1/Q2 für 12V-EIN/AUS sind P-Kanal-FETs (High-Side, +12V) statt N-Kanal (Low-Side, gegen GND) — EBL erwartet Pull-to-GND. Relais schaltet dadurch nicht, unabhängig von Pulslänge/Sense. Fix: IRLML2244→IRLML6244 auf Q1/Q2 (gleiches Footprint). TODO(hardware): Fix verifizieren. |

## RS485-Protokoll (Kurzfassung)
CRLF-terminierte ASCII-JSON-Zeilen, UART2, TX=GPIO9, RX=GPIO10, DE=GPIO8.
Jedes Frame: `{ type, seq, ts, need_ack, … }`. ACK vom Display: `{ type:"ack", ack:<seq> }`.

Topics (Round-Robin, max. 1 pro 100 ms):

| Topic | Intervall | Kernfelder |
|---|---|---|
| `hello` | bis display_ready | `fw`, `uptime` |
| `hb` | 30 s | `uptime`, `heap` |
| `ctrl` | 2 s | `pwr_on` (12V-Sense GPIO14, s. u.), `radio_on`, `ac_present` |
| `imu` | 5 s | `yaw_deg`, `pitch_deg`, `roll_deg`, `hdg`, `cal`, `calibrated` |
| `elec` | 5 s | `v_bus`, `i_a`, `p_w`, `v_shunt_mv` |
| `bat` | 10 s | `b1`, `b2` (V), `nc1`, `nc2` |
| `tank` | 10 s | `t1`, `t2` (%), `t1_l`, `t2_l` (L), Rate-Felder |
| `hx` | 10 s | `src` (`hx`/`ble`/`none`), `a`, `b` (kg), `sum`, `nc`; bei `src:"ble"` `batt_a_mv`, `batt_a_pct`, `chg_a`, `cal_a`, `cfg_a` (nur wenn an USB/Konfigurationsmodus, analog `_b`) |
| `gas` | 10 s | `src`, `active`, `net`, `rate1h`, `rate2h`, `rest_h`, … |
| `bme_in` | 15 s | `chip`, `addr`, `temp_c`, `rh_pct`, `press_hpa`, `ts`; nur BME680: `gas_kohm` sowie – erst nach BSEC-Burn-in – `iaq`, `iaq_acc` (0–3), `eco2_ppm`, `bvoc_ppm`; Ruuvi-Fallback zusätzlich `batt_mv` |
| `bme_out` | 15 s | `chip`, `addr`, `temp_c`, `rh_pct`, `press_hpa`, `ts`; bei Trend `press_trend_state` (`fall_fast`/`fall_slow`/`steady`/`rise_slow`/`rise_fast`) + `press_trend_hpa_h`; nur BME680: `gas_kohm`; kein IAQ/BSEC außen; Ruuvi-Fallback zusätzlich `batt_mv` |
| `ruuvi_scan` | nur auf Anfrage | ein Frame pro Tag: `sid`, `idx`, `cnt` (0 = keiner) + `mac`, `rssi`, `temp_c`, `has_pressure`, `age_s` – alle zuletzt gesehenen Ruuvi-Tags (unabhängig vom Filter), Antwort auf `ruuvi_scan_request` |
| `gasbee_scan` | nur auf Anfrage | ein Frame pro GasBee: `sid`, `idx`, `cnt` (0 = keine) + `mac`, `rssi`, `weight_kg`, `batt_mv`, `batt_pct`, `chg`, `cal`, `cfg`, `bee_seq`, `age_s` – alle gesehenen GasBees, Antwort auf `gasbee_scan_request` |
| `gasbee_cal_result` | nach `gasbee_cal` | `step` (`zero`/`ref`), `ok`, `err` (0 ok, > 0 ATT-Fehler der GasBee, < 0 Sensorboard-Fehler, s. `gasbee_gatt_cal.h`), `msg` |

Display → Sensorboard (Kommandos per `cmd`): `display_ready`, `pwr_12v_on/off`, `radio_on/off`, `tare_a/b`, `gas_bottle_replace`, `level_start/stop`, `wifi_config` (`ssid`, `pass`), `ruuvi_config` (`indoor_mac`, `outdoor_mac` – MAC-Filter für Ruuvi-BLE-Scanner, NVS `ruuvi_cfg`, leer = Slot deaktiviert), `ruuvi_scan_request` (fordert `ruuvi_scan`-Antwort an), `gasbee_config` (`bottle_a_mac`, `bottle_b_mac` – Zuordnung GasBee → Flasche, NVS `gasbee_cfg`), `gasbee_scan_request` (optional `cfg_only:true` = nur GasBees an USB, zuletzt ≤ `SENSOR_GASBEE_CFG_MAX_AGE_S` gesehen), `gasbee_cal_start`/`gasbee_cal_end` (`slot` 0/1 – Kalibrier-Sitzung: Flasche solange `nc`, Rate ruht, max. `SENSOR_GASBEE_CAL_SESSION_MAX_S`), `gasbee_cal` (`mac`, `step` `zero`/`ref`, `g` – GATT-Write an die GasBee, Ergebnis als `gasbee_cal_result`).

**Gas-Quelle:** HX711-Modul am Kabel hat Vorrang; ohne Modul kommen die Gewichte von den GasBees (BLE-Advertising v1, kein Connect, Format in `README.md`). GasBee unter 1 kg = leer (0 %), `nc` nur ohne Werte bzw. unkalibriert; HX711 unter 1 kg = `nc`. Details: `memory-bank/systemPatterns.md`.

**Protokolländerungen immer in `memory-bank/systemPatterns.md` und `README.md` nachpflegen.**

## Dokumentation mitpflegen
Bei Protokoll- oder Hardware-Änderungen aktualisieren:
- `memory-bank/systemPatterns.md` (Protokoll, Architektur)
- `memory-bank/activeContext.md` (laufende Arbeit, Kalibrierungen)
- `memory-bank/progress.md` (Meilensteine, offene TODOs)
- `README.md` (öffentliche Referenz)
- `sensor_config.h` (Pin-/Kalibrierungskonstanten)
