---
type: hardware-reference
title: "Platinen und KiCad-Projekte"
description: Übersicht der WoMoHome-KiCad-Hardwareprojekte (Sensorboard, GasBee ESP32-C3, WoMoHome ESP Touch LCD 7 HAT, RuuviTag-Gas-Fork), ihrer Lizenzierung, ihres Funktionsumfangs, ihres Baustatus sowie der physischen/elektrischen Beziehung des Sensorboards zu dem Schaudt-EBL/LT45x-Bedienpanel, das es ersetzt.
tags: [hardware, kicad, pcb, schematics, licensing, cern-ohl, sensorboard, gasbee, ruuvitag, schaudt, ebl, lt45x]
sources:
  - id: openwiki-source-403a76a52b1693c0dc805dc5
    resource: repo://firmware/sensorboard/main/sensor_config.h
  - id: openwiki-source-409890bca9f3d2cf30925c11
    resource: repo://hardware/datasheets/EBL%20262-3.pdf
  - id: openwiki-source-554b64da257285afac52f4f3
    resource: repo://hardware/datasheets/LT%2045x.pdf
  - id: openwiki-source-33ad0b55446122559ee35843
    resource: repo://hardware/schematics/GasBee/DRC.rpt
  - id: openwiki-source-0df1e73d15c23326c84ba810
    resource: repo://hardware/schematics/GasBee/GasBee.kicad_sch
  - id: openwiki-source-9942700d3dc42578aed0e786
    resource: repo://hardware/schematics/ruuvitag_gas/memory-bank/progress.md
  - id: openwiki-source-a1943c6fa504c23dd53548d0
    resource: repo://hardware/schematics/ruuvitag_gas/PHASE2_HX711.md
  - id: openwiki-source-b2b4b32640d6ff564e4672f9
    resource: repo://hardware/schematics/ruuvitag_gas/README.md
  - id: openwiki-source-98206eddf7af597623f76873
    resource: repo://hardware/schematics/ruuvitag_hw/README.md
  - id: openwiki-source-4d25e4114ea1c11c42eabcb9
    resource: repo://hardware/schematics/Sensorboard/DRC.rpt
  - id: openwiki-source-d9559f52a1401dcc4c1563b8
    resource: repo://hardware/schematics/Sensorboard/production/SensorBoard_Rev_0.5_bom.csv
  - id: openwiki-source-4098b8f8854631e87ee17ba0
    resource: repo://hardware/schematics/Sensorboard/SensorBee/SensorBee.kicad_pcb
  - id: openwiki-source-5e1efd18c582b595887e3c2a
    resource: repo://hardware/schematics/Sensorboard/SensorBee/SensorBee.kicad_sch
  - id: openwiki-source-7e6cf1062c9bba6c7d09ef40
    resource: repo://hardware/schematics/Sensorboard/Sensorboard.kicad_sch
  - id: openwiki-source-8cd86b978230d3037e5c9230
    resource: repo://hardware/schematics/WoMoHome%20ESP%20Touch%20LCD%207%20HAT/ERC.rpt
  - id: openwiki-source-e7a96de552e19d113b52a77b
    resource: repo://hardware/schematics/WoMoHome%20ESP%20Touch%20LCD%207%20HAT/report.txt
  - id: openwiki-source-02366d6021bad641fcc2ab98
    resource: repo://hardware/schematics/WoMoHome%20ESP%20Touch%20LCD%207%20HAT/WoMoHome.kicad_sym
  - id: openwiki-source-47d02fca3524898d5aae2b3b
    resource: repo://LICENSE
  - id: openwiki-source-fade619811d871d8281ecfcb
    resource: repo://website/src/pages/impressum.astro
  - id: openwiki-source-59ff67ef78c61380f8c4c204
    resource: repo://website/src/pages/schaltplan.astro
generated: { by: "openwiki/0.7.2", at: "2026-10-19T20:49:57.319Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-19T20:49:57.319Z
---

## Zweck

`hardware/schematics/` enthält die KiCad-Quellen für jede eigene Platine im
WoMoHome-Projekt sowie einen vendorten Drittanbieter-Fork (RuuviTag). Diese
Seite ist der Katalog: was jede Platine ist, was sie integriert, welche
Lizenz gilt und wie der Bau-/Fertigungsstatus ist. Firmware-seitige
Pin-/Protokolldetails zu diesen Platinen liegen in den unten verlinkten
Architektur-Seiten; diese Seite bleibt auf Platinen-/KiCad-Projekt-Ebene.

| Board | MCU | Rolle | Status |
|---|---|---|---|
| **Sensorboard** | ESP32-S3-WROOM-1 (N16R8 DevKitC-1) | Fahrzeugseitiger Datenerfassungsknoten | Gebaut, überarbeitet (aktuelle Schaltplan-Revision **0.5**) |
| **GasBee (ESP32-C3)** | ESP32-C3-MINI-1 | Legacy-BLE-Gasflaschenwaage | Gebaut/ausgeliefert; abgelöst durch `firmware/gasbee_nrf` auf einem handelsüblichen Seeed-XIAO-nRF52840-Board (für diese Generation gibt es in diesem Repository keine eigene Platine) |
| **WoMoHome ESP Touch LCD 7 HAT** | entfällt (Add-on-Board) | Add-on-HAT für das Waveshare-ESP32-S3-Touch-LCD-7-Display | KiCad-Projekt vorhanden, **keine README** — siehe Vorbehalte unten |
| **RuuviTag Gas** (Fork von RuuviTag Rev.B8) | nRF52832 | Geplanter Ersatz für GasBee als Gasflaschenwaage | **Schaltplan (Phase 2a) fertig; PCB-Layout und Firmware noch nicht begonnen** |

Die vollständige Pin-/Sensor-/GPIO-Referenz des Sensorboards findet sich in
[Sensorboard Node](../architecture/sensorboard.md); Firmware- und
BLE-Protokolldetails zu GasBee/RuuviTag Gas stehen in
[BLE Sensor Nodes](../architecture/ble-sensor-nodes.md); den
Hardware-Kontext des Displays beschreibt [Display Node](../architecture/display.md);
die hier zusammengefassten offenen Hardware-Fehler werden vertieft in
[Hardware Errata & Open Decisions](errata-and-open-decisions.md).

## Lizenzierung

Es gelten zwei unterschiedliche Lizenzen, die sich **nicht** vermischen:

- **Firmware/Software** (alles unter `firmware/`, `website/`, `tools/`) ist
  MIT-lizenziert (Repo-Root `LICENSE`).
- **Original-WoMoHome-Hardwaredesigns** (Sensorboard, GasBee, der Touch LCD 7
  HAT) sind unter der **CERN Open Hardware Licence Permissive v2
  (CERN-OHL-P v2)** lizenziert. Dies wird im Titelblock-Kommentar des
  Sensorboard-Schaltplans selbst festgehalten ("Licensed under CERN-OHL-P v2",
  mit einem Link zu `https://ohwr.org/cern_ohl_p_v2.txt`) und auf den
  Schaltplan- und Impressum-Seiten der Projekt-Website wiederholt.
- **RuuviTag Gas** ist die eine Ausnahme: Da es ein Fork des
  quelloffenen RuuviTag-Rev.B8-Designs von Ruuvi Innovations ist, **erbt es
  Ruuvis Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)**-Lizenz
  anstelle von CERN-OHL-P v2. Das vendorte `ruuvitag_hw`-Upstream-Repository
  (gepinnt auf Commit `afa1b03`) nennt diese Lizenz sowie Ruuvis
  Markenrechts-Einschränkung (abgeleitete Produkte dürfen "Ruuvi" nicht im
  Namen führen) ausdrücklich.

In der Praxis: Wer modifizierte Sensorboard-, GasBee- oder HAT-Schaltpläne
weiterverbreitet, unterliegt den permissiven Bedingungen von CERN-OHL-P v2
(Namensnennung, keine Copyleft-Pflicht). Wer ein modifiziertes RuuviTag-Gas-Board
weiterverbreitet, übernimmt Ruuvis CC-BY-SA-4.0-Share-Alike-Pflicht und darf
das Ergebnis nicht als "Ruuvi"-Produkt bezeichnen.

(Quellen: [Sensorboard.kicad_sch](repo://hardware/schematics/Sensorboard/Sensorboard.kicad_sch#L7-L17),
[ruuvitag_gas/README.md](repo://hardware/schematics/ruuvitag_gas/README.md#L1-L4),
[ruuvitag_hw/README.md](repo://hardware/schematics/ruuvitag_hw/README.md#L14),
[LICENSE](repo://LICENSE), [schaltplan.astro](repo://website/src/pages/schaltplan.astro#L4-L52),
[impressum.astro](repo://website/src/pages/impressum.astro#L69-L77).)

## Sensorboard — gebaut, überarbeitet

**Projekt:** `hardware/schematics/Sensorboard/` (`Sensorboard.kicad_pro` /
`.kicad_sch` / `.kicad_pcb`, KiCad-8/9-Format, A3-Blatt).

Der Titelblock des aktuellen Schaltplans nennt den Titel "WoMoHome Sensorboard",
Revision **"0.5"**, datiert auf 2026-06-14. Fertigungsreife Exporte existieren
für die gesamte Revisionshistorie — `SensorBoard_Rev_0.2.zip` bis
`SensorBoard_Rev_0.5.zip`, plus BOM-/Positions-/Designator-CSVs für 0.4 und 0.5
— unter `hardware/schematics/Sensorboard/production/`. Ein gerendertes
`Sensorboard.pdf`/`.svg` sowie eine interaktive Stückliste (`ibom.html`,
ebenfalls unter `bom/`) liegen im Repository; der DRC-Report der Platine
weist **0 Verstöße, 0 unverbundene Pads, 0 Footprint-Fehler** aus.

Laut Rev.-0.5-BOM integriert es: ein ESP32-S3-DevKitC-1-Modul (U1), einen
BNO055-IMU (U6), einen PCF8523-RTC (U5), einen INA226-Strom-/Leistungsmonitor
(U7), einen HX711-bereiten Wägezelleneingang, einen TRACO-TSR2-2450-DC/DC-Wandler
plus einen LDL1117S33R-3,3-V-LDO, Micro-Fit-/JST-Steckverbinder für
Klimasensoren (BME680/BME280), Gasflaschen-Wägezellen, Frisch-/Grauwassersensorik
sowie den 10-poligen Molex-Micro-Fit-Steckverbinder **J7**, in der BOM als
**"LT452 Malibu"** bezeichnet — das Kabel zum originalen Schaudt-Bediensystem
des Fahrzeugs (siehe unten). Die vollständige GPIO-/I2C-/ADC-Zuordnung liegt
bei [Sensorboard Node](../architecture/sensorboard.md); diese Seite verfolgt
nur die Platinen-/KiCad-Ebene.

### Vorschaltstufen-Fix für die Display-Spannungsversorgung (Q4/Q5)

Schaltplan-Kommentare und BOM bestätigen einen bereits in der aktuellen
(0.5-)Revision vorhandenen Hardware-Fix: GPIO7 (Display-5V-Schalter)
steuert eine N-Kanal-Vorschaltstufe **Q5 (2N7002)** vor dem
P-Kanal-High-Side-Schalter **Q4 (AO3401A)** an, statt das Gate des
P-MOSFETs direkt anzusteuern. Der eigene Inline-Kommentar im Schaltplan
erklärt den Grund: Ein früheres Einzel-FET-Design ließ GPIO7 HIGH einen
direkten Gate-Source-Durchgangsstrom in den P-MOSFET erzeugen; die
Q5-Vorschaltstufe macht "OUTPUT HIGH" auf GPIO7 sicher. `sensor_config.h`
hält ausdrücklich fest, dass die aktuellen Pin-Zuordnungen nur "für die NEUE
Platine" ("for the NEW board") nach diesem Fix gelten, und dokumentiert einen
zweiten, unabhängigen Hardware-Vorfall am selben Steckverbinder (siehe
nächster Abschnitt). Jede Beschreibung dieses Fixes, die an eine bestimmte
ältere Revisionsnummer geknüpft ist, sollte als Annäherung behandelt werden —
maßgeblich ist der aktuelle Stand in Schaltplan/BOM (Q4/Q5 vorhanden, Rev. 0.5);
die vollständige Vorfallshistorie und den Behebungsstatus beschreibt
[Hardware Errata & Open Decisions](errata-and-open-decisions.md).

(Quellen: [Sensorboard.kicad_sch](repo://hardware/schematics/Sensorboard/Sensorboard.kicad_sch#L7-L17),
[SensorBoard_Rev_0.5_bom.csv](repo://hardware/schematics/Sensorboard/production/SensorBoard_Rev_0.5_bom.csv),
[DRC.rpt](repo://hardware/schematics/Sensorboard/DRC.rpt),
[sensor_config.h](repo://firmware/sensorboard/main/sensor_config.h#L237-L263).)

### `SensorBee` — ungebauter, nur-Schaltplan-Entwurf

Ein verschachteltes Unterprojekt, `hardware/schematics/Sensorboard/SensorBee/`,
enthält nur einen Schaltplan (kein PCB-Layout — `SensorBee.kicad_pcb` ist
eine leere KiCad-9-Stub-Datei). Es skizziert eine HX711- + TP4056-Li-Ion-Lader-
+ USB-C-Schaltung um ein "WEMOS C3 mini"-Dev-Board-Symbol herum — also einen
frühen, nie layouteten Entwurf aus derselben Designfamilie wie GasBee, der im
Sensorboard-Projekt verwahrt wird, statt als eigene Platine gefertigt zu
werden.

(Quelle: [SensorBee.kicad_pcb](repo://hardware/schematics/Sensorboard/SensorBee/SensorBee.kicad_pcb),
[SensorBee.kicad_sch](repo://hardware/schematics/Sensorboard/SensorBee/SensorBee.kicad_sch#L20-L1046).)

## GasBee (ESP32-C3) — gebaut/ausgeliefert, firmwareseitig abgelöst

**Projekt:** `hardware/schematics/GasBee/` (`GasBee.kicad_pro` / `.kicad_sch`
/ `.kicad_pcb`, plus Gerber-Daten, Bohrdatei und eine mit Freerouter
geroutete Variante).

Dies ist die ursprüngliche, gefertigte GasBee-Gasflaschenwaagen-Platine,
aufgebaut um einen **ESP32-C3-MINI-1**, mit einem HX711-Wägezellen-ADC, einem
TP4056-Li-Ion-Lade-IC, einem MAX17261-Fuel-Gauge, einem MCP1700-3,3-V-LDO,
einer USB-C-Buchse und einer SMBJ5.0A-TVS-Diode zum Eingangsschutz. Der
DRC-Report der Platine listet **60 Verstöße** (Bohrung-zu-Kupfer- und
Kupfer-zu-Rand-Abstandsfehler unterhalb der konfigurierten
Platinenumriss-/Bohrregeln) — knapp, aber eine Platine, die dennoch gefertigt
und ausgeliefert wurde; sie ist nicht DRC-sauber wie das Sensorboard.

Dieses KiCad-Projekt ist die Hardware für das **legacy** Firmware-Image
`firmware/gasbee` (ESP32-C3). Die aktuell genutzte GasBee-Generation
(`firmware/gasbee_nrf`) läuft stattdessen auf einem handelsüblichen
**Seeed-XIAO-nRF52840**-Dev-Board — für diese Generation gibt es in diesem
Repository kein eigenes KiCad-Projekt. Firmware-/BLE-Protokolldetails für
beide GasBee-Generationen liegen bei
[BLE Sensor Nodes](../architecture/ble-sensor-nodes.md); dieser Eintrag
bleibt auf Platinen-Ebene.

(Quellen: [GasBee.kicad_sch](repo://hardware/schematics/GasBee/GasBee.kicad_sch),
[DRC.rpt](repo://hardware/schematics/GasBee/DRC.rpt#L1-L4).)

## WoMoHome ESP Touch LCD 7 HAT — KiCad-Projekt vorhanden, Pinout undokumentiert

**Projekt:** `hardware/schematics/WoMoHome ESP Touch LCD 7 HAT/` (Leerzeichen
im Verzeichnisnamen; KiCad-9-Format).

**Diesem Projekt liegt keine README bei**, anders als bei Sensorboard und den
RuuviTag-Forks. Gemäß der Quellenregel dieser Seite müssen Pinout und genaue
Integrationsrolle direkt aus dem KiCad-Projekt gelesen werden statt abgeleitet
zu werden; die folgenden Punkte sind nur das, was unmittelbar im
Schaltplan/ERC/Footprint-Verarbeitungslog sichtbar ist, alles darüber hinaus
ist ausdrücklich **unklar**:

- Der Schaltplan definiert ein eigenes Symbol, `WoMoHome:SIM7600X-ESP32`
  (Bibliothek `WoMoHome.kicad_sym`), verwendet als Bauteil **U3** — ein
  Platzhalter-/reines-Footprint-Symbol (kein Datenblatt oder Beschreibung
  hinterlegt), das die Montage-/Steckverbinder-Schnittstelle zu einer
  Waveshare-SIM7600X-ESP32-Basisplatinen-Familie repräsentiert. **Unklar**:
  ob dieser HAT tatsächlich ein eigenes SIM7600X-Mobilfunkmodem trägt oder
  ob U3 nur ein Steckverbinder-Footprint auf eine bestehende
  Waveshare-Platine ist — dies ist im Projekt nirgends festgehalten und muss
  durch direktes Öffnen des KiCad-Projekts bestätigt werden.
- Weitere Bauteile laut Footprint-Verarbeitungslog: drei Infineon-ICs im
  `PG-DSO-8-43`-SOIC-8-Gehäuse (U4–U6, vermutlich High-Side-Leistungsschalter,
  genaues Bauteil **unklar**), ein IC im `TSOT-23-6`-Gehäuse (U7), zwei
  DIP-ICs (U1 DIP-16, U2 DIP-8), eine Sicherung (F1), mehrere Dioden (D1–D7,
  gemischte MiniMELF-/SMA-Gehäuse), ein Taster (SW1), Phoenix-3-polige
  Klemmleisten (J6, J8), JST-XH-Steckverbinder (J2–J5), 2,54-mm-Stiftleisten
  (J1, J9, J10) sowie ein **10-poliger Molex-Micro-Fit-3.0-Steckverbinder
  (J7)** — dieselbe Steckverbinderfamilie/-rastermaß wie beim EBL-seitigen
  J7 des Sensorboards, aber **unklar**, ob das J7 dieses HATs dieselbe
  Funktion erfüllt; nichts in diesem Projekt dokumentiert das.
- `report.txt` (ein Footprint-Update-Log, kein sauberes Build-Artefakt) zeigt
  einen ungelösten Fehler: Der Footprint `Connector_JST:...1x02...` konnte
  für Bauteil **J4** nicht gefunden werden, zusammen mit Warnungen zu
  fehlenden Netzzuordnungen an den Pads 3/4 von **F1** und **J4**. `ERC.rpt`
  verzeichnet 3 Fehler (unverbundene Power-Input-Pins an `#PWR01`, `U3` VBUS,
  `U4` VS) und 1 Warnung (ein doppelter Netzname an `#PWR02`/"WB -"). Nichts
  davon wird durch begleitende Dokumentation erklärt, daher ist die Platine
  als **Work-in-Progress / nicht als baubar bestätigt** zu behandeln, solange
  dies nicht durch Prüfung der KiCad-Dateien bestätigt wird.

Angesichts der fehlenden README und der nicht sauberen ERC-/Footprint-Ergebnisse
sollte keine weitergehende Aussage zu Pinout, beabsichtigter Nutzung oder
Baustatus dieser Platine als gesichert gelten — alles darüber hinaus ist
direkt aus den `.kicad_sch`-/`.kicad_pcb`-Dateien zu lesen.

(Quellen: [report.txt](repo://hardware/schematics/WoMoHome%20ESP%20Touch%20LCD%207%20HAT/report.txt),
[ERC.rpt](repo://hardware/schematics/WoMoHome%20ESP%20Touch%20LCD%207%20HAT/ERC.rpt),
[WoMoHome.kicad_sym](repo://hardware/schematics/WoMoHome%20ESP%20Touch%20LCD%207%20HAT/WoMoHome.kicad_sym#L1-L52).)

## RuuviTag Gas — geplanter, nur-Schaltplan-Fork

**Projekt:** `hardware/schematics/ruuvitag_gas/` — ein Fork von
`ruuvi/ruuvitag_hw` Rev.B8 (vendorte Nur-Lese-Referenzkopie des
Upstream-Repos unter `hardware/schematics/ruuvitag_hw/`, gepinnt auf Commit
`afa1b03`).

Ziel: ein nRF52832-BLE-Beacon, der zusätzlich zur serienmäßigen
Klimasensorik des Rev.B8 über ein HX711 eine Gasflaschen-Wägezelle ausliest
und das Gewicht per BLE-Advertising meldet — gedacht, um langfristig die
ESP32-C3-GasBee-Linie abzulösen. **Deutlich getrennter Plan-vs.-Bau-Status:**

| Phase | Status |
|---|---|
| Phase 1 — Migration Rev.B8 KiCad 5 → KiCad 9, unverändert | Fertig |
| Phase 2a — Schaltplan: TMP117/DPS310/LIS2DH12 entfernen, HX711 + Lastschalter hinzufügen, J1–J4-Wägezellen-Pads und DNP-J5/J6-Fremdversorgungs-Pads verdrahten | **Fertig** (ERC = 3, alle laut `PHASE2_HX711.md` §8 erwartet) |
| Phase 2b — PCB: "Update PCB from Schematic", Platzierung, 4-Lagen-Routing, DRC | **Nicht begonnen** |
| Phase 3 — Dokumentation, BOM, Gerber | Nicht begonnen |
| Firmware (Nordic SDK/Zephyr oder ein Ruuvi-Firmware-Fork — ausdrücklich **nicht** ESP-IDF) | **Nicht begonnen** |

Bereits festgelegte Hardware-Entwurfsentscheidungen (Phase 2a,
"Option B"-Stromversorgungskonzept, 2026-09-03): HX711 und die
Wägezellen-Brücke werden beide über einen High-Side-P-MOSFET
(wie gebaut **Q2**, `Q_PMOS_GSD`) geschaltet, der von einem
N-MOSFET-Treiber (wie gebaut **Q1**, `Q_NMOS_GSD`) angesteuert wird, der an
GPIO `HX711_EN` (P0.03) hängt — dadurch zieht die Schaltung im Leerlauf
praktisch keinen Strom, anders als GasBee nRF, das sein HX711 aus einer
dauerhaft eingeschalteten 3V3-Schiene versorgt (bei USB-/LiPo-Versorgung dort
unproblematisch, bei der CR2477-Knopfzelle des RuuviTag jedoch nicht
akzeptabel). `PD_SCK`/`DOUT` liegen auf P0.07/P0.08, die durch Entfernen von
DPS310, LIS2DH12 und TMP117 (und deren SPI-Bus-/Interrupt-Netzen) frei
wurden. Platinenumriss und Antennen-Freihaltezone bleiben gegenüber Rev.B8
unverändert, sodass dasselbe Gehäuse unter der Wiegeplatte weiterverwendet
werden kann. Offene Hardware-TODOs vor Beginn des PCB-Layouts: den
tatsächlichen Brückenwiderstand der Wägezelle messen, um Erregerstrom und
Batteriebudget zu bestätigen, das HX711-Gehäuse-Footprint festlegen
(SOP-16L vs. SSOP-16) und die Gate-Schwellspannungs-Marge des P-MOSFETs am
Ende der Batterielebensdauer verifizieren.

(Quellen: [ruuvitag_gas/README.md](repo://hardware/schematics/ruuvitag_gas/README.md),
[PHASE2_HX711.md](repo://hardware/schematics/ruuvitag_gas/PHASE2_HX711.md#L59-L177),
[memory-bank/progress.md](repo://hardware/schematics/ruuvitag_gas/memory-bank/progress.md).)

## Beziehung zum Schaudt-EBL/LT45x-Bedienpanel

Das Sensorboard ist kein eigenständiges Add-on — es ist dafür gebaut, das
werksseitige Schaudt-Bedienpanel im originalen 12V-Verteilsystem des
Fahrzeugs physisch und elektrisch zu **ersetzen**, und diese Beziehung
erklärt mehrere der in
[Hardware Errata & Open Decisions](errata-and-open-decisions.md)
dokumentierten Hardware-Eigenheiten.

Das Originalsystem des Fahrzeugs ist (laut den Schaudt-Handbüchern
`EBL 262-3.pdf` und `LT 45x.pdf`):

- Ein **Elektroblock EBL 262-3** — eine wand-/schrankmontierte
  Stromverteilereinheit, die das Ladegerät ("Lademodul"), die komplette
  12-V-Sicherungs- und Verteillogik sowie Relais für zusätzliche
  Verbraucher (Kühlschrank, Markisenlicht) enthält. Der EBL selbst wird im
  Alltag nicht direkt bedient.
- Ein **Schaudt-LT-45X-Bedien-/Anzeigepanel** (mehrere Varianten, LT450 bis
  LT455, die sich nur darin unterscheiden, welche Tasten/Anzeigen bestückt
  sind), das türseitig montiert ist und ohne das der EBL gar nicht
  funktionsfähig ist — das Handbuch des EBL selbst hält fest, dass seine
  Steuereingänge *ausschließlich* über das angeschlossene LT-/IT-Panel
  angesteuert werden.
- Das Panel ist über drei Molex-Micro-Fit-Steckleisten (ST1/ST2/ST3) mit
  dem EBL verbunden. Der 12-polige **Block-1**-Steckverbinder des EBL führt
  unter anderem Pins für **"12V Ein" / "12V Aus"** (Tastimpulse, die das
  interne bistabile High-Side-Relais des EBL ansteuern — "HS-Relais (60A)"
  im eigenen Blockschaltbild des EBL), **"Netzkontrolle"**,
  Batteriefühler-Leitungen und einen **"Multimedia"**-Ausgangspin — also
  genau denselben Funktionsumfang (12-V-Impuls Ein/Aus, Netzpräsenz-Erkennung,
  Multimedia-/AUX-Schaltung), den heute die GPIOs des Sensorboards
  GPIO11/GPIO12 (12-V-Relais EIN/AUS-Impuls), GPIO21 (AC-Präsenz-Erkennung)
  und GPIO13 (Multimedia-Spannung) ansteuern.

Der Steckverbinder **J7** des Sensorboards (ein 10-poliger
Molex-Micro-Fit-3.0, `43045-1012`, 2×5) ist genau an diese Schnittstelle
angeschlossen — sein BOM-Eintrag ist wörtlich mit **"LT452 Malibu"**
beschriftet, d. h. "dieses Kabel ersetzt ein LT452-Panel in einem
Fahrzeug der Marke Malibu." `sensor_config.h` bestätigt dies unabhängig: Es
hält fest, dass ein Vorfall am 2026-08-30 einen Hardwareschaden an
GPIO13/GPIO17 auf einen verpolt angeschlossenen "Steckverbinder J7 ('LT452
Malibu', 10-pol. Molex Micro-Fit)" zurückführte. Mit anderen Worten:
Das Sensorboard übernimmt den physischen Montageplatz des LT45x-Panels und
dessen EBL-seitigen Kabelbaum und steuert die 12-V-EIN/12-V-AUS-Relaiseingänge,
den Netzpräsenz-Eingang sowie die geschalteten Multimedia-/Licht-/Pumpen-Ausgänge
des EBL direkt aus eigenen GPIOs an, statt aus der ursprünglichen
Tastsensor-Logik des Panels. Das erklärt, warum die Erwartungen des EBL an
Signalpolarität/-pegel auf diesen Leitungen — und jede Fehlverdrahtung an
diesem Steckverbinder — unmittelbar zu Sensorboard-Hardware-Fehlern werden
statt rein eine Designentscheidung des Sensorboards zu sein; den konkreten
J7-Treiber-FET-Polaritätsfehler und den GPIO13/17-Hardwareschaden-Vorfall,
den diese Beziehung verursacht hat, beschreibt
[Hardware Errata & Open Decisions](errata-and-open-decisions.md).

(Quellen: [LT 45x.pdf](repo://hardware/datasheets/LT%2045x.pdf) S. 1, 5, 12
(Panelvarianten, Systemdiagramm, MicroFit-ST1–ST3-Pinout),
[EBL 262-3.pdf](repo://hardware/datasheets/EBL%20262-3.pdf) S. 3, 7, 11–12
("Bedienung erfolgt ausschließlich über die angeschlossene Kontroll- und
Schalttafel", Block-1-Steckverbinder-Pinout, Blockschaltbild mit dem
60-A-HS-Relais), [SensorBoard_Rev_0.5_bom.csv](repo://hardware/schematics/Sensorboard/production/SensorBoard_Rev_0.5_bom.csv),
[sensor_config.h](repo://firmware/sensorboard/main/sensor_config.h#L237-L245),
[schaltplan.astro](repo://website/src/pages/schaltplan.astro#L58-L62).)

## Arbeiten mit diesen Projekten

- Alle KiCad-Projekte zielen auf die Dateiformate **KiCad 8/9**;
  `ruuvitag_gas` wurde aus dem KiCad-5-Legacy-Format migriert (seine README
  dokumentiert die einmaligen GUI-Migrationsschritte — Symbol-Rettung aus
  `ruuvitag_gas-cache.lib`, anschließendes erneutes Speichern von Schaltplan
  und PCB).
- `fp-lib-table`-/`sym-lib-table`-Dateien sind projektlokal; RuuviTag Gas
  führt zusätzlich eine eigene, aus der Rev.B8-Quelle rekonstruierte
  Footprint-Bibliothek `RuuviTag.pretty`, die nur für aus dem Originaldesign
  übernommene Bauteile genutzt wird (neue Bauteile nutzen Standard-KiCad-
  Footprint-Bibliotheken).
- Die DRC-/ERC-Sauberkeit variiert stark zwischen den Platinen: Sensorboard
  ist DRC-sauber (0 Verstöße); GasBee hat 60 DRC-Verstöße, wurde aber
  dennoch gefertigt; der Schaltplan von RuuviTag Gas hat 3 erwartete/akzeptierte
  ERC-Meldungen, und dessen PCB trägt eine Grundlast von 245 bereits
  vorhandenen (Rev.B8-Legacy-)DRC-Meldungen, die Phase 2b nicht vergrößern
  soll; der Touch LCD 7 HAT hat ungelöste ERC-Fehler und einen
  fehlenden-Footprint-Fehler ohne erklärende Dokumentation.
</content>
