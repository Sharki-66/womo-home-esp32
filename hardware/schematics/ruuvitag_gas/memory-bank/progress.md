# Progress – RuuviTag Gas

*Stand: 2026-09-02*

## Fertig
| Schritt | Anmerkung |
|---|---|
| Phase 1: Dateien aus `ruuvitag_revb8` kopiert/umbenannt | `.sch`, `.kicad_pcb`, `.kicad_pro`, `-cache.lib`, Lib-Tabellen |
| `sym-lib-table` → alle 6 Legacy-Nicknames auf lokale `ruuvitag_gas-cache.lib` | |
| `fp-lib-table` geleert | Footprints im PCB eingebettet |
| Titelblöcke (`.sch`, `.kicad_pcb`) auf „RuuviTag Gas (WoMoHome)", Rev `GAS-A` | Herkunft/Lizenz vermerkt |
| Bestätigt: KiCad 9 / kicad-cli lesen beide Legacy-Dateien fehlerfrei | Board ist 4-lagig |

## Offen
| Schritt | Status |
|---|---|
| GUI-Migration nach KiCad 9 (`.kicad_sch`, PCB v9) + ERC/DRC-Baseline | Nutzer |
| Phase 2: Sensor-Rückbau + HX711 + Pads | wartet auf Migration |
| Phase 3: README-Feinschliff, BOM, Gerber | — |
| Firmware (nRF52, eigenständig) | nicht begonnen |

## Bekannte Punkte
- ERC/DRC am Legacy-Stand zeigen ~360/~334 Meldungen – Artefakte des un-migrierten
  Zustands, echte Baseline erst nach GUI-Migration.
- Ruuvi-`*.pretty`-Footprintlibs fehlen (nur Cache); für Phase 2 nur neue Bauteile
  bekommen Footprints aus KiCad-Standardlibs.
