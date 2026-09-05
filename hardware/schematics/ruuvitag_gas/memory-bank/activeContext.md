# Active Context – RuuviTag Gas

*Stand: 2026-09-05*

## Aktueller Schritt
**Phase 2a (Schaltplan) abgeschlossen.** `ruuvitag_gas.kicad_sch` umgesetzt und gegen
`PHASE2_HX711.md` §4 verifiziert. ERC = 3 (alle erwartet: 2× `pin_to_pin` PWR_FLAG an
P0.12/P0.26 aus Rev.B8, 1× `unconnected_wire_endpoint` am Antennen-Übergang).
Alle 73 Bauteile mit Footprint.

**Nächster Schritt: Phase 2b (PCB)** – in Pcbnew „Update PCB from Schematic",
Footprints im Ex-Sensorbereich platzieren, 4-lagig routen, DRC.

## As-built (Details: PHASE2_HX711.md §8)
- HX711 = **U2**; Load-Switch: **Q2** P-MOSFET High-Side (S→VDD, D→`HX711_PWR`),
  **Q1** N-MOSFET Gate-Treiber (G→`HX711_EN`), R7/R8 = 100k Gate-Bias.
- INA: **R5/R6** 1k in Reihe, **C6** 100n differentiell, **C2/C4** 1n→GND (DNP).
- Entkopplung: **C5** 10u + **C3** 100n (AVDD), **C15** 100n (DVDD), **C7** 100n (VBG).
- Wägezellen-Pads **J1–J4** (E+/E−/A+/A−), ext. Speise-Pads **J5/J6** (DNP).
- **Y3**: 32-MHz-Quarz auf `Device:Crystal_GND24_Small` umgestellt (war lokales Symbol).
- nRF (U1): `HX711_SCK`→P0.07/9, `HX711_DOUT`→P0.08/10, `HX711_EN`→P0.03/5. SHTC3 = U4.
- Entfernt: TMP117, DPS310, LIS2DH12 + zugehörige SPI/ACC-Netze.

## Speisung: Option B
AVDD=VFB=DVDD=VSUP an geschaltetem Rail `HX711_PWR`; High-Side P-MOSFET von VDD,
Gate über NMOS an `HX711_EN`. Rail aus → HX711 + Brücke ~0 µA.

## Offene TODOs (nicht raten – messen)
- Sichtprüfung Y3-Platzierung in KiCad.
- Wägezellen-Brückenwiderstand (4× 50 kg kombiniert) messen → Excitation-Strom
  I = VDD / R_brücke, Batteriebudget, Q2 Rds(on) bestätigen.
- HX711-Package des beschafften Teils (SOP-16L / SSOP-16) → Footprint final.
- Q2 P-MOSFET Vgs(th) < 1,5 V (Gate-Swing ~2,4–3 V).

## Notizen
- kicad-cli (KiCad 9.0.7, Snap: `/snap/bin/kicad.kicad-cli`) – Schreibziele nur unter
  `$HOME` (Snap-Confinement), nicht `/tmp`. Analyse via
  `kicad-cli sch export netlist --format kicadsexpr` + `sch erc --format json`.
- Symbol-Swaps (z. B. Y2→Y3) macht der Nutzer in der KiCad-GUI („Symbol wechseln"),
  nicht per Datei-Edit – Pin-Positionen des neuen Symbols weichen ab.
- Footprint-Property-Fixes / Bibliotheksnamen kann Claude direkt in `.kicad_sch`
  editieren, solange KiCad zu ist.
- `Schreibtisch/womo-home-esp32` ist ein Bind-Mount auf denselben Projektordner.
