# Progress – RuuviTag Gas

*Stand: 2026-09-05*

## Fertig
| Schritt | Anmerkung |
|---|---|
| Phase 1: Migration Rev.B8 → `ruuvitag_gas` | Commit `5d03337` |
| GUI-Migration KiCad 9: `.kicad_sch` + v9-`.kicad_pcb` | Commit `d9b0cf7`, Legacy-`.sch` entfernt |
| Footprint-Bibliothek `RuuviTag.pretty` rekonstruiert | Commit `056716f` |
| Phase 2 spezifiziert | `PHASE2_HX711.md`, HX711-Speisung **Option B** |
| **Phase 2a: Schaltplan umgesetzt + verifiziert** | Rückbau TMP117/DPS310/LIS2DH12; HX711 (U2) + Load-Switch (Q2 P-MOS / Q1 N-MOS) + INA-Filter (R5/R6/C6, C2/C4 DNP) + Entkopplung (C3/C5/C7/C15) + Wägezellen-Pads J1–J4 + Speise-Pads J5/J6 (DNP); Symbole/Footprints auf KiCad-Standard; Y2→Y3 Quarz. **ERC = 3, alle erwartet.** Details: `PHASE2_HX711.md` §8 |

## Offen
| Schritt | Status |
|---|---|
| Sichtprüfung Y3-Platzierung in KiCad | Nutzer |
| Phase 2b: „Update PCB from Schematic", Platzierung, 4-lagig routen, DRC | als Nächstes |
| Wägezellen-Brückenwiderstand messen → Strombudget / Q2-Wahl | Nutzer |
| HX711-Package (SOP-16L / SSOP-16) → Footprint final | Nutzer |
| Phase 3: BOM, Gerber, README-Feinschliff | — |
| Firmware (nRF52, eigenständig) | nicht begonnen |

## Bekannte Punkte
- ERC-Restmeldungen (3) sind erwartet: 2× `pin_to_pin` (PWR_FLAG an GPIO P0.12/P0.26,
  Rev.B8-Eigenart), 1× `unconnected_wire_endpoint` am Antennen-Übergang.
- PCB-DRC-Baseline vor Phase 2b: 245 Legacy-Meldungen – Ziel: keine **neuen** durch HX711.
- Original-RuuviTag-Passive nutzen `RuuviTag:0402_*`; neue Bauteile nutzen KiCad-Standard
  (`Resistor_SMD` / `Capacitor_SMD` / `Package_TO_SOT_SMD` / `TestPoint`) – optisch
  gemischt, elektrisch identisch.
