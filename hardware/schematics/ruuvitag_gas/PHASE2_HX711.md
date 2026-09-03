# Phase 2 – Sensor-Rückbau + HX711 (Umsetzungs-Spec)

Ausgangsbasis: `ruuvitag_gas` @ Commit `d9b0cf7` (unveränderte KiCad-9-Migration von RuuviTag Rev.B8).
Schaltplan-/Layout-Arbeit erfolgt in der KiCad-9-GUI; diese Datei ist die verbindliche Vorgabe.

---

## 1. Entfernen

### Bauteile
| Ref | Bauteil | Grund |
|---|---|---|
| U2 | DPS310XTSA1 (Druck) | nicht benötigt |
| U4 | LIS2DH12 (Beschleunigung) | nicht benötigt |
| U5 | TMP117 (Temperatur) | SHTC3 genügt |
| C2 | 1u | Entkopplung U2 |
| C17 | 1u | Entkopplung U5 |
| C18 | 4u7 | Entkopplung U4 |
| C12 | 12p | Filter auf `SPI_MOSI` |
| P2, P3, P12 | Testpunkte | lagen auf `SPI_SCK/MISO/MOSI` |

### Netze, die dadurch entfallen
`/ACC_INT1`, `/ACC_INT2`, `/~{SPI_ACC_CS}`, `/~{SPI_PRESS_CS}`, `/SPI_SCK`, `/SPI_MOSI`, `/SPI_MISO`, `/~{TMP_ALERT}`

### Behalten
- **U3 SHTC3** samt `C15` (1u), I²C an `SDA`/`SCL`, Power über `SENSOR_PWR_1` (P0.12).
- **R3 / R4** (I²C-Pull-ups 1k) – SHTC3 braucht sie weiterhin.
- `P10 / P11` (Testpunkte auf SDA/SCL) – optional, dürfen bleiben.
- nRF-Kern, beide Quarze (Y1 32k, Y2 32M), HF-Matching (L1–L5, C1/C3–C7 etc.), NFC (P4), Debug-Port (P1), Batterie-Clip (P5), LEDs (D1/D2), Button (SW1), Ruuvi-FFC (P26).

### Lose Enden nach dem Entfernen
- `P26.7` (FFC-Pin, war `~{TMP_ALERT}`): offen lassen oder in KiCad als „nicht angeschlossen" markieren.
- nRF-Pins, die frei werden bzw. neu belegt: siehe §2.

---

## 2. nRF52832-Pinbelegung (aus Netzliste verifiziert)

| Pin | Port | Rev.B8-Funktion | nach Phase 2 |
|---|---|---|---|
| 6 | P0.04 | SDA | **bleibt** SDA (SHTC3) |
| 7 | P0.05 | SCL | **bleibt** SCL (SHTC3) |
| 15 | P0.12 | SENSOR_PWR_1 | **bleibt** (SHTC3-Power) |
| 9 | P0.07 | SENSOR_PWR_2 | **→ `HX711_SCK` (PD_SCK)** |
| 10 | P0.08 | ~SPI_ACC_CS | **→ `HX711_DOUT`** |
| 5 | P0.03/AIN1 | ~SPI_PRESS_CS | **→ `HX711_EN`** (Load-Switch) |
| 4 | P0.02/AIN0 | ACC_INT1 | frei |
| 8 | P0.06 | ACC_INT2 | frei |
| 19 | P0.16 | ~TMP_ALERT | frei |
| 37 | P0.25 | SPI_MOSI | frei |
| 40 | P0.28/AIN4 | SPI_MISO | frei |
| 41 | P0.29/AIN5 | SPI_SCK | frei |

`HX711_SCK` an P0.07 und `HX711_DOUT` an P0.08 sind benachbarte nRF-Pins und lagen
bereits im Sensorbereich → kurze Leiterbahnen zum HX711.

---

## 3. HX711-Speisung – festgelegt: **Option B** (2026-09-03)

GasBee speist die Brücke direkt vom Dauer-3V3-Rail – an der Knopfzelle untauglich
(~3 mA Dauerstrom). Für `ruuvitag_gas`:

**AVDD = VFB = DVDD = VSUP an einem geschalteten Rail `HX711_PWR`**, High-Side per
P-MOSFET von VDD:

```
VDD ──S│ Q2 (P-MOSFET, DMG2301L) │D── HX711_PWR ──> U6.1/3/4/16 + Brücke E+
              │G
              ├── R14 100k ── VDD           (Default: MOSFET aus)
              └── D│ Q3 (NMOS, BSS138) │S ── GND
                       │G
                       ├── R15 100k ── GND  (Default: NMOS aus)
                       └── HX711_EN ── nRF P0.03 (Pin 5)
```

- `HX711_EN` high → Q3 leitet → Q2-Gate auf GND → Q2 leitet → `HX711_PWR` = VDD.
- Rail aus → HX711 **und** Brücke stromlos (~0 µA). `PD_SCK`-Power-Down zusätzlich möglich.
- Volle ratiometrische Genauigkeit über den ganzen VDD-Bereich, kein VFB-Teiler-TODO.
- Brücke E+ am `HX711_PWR`-Knoten **direkt an der U6-AVDD-Entkopplung** abgreifen
  (sauberer Ratiometrie-Bezug), nicht am Q2-Drain.

`RATE` (15) → GND (10 SPS, rauschärmer), `INB±` (9/10) → GND (Kanal B ungenutzt),
`BASE` (2) / `XI` (14) / `XO` (13) → offen (interner Oszillator, Regler ungenutzt).

---

## 4. HX711-Beschaltung – Netzliste (Pins des `Analog_ADC:HX711`)

| U6-Pin | Netz / Verbindung |
|---|---|
| 1 VSUP | `HX711_PWR` |
| 2 BASE | offen |
| 3 AVDD | `HX711_PWR` + C21 (10u) + C22 (100n) + `J2.E+` (Brücke) |
| 4 VFB | `HX711_PWR` |
| 5 AGND | GND (Sternpunkt Analog-GND) |
| 6 VBG | C24 (100n) → GND |
| 7 INA− | R13 (1k) → `LC_A-` → `J2.A-` |
| 8 INA+ | R12 (1k) → `LC_A+` → `J2.A+` |
| 9 INB− | GND |
| 10 INB+ | GND |
| 11 PD_SCK | `HX711_SCK` → nRF P0.07 (Pin 9) |
| 12 DOUT | `HX711_DOUT` → nRF P0.08 (Pin 10) |
| 13 XO | offen |
| 14 XI | offen |
| 15 RATE | GND |
| 16 DVDD | `HX711_PWR` + C23 (100n) |

**Load-Switch:** Q2.S→VDD, Q2.D→`HX711_PWR`, Q2.G→`GATE_P`; R14 `GATE_P`↔VDD;
Q3.D→`GATE_P`, Q3.S→GND, Q3.G→`HX711_EN`; R15 `HX711_EN`↔GND; `HX711_EN`→nRF P0.03.

**INA-EMV-Filter** (lange Wägezellen-Leitung): `C25` 100n differentiell zwischen
INA+ und INA− (nach R12/R13). `C26` / `C27` je 1n von INA+ bzw. INA− nach GND –
als **DNP** vorsehen.

**Wägezellen-Pads `J2` (4×):** `E+`→`HX711_PWR`(AVDD-Knoten), `E−`→GND,
`A+`→`LC_A+`, `A−`→`LC_A−`.
**Speise-Pads `J3` (2×, DNP):** `VBAT`→VDD, `GND`→GND.

---

## 5. Neue Bauteile (Option B)

| Ref | Wert | Footprint | Bemerkung |
|---|---|---|---|
| U6 | HX711 | `Package_SO:SOIC-16_3.9x9.9mm_P1.27mm` | **TODO** Package des beschafften Teils prüfen (SOP-16L vs. SSOP-16) |
| Q2 | P-MOSFET, DMG2301L (o. äq. Vgs(th) < 1,5 V, Rds < 100 mΩ @ 2,5 V) | `Package_TO_SOT_SMD:SOT-23` | High-Side-Switch |
| Q3 | NMOS, BSS138 | `Package_TO_SOT_SMD:SOT-23` | Gate-Ansteuerung Q2 |
| R12, R13 | 1k | `Device:R` / 0402 | INA-Serienwiderstände |
| R14 | 100k | 0402 | Q2 Gate-Pull-up |
| R15 | 100k | 0402 | Q3 Gate-Pull-down |
| C21 | 10u | 0603 | `HX711_PWR`-Puffer an AVDD |
| C22, C23, C24 | 100n | 0402 | AVDD- / DVDD- / VBG-Entkopplung |
| C25 | 100n | 0402 | INA differentiell |
| C26, C27 | 1n | 0402 | INA common-mode – **DNP** |
| J2 | 4× SMD-Lötpad | `Connector:TestPoint` 1,5 mm o. eigenes 4-Pad-Array | `E+ / E− / A+ / A−`, Draht direkt, kein Stecker |
| J3 | 2× SMD-Lötpad | dito | `VBAT / GND`, **DNP** |

Alle neuen Footprints aus **KiCad-Standardbibliotheken** (Package_SO,
Package_TO_SOT_SMD, Device, Connector) – `RuuviTag.pretty` wird nicht gebraucht.

Referenzen U6/Q2/Q3/R12… sind Vorschläge; KiCad vergibt bei „Annotate" ggf. andere –
maßgeblich ist die Funktion, nicht der Bezeichner.

---

## 6. PCB

1. **Update PCB from Schematic** – entfernte Footprints (U2/U4/U5/C2/C17/C18/C12/P2/P3/P12)
   werden gelöscht, neue kommen unplatziert herein.
2. Platzieren im frei gewordenen Sensorbereich (Ex-DPS310 unten links, Ex-TMP117 Mitte
   links, Ex-LIS2DH12 rechts). HX711-SOIC-16 ist das große Teil – in die Boardmitte,
   **Antennen-Sperrfläche am Rand freihalten**.
3. Wägezellen-Pads `J2` an den Boardrand (Draht-Zugentlastung), aber nicht in die
   Antennenzone.
4. `AVDD`/`HX711_PWR`, `INA+/INA−` als kurze, symmetrische, GND-begleitete Leitungen;
   INA-Paar eng führen. Analog-GND-Anbindung an AGND (Pin 5) sternförmig.
5. 4-lagig: In1 = GND-Fläche unter dem HX711, VDD/Signale auf F/B.
6. DRC. Baseline vor Phase 2: 245 Meldungen (Legacy-Artefakte) – Ziel: keine **neuen**
   Fehler durch die HX711-Ergänzung.

---

## 7. Offene TODOs (nicht raten – ableiten/messen)

- **Wägezelle:** kombinierten Brückenwiderstand der 4× 50 kg messen → realen
  Excitation-Strom (I = VDD / R_brücke) und Batterie-Budget bestätigen. Falls
  R_brücke ≲ 300 Ω (> ~10 mA): Q2 Rds(on) / Spannungsabfall prüfen.
- **HX711-Package:** beschafftes Teil (SOP-16L / SSOP-16) → Footprint final.
- **Q2 P-MOSFET:** Logic-Level mit Vgs(th) sicher < 1,5 V (Gate-Swing nur ~VDD ≈ 3 V,
  am Batterieende ~2,4 V) – DMG2301L passt, Alternative prüfen falls nicht lieferbar.
- **Wäge-/Advertising-Intervall:** Firmware; bestimmt die reale Batterielaufzeit.
- **Kalibrierung** (Offset/Scale): am fertigen Aufbau mit bekannten Gewichten – gehört
  zum Kalibrierungs-Todo des Gesamtprojekts.
