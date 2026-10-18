# GasBee nRF – BLE-Gaswaage auf Seeed XIAO nRF52840

Ersatz für [firmware/gasbee](../gasbee/) (ESP32-C3). Wiegt die Gasflasche mit einer Wägezelle über einen HX711 und sendet Gewicht, Gas-Netto und Füllstand per BLE GATT an das Sensorboard. Das BLE-Protokoll ist identisch, deshalb braucht das Sensorboard keine Änderung.

## Hardware

| Komponente | Details |
|---|---|
| MCU | Seeed XIAO nRF52840 **Sense** (Zephyr-Board `xiao_ble`). Die Sense-Variante hat dieselben D-Pins; IMU und Mikrofon werden nicht genutzt. |
| Bootloader (geliefert) | UF2 Bootloader 0.6.1, SoftDevice S140 7.3.0 (aus `INFO_UF2.TXT`). Die Firmware liegt ab 0x27000 und nutzt den Zephyr-/SDC-Controller, nicht die SoftDevice. |
| ADC | HX711, Kanal A, Gain 128 |
| Versorgung HX711 | **3V3 vom XIAO**. Die GPIOs des nRF52840 sind nicht 5-V-tolerant. |
| Programmierung | USB, UF2-Bootloader (Adafruit), kein SWD nötig |
| Akku | LiPo 1 Zelle 3,7 V / 1000 mAh an den Pads BAT+/BAT− (Unterseite). Geladen wird über USB-C vom Laderegler BQ25101 auf dem Board, ohne externe Beschaltung. |
| Ladestrom | ca. **100 mA** (0,1C): P0.13 (`HICHG`) per `gpio-hog` fest Low, damit liegen R12 und R18 (je 2,7 kΩ) parallel an ISET. Hochohmig wären es ca. 50 mA. **Nicht High setzen.** Nachgemessen ist der Wert noch nicht. |
| Ladestatus | P0.17 = BQ25101 `~CHG`, Low = lädt (Seeed-Schaltplan). Das Boardfile legt dort `pwm0` auf, das ist im Overlay deaktiviert. |
| Akkumessung | Laut Seeed-Schaltplan: VBAT – R16 1 MΩ – P0.31 (AIN7) – R17 510 kΩ – P0.14 (READ_BAT). P0.14 bleibt per `gpio-hog` dauerhaft Low, wie Seeed empfiehlt. Bei High kann P0.31 über 3,6 V kommen. |

### Pinbelegung

| HX711 | XIAO | nRF52840 |
|---|---|---|
| VCC | 3V3 | – |
| GND | GND | – |
| DOUT | D2 | P0.28 (interner Pull-Up) |
| PD_SCK | D3 | P0.29 |

Die RGB-LED kommt aus dem Boardfile: Rot `led0` an P0.26, Grün `led1` an P0.30, Blau `led2` an P0.06, alle active low. Genutzt wird sie nur im Testmodus, siehe [Status-LED](#status-led-testmodus).

Die HX711-Pins werden in [app.overlay](app.overlay) festgelegt. Die Zuordnung von D-Pin zu Port stammt aus `zephyr/boards/seeed/xiao_ble/seeed_xiao_connector.dtsi`.

## Struktur

```
CMakeLists.txt
prj.conf              BT Peripheral, Settings/NVS, Logging
app.overlay           HX711-Pins (zephyr,user), Akkuteiler (vbatt), ADC-Kanal AIN7, P0.14-Hog
src/main.c            Mess-Schleife, Gas-Berechnung, Tare, Kalibrierung
src/hx711.c/.h        HX711-GPIO-Treiber (Port von esp-idf-lib/hx711, BSD-3)
Kconfig               Projektoption CONFIG_GASBEE_STATUS_LED
debug.conf            Testmodus (Status-LED an), Zusatz zu prj.conf
src/ble_gas.c/.h      GATT-Server + Advertising
src/battery.c/.h      Akkuspannung (Zephyr voltage-divider) + Ladestatus (P0.17)
src/status_led.c/.h   RGB-Status-LED (nur mit CONFIG_GASBEE_STATUS_LED)
src/gasbee_config.h   Konstanten (Kalibrierung, Gas, BLE-UUIDs, LED-Timing)
```

## BLE-Protokoll

### Betriebsart

Alle `CONFIG_GASBEE_MEASURE_INTERVAL_S` (Standard 300 s, Testmodus 60 s) läuft ein Messzyklus:

1. HX711 aufwecken und 4 Wandlungen verwerfen, weil er nach dem Aufwachen erst einschwingen muss.
2. 5 Samples mitteln.
3. HX711 in den Power-Down legen.
4. Akkuspannung und Ladestatus lesen.
5. Einen Advertising-Burst senden: 10 Pakete im Abstand von 1 s. Danach herrscht Funkstille bis zum nächsten Zyklus.

Auch ohne HX711 wird gesendet, das Gewicht ist dann als ungültig markiert. Im Leerlauf geht der nRF52840 automatisch in System ON Idle. System OFF ist nicht möglich, weil der nRF52840 daraus nicht per Timer aufwacht. Der Stromverbrauch ist noch nicht gemessen.

### Advertising-Format v1 (für passiven Scan)

| AD-Element | Inhalt |
|---|---|
| Flags | 0x06 |
| Complete Local Name | `GasBee`. Steht im Advertising selbst, nicht in der Scan-Response. |
| Manufacturer Specific Data | Company-ID 0xFFFF (Bluetooth SIG: Test/interne Nutzung) + 7 Byte |

Zusammen sind das 22 von 31 Byte, eine Scan-Response wird nicht gebraucht.

| Byte | Typ | Feld | Ungültig |
|---|---|---|---|
| 0 | uint8 | Format-Version = 1 | – |
| 1–2 | int16 LE | Bruttogewicht der Waage in 10 g, **ohne Tara** | 0x8000 |
| 3–4 | uint16 LE | Akkuspannung in mV | 0xFFFF |
| 5 | uint8 | Sequenzzähler, +1 pro Messzyklus (alle 10 Pakete eines Bursts haben denselben Wert) | – |
| 6 | uint8 | Flags: Bit 0 = HX711 ok, Bit 1 = lädt (BQ25101 ~CHG), Bit 2 = kalibriert (Offset und Scale aus NVS), Bit 3–7 = 0 | – |

Tara, Gas-Netto, Füllstand und Akku-% berechnet das Sensorboard. Die Adresse ist random static und bleibt fest.

### GATT (Kompatibilität)

Das Advertising ist verbindbar. Ein Central kann sich während eines Bursts verbinden. Solange die Verbindung steht, gibt es keine Bursts mehr, die Werte kommen dann nur per Notify. Nach dem Trennen sendet der GasBee erst beim nächsten Messzyklus wieder.

- Service: `4a5b6c7d-8e9f-0a1b-2c3d-4e5f60718293`

| UUID | Inhalt | Typ | Zugriff |
|---|---|---|---|
| 0x2B01 | Gewicht kg (nach Tara) | float32 LE | Read, Notify |
| 0x2B02 | Füllstand % | uint8 | Read, Notify |
| 0x2B03 | Gas-Netto kg | float32 LE | Read, Notify |
| 0x2B04 | Akkuspannung mV | uint16 LE | Read, Notify |
| 0x2B10 | Tare | uint8 (`0x01`) | Write |

Die GATT-Werte werden im Messzyklus aktualisiert. 0x2B01 bis 0x2B03 rechnen wie bisher mit der GasBee-Tara und mit 10,5 kg Leergewicht der Flasche.

0x2B04 gibt es nur beim GasBee nRF, der ESP32-C3-GasBee hat sie nicht. Die Umrechnung in % (LiPo-Kennlinie) übernimmt die Gegenstelle.

## Build

Voraussetzung: nRF Connect SDK **v3.4.1** mit passender Toolchain, installiert über die VS-Code-Extension „nRF Connect" nach `~/ncs`.

**VS Code:** Im nRF-Connect-Panel „Open an existing application" wählen, diesen Ordner öffnen und eine Build-Konfiguration mit Board **`xiao_ble`** anlegen.

**VS Code mit Workspace-Datei:** [`womo-gasbee.code-workspace`](../../womo-gasbee.code-workspace) im Repo-Wurzelverzeichnis öffnen. Darin sind diese Tasks:
- Build Testmodus (Standard, Strg+Shift+B)
- Build Normalbetrieb
- Flash per UF2-Kopie auf `XIAO-SENSE`
- Monitor über `tools/gasbee-monitor.sh`
- Clean

Der ESP32-C3-GasBee ist dort nur als Referenzordner eingebunden.

**Kommandozeile:**

```bash
cd firmware/gasbee_nrf
~/.vscode/extensions/nordic-semiconductor.nrf-connect-*/platform/nrfutil/bin/nrfutil-sdk-manager \
  toolchain launch --ncs-version v3.4.1 -- \
  env ZEPHYR_BASE=$HOME/ncs/v3.4.1/zephyr west build -b xiao_ble -d build .
```

Ergebnis: `build/gasbee_nrf/zephyr/zephyr.uf2`

**Testmodus mit Status-LED:** Gleicher Befehl, nur der `west build`-Teil ändert sich:

```bash
... west build -p always -b xiao_ble -d build . -- -DEXTRA_CONF_FILE=debug.conf
```

Ohne diesen Zusatz bleibt die LED aus. `-p always` sorgt dafür, dass der Wechsel zwischen den beiden Varianten sicher übernommen wird.

### Status-LED (Testmodus)

Jede Farbe zeigt einen eigenen Zustand. Mischfarben entstehen, wenn mehrere zutreffen, z. B. Magenta = verbunden + HX711 fehlt.

| Farbe | Zustand | Bedeutung |
|---|---|---|
| Blau | blinkt (80 ms an, alle 1 s) | Advertising-Burst läuft (ca. 10 s nach jeder Messung) |
| Blau | an | verbunden (GATT) |
| Rot | an | HX711 nicht bereit oder keine gültige Messung im letzten Zyklus |
| Grün | 1 Blitz | Messung fertig |
| Grün | 3 Blitze | Tare übernommen |

Timing: `GASBEE_LED_FLASH_MS` und `GASBEE_LED_ADV_BLINK_MS` in `gasbee_config.h`.

Hinweis: Der Projektpfad darf **keine Leerzeichen** enthalten, sonst bricht der Zephyr-Build ab.

## Flashen

1. XIAO per USB anschließen.
2. Reset-Taster **zweimal schnell** drücken. Das USB-Laufwerk `XIAO-SENSE` erscheint.
3. `build/gasbee_nrf/zephyr/zephyr.uf2` auf das Laufwerk kopieren, z. B. mit dem Dateimanager (nicht aus dem VS-Code-Explorer) oder mit:
   ```bash
   cp build/gasbee_nrf/zephyr/zephyr.uf2 /media/$USER/XIAO-SENSE/ && sync
   ```
   Der XIAO startet sofort neu und wirft das Laufwerk aus. Der Kopiervorgang kann deshalb wie fehlgeschlagen aussehen, obwohl das Flashen geklappt hat. Erfolgreich war es, wenn sich der serielle Port danach als `Zephyr_Project_CDC_ACM` meldet.

## Log-Monitor (USB-CDC)

Das Log läuft über USB-CDC (VID/PID `2fe3:0004`, Zephyr-Standard). Statt wechselnder `/dev/ttyACMx`-Nummern bekommt jede GasBee über die udev-Regel [`tools/udev/99-esp32-womo.rules`](../../tools/udev/99-esp32-womo.rules) automatisch einen festen Port:

```
/dev/ttyGASBEE_<USB-Seriennummer>     (Seriennummer = Chip-ID, je XIAO eindeutig)
```

Die Regel ist allgemein gehalten. Für weitere GasBees muss nichts eingetragen werden. Einmalig installieren:

```bash
sudo cp tools/udev/99-esp32-womo.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
ls -l /dev/ttyGASBEE_*
```

Monitor starten (aus dem Repo-Root, oder Task „Monitor GasBee nRF (XIAO, Auswahl)“ in `womo-gasbee.code-workspace`):

```bash
tools/gasbee-monitor.sh            # eine GasBee: startet direkt, mehrere: Auswahl per Nummer
tools/gasbee-monitor.sh 9F3A       # Filter auf einen Teil der Seriennummer
```

Beenden mit `Strg+]`. Das Boot-Log ist nur zu sehen, wenn der Port gleich nach einem einfachen Reset geöffnet wird. Im UF2-Bootloader (Laufwerk `XIAO-SENSE`) gibt es keinen seriellen Port.

**Empfehlung:** In `prj.conf` einen eigenen Produktnamen setzen (`CONFIG_CDC_ACM_SERIAL_PRODUCT_STRING="GasBee"`). Dann kann die udev-Regel zusätzlich auf den Namen prüfen und erfasst keine anderen Zephyr-Geräte mit derselben Standard-ID (TODO in der Regel).

## Kalibrierung

> **TODO:** Die Werte in `gasbee_config.h` (`GASBEE_HX711_OFFSET`, `GASBEE_HX711_SCALE`) stammen vom ESP32-C3-GasBee. Sie gelten mit HX711 an 3,3 V **nicht** und müssen neu gemessen werden.

1. Waage leer: im Log `HX711 raw=…` ablesen. Das ergibt `GASBEE_HX711_OFFSET`.
2. Bekanntes Gewicht `m` (kg) auflegen und `raw_m` ablesen.
3. `GASBEE_HX711_SCALE = m / (raw_m - OFFSET)`
4. Werte in `gasbee_config.h` eintragen und neu bauen.

Werte in den Settings (`gasbee/offset`, `gasbee/scale`) haben Vorrang vor den Defaults. Im Moment schreibt die Firmware nur `gasbee/tare`.

### Tare

Mit Write `0x01` auf 0x2B10 wird das **nächste gemessene Bruttogewicht** als Tara gespeichert, dauerhaft in NVS. Die Waage muss dafür **leer sein, also ohne Flasche**. Danach gilt: Gewicht = Brutto − Tara, und Gas-Netto = Gewicht − 10,5 kg (Leergewicht der Flasche).

Unterschied zum ESP32-C3-GasBee: Dort setzte Tare die Tara nur auf 0 und machte keinen echten Nullabgleich.

## Status

- [x] Build mit NCS v3.4.1, Board `xiao_ble`
- [x] Erster Test auf Hardware (2026-10-17): UF2-Flash, USB-Log, NVS und Advertising „GasBee" laufen. Ohne HX711 kommt „HX711 nicht bereit … retry".
- [x] HX711 angeschlossen, liefert Rohwerte (2026-10-17)
- [x] Verbindung mit dem Sensorboard: Die Werte kommen dort an (2026-10-17). Solange die Verbindung besteht, sendet der XIAO kein Advertising und taucht in keinem Scan auf.
- [x] Akkubetrieb ohne USB funktioniert (2026-10-17)
- [ ] Advertising-Format v1, Burst, HX711-Power-Down, Ladestatus: gebaut, auf Hardware noch nicht geprüft
- [ ] Akkuspannung: Den Log-Wert `Akku=… mV` mit dem Multimeter an BAT+/BAT− vergleichen.
- [ ] Ladestatus P0.17 mit Pull-Up: Prüfen, ob „lädt" bei USB richtig erkannt wird.
- [ ] Stromverbrauch im Akkubetrieb messen (Leerlauf und Burst)
- [ ] HX711-Timing auf nRF verifizieren: PD_SCK-high < 50 µs, auch bei BLE-Aktivität
- [ ] Rauschen und Sprünge der Rohwerte klären, Kalibrierung messen
