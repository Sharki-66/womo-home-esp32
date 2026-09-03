# Active Context – RuuviTag Gas

*Stand: 2026-09-03*

## Aktueller Schritt
**Phase 1 abgeschlossen** (Commits `5d03337`, `d9b0cf7`): Rev.B8 nach `ruuvitag_gas`
migriert, in KiCad 9 geöffnet und als `.kicad_sch` / v9-`.kicad_pcb` gespeichert.
ERC/DRC-Baseline: je 245 Meldungen (Legacy-Artefakte), 0 unverbundene Elemente.

**Phase 2 spezifiziert:** `PHASE2_HX711.md` – vollständige Umsetzungs-Vorgabe
(Rückbau, nRF-Pinbelegung aus Netzliste verifiziert, HX711-Netzliste, Bauteilliste,
PCB-Hinweise).

## Blockiert auf Nutzerentscheidung
**HX711-Speisearchitektur A oder B** (Details in `PHASE2_HX711.md` §3):
- A: HX711-interner Regler + NPN, kein Extra-GPIO, aber unteres Batterie-Ende fällt weg
- B (empfohlen): AVDD=VDD + High-Side-Load-Switch, 1 GPIO (P0.03 frei), volle Genauigkeit

## Danach (Phase 2, KiCad-GUI)
1. U2/U4/U5 + C2/C17/C18/C12 + P2/P3/P12 entfernen.
2. HX711 (U6) + gewählte Speisung + INA-Filter + `J2` (4 Wägezellen-Pads) + `J3`
   (2 DNP-Speisepads) einfügen.
3. `HX711_SCK` → nRF P0.07 (Pin 9), `HX711_DOUT` → nRF P0.08 (Pin 10).
4. Update PCB from Schematic, Platzierung im Ex-Sensorbereich, Routing, DRC.

## Notizen
- kicad-cli (KiCad 9.0.7, Snap: `/snap/bin/kicad.kicad-cli`) – Schreibziele nur unter
  `$HOME` (Snap-Confinement), nicht `/tmp`.
- Netzlisten-Export für Analysen: `kicad-cli sch export netlist --format kicadsexpr`.
- Schaltplan-/PCB-Bearbeitung erfolgt in der GUI (Nutzer), `PHASE2_HX711.md` ist die Vorgabe.
- fp-info-cache ist per `.gitignore` ausgeschlossen; `*-drc.rpt` nicht committen.
