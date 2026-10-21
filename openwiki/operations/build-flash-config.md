---
type: operations-guide
title: Build, Flash und Konfiguration
description: Referenz zum Bauen, Flashen und Konfigurieren der vier WoMoHome-Firmware-Ziele (ESP-IDF v5.5.2 und nRF Connect SDK v3.4.1), inklusive gemeinsamer Konfigurationskonstanten, Credentials-Setup und der NVS-Namespaces, die den Kalibrierungszustand halten.
tags: [firmware, build, flash, esp-idf, zephyr, nrf-connect-sdk, configuration, nvs, vscode-workspace]
sources:
  - id: openwiki-source-a2371d6362e5db4bc834ad03
    resource: repo://CLAUDE.md
  - id: openwiki-source-b97e286336b3377bb26da963
    resource: repo://firmware/display/partitions.csv
  - id: openwiki-source-85ea06d1545a6f69836e7778
    resource: repo://firmware/gasbee_nrf/README.md
  - id: openwiki-source-3ebe585d50641be479be9c39
    resource: repo://firmware/sensorboard/CLAUDE.md
  - id: openwiki-source-a5d6c022a01f384e26d1fdda
    resource: repo://firmware/sensorboard/partitions_2m_app.csv
  - id: openwiki-source-47ca33e28706ed8332656306
    resource: repo://firmware/sensorboard/README.md
  - id: openwiki-source-7ff6287b239f90d6c9ccd298
    resource: repo://firmware/shared/womo_config.h
  - id: openwiki-source-4f26020d793df1dc5caaceac
    resource: repo://firmware/shared/womo_credentials.h.example
  - id: openwiki-source-b5e2d906a1edc4ea5c891837
    resource: repo://womo-display.code-workspace
  - id: openwiki-source-78b0b6b8dc2b520da8a55f1f
    resource: repo://womo-gasbee.code-workspace
  - id: openwiki-source-bc08fb7a78ac84a71df5fd83
    resource: repo://womo-sensor.code-workspace
generated: { by: "openwiki/0.7.2", at: "2026-10-19T20:49:57.319Z" }
verified:
  - by: openwiki/0.7.2
    at: 2026-10-19T20:49:57.319Z
---

## Übersicht

Das WoMoHome-Projekt baut aus einem Repository vier unabhängige Firmware-Images:

| Teilprojekt | MCU | Verzeichnis | SDK |
|---|---|---|---|
| Sensorboard | ESP32-S3-WROOM-1 N16R8 | `firmware/sensorboard/` | ESP-IDF v5.5.2 |
| Display | ESP32-S3-Touch-LCD-7 | `firmware/display/` | ESP-IDF v5.5.2 |
| GasBee | ESP32-C3 | `firmware/gasbee/` | ESP-IDF v5.5.2 |
| GasBee nRF | Seeed XIAO nRF52840 | `firmware/gasbee_nrf/` | nRF Connect SDK v3.4.1 (Zephyr) |

Sensorboard und Display werden gegen denselben gemeinsamen Protokoll-/Konfigurations-Header (`firmware/shared/womo_config.h`) gebaut; wie diese beiden Firmwares über RS485 zusammenspielen, zeigen die [Sensorboard-Architektur](../architecture/sensorboard.md) und die [Display-Architektur](../architecture/display.md), das Wire-Format selbst beschreibt [RS485-Protokoll](../workflows/rs485-protocol.md). GasBee nRF ist die aktive BLE-Waagen-Firmware; das ESP32-C3-GasBee-Projekt wird nur als Referenz-/Kompatibilitäts-Fallback gepflegt (sein GATT-Service bleibt für Handy-Apps und den Tare-Befehl erhalten).

**Arbeitsregel für Agenten:** Laut `CLAUDE.md` werden Build-, Flash- und Monitor-Aktionen vom Nutzer über VS-Code-Tasks gestartet. Claude/Agenten dürfen die Build-/Flash-/Monitor-Schritte nur erklären und niemals selbst Flash-Kommandos ausführen.

## Gemeinsame Konfiguration: `firmware/shared/womo_config.h`

`firmware/shared/womo_config.h` enthält Konstanten, gegen die sowohl die Sensorboard- als auch die Display-Firmware kompilieren: RS485-/ESP-NOW-Protokollparameter (Heartbeat-Intervall, Command-Timeout, maximale Anzahl ausstehender Kommandos), Sende-Intervalle pro Topic (`WOMO_TOPIC_*_INTERVAL_MS`), die IP des RUTX11-Routers sowie ESP-NOW-Transportparameter (Fallback-WiFi-Kanal, maximale Payload-Größe, Länge der Empfangswarteschlange). Die Datei dokumentiert ihre eigene Einschränkung ausdrücklich: hardwarespezifische Werte (GPIOs, UART-Port, Puffergrößen) gehören **nicht** hierher — nur Werte, die auf beiden Seiten der RS485-/ESP-NOW-Verbindung identisch bleiben müssen.

**Jede Änderung an `firmware/shared/womo_config.h` erfordert ein Rebuild UND Reflash sowohl des Sensorboards als auch des Displays.** Da beide Firmwares diesen Header zur Compile-Zeit einbinden, bricht eine Abweichung zwischen einem geflashten Sensorboard und einem geflashten Display (z. B. ein auf nur einer Seite geänderter `WOMO_ESPNOW_CHANNEL_FALLBACK` oder ein geändertes Intervall) die Protokollkompatibilität zwischen beiden.

### Credentials

`womo_config.h` bindet `womo_credentials.h` ein, die **nicht in Git eingecheckt ist** (in `.gitignore` gelistet) und vor dem Bauen eines der beiden Projekte lokal angelegt werden muss:

```bash
cp firmware/shared/womo_credentials.h.example firmware/shared/womo_credentials.h
```

Danach die Kopie bearbeiten und die echten Werte eintragen:

```c
#define WOMO_WIFI_DEFAULT_SSID  "DEINE-SSID"
#define WOMO_WIFI_DEFAULT_PASS  "DEIN-PASSWORT"
```

`WOMO_WIFI_DEFAULT_SSID`/`WOMO_WIFI_DEFAULT_PASS` sind die WiFi-Standard-Zugangsdaten, mit denen sich das Sensorboard dem Netz des RUTX11-Routers anschließt, bevor in NVS gespeicherte Zugangsdaten (Namespace `wifi_cfg`, siehe unten) konfiguriert sind. Ohne vorhandene `womo_credentials.h` schlägt der ESP-IDF-Build von Sensorboard oder Display fehl, weil der Header nicht existiert.

## ESP-IDF-Ziele (Sensorboard, Display, GasBee)

Alle drei ESP-IDF-Ziele teilen sich dasselbe Toolchain-Setup und dasselbe Befehlsmuster:

```bash
source /home/hajo/esp/v5.5.2/esp-idf/export.sh
cd firmware/<sensorboard|display|gasbee>
idf.py build
```

### VS-Code-Workspaces und Tasks

Drei Workspace-Dateien im Repo-Wurzelverzeichnis gruppieren die ESP-IDF-Projekte und definieren ihre VS-Code-Tasks:

- **`womo-sensor.code-workspace`** — primärer Workspace für die Arbeit am Sensorboard. Ordner: Sensorboard (`firmware/sensorboard`, MAIN), Display (`firmware/display`, REF), GasBee ESP32-C3 (`firmware/gasbee`). Tasks: `Build Sensor Firmware`, `Flash Sensor Firmware` (`idf.py -p /dev/ttyESP_SENSOR flash`), `Monitor Sensor`, `Build + Flash + Monitor Sensor`, `Clean Sensor Build`, dazu die GasBee-ESP32-C3-Tasks `Build GasBee Firmware`, `Flash GasBee (/dev/ttyUSB0)`, `Monitor GasBee (/dev/ttyUSB0)`, `Build + Flash + Monitor GasBee`.
- **`womo-display.code-workspace`** — primärer Workspace für die Arbeit am Display. Ordner: Display (`firmware/display`, MAIN), Sensorboard (`firmware/sensorboard`, REF). Tasks: `Build Display Firmware`, `Flash Display Firmware` (`idf.py -p /dev/ttyESP_DISPLAY flash`), `Monitor Display`, `Build + Flash + Monitor Display`, `Clean Display Build`.
- **`womo-gasbee.code-workspace`** — Workspace für das GasBee-nRF-Ziel (Zephyr). Ordner: GasBee nRF (`firmware/gasbee_nrf`, MAIN), GasBee ESP32-C3 (`firmware/gasbee`, nur Referenz). Tasks: `Build GasBee nRF (Testmodus)`, `Build GasBee nRF (Normalbetrieb)`, `Flash GasBee nRF (UF2)`, `Monitor GasBee nRF`, `Clean GasBee nRF`.

Beide ESP-IDF-Workspaces legen pro Board einen festen seriellen Port fest (`idf.port": "/dev/ttyESP_SENSOR"` bzw. `/dev/ttyESP_DISPLAY`) sowie eine gemeinsame Flash-Baudrate von `460800` und verlassen sich dabei auf udev-Regeln, die stabile Gerätenamen auf den USB-Seriell-Adapter des jeweiligen Boards abbilden. Die GasBee-ESP32-C3-Tasks verwenden stattdessen fest `/dev/ttyUSB0`.

### Partitionslayouts

| Ziel | Partitionsdatei | nvs | phy_init | app (factory) | storage (SPIFFS) |
|---|---|---|---|---|---|
| Sensorboard | `firmware/sensorboard/partitions_2m_app.csv` | `0x9000`, 24 KB | `0xf000`, 4 KB | 2 MB | 1 MB |
| Display | `firmware/display/partitions.csv` | `0x9000`, 20 KB | `0xe000`, 4 KB | 3 MB ab `0x10000` | `0x190000` (~1,56 MB) |

Beide Partitionstabellen reservieren eine eigene `nvs`-Partition für die unten beschriebenen Kalibrierungs-/Konfigurations-Namespaces sowie eine `storage`-SPIFFS-Partition (Sensorboard: Web-Dashboard-Assets, ausgeliefert unter `/spiffs`; Display: Hintergrundbilder/weitere statische Assets). Veränderte oder in der Größe angepasste Partitionstabellen machen bestehende NVS-Daten an den alten Offsets ungültig; Änderungen an der Partitionstabelle sollten daher mit derselben Rebuild-/Reflash-Disziplin behandelt werden wie Änderungen an `womo_config.h` — allerdings nur für das betroffene Board (das Partitionslayout ist pro Firmware, nicht gemeinsam).

## NVS-Namespaces (Sensorboard)

Das Sensorboard persistiert Kalibrierungs- und Laufzeit-Konfigurationszustand in mehreren NVS-Namespaces, damit Werte Neustarts und Firmware-Updates überstehen. Das ist beim Debuggen von Sensordrift direkt relevant, da ein zur Laufzeit falsch wirkender Wert tatsächlich eine veraltete persistierte Kalibrierung statt eines aktuellen Sensormesswerts sein kann:

| Namespace | Key(s) | Inhalt |
|---|---|---|
| `bno055` | `calib`, `pr_zero` | IMU-Kalibrierungsoffsets, Druck-Nullpunkt |
| `bme680` | `press_hist` | Luftdruck-Historie (96 Samples / 24 h) für die Trendberechnung |
| `bsec` | `state_in_<addr>_v<n>` | BSEC-IAQ-State, pro Sensoradresse/-version (z. B. `state_in_76_v3`); gespeichert ab einer Accuracy von ≥ 3, damit ein Neustart nahe der zuletzt bekannten Genauigkeit fortsetzt |
| `gas_state` | `gas` | Gasverbrauchs-Zustand: Raten, Voll-Flaschen-Referenzen, letzter Wert mit Zeitstempel und Quelle |
| `tank_state` | `tank1`, `tank2` | Tank-Verbrauchsraten |
| `wifi_cfg` | `ssid`, `pass` | WLAN-Zugangsdaten für das RUTX11-Netz (überschreiben die Compile-Time-Defaults aus `womo_credentials.h`, sobald gesetzt) |
| `ruuvi_cfg` | `indoor_mac`, `outdoor_mac` | MAC-Filter für den Ruuvi-BLE-Scanner, vom Display aus konfiguriert |
| `gasbee_cfg` | `bottle_a_mac`, `bottle_b_mac` | Zuordnung der GasBee-BLE-Geräte zu den Flaschen-Slots A/B, vom Display aus konfiguriert |

Zwei verwandte Stellschrauben in `firmware/sensorboard/main/sensor_config.h` beeinflussen, wie dieser persistierte Zustand interpretiert wird:

- `SENSOR_INA226_SHUNT_MOHM` muss zum verbauten Shunt-Widerstand (mΩ) passen; `SENSOR_INA226_MAX_CURRENT_A` legt die ADC-Auflösung fest (`Current_LSB = MAX_CURRENT / 32768` A), begrenzt durch eine maximale Shuntspannung von ±81,92 mV, die `I_max` bestimmt.
- `SENSOR_BME680_BSEC_STATE_VERSION` muss immer dann hochgezählt werden, wenn der Indoor-BME680-Sensor physisch getauscht wird, damit der alte BSEC-Baseline-State im `bsec`-Namespace nicht auf den neuen Sensor angewandt wird.

Die GasBee-nRF-Waage (eigener MCU, eigenes Flash/NVS) persistiert ihre eigene Pro-Gerät-Kalibrierung unabhängig: Offset und Scale liegen unter den Keys `gasbee/offset` und `gasbee/scale` auf der GasBee selbst, gesetzt über die GATT-Characteristics 0x2B11–0x2B13, während sich das Gerät im USB-Konfigurationsmodus befindet. Diese Werte sind getrennt vom `gasbee_cfg`-Namespace des Sensorboards, der nur die MAC-zu-Slot-Zuordnung speichert, nicht die interne Kalibrierung der Waage.

## ESP-IDF-Build-/Flash-/Monitor-Workflow (nur erklären)

Für Sensorboard und Display ist der Standardablauf, den ein Nutzer über VS-Code-Tasks ausführt (oder gleichwertig auf der Kommandozeile nach `source .../esp-idf/export.sh`):

```bash
idf.py build
idf.py -p <port> flash
idf.py -p <port> monitor
```

wobei `<port>` jeweils `/dev/ttyESP_SENSOR` bzw. `/dev/ttyESP_DISPLAY` ist (in den „Build + Flash + Monitor“-Tasks kombiniert als `idf.py -p <port> build flash monitor`). `idf.py fullclean` leert das Build-Verzeichnis eines Projekts. Für das ESP32-C3-GasBee-Referenzziel ist der Port statt eines benannten udev-Alias `/dev/ttyUSB0`.

## nRF-Connect-SDK-Ziel (GasBee nRF)

GasBee nRF ersetzt das ESP32-C3-basierte `firmware/gasbee` im täglichen Einsatz: Es wiegt die Gasflasche über einen HX711-Wägezellenverstärker und sendet Gewicht, Akkuspannung und Status als BLE-Advertising-Daten (Advertising-Format v1), die vom Sensorboard (`gasbee_ble_scanner.c`) passiv gescannt werden, ohne GATT-Verbindung. Der GATT-Service existiert weiterhin für andere Clients (z. B. Handy-Apps) und für den Tare-Befehl.

### Voraussetzung

nRF Connect SDK **v3.4.1** mit passender Toolchain, installiert über die VS-Code-Extension „nRF Connect“ nach `~/ncs`.

### Build

**VS Code (ad hoc):** Im nRF-Connect-Panel „Open an existing application“ wählen, `firmware/gasbee_nrf` öffnen und eine Build-Konfiguration mit Board **`xiao_ble`** anlegen.

**VS Code mit Workspace-Datei:** `womo-gasbee.code-workspace` im Repo-Wurzelverzeichnis öffnen. Sie definiert:
- Build Testmodus (Standard, Strg+Shift+B)
- Build Normalbetrieb
- Flash per UF2-Kopie auf `XIAO-SENSE`
- Monitor über `tools/gasbee-monitor.sh`
- Clean

Der ESP32-C3-GasBee-Ordner ist in diesem Workspace nur als Referenz eingebunden, nicht zum Bauen.

**Kommandozeile (Build für Normalbetrieb):**

```bash
cd firmware/gasbee_nrf
~/.vscode/extensions/nordic-semiconductor.nrf-connect-*/platform/nrfutil/bin/nrfutil-sdk-manager \
  toolchain launch --ncs-version v3.4.1 -- \
  env ZEPHYR_BASE=$HOME/ncs/v3.4.1/zephyr west build -b xiao_ble -d build .
```

Ergebnis: `build/gasbee_nrf/zephyr/zephyr.uf2`.

**Build für Testmodus (Status-LED aktiviert):** derselbe Befehl, mit geändertem `west build`-Teil:

```bash
... west build -p always -b xiao_ble -d build . -- -DEXTRA_CONF_FILE=debug.conf
```

Ohne `-DEXTRA_CONF_FILE=debug.conf` bleibt die Status-LED aus. `-p always` erzwingt eine vollständige Rekonfiguration, damit der Wechsel zwischen Testmodus- und Normalbetrieb-Build zuverlässig übernommen wird. Der Testmodus verkürzt außerdem das Messintervall (`CONFIG_GASBEE_MEASURE_INTERVAL_S`, Standard 300 s, Testmodus 60 s).

Der Projektpfad darf **keine Leerzeichen** enthalten, sonst bricht der Zephyr-Build ab.

### Flashen (UF2, kein SWD nötig)

1. Den XIAO per USB anschließen.
2. Den Reset-Taster **zweimal schnell** drücken; das USB-Massenspeicher-Laufwerk `XIAO-SENSE` erscheint.
3. `build/gasbee_nrf/zephyr/zephyr.uf2` auf dieses Laufwerk kopieren, z. B.:
   ```bash
   cp build/gasbee_nrf/zephyr/zephyr.uf2 /media/$USER/XIAO-SENSE/ && sync
   ```
   Das Board startet sofort neu und wirft das Laufwerk selbst aus, sodass der Kopiervorgang wie fehlgeschlagen aussehen kann, obwohl das Flashen erfolgreich war. Erfolg bestätigt sich dadurch, dass der serielle Port wieder als `Zephyr_Project_CDC_ACM` erscheint.

### Monitor (USB-CDC)

Das Logging läuft über USB-CDC (VID/PID `2fe3:0004`). Die udev-Regel `tools/udev/99-esp32-womo.rules` weist jeder GasBee einen festen Gerätenamen `/dev/ttyGASBEE_<serial>` zu (serial = Chip-individuelle Seriennummer), einmalig installiert mit:

```bash
sudo cp tools/udev/99-esp32-womo.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Danach Monitor mit:

```bash
tools/gasbee-monitor.sh            # eine GasBee: verbindet direkt; mehrere: nummerierte Auswahl
tools/gasbee-monitor.sh 9F3A       # Filter auf einen Teil der Seriennummer
```

Beenden mit `Strg+]`. Die Boot-Log-Ausgabe ist nur sichtbar, wenn der Port direkt nach einem einfachen Reset geöffnet wird; das UF2-Bootloader-Laufwerk (`XIAO-SENSE`) stellt keinen seriellen Port bereit.

## GasBee-nRF-Kalibrierungszustand (getrennt vom Sensorboard-NVS)

Jede GasBee-nRF-Einheit wird einzeln kalibriert, und ihre Kalibrierung liegt im eigenen On-Device-NVS, nicht im NVS des Sensorboards:

- `gasbee/offset`, `gasbee/scale` — persistierter Nullpunkt-Offset und Skalierungsfaktor, überstehen Reset und Firmware-Updates.
- Ohne diese NVS-Werte fällt das Gerät auf die Compile-Time-Defaults in `gasbee_config.h` zurück und meldet ein „unkalibriert“-Flag (Advertising-Byte 6, Bit 2 = 0); das Sensorboard zeigt die Flasche dann als „nc“ an.
- Die Kalibrierung erfolgt über GATT, während sich das Gerät im USB-Konfigurationsmodus befindet: `0x01` auf Characteristic `0x2B11` schreiben, während die Waage leer ist (Nullpunkt), danach das Referenzgewicht in Gramm (uint32 LE) auf `0x2B12`, danach optional Offset/Scale von `0x2B13` zurücklesen (int32 LE + float32 LE, 8 Byte). Ein direktes Schreiben von `0x2B13` mit `scale = 0` löscht die gespeicherte Kalibrierung und stellt die Defaults aus `gasbee_config.h` wieder her.
- Tare (Characteristic `0x2B10`, Schreiben von `0x01`) speichert das als Nächstes gemessene Bruttogewicht als Tara-Punkt in NVS, unabhängig von der obigen Offset-/Scale-Kalibrierung.

Das ist getrennt vom eigenen `gasbee_cfg`-NVS-Namespace des Sensorboards (nur Flasche-zu-MAC-Zuordnung, siehe Tabelle oben) — eine „unkalibrierte“ GasBee und ein „nicht zugewiesener“ Flaschen-Slot sind zwei unterschiedliche Fehlerbilder, die beim Debuggen eines fehlenden oder unplausiblen Gaswerts getrennt geprüft werden müssen.
