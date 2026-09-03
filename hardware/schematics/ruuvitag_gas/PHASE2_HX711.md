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
- nRF-Pins, die frei werden: siehe §3.

---

## 2. nRF52832-Pinbelegung (aus Netzliste verifiziert)

| Pin | Port | Rev.B8-Funktion | nach Phase 2 |
|---|---|---|---|
| 6 | P0.04 | SDA | **bleibt** SDA (SHTC3) |
| 7 | P0.05 | SCL | **bleibt** SCL (SHTC3) |
| 15 | P0.12 | SENSOR_PWR_1 | **bleibt** (SHTC3-Power) |
| 9 | P0.07 | SENSOR_PWR_2 | **→ `HX711_SCK` (PD_SCK)** |
| 10 | P0.08 | ~SPI_ACC_CS | **→ `HX711_DOUT`** |
| 5 | P0.03/AIN1 | ~SPI_PRESS_CS | frei (Option B: Load-Switch-Enable) |
| 4 | P0.02/AIN0 | ACC_INT1 | frei |
| 8 | P0.06 | ACC_INT2 | frei |
| 19 | P0.16 | ~TMP_ALERT | frei |
| 37 | P0.25 | SPI_MOSI | frei |
| 40 | P0.28/AIN4 | SPI_MISO | frei |
| 41 | P0.29/AIN5 | SPI_SCK | frei |

`HX711_SCK` an P0.07 und `HX711_DOUT` an P0.08 sind benachbarte nRF-Pins und lagen
bereits im Sensorbereich → kurze Leiterbahnen zum HX711.

---

## 3. HX711 – Architekturentscheidung offen (A oder B)

Das GasBee-Design speist die Wägezellen-Brücke **direkt vom 3V3-Rail** (E+ = 3V3,
E− = GND) und lässt den HX711-Regler ungenutzt (AVDD = VFB = VCC). Für GasBee (USB/Akku)
ok, für die **Knopfzelle nicht** – die Brücke (~3 mA) würde dauerhaft ziehen.
Zwei taugliche Varianten:

### Option A – HX711-interner Regler + NPN
- `VSUP`→VDD, `BASE`→Q1(NPN), `AVDD`→Q1-Emitter, `VFB`→Teiler R10/R11, `VBG`→100n
- Brücke E+ = `AVDD`. Im HX711-Power-Down (`PD_SCK` >60 µs high) schaltet der Regler ab
  → Brückenstrom nur während der Messung. **Kein zusätzlicher GPIO, kein MOSFET.**
- Nachteil: `AVDD` < VDD (Regler-Headroom). Waage arbeitet nur solange
  VDD ≳ AVDD + V_CE(sat). Unteres Knopfzellen-Ende (< ~2,8 V) fällt weg.
- `R10/R11`: AVDD = V_BG·(R10+R11)/R11, V_BG ≈ 1,25 V.
  **TODO Wert:** Ziel-AVDD aus nutzbarer VDD-Untergrenze (CR2477-Entladekurve) minus
  Q1-Sättigung ableiten und V_BG am Muster messen; Startwert 12k/12k → AVDD ≈ 2,5 V.

### Option B – AVDD = DVDD = VDD + High-Side-Load-Switch (Empfehlung)
- `AVDD`=`VFB`=`DVDD`=`VSUP` alle an geschaltetes Rail `HX711_PWR`.
- P-MOSFET High-Side (z. B. DMG2301L / SI2301) von VDD → `HX711_PWR`,
  Gate über 100k an VDD, geschaltet über kleinen NMOS (BSS138) an nRF-GPIO
  **P0.03 (Pin 5, Enable `HX711_EN`)**, oder Load-Switch-IC (SIP32431 / TPS22916, 1 GPIO).
- Brücke E+ = `HX711_PWR`. Rail aus → HX711 **und** Brücke stromlos (~0 µA).
- Zusätzlich `PD_SCK`-Power-Down möglich.
- Vorteil: volle ratiometrische Genauigkeit über den ganzen VDD-Bereich,
  kein Kalibrier-TODO für einen VFB-Teiler.
- Kosten: 1 GPIO (P0.03 ist frei) + 1 P-MOSFET + 1 NMOS + 2 R (bzw. 1 Load-Switch-IC).

**Beide Varianten:** `RATE` (15) → GND (10 SPS, rauschärmer), `INB±` (9/10) → GND
(Kanal B ungenutzt), `XI/XO` (13/14) → offen (interner Oszillator).

---

## 4. HX711-Beschaltung (Netzliste, Pins des `Analog_ADC:HX711`)

| HX711-Pin | Option A | Option B |
|---|---|---|
| 1 VSUP | VDD | `HX711_PWR` |
| 2 BASE | Q1-Basis | offen |
| 3 AVDD | Q1-Emitter, C21(10u), C22(100n), Brücke E+ | `HX711_PWR`, C21, C22, Brücke E+ |
| 4 VFB | R10/R11-Mitte | `HX711_PWR` |
| 5 AGND | GND | GND |
| 6 VBG | C24 (100n) → GND | C24 (100n) → GND |
| 7 INA− | R13 (1k) → Brücke A− | dito |
| 8 INA+ | R12 (1k) → Brücke A+ | dito |
| 9 INB− | GND | GND |
| 10 INB+ | GND | GND |
| 11 PD_SCK | `HX711_SCK` → nRF P0.07 | dito |
| 12 DOUT | `HX711_DOUT` → nRF P0.08 | dito |
| 13 XO | offen | offen |
| 14 XI | offen | offen |
| 15 RATE | GND | GND |
| 16 DVDD | VDD, C23 (100n) | `HX711_PWR`, C23 (100n) |

**INA-Filter (beide Optionen, EMV wegen langer Wägezellen-Leitung):**
`C25` 100n differentiell zwischen INA+ und INA− (nach R12/R13); `C26/C27` je 1n von
INA+ bzw. INA− nach GND. Dürfen als DNP vorgesehen werden.

---

## 5. Neue Bauteile

| Ref | Wert | Footprint | Bemerkung |
|---|---|---|---|
| U6 | HX711 | `Package_SO:SOIC-16_3.9x9.9mm_P1.27mm` | **TODO** Package des beschafften Teils prüfen (SOP-16 vs. SSOP-16) |
| Q1 | NPN (BC817-40) | `Package_TO_SOT_SMD:SOT-23` | **nur Option A** |
| Q2 | P-MOSFET (DMG2301L) | `Package_TO_SOT_SMD:SOT-23` | **nur Option B** |
| Q3 | NMOS (BSS138) | `Package_TO_SOT_SMD:SOT-23` | **nur Option B**, Pegelwandler Gate |
| R10, R11 | Option A: 12k/12k (**TODO**) · Option B: – | `Device:R` / 0402 | VFB-Teiler (A) |
| R14 | 100k | 0402 | **Option B** Gate-Pullup P-MOSFET |
| R15 | 100k | 0402 | **Option B** Gate-Pulldown NMOS |
| R12, R13 | 1k | 0402 | INA-Serienwiderstände |
| C21 | 10u | 0603 | AVDD-Puffer |
| C22, C23, C24 | 100n | 0402 | AVDD- / DVDD- / VBG-Entkopplung |
| C25 | 100n | 0402 | INA differentiell |
| C26, C27 | 1n | 0402 | INA common-mode (DNP-Option) |
| J2 | – | 4× SMD-Lötpad (`E+`, `E−`, `A+`, `A−`) | kein Stecker; Draht direkt. Footprint: `Connector:TestPoint` 1,5 mm o. eigenes Pad-Array |
| J3 | – | 2× SMD-Lötpad (`VBAT`, `GND`), **DNP** | optionale externe Speisung |

Neue Bauteile bekommen Footprints aus **KiCad-Standardbibliotheken** (Package_SO,
Package_TO_SOT_SMD, Device, Connector) – die fehlende `RuuviTag.pretty` wird nicht
gebraucht.

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

- **Architektur A oder B** – Nutzerentscheidung, bevor der Schaltplan geändert wird.
- **Wägezelle:** kombinierten Brückenwiderstand der 4× 50 kg messen → realen
  Excitation-Strom und Batterie-Budget bestätigen.
- **AVDD-Ziel (Option A):** aus CR2477-Entladekurve + Q1-Sättigung; R10/R11 danach.
- **HX711-Package:** beschafftes Teil (SOP-16L / SSOP-16) → Footprint final.
- **Wäge-/Advertising-Intervall:** Firmware; bestimmt die reale Batterielaufzeit.
- **Kalibrierung** (Offset/Scale): am fertigen Aufbau mit bekannten Gewichten – gehört
  zum Kalibrierungs-Todo des Gesamtprojekts.
