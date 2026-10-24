# GasBee nRF – BLE-Gaswaage auf Seeed XIAO nRF52840

Ersatz für [firmware/gasbee](../gasbee/) (ESP32-C3). Wiegt die Gasflasche mit einer Wägezelle über einen HX711 und sendet Bruttogewicht, Akkuspannung und Status per **BLE-Advertising (Format v1)**. Das Sensorboard empfängt das per passivem Scan und verbindet sich nur zur Kalibrierung (Messwerte: `firmware/sensorboard/main/network/gasbee_ble_scanner.c`, dann `sensors/gas_source.c` ins Gas-Topic). Den GATT-Service des ESP32-C3-GasBee gibt es weiterhin, für die Kalibrierung durch das Sensorboard (`gasbee_gatt_cal.c`, nur an USB), für andere Clients wie Handy-Apps und für den Tare-Befehl.

## Hardware

| Komponente | Details |
|---|---|
| MCU | Seeed XIAO nRF52840 **Sense** (Zephyr-Board `xiao_ble`). Die Sense-Variante hat dieselben D-Pins; IMU und Mikrofon werden nicht genutzt. |
| Bootloader (geliefert) | UF2 Bootloader 0.6.1, SoftDevice S140 7.3.0 (aus `INFO_UF2.TXT`). Die Firmware liegt ab 0x27000 und nutzt den Zephyr-/SDC-Controller, nicht die SoftDevice. |
| ADC | HX711, Kanal A, Gain 128 |
| Versorgung HX711 | **3V3 vom XIAO**. Die GPIOs des nRF52840 sind nicht 5-V-tolerant. |
| HX711-Modul | Grünes Standardmodul: VSUP und DVDD an VCC, die Wägezelle (E+) wird über den PNP Q2 (S9015) als Regler des HX711 versorgt, gesteuert über `BASE`. Im Power-Down des HX711 sind HX711 und Messbrücke abgeschaltet. |
| AVDD (E+) | Spannungsteiler 20 kΩ von AVDD nach VFB und 8,2 kΩ von VFB nach GND (wie im SparkFun-Schaltplan `hardware/datasheets/SparkFun_HX711_Load_Cell_v11.pdf`). Daraus ergibt sich ein Sollwert von **AVDD = 1,25 V × (20k + 8,2k) / 8,2k ≈ 4,3 V**. Die Formel „(R1+R2)/R1" auf dem SparkFun-Plan passt nicht zur gezeichneten Schaltung. An 3,3 V schaltet Q2 deshalb voll durch, an E+ liegen dann ca. 3,1–3,3 V (nicht gemessen). Die Kalibrierung ist davon unabhängig, weil der HX711 ratiometrisch misst. |
| Programmierung | USB, UF2-Bootloader (Adafruit), kein SWD nötig |
| Akku | LiPo 1 Zelle 3,7 V / 1000 mAh an den Pads BAT+/BAT− (Unterseite). Geladen wird über USB-C vom Laderegler BQ25101 auf dem Board, ohne externe Beschaltung. Händlerangaben: Ladeschluss 4,2 V, Entladeschluss 2,75 V, Standardladung 0,5C bis 0,05C, max. 1C laden/entladen, Betrieb 0–45 °C, Lagerung −10–45 °C. Laut Produktfoto mit Schutzplatine am Kabelabgang, Abschaltschwelle unbekannt (TODO: an der eigenen Zelle prüfen). |
| Laderegler | BQ25101 (TI SLUSBV8C): CC-CV bis 4,20 V, Vorladen mit 20 % unter 2,5 V, Ladeende bei 10 % des Ladestroms (ca. 10 mA), Nachladen ca. 95 mV unter 4,20 V, Sicherheits-Timer 10 h. Bei 1000 mAh und 100 mA kann der Timer kurz vor „ganz voll“ abschalten, USB ab- und wieder anstecken startet ihn neu. |
| Temperatur beim Laden | **Keine Überwachung:** TS hängt laut Seeed-Schaltplan fest an R20 (10 kΩ nach GND). Der BQ25101 lädt deshalb bei jeder Temperatur mit vollem Strom. Unter 0 °C nicht laden (Akku: Betrieb 0–45 °C). |
| Ladestrom | ca. **100 mA** (0,1C): P0.13 (`HICHG`) per `gpio-hog` fest Low, damit liegen R12 und R18 (je 2,7 kΩ) parallel an ISET. Hochohmig wären es ca. 50 mA. **Nicht High setzen.** Nachgemessen ist der Wert noch nicht. |
| Stromsparen | UART0 aus (D6/D7 frei). IMU und Mikrofon der Sense-Variante aus: P1.08 und P1.10 per `gpio-hog` auf Low. QSPI-Flash beim Start in Deep Power-Down (`CONFIG_PM_DEVICE`). HX711 zwischen den Messungen im Power-Down. |
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
prj.conf              Normalbetrieb: BT Peripheral, Settings/NVS, ohne USB/Log (Stromsparen)
app.overlay           HX711-Pins (zephyr,user), Akkuteiler (vbatt), ADC-Kanal AIN7, P0.14-Hog
src/main.c            Mess-Schleife, Gas-Berechnung, Tare, Kalibrierung
src/hx711.c/.h        HX711-GPIO-Treiber (Port von esp-idf-lib/hx711, BSD-3)
Kconfig               Projektoption CONFIG_GASBEE_STATUS_LED
debug.conf            Testmodus: Status-LED, 60 s, USB-Konsole + Log, Zusatz zu prj.conf
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

**Konfigurationsmodus (USB angesteckt):** Sobald USB-Spannung anliegt, ob vom Netzteil oder vom PC, wechselt der GasBee spätestens nach 2 s in den Konfigurationsmodus:
- Er sendet dauerhaft im Sekundentakt und ist verbindbar.
- Er misst alle 5 s, damit man während der Kalibrierung live sieht, was die Waage misst.
- Er setzt Flag Bit 3.
- Die Kalibrier-Befehle 0x2B11–0x2B13 sind nur in diesem Modus erlaubt. Im Akkubetrieb antwortet der GasBee mit dem ATT-Fehler „Write Not Permitted".

Nach dem Abziehen trennt er laufende Verbindungen und kehrt in den Akkubetrieb zurück (5 min, Burst). Erkannt wird USB über das VBUS-Register des nRF52840 (`nrf_power_usbregstatus_vbusdet_get`, Abfrage alle `GASBEE_USB_POLL_MS`). Ein GasBee, der dauerhaft an USB hängt, bleibt im Konfigurationsmodus und liefert weiter Werte, nur häufiger.

**Befehle über GATT** (Tare, Kalibrierung) lösen sofort eine Zusatzmessung aus. Der 5-min-Takt bleibt dabei erhalten.

Auch ohne HX711 wird gesendet, das Gewicht ist dann als ungültig markiert. Im Leerlauf geht der nRF52840 automatisch in System ON Idle. System OFF ist nicht möglich, weil der nRF52840 daraus nicht per Timer aufwacht. Gemessen ist bisher nur der Ruhestrom (6,7 µA, siehe Status), der mittlere Strom inklusive Messung und Burst noch nicht.

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
| 6 | uint8 | Flags: Bit 0 = HX711 ok, Bit 1 = lädt (BQ25101 ~CHG), Bit 2 = kalibriert: Offset und Scale stehen in NVS, dieser GasBee ist also per GATT kalibriert. Bei 0 zeigt das Sensorboard „nc". Bit 3 = Konfigurationsmodus (USB angesteckt, Kalibrierung möglich). Bit 4–7 = 0 | – |

Tara, Gas-Netto, Füllstand und Akku-% berechnet das Sensorboard. Die Adresse ist random static und bleibt fest.

### GATT (Kompatibilität)

Für die Messwerte nutzt das Sensorboard GATT nicht. Es verbindet sich nur zur Kalibrierung (0x2B11/0x2B12, `firmware/sensorboard/main/network/gasbee_gatt_cal.c`), und das nur im Konfigurationsmodus an USB. Ansonsten ist der Service für andere Clients gedacht, etwa nRF Connect auf dem Handy, und für den Tare-Befehl.

Das Advertising ist verbindbar. Ein GATT-Client kann sich während eines Bursts verbinden. Solange diese Verbindung steht, gibt es keine Bursts, und auch das Sensorboard bekommt dann keine Werte. Nach dem Trennen sendet der GasBee erst beim nächsten Messzyklus wieder.

- Service: `4a5b6c7d-8e9f-0a1b-2c3d-4e5f60718293`

| UUID | Inhalt | Typ | Zugriff |
|---|---|---|---|
| 0x2B01 | Gewicht kg (nach Tara) | float32 LE | Read, Notify |
| 0x2B02 | Füllstand % | uint8 | Read, Notify |
| 0x2B03 | Gas-Netto kg | float32 LE | Read, Notify |
| 0x2B04 | Akkuspannung mV | uint16 LE | Read, Notify |
| 0x2B10 | Tare | uint8 (`0x01`) | Write |
| 0x2B11 | Kalibrierung Nullpunkt (Waage leer) | uint8 (`0x01`) | Write |
| 0x2B12 | Kalibrierung Referenzgewicht in Gramm | uint32 LE | Write |
| 0x2B13 | Kalibrierwerte: Offset, Scale (Scale = 0 löscht) | int32 LE + float32 LE (8 Byte) | Read, Write |

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
   Der XIAO startet sofort neu und wirft das Laufwerk aus. Der Kopiervorgang kann deshalb wie fehlgeschlagen aussehen, obwohl das Flashen geklappt hat. Erfolgreich war es, wenn der GasBee danach wieder sendet, zum Beispiel am Display oder im BLE-Scan. Nur das Testmodus-Image meldet sich zusätzlich als serieller Port `Zephyr_Project_CDC_ACM`.

## Log-Monitor (USB-CDC, nur Testmodus)

**Das Normalbetrieb-Image hat keinen USB-Stack und kein Log**, damit spart es Strom: Gemessen waren ca. 1,9 mA mit USB-Konsole und 6,7 µA ohne. Zum Mitlesen das **Testmodus-Image** (`debug.conf`) flashen.

Im Testmodus läuft das Log über USB-CDC (VID/PID `2fe3:0004`, Zephyr-Standard). Statt wechselnder `/dev/ttyACMx`-Nummern bekommt jede GasBee über die udev-Regel [`tools/udev/99-esp32-womo.rules`](../../tools/udev/99-esp32-womo.rules) automatisch einen festen Port:

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

Jeder GasBee wird **einzeln** kalibriert. Die Werte liegen pro Gerät in NVS (`gasbee/offset`, `gasbee/scale`) und überstehen Reset und Firmware-Update. Neu kalibrieren geht jederzeit, das überschreibt die alten Werte. Ohne NVS-Werte rechnet der GasBee mit den Startwerten aus `gasbee_config.h` und meldet Bit 2 = 0. Das Sensorboard zeigt dann „nc".

### Ablauf

Für Nutzer läuft die Kalibrierung am **Display** (Einstellungen > Sensoren > „Einrichten“): Das Sensorboard verbindet sich und schreibt die Befehle. Für Entwickler geht es auch mit **nRF Connect** auf dem Handy:

1. GasBee an USB anstecken, ein Netzteil reicht. Er ist nach spätestens 2 s im Konfigurationsmodus, Flag Bit 3.
2. In nRF Connect „GasBee" suchen und verbinden. Die richtige MAC steht im Scan, zum Beispiel E5:DC:C9:7D:C4:27.
3. Waage **leer**: auf **0x2B11** den Wert `01` schreiben. Der GasBee misst sofort und speichert den Nullpunkt.
4. Bekanntes Gewicht auflegen und auf **0x2B12** das Gewicht in Gramm als uint32 LE schreiben. Für 14285 g sind das die Bytes `CD370000`.
5. Zur Kontrolle **0x2B13** lesen: 8 Byte, int32 Offset und float32 Scale.
6. USB abziehen. Der GasBee trennt die Verbindung, kehrt in den Akkubetrieb zurück und meldet „kalibriert". Bei aktivierter Status-LED blitzt Grün nach jedem Kalibrierschritt 3×.

Die Writes prüfen nur das Format: falsche Länge, 0 g oder mehr als 200 kg führen zu einem ATT-Fehler. Ob die Messung geklappt hat, zeigen 0x2B13 und das Log. Gespeichert wird zum Beispiel nicht, wenn der HX711 keinen Wert liefert oder beim Referenzgewicht kaum eine Änderung gegenüber dem Nullpunkt messbar ist (`GASBEE_CAL_MIN_DELTA_RAW`, ca. 40 g). Ein negatives Scale ist erlaubt, falls die Wägezelle andersherum angeschlossen ist.

### Werte direkt setzen, sichern, löschen

Auf **0x2B13** 8 Byte schreiben: int32 Offset und float32 Scale, beide LE. Mit Scale = 0 wird die Kalibrierung gelöscht, dann gelten wieder die Startwerte und Bit 2 ist 0.

| GasBee | MAC | Offset | Scale | 0x2B13 (Hex) | gemessen |
|---|---|---|---|---|---|
| 1 | F7:8A:BE:C9:83:CB | −294304 | 0.0000395471 | `6082FBFF60DF2538` | 2026-10-18: leer −0,64 kg, 14,285 kg → 13,805 kg (mit Startwerten ESP32-C3) |
| 2 | E5:DC:C9:7D:C4:27 | – | – | – | 2026-10-19 über das Display kalibriert. Die Werte stehen nur im NVS, bei Bedarf über 0x2B13 auslesen. |

Die Startwerte in `gasbee_config.h` sind die von GasBee 1. Die Tara (Nullpunkt mit Flaschenhalter) und die Feinkalibrierung übernimmt das Sensorboard.

### Tare

Mit Write `0x01` auf 0x2B10 wird das **nächste gemessene Bruttogewicht** als Tara gespeichert, dauerhaft in NVS. Die Waage muss dafür **leer sein, also ohne Flasche**. Danach gilt: Gewicht = Brutto − Tara, und Gas-Netto = Gewicht − 10,5 kg (Leergewicht der Flasche).

Unterschied zum ESP32-C3-GasBee: Dort setzte Tare die Tara nur auf 0 und machte keinen echten Nullabgleich.

## Status

- [x] Build mit NCS v3.4.1, Board `xiao_ble`
- [x] Erster Test auf Hardware (2026-10-17): UF2-Flash, USB-Log, NVS und Advertising „GasBee" laufen. Ohne HX711 kommt „HX711 nicht bereit … retry".
- [x] HX711 angeschlossen, liefert Rohwerte (2026-10-17)
- [x] Empfang am Sensorboard per GATT, erster Test (2026-10-17). Inzwischen überholt.
- [x] Empfang am Sensorboard per Advertising v1, passiver Scan über `gasbee_ble_scanner` und `gas_source` ins Gas-Topic (2026-10-17)
- [x] Akkubetrieb ohne USB funktioniert (2026-10-17)
- [x] Advertising-Format v1 auf Hardware empfangen (Laptop-Scan), seq im 60-s-Takt (2026-10-17)
- [x] Akkuspannung: Multimeter 4,025 V, GasBee 4,003 V, −0,55 %, keine Korrektur nötig (2026-10-18)
- [x] Ladestatus P0.17: mit USB Flags `03`, ohne USB `01` (2026-10-18)
- [x] Stromspar-Image (UART0 aus, QSPI-Flash DPD, IMU/Mikrofon aus, 100 mA Ladestrom) läuft, Bursts im 5-min-Takt (2026-10-18)
- [x] Ruhestrom im Akkubetrieb (2026-10-19, Metra Hit 28S in der Akkuleitung): **6,7 µA** ohne USB-Stack/Log, ca. 1,9 mA mit USB-CDC-Konsole + Log. Deshalb ist USB/Log nur noch im Testmodus.
- [ ] Mittlerer Strom inkl. Messung und Burst (Spitzen) messen
- [ ] HX711-Timing auf nRF verifizieren: PD_SCK-high < 50 µs, auch bei BLE-Aktivität
- [x] Grob-Kalibrierung GasBee 1 (2026-10-18), Gegenprobe 14,32 statt 14,285 kg (+0,25 %)
- [x] Kalibrierung per GATT (0x2B11–0x2B13, NVS pro Gerät) und Konfigurationsmodus per USB: beide GasBees über das Display kalibriert (2026-10-19)
- [x] Kalibrierung am Display (Sensorboard als GATT-Client), umgesetzt im anderen Arbeitsbereich
- [ ] Feinkalibrierung pro GasBee im Sensorboard (anderer Arbeitsbereich)
