# RuuviTag Gas (WoMoHome)

Gaswaagen-Variante des RuuviTag für das WoMoHome-ESP32-Projekt.
Fork von **RuuviTag Rev.B8** (`ruuvi/ruuvitag_hw` @ `afa1b03`), Lizenz **CC BY-SA 4.0** (von RuuviTag geerbt).

## Ziel

BLE-Beacon auf nRF52832-Basis, der zusätzlich zur Klimamessung eine
**Gasflaschen-Waage** über einen **HX711** ausliest und das Gewicht ins
BLE-Advertisement schreibt. Ersetzt mittelfristig das ESP32-C3-`GasBee`.

## Hardware

| Block | Bauteil | Schnittstelle | Status |
|---|---|---|---|
| MCU | nRF52832-QFAA | — | von B8 übernommen |
| Klima | SHTC3 | I²C (`SDA`/`SCL`), Power über `SENSOR_PWR_1` | von B8 übernommen |
| Wägung | HX711 + High-Side-Load-Switch | 3 GPIO: `DOUT`=P0.08, `PD_SCK`=P0.07, `HX711_EN`=P0.03 | **Phase 2 – geplant** |
| Wägezelle | Vollbrücke aus 4× 50 kg (extern vorkombiniert) | 4 SMD-Lötpads `E+ / E- / A+ / A-` | **Phase 2 – geplant** |
| Speisung | CR2477-Knopfzelle (Clip von B8) + 2 DNP-Pads `VBAT`/`GND` für optionalen externen Pack | — | **Phase 2 – geplant** |

**Entfällt gegenüber Rev.B8:** TMP117, DPS310, LIS2DH12 (inkl. SPI-Bus und Interrupt-Leitungen).

**Board:** 4-lagig, Außenmaße unverändert gegenüber Rev.B8 (Gehäuse wird unter die Wiegeplatte geklebt).
Antennen-Sperrfläche am Boardrand bleibt frei.

## Firmware

Eigene nRF52-Firmware (Nordic SDK / Zephyr bzw. Ruuvi-FW-Fork) – **nicht** ESP-IDF.
Separates Arbeitspaket, noch nicht begonnen.

## Verzeichnisinhalt

| Datei | Zweck |
|---|---|
| `ruuvitag_gas.sch` | Schaltplan (noch KiCad-5-Legacy, wird bei erstem Öffnen in KiCad 9 migriert) |
| `ruuvitag_gas.kicad_pcb` | Layout (KiCad-5-Format, Migration analog) |
| `ruuvitag_gas.kicad_pro` | Projektdatei |
| `ruuvitag_gas-cache.lib` | Symbol-Cache aus Rev.B8 (Migrationsquelle für nRF52832, SHTC3 usw.) |
| `sym-lib-table` / `fp-lib-table` | Bibliotheks-Tabellen (projektlokal); Footprints sind im PCB eingebettet |

## Migration nach KiCad 9 (einmalig, GUI)

1. `ruuvitag_gas.kicad_pro` in KiCad 9 öffnen.
2. Rückfrage „älteres Format" bestätigen.
3. Im **Symbol-Remap-/Rescue-Dialog** die Symbole aus `ruuvitag_gas-cache.lib` übernehmen.
4. Eeschema: **Speichern** → `ruuvitag_gas.kicad_sch` entsteht.
5. Pcbnew öffnen → **Speichern** → PCB wird ins v9-Format konvertiert
   (Hinweis „alte Zonenfüll-Strategie" ist erwartet).
6. ERC/DRC laufen lassen → das ist die saubere Ausgangsbasis für Phase 2.

## Status

- [x] **Phase 1** – Migration von Rev.B8 nach `ruuvitag_gas` (KiCad 9, unverändert)
- [x] **Phase 2a – Schaltplan** – Sensor-Rückbau (TMP117/DPS310/LIS2DH12), HX711 + Load-Switch
      (Option B), Wägezellen-Pads J1–J4, ext. Speise-Pads J5/J6 (DNP), Symbole/Footprints
      auf KiCad-Standard. ERC = 3 (alle erwartet). Details: [`PHASE2_HX711.md`](PHASE2_HX711.md) §8
- [ ] **Phase 2b – PCB** – „Update PCB from Schematic", Platzierung, 4-lagig routen, DRC
- [ ] **Phase 3** – Doku, BOM, Gerber
