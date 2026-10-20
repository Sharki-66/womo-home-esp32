---
type: hardware-node-reference
title: "BLE-Sensorknoten: GasBee und RuuviTag"
description: Referenz für die batteriebetriebenen BLE-Peripheriegeräte, die vom Sensorboard passiv gescannt werden — legacy GasBee ESP32-C3, aktueller GasBee nRF (Seeed XIAO nRF52840), der geplante RuuviTag-Gas-Hardware-Fork und Standard-Ruuvi-DF5-Tags für den Klima-Fallback innen/außen.
tags: [ble, gasbee, ruuvitag, nrf52840, esp32-c3, hx711, sensorboard, hardware]
sources:
  - id: openwiki-source-85ea06d1545a6f69836e7778
    resource: repo://firmware/gasbee_nrf/README.md
  - id: openwiki-source-a4907e04e0742464ac6d40f7
    resource: repo://firmware/gasbee/main/ble_gas.c
  - id: openwiki-source-b20f700d7ef8dcce279d0e14
    resource: repo://firmware/gasbee/main/gasbee_config.h
  - id: openwiki-source-f3e855e0b85b68b98eebddbc
    resource: repo://firmware/sensorboard/memory-bank/systemPatterns.md
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

## Überblick

Das Sensorboard baut im Normalbetrieb nie eine BLE-Verbindung auf, um Sensordaten
zu lesen. Stattdessen läuft ein zentraler NimBLE-Stack im passiven Scan-Modus
(`ble_manager`), der empfangene Advertisements an protokollspezifische Decoder
weiterreicht. Diese Seite beschreibt die batteriebetriebenen Peripheriegeräte
auf der anderen Seite dieses Scans:

| Knoten | MCU | Rolle | Status |
|---|---|---|---|
| **GasBee (ESP32-C3)** | ESP32-C3 Mini | Legacy-Gasflaschenwaage | Abgelöst; GATT-Service nur noch für Drittanbieter-Clients erhalten |
| **GasBee nRF** | Seeed XIAO nRF52840 (Sense) | Aktuelle Gasflaschenwaage | Aktiv, im Feldeinsatz |
| **RuuviTag Gas** | nRF52832 (RuuviTag-Rev.B8-Fork) | Künftige Gasflaschenwaage als Ersatz für ESP32-C3-GasBee | Hardware-Fork in Arbeit; Firmware noch nicht begonnen |
| **Standard-RuuviTag (DF5)** | nRF52832, Ruuvi-Standardfirmware | Klima-Fallback innen/außen | Aktiv, im Feldeinsatz |

Wie das Sensorboard diese Quellen Ende-zu-Ende einliest und arbitriert, steht in
[BLE Data Flow](../workflows/ble-data-flow.md). Zur gesamten Sensorboard-Architektur
siehe [Sensorboard](sensorboard.md). Für den KiCad-/Board-Kontext siehe
[Boards and KiCad](../hardware/boards-and-kicad.md). Zu den Kalibrierabläufen
siehe [Gas Calibration](../workflows/gas-calibration.md).

## GasBee nRF (Seeed XIAO nRF52840) — aktuelle Implementierung

GasBee nRF ist die aktive Gasflaschenwaagen-Firmware samt Hardware und löst
`firmware/gasbee` (ESP32-C3) ab. Sie wiegt die Gasflasche über eine Wägezelle
mit einem HX711 und meldet Bruttogewicht, Akkuspannung und Status per **BLE
Advertising (Format v1)**. Das Sensorboard empfängt das per passivem Scan
(`gasbee_ble_scanner.c`) und speist es in die Gas-Quellen-Arbitrierung in
`sensors/gas_source.c` ein; eine Verbindung baut es nie auf.

### Hardware

- MCU: Seeed XIAO nRF52840 **Sense** (Zephyr-Board `xiao_ble`). Der zusätzliche
  IMU und das Mikrofon der Sense-Variante sind vorhanden, werden aber nicht
  genutzt.
- HX711 auf Kanal A, Gain 128, versorgt vom 3V3-Rail des XIAO (die GPIOs des
  nRF52840 sind nicht 5V-tolerant). Verdrahtung: HX711 `DOUT` → XIAO `D2` /
  `P0.28` (interner Pull-up), `PD_SCK` → `D3` / `P0.29`.
- Stromversorgung: Einzelzellen-LiPo (3,7V / 1000mAh) an den Pads `BAT+`/`BAT−`
  des Boards, geladen über USB-C durch den onboard-Laderegler BQ25101. Der
  Ladestrom ist fest auf ca. **100mA (0,1C)** eingestellt, indem `HICHG`
  (P0.13) per `gpio-hog` dauerhaft Low gehalten wird, sodass zwei 2,7kΩ-
  Widerstände parallel an `ISET` liegen; die hochohmige Alternative wären
  ca. 50mA. **Diesen Pin nicht High setzen.** Der tatsächliche Ladestrom wurde
  auf Hardware noch nicht gemessen.
- Stromsparen: UART0 deaktiviert, IMU/Mikrofon der Sense-Variante per
  `gpio-hog` auf P1.08/P1.10 deaktiviert, QSPI-Flash beim Start in Deep
  Power-Down versetzt (`CONFIG_PM_DEVICE`), und der HX711 zwischen den
  Messungen im Power-Down. Der Ladestatus wird von P0.17 gelesen (BQ25101
  `~CHG`, Low = lädt). Die Akkuspannung wird über einen Spannungsteiler in
  AIN7 (P0.31) gemessen, gesteuert über P0.14, das gemäß Seeeds Empfehlung
  dauerhaft Low gehalten wird.
- Programmierung erfolgt über USB und den werkseitigen UF2-Bootloader
  (Adafruit); kein SWD erforderlich.

### Messzyklus und Advertising-Burst

Alle `CONFIG_GASBEE_MEASURE_INTERVAL_S` (Standard **300s**, **60s im
Testmodus**) läuft das Gerät einen Messzyklus:

1. HX711 aufwecken und 4 Wandlungen verwerfen, während er sich einschwingt.
2. 5 Samples mitteln.
3. HX711 zurück in den Power-Down legen.
4. Akkuspannung und Ladestatus lesen.
5. Einen Advertising-Burst senden: 10 Pakete im Abstand von 1s, danach
   Funkstille bis zum nächsten Zyklus.

GATT-Befehle (Tare, Kalibrierung) lösen eine sofortige Zusatzmessung aus, ohne
den 5-Minuten-Takt zu ändern. Fehlt der HX711, wird trotzdem gesendet, aber das
Gewicht ist als ungültig markiert. Die Leerlaufzeit verbringt das Gerät in
Zephyr System ON Idle; System OFF ist nicht nutzbar, weil der nRF52840 daraus
nicht per Timer aufwachen kann. **Der Stromverbrauch im Normalbetrieb wurde
noch nicht gemessen** und wird hier bewusst nicht mit einer Zahl dokumentiert.

### USB-Konfigurationsmodus

Sobald USB-Spannung erkannt wird (VBUS, über
`nrf_power_usbregstatus_vbusdet_get`, abgefragt alle `GASBEE_USB_POLL_MS`),
wechselt der GasBee nRF innerhalb von höchstens 2 Sekunden in den
Konfigurationsmodus:

- Er sendet durchgehend, einmal pro Sekunde, und ist verbindbar.
- Er misst alle 5s, damit während der Kalibrierung Live-Messwerte der Waage
  sichtbar sind.
- Er setzt Advertising-Flag Bit 3.
- Die Kalibrier-Characteristics `0x2B11`–`0x2B13` akzeptieren nur in diesem
  Modus Writes; im Akkubetrieb liefern sie den ATT-Fehler „Write Not
  Permitted".

Wird USB abgezogen, trennt das eine offene Verbindung und bringt das Gerät
zurück in den Akkubetrieb (5-Minuten-Burst-Takt). Ein dauerhaft an USB
hängendes Gerät bleibt einfach im Konfigurationsmodus und meldet weiter, nur
häufiger.

### Advertising-Format v1 (passiver Scan)

22 von 31 Advertising-Byte werden genutzt (Flags + Complete Local Name
„GasBee" + Manufacturer Specific Data, Company-ID `0xFFFF`); eine
Scan-Response wird nicht benötigt.

| Byte(s) | Typ | Feld | Ungültig-Marker |
|---|---|---|---|
| 0 | uint8 | Format-Version = 1 | – |
| 1–2 | int16 LE | Bruttogewicht der Waage, in 10g-Einheiten, **ohne Tara** | `0x8000` |
| 3–4 | uint16 LE | Akkuspannung, mV | `0xFFFF` |
| 5 | uint8 | Sequenzzähler, +1 pro Messzyklus (alle 10 Pakete eines Bursts teilen sich einen Wert) | – |
| 6 | uint8 | Flags: Bit0 HX711 ok, Bit1 lädt, Bit2 kalibriert (Offset/Scale in NVS), Bit3 Konfigurationsmodus (USB angesteckt) | – |

Tara, Gas-Nettogewicht, Füllstand in % und Akku-% werden alle vom Sensorboard
berechnet, nicht vom Knoten selbst. Die BLE-Adresse ist random-static und pro
Gerät fest.

### GATT-Service (nur Kompatibilität)

Das Sensorboard nutzt GATT auf dem GasBee nRF nicht; es ist für andere Clients
wie nRF Connect auf dem Handy sowie für den Tare-/Kalibrier-Ablauf erhalten
geblieben. Das Advertising bleibt verbindbar, sodass sich ein GATT-Client
mitten in einem Burst verbinden kann — solange diese Verbindung besteht,
finden keine weiteren Bursts (und damit keine Sensorboard-Updates) statt, bis
der Client die Verbindung trennt.

| UUID | Inhalt | Typ | Zugriff |
|---|---|---|---|
| `0x2B01` | Gewicht, kg (nach Tara) | float32 LE | Read, Notify |
| `0x2B02` | Füllstand, % | uint8 | Read, Notify |
| `0x2B03` | Gas-Nettogewicht, kg | float32 LE | Read, Notify |
| `0x2B04` | Akkuspannung, mV | uint16 LE | Read, Notify |
| `0x2B10` | Tare | uint8 (`0x01`) | Write |
| `0x2B11` | Kalibrierung Nullpunkt (Waage leer) | uint8 (`0x01`) | Write |
| `0x2B12` | Kalibrierung Referenzgewicht, Gramm | uint32 LE | Write |
| `0x2B13` | Kalibrierwerte: Offset (int32 LE) + Scale (float32 LE); Scale = 0 löscht die Kalibrierung | 8 Byte | Read, Write |

`0x2B04` (Akkuspannung) gibt es nur beim GasBee nRF, nicht beim ESP32-C3
GasBee. Die Kalibrierung ist pro Gerät in NVS (`gasbee/offset`, `gasbee/scale`)
persistiert und übersteht Reset und Firmware-Updates; ohne gespeicherte Werte
greift die Firmware auf Compile-Time-Defaults in `gasbee_config.h` zurück und
meldet Flag Bit 2 = 0 („nicht kalibriert"). Ein Write auf `0x2B10` übernimmt
das *nächste* gemessene Bruttogewicht als neue Tara — anders als beim
ESP32-C3-GasBee, wo Tare nur den Offset auf null setzte, ohne einen echten
Nullpunktabgleich. Den vollständigen Ablauf beschreibt
[Gas Calibration](../workflows/gas-calibration.md).

## GasBee (ESP32-C3) — Legacy

`firmware/gasbee` ist die ursprüngliche GasBee-Implementierung auf einem
ESP32-C3 Mini mit NimBLE auf ESP-IDF. Sie wird **vom Sensorboard nicht mehr
genutzt**, das nur noch die Advertising-Daten des GasBee nRF konsumiert. Sie
verbleibt im Repository, weil ihr GATT-Service weiterhin von
Drittanbieter-BLE-Clients genutzt wird (z. B. mobile Apps), die noch nicht
migriert sind.

- HX711 an `GPIO4` (DOUT) / `GPIO5` (SCK), einkanalig, alle 5s abgefragt,
  5-Sample-Mittelung (`GASBEE_HX711_POLL_INTERVAL_MS`,
  `GASBEE_HX711_AVG_SAMPLES` in `gasbee_config.h`).
- Sendet Advertising als `GasBee` mit festem Intervall
  (`GASBEE_BLE_ADV_INTERVAL_MS` = 500ms) und bietet einen eigenen 128-Bit-
  GATT-Service (`4a5b6c7d-8e9f-0a1b-2c3d-4e5f60718293`):

  | UUID | Inhalt | Typ | Zugriff |
  |---|---|---|---|
  | `0x2B01` | Gewicht, kg | float32 LE | Notify |
  | `0x2B02` | Füllstand, % | uint8 | Notify |
  | `0x2B03` | Gas-Nettogewicht, kg | float32 LE | Notify |
  | `0x2B10` | Tare | uint8 (`0x01` = auslösen) | Write |

  Es gibt keine Akkuspannungs-Characteristic und keinen Advertising-basierten
  Datenpfad — ein Client muss sich verbinden und Notifications abonnieren, um
  Werte zu bekommen, was genau der Grund ist, warum das nur passiv scannende
  Sensorboard dies nicht nutzen kann.
- Kalibrierung (Offset, Scale) sowie Flaschenfüllung/Tara-Konstanten
  (`GASBEE_GAS_FILL_KG` = 11,0kg, `GASBEE_GAS_TARE_KG` = 10,5kg) liegen als
  Compile-Time-Defaults in `gasbee_config.h`, wobei Offset/Scale optional in
  NVS persistiert werden (Namespace `gasbee`).

## RuuviTag Gas — geplanter Hardware-Fork

RuuviTag Gas ist ein Hardware-Fork von **RuuviTag Rev.B8**
(`ruuvi/ruuvitag_hw` @ `afa1b03`, CC BY-SA 4.0), der mittelfristig den
ESP32-C3-GasBee durch ein nRF52832-basiertes Gasflaschen-Beacon ersetzen soll.
**Stand dieses Textes befindet er sich im laufenden Hardware-Fork
(„Phase 2 – geplant"), die Firmware-Arbeit hat noch nicht begonnen.** Es ist
ein eigenständiges Projekt, getrennt vom ausgelieferten GasBee nRF, keine
alternative aktuelle Implementierung.

### Status

| Phase | Zustand |
|---|---|
| Phase 1 — Rev.B8-Schaltplan/PCB nach KiCad 9 migrieren, unverändert | Erledigt |
| Phase 2a — Schaltplan: ungenutzte Sensoren entfernen, HX711 + Load-Switch ergänzen | Erledigt |
| Phase 2b — PCB: vom Schaltplan aktualisieren, 4-Lagen-Routing, DRC | Nicht begonnen |
| Phase 3 — Dokumentation, Stückliste, Gerbers | Nicht begonnen |
| Firmware (Nordic SDK / Zephyr oder Ruuvi-Firmware-Fork, **nicht** ESP-IDF) | Nicht begonnen |

### Geplante Hardware-Änderungen gegenüber Rev.B8

Entfernt: Drucksensor (DPS310), Beschleunigungssensor (LIS2DH12) und der
werkseitige Temperatursensor (TMP117) — der verbleibende SHTC3 deckt
Temperatur und Feuchte ab. Damit entfallen auch der SPI-Bus und dessen
Interrupt-Leitungen (`ACC_INT1/2`, `~SPI_ACC_CS`, `~SPI_PRESS_CS`,
`SPI_SCK/MOSI/MISO`, `~TMP_ALERT`).

Hinzugefügt: ein HX711-Wägezellen-ADC, verdrahtet an drei durch den
Sensor-Rückbau freigewordene nRF52832-GPIOs — `PD_SCK` auf P0.07, `DOUT` auf
P0.08 — sowie eine eigene `HX711_EN`-Leitung auf P0.03, die einen
High-Side-P-MOSFET-Load-Switch ansteuert (as-built: Q2 `Q_PMOS_GSD`,
angesteuert über Q1 `Q_NMOS_GSD`). Das schaltet sowohl den HX711 als auch die
Wägezellen-Brücke stromlos, sodass die Schaltung im Leerlauf praktisch keinen
Strom zieht — eine bewusste Abweichung vom GasBee-nRF-Design, das den HX711
von einem dauerhaft eingeschalteten 3V3-Rail versorgt (dort akzeptabel, aber
unzulässig gegen eine CR2477-Knopfzelle, die Stromquelle des RuuviTag ist).
Die Wägezelle ist eine 4×50kg-Vollbrücke, verdrahtet an vier SMD-Pads
(`E+`/`E-`/`A+`/`A-`); zwei zusätzliche DNP-Pads (`VBAT`/`GND`) existieren für
ein optionales externes Batteriepack. `RATE` liegt fest auf GND für den
langsameren, rauschärmeren 10-SPS-HX711-Modus. Ein RC-/Gleichtaktfilter an
den Verstärkereingängen existiert gezielt, um die 2,4-GHz-BLE-HF-Einkopplung
des eigenen Boards in den 24-Bit-ADC-Eingang zu unterdrücken, nicht aus
Gründen der Kabellänge.

Offene Hardware-TODOs, bevor dies gebaut und kalibriert werden kann:
tatsächlichen Wägezellen-Brückenwiderstand messen, um Erregerstrom und
Batteriebudget zu bestätigen, das HX711-Gehäuse-Footprint finalisieren, die
Gate-Schwellspannungsreserve des P-MOSFET bei Batterie-Lebensende-Spannung
prüfen, und das Wäge-/Advertising-Intervall festlegen (eine
Firmware-Entscheidung, die die tatsächliche Batterielaufzeit direkt
bestimmt).

## Standard-RuuviTag (DF5) — Klima-Fallback innen/außen

Unveränderte RuuviTag-Sensoren, die Ruuvi **Data Format 5 (RAWv2)** senden,
werden als Klimasensoren innen/außen genutzt. Der `ruuvi_ble_scanner` des
Sensorboards dekodiert diese aus den Manufacturer-Data des Advertisements als
weiterer passiver Scan-Handler, der bei demselben zentralen `ble_manager`
registriert ist. Es existieren zwei Slots (innen/außen), rein nach
MAC-Filter geroutet, der zur Laufzeit über das Display (`cmd:"ruuvi_config"`)
konfigurierbar ist statt fest einkompiliert zu sein, und in NVS persistiert
wird.

Extrahierte Felder: `temp_c`, `humidity_pct`, `pressure_hpa` (wenn vorhanden)
und `battery_mv` (dekodiert aus Byte 15–16 der DF5-Payload als 11-Bit-Wert
mit 1600mV-Offset, sofern gültig). Ein fest verdrahteter BME680/BME280 hat
für denselben Standort immer Vorrang vor einem Ruuvi-Tag; Ruuvi dient nur als
Fallback, wenn kein verdrahteter Klimasensor vorhanden ist. Die vollständige
Einlese- und Publizierkette steht in
[BLE Data Flow](../workflows/ble-data-flow.md).

## Beziehungen und Invarianten zwischen den Knoten

- Alle vier Knotentypen teilen sich eine physische Scan-Ressource: das
  einzelne NimBLE-Funkmodul des Sensorboards im passiven Scan-Modus. Keiner
  von ihnen wird im Normalbetrieb verbunden, außer vorübergehend für die
  GasBee-Kalibrierung oder Legacy-GATT-Clients; währenddessen sind die
  Advertising-Bursts dieses Knotens blockiert, und das Sensorboard sieht für
  ihn veraltete oder keine Daten, bis die Verbindung endet.
- GasBee (beide Generationen) und RuuviTag Gas sind langfristig sich
  gegenseitig ausschließende Rollen für die *Gas*-Messung: GasBee nRF ist die
  aktuelle, per BLE-Advertising konsumierte Gasflaschenquelle; ein
  verdrahteter HX711 direkt am Sensorboard hat Vorrang vor beiden, wenn
  vorhanden (`gas_source.c`); RuuviTag Gas soll, sobald gebaut, die
  ESP32-C3-GasBee-Linie ersetzen, nicht parallel zum GasBee nRF laufen.
- Standard-Ruuvi-DF5-Tags stehen in keiner Beziehung zur Gasmessrolle; sie
  dienen ausschließlich als Klima-Fallback innen/außen hinter verdrahteten
  BME680/BME280-Sensoren.
</content>
</invoke>
