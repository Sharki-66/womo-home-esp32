# Claude Code — WoMoHome Sensorboard

## Vor substanzieller Arbeit
Alle Dateien unter `memory-bank/` lesen (Reihenfolge: projectbrief → productContext / techContext / systemPatterns → activeContext → progress). Erst dann planen oder umsetzen.

## Build & Flash
Build, Flash und Monitor werden vom Nutzer über VS Code Tasks gestartet — nur erklären, keine Befehle ausführen.

## Kritische Hardware-Constraints

| Constraint | Grund |
|---|---|
| RS485: **57600** Baud, nicht 115200 | 115200 verursacht Framing-Fehler durch DE-Toggle |
| GPIO7 (Display 5V): nur **OUTPUT LOW** oder **INPUT (Hi-Z)** | OUTPUT HIGH → Vgs=−1,7V → P-MOSFET Q4 leitet → ~300mA Querstrom! |
| GPIO35, GPIO36, GPIO37: **nicht verwenden** | Intern vom Octal-PSRAM (N16R8) belegt |
| GPIO11/12 (12V-Relais): im Ruhezustand **LOW** | Bistabiles Relais — versehentlicher Puls schaltet Bordnetz |

## RS485-Protokoll (Kurzfassung)
CRLF-terminierte ASCII-JSON-Zeilen, UART2, TX=GPIO9, RX=GPIO10, DE=GPIO8.
Jedes Frame: `{ type, seq, ts, need_ack, … }`. ACK vom Display: `{ type:"ack", ack:<seq> }`.

Topics (Round-Robin, max. 1 pro 100 ms):

| Topic | Intervall | Kernfelder |
|---|---|---|
| `hello` | bis display_ready | `fw`, `uptime` |
| `hb` | 30 s | `uptime`, `heap` |
| `ctrl` | 2 s | `pwr_on`, `radio_on`, `ac_present` |
| `imu` | 5 s | `yaw_deg`, `pitch_deg`, `roll_deg`, `hdg`, `cal`, `calibrated` |
| `elec` | 5 s | `v_bus`, `i_a`, `p_w`, `v_shunt_mv` |
| `bat` | 10 s | `b1`, `b2` (V), `nc1`, `nc2` |
| `tank` | 10 s | `t1`, `t2` (%), `t1_l`, `t2_l` (L), Rate-Felder |
| `hx` | 10 s | `a`, `b` (kg), `sum`, `nc` |
| `gas` | 10 s | `active`, `net`, `rate1h`, `rate2h`, `rest_h`, … |
| `bme` | 15 s | `"0x76"` (BME680 indoor), `"0x77"` (BME280 outdoor) |

Display → Sensorboard (Kommandos per `cmd`): `display_ready`, `pwr_12v_on/off`, `radio_on/off`, `tare_a/b`, `gas_bottle_replace`, `level_start/stop`.

**Protokolländerungen immer in `memory-bank/systemPatterns.md` und `README.md` nachpflegen.**

## Dokumentation mitpflegen
Bei Protokoll- oder Hardware-Änderungen aktualisieren:
- `memory-bank/systemPatterns.md` (Protokoll, Architektur)
- `memory-bank/activeContext.md` (laufende Arbeit, Kalibrierungen)
- `memory-bank/progress.md` (Meilensteine, offene TODOs)
- `README.md` (öffentliche Referenz)
- `sensor_config.h` (Pin-/Kalibrierungskonstanten)
