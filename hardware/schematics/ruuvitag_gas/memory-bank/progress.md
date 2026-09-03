# Progress – RuuviTag Gas

*Stand: 2026-09-03*

## Fertig
| Schritt | Anmerkung |
|---|---|
| Phase 1: Migration Rev.B8 → `ruuvitag_gas` | Commit `5d03337` |
| Lib-Tabellen projektlokal, Titelblöcke, README, memory-bank | |
| GUI-Migration KiCad 9: `.kicad_sch` + v9-`.kicad_pcb` | Commit `d9b0cf7`, Legacy-`.sch` entfernt |
| ERC/DRC-Baseline | je 245 Legacy-Meldungen, **0 unverbundene Elemente** |
| Phase 2 spezifiziert | `PHASE2_HX711.md` (Rückbau, Pinbelegung, Netzliste, BOM, PCB) |
| HX711-Speisung festgelegt: **Option B** (Load-Switch, `HX711_EN`→P0.03) | 2026-09-03 |

## Offen
| Schritt | Status |
|---|---|
| Phase 2: Rückbau U2/U4/U5 + HX711 + Load-Switch + Wägezellen-Pads (KiCad-GUI) | Nutzer, Vorgabe steht |
| Wägezellen-Brückenwiderstand messen → Strombudget / Q2-Wahl bestätigen | Nutzer |
| Phase 3: BOM, Gerber, README-Feinschliff | — |
| Firmware (nRF52, eigenständig) | nicht begonnen |

## Bekannte Punkte
- ERC/DRC am Legacy-Stand zeigen ~360/~334 Meldungen – Artefakte des un-migrierten
  Zustands, echte Baseline erst nach GUI-Migration.
- Ruuvi-`*.pretty`-Footprintlibs fehlen (nur Cache); für Phase 2 nur neue Bauteile
  bekommen Footprints aus KiCad-Standardlibs.
