# Active Context – RuuviTag Gas

*Stand: 2026-09-03*

## Aktueller Schritt
**Phase 1 abgeschlossen** (Commits `5d03337`, `d9b0cf7`): Rev.B8 nach `ruuvitag_gas`
migriert, in KiCad 9 geöffnet und als `.kicad_sch` / v9-`.kicad_pcb` gespeichert.
ERC/DRC-Baseline: je 245 Meldungen (Legacy-Artefakte), 0 unverbundene Elemente.

**Phase 2 spezifiziert:** `PHASE2_HX711.md` – vollständige Umsetzungs-Vorgabe
(Rückbau, nRF-Pinbelegung aus Netzliste verifiziert, HX711-Netzliste, Bauteilliste,
PCB-Hinweise).

## Speisung festgelegt: Option B (2026-09-03)
AVDD=VFB=DVDD=VSUP an geschaltetem Rail `HX711_PWR`; High-Side P-MOSFET (Q2 DMG2301L)
von VDD, Gate über NMOS (Q3 BSS138) an `HX711_EN` = nRF **P0.03** (Pin 5).
Netzliste komplett in `PHASE2_HX711.md` §4.

## Phase 2 (KiCad-GUI, Nutzer – Vorgabe: PHASE2_HX711.md)
1. U2/U4/U5 + C2/C17/C18/C12 + P2/P3/P12 entfernen.
2. HX711 (U6) + Load-Switch (Q2/Q3/R14/R15) + INA-Filter (R12/R13/C21–C27)
   + `J2` (4 Wägezellen-Pads) + `J3` (2 DNP-Speisepads).
3. Netze: `HX711_SCK`→P0.07 (9), `HX711_DOUT`→P0.08 (10), `HX711_EN`→P0.03 (5).
4. Update PCB from Schematic, Platzierung im Ex-Sensorbereich, Routing, DRC.

## Notizen
- kicad-cli (KiCad 9.0.7, Snap: `/snap/bin/kicad.kicad-cli`) – Schreibziele nur unter
  `$HOME` (Snap-Confinement), nicht `/tmp`.
- Netzlisten-Export für Analysen: `kicad-cli sch export netlist --format kicadsexpr`.
- Schaltplan-/PCB-Bearbeitung erfolgt in der GUI (Nutzer), `PHASE2_HX711.md` ist die Vorgabe.
- fp-info-cache ist per `.gitignore` ausgeschlossen; `*-drc.rpt` nicht committen.
