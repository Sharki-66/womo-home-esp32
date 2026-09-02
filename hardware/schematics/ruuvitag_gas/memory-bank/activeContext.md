# Active Context – RuuviTag Gas

*Stand: 2026-09-02*

## Aktueller Schritt
**Phase 1 abgeschlossen** (dateiseitig): Rev.B8 unverändert nach `ruuvitag_gas` kopiert und
umbenannt, Bibliotheks-Tabellen projektlokal, Titelblöcke angepasst.

### Noch vom Nutzer in KiCad 9 zu erledigen
Einmalige GUI-Migration (siehe `README.md` → „Migration nach KiCad 9"):
Projekt öffnen → Symbol-Remap aus `ruuvitag_gas-cache.lib` → Eeschema speichern
(`.kicad_sch`) → Pcbnew speichern (v9) → ERC/DRC als Baseline.

## Als Nächstes (Phase 2)
1. TMP117 (U5, C17), DPS310 (U2, C2), LIS2DH12 (U4, C18) + tote Netze entfernen.
2. HX711-Teilschaltung aus `../GasBee/GasBee.kicad_sch` übernehmen.
3. 4 Wägezellen-Pads + 2 DNP-Speisepads.
4. `PD_SCK` ← Ex-`SENSOR_PWR_2`-GPIO, `DOUT` ← Ex-SPI/INT-GPIO (P0.x im Schaltplan verifizieren).
5. Platzierung im frei gewordenen Sensorbereich, Routing, DRC.

## Notizen
- kicad-cli (KiCad 9.0.7, Snap: `/snap/bin/kicad.kicad-cli`) liest die Legacy-`.sch`
  und `.kicad_pcb` fehlerfrei → GUI-Migration unkritisch.
- `pcbnew`-Python im Snap nicht direkt aufrufbar; PCB-Bearbeitung über GUI.
- fp-info-cache ist per `.gitignore` ausgeschlossen.
