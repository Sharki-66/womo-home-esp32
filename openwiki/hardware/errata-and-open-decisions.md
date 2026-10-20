---
type: hardware-reference
title: "Bekannte Hardware-Errata und offene Hardware-Entscheidungen"
description: Konsolidierte, klar getrennte Listen bestätigter Hardwarefehler (Ist-Zustand) gegenüber offenen/unverifizierten Hardwarefragen über die Boards Sensorboard, GasBee und RuuviTag Gas hinweg, damit Fixes und neue Arbeiten bekannte Fehler nicht wiederholen oder sich auf ungeprüfte Annahmen verlassen.
tags: [hardware, errata, sensorboard, gasbee, ruuvitag, ebl, relay, ina226, bsec, unklar]
sources:
  - id: openwiki-source-85ea06d1545a6f69836e7778
    resource: repo://firmware/gasbee_nrf/README.md
  - id: openwiki-source-3ebe585d50641be479be9c39
    resource: repo://firmware/sensorboard/CLAUDE.md
  - id: openwiki-source-f7ab183987a2ed5146a13126
    resource: repo://firmware/sensorboard/main/network/espnow_modem.c
  - id: openwiki-source-403a76a52b1693c0dc805dc5
    resource: repo://firmware/sensorboard/main/sensor_config.h
  - id: openwiki-source-ef27b66aa1a90e05dc041fe3
    resource: repo://firmware/sensorboard/main/sensors/bme680_sensor.c
  - id: openwiki-source-c3efa78e4dbe877232e55790
    resource: repo://firmware/sensorboard/main/sensors/ina226_sensor.c
  - id: openwiki-source-4fe94ba515c34e98b8ef1df5
    resource: repo://firmware/sensorboard/memory-bank/progress.md
  - id: openwiki-source-47ca33e28706ed8332656306
    resource: repo://firmware/sensorboard/README.md
  - id: openwiki-source-a1943c6fa504c23dd53548d0
    resource: repo://hardware/schematics/ruuvitag_gas/PHASE2_HX711.md
  - id: openwiki-source-b2b4b32640d6ff564e4672f9
    resource: repo://hardware/schematics/ruuvitag_gas/README.md
generated: { by: "openwiki/0.7.2", at: "2026-10-19T20:49:57.319Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-19T20:49:57.319Z
---

## Zweck

Hardwarefehler und offene Hardwarefragen sind über Firmware-READMEs,
Memory-Bank-Fortschrittsnotizen und Inline-Quellcode-Kommentare in drei
Projekten (Sensorboard, GasBee nRF, RuuviTag Gas) verstreut. Diese Seite
führt sie an einem Ort zusammen, damit eine Änderung nicht stillschweigend
einen bekannten Fehler wiederholt (z. B. das erneute Einbauen eines
FET-Polaritätsfehlers) oder sich unbemerkt auf einen Wert verlässt, der nie
tatsächlich gemessen wurde.

Zwei Abschnitte werden **strikt getrennt** gehalten und dürfen nicht
vermischt werden:

1. **Bekannte Hardware-Errata (bestätigt, Ist-Zustand)** — Defekte, die
   durch Schaltplananalyse, physische Inspektion oder Messung bestätigt
   sind. Sie beschreiben, was an der heute existierenden Hardware
   tatsächlich falsch ist.
2. **Offene Hardware-Entscheidungen / Geplant (noch nicht umgesetzt oder
   verifiziert)** — Werte, Polaritäten und Designfragen, die entweder
   ungemessene, in die Firmware einkompilierte Annahmen sind, oder
   zukünftige Arbeiten, die noch nicht begonnen wurden.

Für die Boards und KiCad-Projekte, zu denen diese Punkte gehören, siehe
[Boards & KiCad](boards-and-kicad.md). Für die vollständige GPIO-/Sensor-Map
des Sensorboards siehe
[Sensorboard Node](../architecture/sensorboard.md). Für Hardware-Details zu
den BLE-Sensorknoten (GasBee/RuuviTag) siehe
[BLE Sensor Nodes](../architecture/ble-sensor-nodes.md). Für die
12V-Relais-Sequenzierung siehe
[Power Control](../workflows/power-control.md).

## Bekannte Hardware-Errata (bestätigt, Ist-Zustand)

### 1. Q1/Q2 12V-Relaistreiber-FET-Polaritätsfehler — Relais schaltet nie

Der J7-Steckverbinder des Sensorboards steuert das Schaudt-EBL des
Fahrzeugs genau so an, wie es das ursprüngliche LT45x-Bedienfeld tat: Die
Eingänge "12V Ein"/"12V Aus" des EBL sind momentane,
**gegen-GND-ziehende (Low-Side-) Impulse**, die eine interne bistabile
60A-Relaisspule erregen, die ansonsten vom EBL selbst auf +12V gehalten
wird.

Die J7-Treibertransistoren Q1/Q2 des Sensorboards (für GPIO11 "12V ON" /
GPIO12 "12V OFF") sind als **IRLML2244 bestückt, ein P-Kanal-Bauteil, das
als High-Side-Schalter verdrahtet ist** (Source an +12V, Drain zum
J7-Pin, SS14-Freilaufdiode nach +12V). Wenn diese Schaltung "aktiv" ist,
legt sie **+12V** auf den J7-Pin, statt ihn nach GND zu ziehen. Da an
beiden Enden der internen Relaisspule des EBL +12V anliegen, baut sich
nie eine Spannung über der Spule auf, sodass **das Relais nie
schaltet — unabhängig von der Impulsdauer (100/500/1000ms wurden alle
getestet) und unabhängig vom GPIO14-Sense-Wert**. Dies wurde zunächst als
Firmware-Timing-Fehler vermutet und es dauerte Monate, es auf den
Schaltplan zurückzuführen.

- **Identifizierter Fix (auf der Hardware noch nicht verifiziert):**
  Austausch von Q1/Q2 gegen **IRLML6244 (N-Kanal, identischer
  SOT-23-Footprint)**, vorausgesetzt die Source des Transistors ist auf
  der Platine bereits mit GND verbunden (vor der Bestellung anhand des
  Schaltplans zu bestätigen). Die vorhandenen SS14-Freilaufdioden an
  Q1/Q2 können nach dem Austausch voraussichtlich entfernt werden (das
  EBL hat bereits eigene), sollten aber bei den Licht-/Pumpentreiber-
  Schaltungen derselben Bauteilfamilie **beibehalten** werden, da diese
  eine induktive Pumpenmotorlast schalten.
- Als direkte Konsequenz meldet `ctrl.pwr_on` in der Firmware bereits den
  tatsächlich gemessenen GPIO14-Zustand, und die 500ms-Impulslogik
  (`SENSOR_PWR_12V_PULSE_MS`) ist ansonsten vollständig — aber **die
  gesamte 12V-Hausbatterie-Relaissteuerung funktioniert auf der aktuellen
  Platine nicht**, bis dieser FET-Tausch durchgeführt und verifiziert
  wurde.

(Quellen: [progress.md](repo://firmware/sensorboard/memory-bank/progress.md#L44-L45),
[CLAUDE.md](repo://firmware/sensorboard/CLAUDE.md#L16-L18),
[sensor_config.h](repo://firmware/sensorboard/main/sensor_config.h#L210-L233).)

### 2. Physische Beschädigung von GPIO13/17 (und vermutlich GPIO11/12) durch einen Verpolungsvorfall

Am 30.08.2026 verursachte ein verpolter Anschluss an **J7** ("LT452
Malibu", 10-poliger Molex Micro-Fit, derselbe Steckverbinder, der die
Verkabelung des originalen LT45x-Bedienfelds des Fahrzeugs zum EBL
ersetzt) einen echten, nicht behebbaren Hardwareschaden an einer
Sensorboard-Revision:

- **GPIO13 (Multimedia-Stromversorgung)**: Die Multimedia-Leitung wurde
  gegen GND kurzgeschlossen; das Treiben des ESP32-Ausgangs auf HIGH
  erzeugte einen harten Kurzschluss durch den Ausgangstreiber, der nun
  vermutlich dauerhaft beschädigt ist (Ausgang liest unabhängig vom
  Softwarezustand 0V).
- **GPIO17 (Licht)**: Durch ein vertauschtes 12V-Steuersignal
  ausgesetzt, vermuteter Überspannungsschaden.
- **GPIO18 (Pumpe)**: Hat denselben Vorfall unbeschädigt überstanden.
- **GPIO11/GPIO12 (12V-Relais EIN/AUS)**: Ebenfalls über J7 geführt und
  im ursprünglichen Vorfallsbericht als vertauscht (WB/SB) gemeldet. Eine
  Benutzer-Messung auf der Werkbank mit einem MIN/MAX-Multimeter
  (Metrahit 28S, 0,05s Abtastung) deutet auch hier auf einen Schaden hin,
  dies ist jedoch **noch nicht abschließend verifiziert**.

Für einen hart kurzgeschlossenen oder überspannungsgeschädigten
Ausgangstreiber existiert keine Firmware-Umgehung. Die getroffene
Entscheidung war, **keine** Pin-Neubelegung auf der beschädigten Platine
vorzunehmen; stattdessen wurde eine neue Platinenrevision mit korrigiertem
J7 bestellt, der veraltete RS485-Transceiver entfernt und Q5 korrekt
gepolt. Die aktuellen Pin-Zuordnungen in `sensor_config.h`
(GPIO13/17/18) sind dokumentiert als gültig **nur für diese neue,
unbeschädigte Platinenrevision** — da auch die 12V-Relais-Steuerleitungen
auf der beschädigten Platine betroffen sein können, gilt diese Platine
unabhängig vom oben genannten Q1/Q2-Fix als für den Normalbetrieb
ungeeignet.

(Quellen: [progress.md](repo://firmware/sensorboard/memory-bank/progress.md#L52),
[sensor_config.h](repo://firmware/sensorboard/main/sensor_config.h#L237-L245).)

### 3. INA226-Shuntwiderstand ist ein Platzhalter, kein gemessener Wert

`SENSOR_INA226_SHUNT_MOHM` in `sensor_config.h` ist auf einen
**Platzhalterwert von 10 mΩ** gesetzt, der direkt in der
Stromberechnung (`i_calc = v_shunt_mV / SENSOR_INA226_SHUNT_MOHM`) und im
Kalibrierregister-Schreibzugriff des INA226 verwendet wird. Gemäß den
aktuellsten Fortschrittsnotizen war die INA226-Shunt-Hardware noch nicht
physisch verbaut/gemessen, sodass jede aus dieser Konstante abgeleitete
Strom-/Leistungsmessung im Thema `elec` als **noch nicht auf den
tatsächlichen Shunt kalibriert** zu betrachten ist, bis jemand den
verbauten Shuntwiderstand misst und diese Konstante aktualisiert.

(Quellen: [sensor_config.h](repo://firmware/sensorboard/main/sensor_config.h#L123),
[ina226_sensor.c](repo://firmware/sensorboard/main/sensors/ina226_sensor.c#L46),
[progress.md](repo://firmware/sensorboard/memory-bank/progress.md#L34).)

### 4. BSEC-Zustand muss beim Austausch des Innenraumsensors versioniert werden

Der BSEC-IAQ-Baseline-Zustand des BME680 wird persistent im NVS unter dem
Schlüssel `state_in_<i2c-addr>_v<SENSOR_BME680_BSEC_STATE_VERSION>`
(aktuell Version 3) gespeichert. Dies ist nicht bloß eine
Performance-Optimierung: Würde man den akkumulierten BSEC-Baseline-Zustand
eines alten Sensors nach einem Hardwaretausch auf ein **physisch anderes**
BME680-Modul anwenden, würde BSEC eine Gaswiderstands-Baseline erhalten,
die nicht mehr dem tatsächlichen Verhalten des neuen Sensors entspricht,
was dessen IAQ-Kalibrierung verfälschen würde. Die dokumentierte
Betriebsregel lautet, **`SENSOR_BME680_BSEC_STATE_VERSION` immer dann zu
erhöhen, wenn das Innenraum-BME680 physisch ausgetauscht wird**, damit der
neue Sensor mit einer frischen Baseline startet, anstatt unter demselben
NVS-Schlüssel den Zustand des alten Sensors zu übernehmen.

(Quellen: [sensor_config.h](repo://firmware/sensorboard/main/sensor_config.h#L287),
[bme680_sensor.c](repo://firmware/sensorboard/main/sensors/bme680_sensor.c#L1095-L1104),
[README.md](repo://firmware/sensorboard/README.md#L146).)

## Offene Hardware-Entscheidungen / Geplant (noch nicht umgesetzt oder verifiziert)

Diese Punkte sind ausdrücklich **keine** bestätigten Defekte — es sind
entweder unverifizierte, bereits in die Firmware einkompilierte Annahmen
oder zukünftige Arbeiten, die noch nicht begonnen haben. Gemäß
Repository-Konvention wird jeder Wert, der nicht explizit im Code oder
Schaltplan gefunden wurde, als **unklar** markiert, statt geraten zu
werden.

### 12V-Sense-Polarität (`SENSOR_PWR_12V_SENSE_ACTIVE_HIGH`)

`sensor_config.h` setzt `SENSOR_PWR_12V_SENSE_ACTIVE_HIGH` auf `1`, nur
basierend auf einer Analogie zum AC-Sense-Eingang ("12V vorhanden →
HIGH angenommen"), mit einem expliziten `TODO(hardware)`-Kommentar, dies
per Messung an GPIO14 zu verifizieren und auf `0` umzustellen, falls sich
die Verdrahtung als invertiert herausstellt. Dies ist unabhängig von
Erratum #1 oben (dem Relaisschalt-Fehler) — selbst wenn der Q1/Q2-FET-Fix
angewendet ist und das Relais tatsächlich schaltet, muss diese
Sense-Polarität noch auf der Werkbank verifiziert werden, bevor
`ctrl.pwr_on` in beide Richtungen als zuverlässig gelten kann. **Status:
unklar, unverifiziert.**

(Quelle: [sensor_config.h](repo://firmware/sensorboard/main/sensor_config.h#L231-L233),
[espnow_modem.c](repo://firmware/sensorboard/main/network/espnow_modem.c#L563).)

### GasBee-nRF-LiPo-Kapazitätskurve nicht gegen eine reale Batterie verifiziert

Der GasBee nRF rechnet seine gemessenen Batteriemillivolt mithilfe einer
zur Kompilierzeit festgelegten LiPo-Entladekurve
(`SENSOR_GASBEE_LIPO_CURVE`, angewendet auf der Sensorboard-Seite, die den
rohen Millivolt-Messwert konsumiert) in einen
Batterieprozentsatz-Anzeigewert um. Diese Kurve wurde nicht gegen das
reale Entladeverhalten einer tatsächlichen GasBee-LiPo-Zelle verifiziert;
eine einzelne Spannungsstichprobe (Multimeter 4,025V vs. gemeldete
4,003V, −0,55%) bestätigt nur, dass der rohe ADC-/Spannungsteiler-Messwert
nahe der Vollladung korrekt ist, nicht dass die Prozentkurve über den
gesamten Entladebereich stimmt. **Status: Kurvenform unverifiziert.**

(Quellen: [README.md (sensorboard)](repo://firmware/sensorboard/README.md#L78),
[README.md (gasbee_nrf)](repo://firmware/gasbee_nrf/README.md#L246).)

### GasBee-nRF-Ladestrom (~100mA) noch nicht gemessen

Der Ladestrom ist in Hardware/Firmware fest vorgegeben, indem `HICHG`
(P0.13) über einen Zephyr-`gpio-hog` auf Low gehalten wird, wodurch zwei
2,7kΩ-Widerstände (R12, R18) parallel am `ISET`-Pin des BQ25101 liegen,
was grob **100mA (0,1C)** Ladestrom ergeben soll (die
hochohmige Alternative wäre ~50mA). Dies ist ein berechneter Zielwert,
kein auf der Werkbank gemessener — der tatsächlich an die LiPo-Zelle
gelieferte Ladestrom wurde **noch nicht gemessen**. Die Firmware darf
`HICHG` niemals auf High treiben, da dies den Ladestrom-Sollwert außerhalb
des für die 1000mAh-Zelle verifizierten sicheren Bereichs verändern würde.

(Quelle: [README.md (gasbee_nrf)](repo://firmware/gasbee_nrf/README.md#L15).)

### Ursprüngliche WoMo-Tanksensor-Integration durch GPIO17/18-Pin-Konflikt blockiert

Die Design-Dokumente des Sensorboards hatten ursprünglich GPIO17/GPIO18
als freie Pins für eine geplante 4-Stufen-Reedschalter-Widerstandsnetzwerk-
Schnittstelle zu den **originalen** WoMo-Tanksensoren des Fahrzeugs
vorgesehen (unterschieden von den bereits auf GPIO1/GPIO2 verdrahteten
Votronic-Kapazitivsensoren). Seit dem 30.08.2026 sind dieselben zwei Pins
stattdessen an die geschalteten Ausgänge für Licht (GPIO17) und Pumpe
(GPIO18) vergeben, die bereits in der Hardware verdrahtet sind. Die
ursprüngliche Tanksensor-Integration ist daher **blockiert, bis ein
anderes Paar freier GPIOs ausgewählt und die Reed-/Widerstandsnetzwerk-
Schnittstelle entworfen wird** — sie wurde nicht implementiert, und der
Konflikt ist in `sensor_config.h` als explizite Korrektur zu früheren
Planungsnotizen an anderer Stelle derselben Datei dokumentiert.

(Quellen: [sensor_config.h](repo://firmware/sensorboard/main/sensor_config.h#L247-L255),
[progress.md](repo://firmware/sensorboard/memory-bank/progress.md#L35-L36).)

### Noch kein Software-Unterspannungsschutz für die 12V-Hausbatterie

Das Sensorboard schaltet das Haubatterie-HS-Relais derzeit nur auf
expliziten Display-Befehl (`pwr_12v_on`/`pwr_12v_off`); es gibt **keine
automatische Abschaltung bei Unterspannung der Hausbatterie**. Die
"Batteriewächter mit Abschaltautomatik" des Schaudt-EBL selbst wird nur
als **Referenzverhaltensmodell** angemerkt — einige neuere
Schaudt-EBL-Modelle schalten Berichten zufolge bei anhaltend ca. 10,6V
über mehr als 60 Sekunden ab, dies wurde jedoch ausdrücklich **nicht
gegen die in diesem Fahrzeug verbaute Einheit EBL 262-3 verifiziert**.
Weder der Spannungsschwellwert noch die Zeitfensterdauer wurden für
dieses Projekt bisher festgelegt. Der bestehende
Batterie-ADC-Messpfad (`analog_sensor.c`, Thema `bat`) ist die
wahrscheinliche Implementierungsgrundlage, sobald ein Schwellwert/Timer
gewählt wird, aber eine solche Logik existiert heute nicht. **Status:
Design nicht begonnen, Werte unklar.**

(Quelle: [progress.md](repo://firmware/sensorboard/memory-bank/progress.md#L39).)

### RuuviTag-Gas-PCB-Layout und Firmware-Phase nicht begonnen

RuuviTag Gas (ein auf dem nRF52832 basierender Fork von RuuviTag Rev.B8,
der perspektivisch die ESP32-C3-basierte GasBee-Linie ersetzen soll) hat
bisher nur die Schaltplan-Design-Phase abgeschlossen (Phase 2a: Entfernung
von TMP117/DPS310/LIS2DH12, Hinzufügen einer HX711- + Lastschalter-
Schaltung, ERC-sauber). **PCB-Layout (Phase 2b), Fertigungsdokumentation
(Phase 3) und sämtliche Firmware-Arbeiten sind nicht begonnen.** Bevor das
PCB-Layout fortgesetzt werden kann, bleiben mehrere Hardwarefragen offen:
Der tatsächliche Brückenwiderstand der Wägezelle (nötig zur Bestätigung
von Anregungsstrom und Batteriebudget) wurde nicht gemessen, das
HX711-Gehäuse-Footprint (SOP-16L vs. SSOP-16) ist nicht final festgelegt,
und die Gate-Schwellspannungsmarge des Lastschalt-P-MOSFET am Ende der
Batterielebensdauer wurde nicht verifiziert. Siehe
<!-- openwiki: broken internal link [boards-and-kicad.md#ruuvitag-gas--planned-schematic-only-fork] heading anchor "ruuvitag-gas--planned-schematic-only-fork" does not exist in "boards-and-kicad.md". Fix the href or restore the target, then delete this comment. -->
[Boards & KiCad](boards-and-kicad.md#ruuvitag-gas--planned-schematic-only-fork)
für die vollständige Phasentabelle.

(Quellen: [ruuvitag_gas/README.md](repo://hardware/schematics/ruuvitag_gas/README.md),
[PHASE2_HX711.md](repo://hardware/schematics/ruuvitag_gas/PHASE2_HX711.md#L59-L177).)

## Verwandte Seiten

- [Boards & KiCad](boards-and-kicad.md) — Board-/KiCad-Projektinventar, Lizenzierung und Build-Status für jedes oben genannte Board
- [Sensorboard Node](../architecture/sensorboard.md) — vollständige GPIO-/I2C-/ADC-Referenz, einschließlich der von Errata #1 und #2 betroffenen Steuerungs-GPIOs
- [BLE Sensor Nodes](../architecture/ble-sensor-nodes.md) — Hardware-Details zu GasBee nRF und RuuviTag Gas
- [Power Control](../workflows/power-control.md) — 12V-Relais-Impulssequenzierung und Display-Power-Logik rund um Erratum #1
</content>
