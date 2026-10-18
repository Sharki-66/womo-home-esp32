# Claude Code – GasBee nRF (XIAO nRF52840)

Ersatz für `firmware/gasbee` (ESP32-C3). Details zu Hardware, Protokoll und Kalibrierung stehen in der [README.md](README.md).

## Toolchain

- nRF Connect SDK **v3.4.1** unter `~/ncs/v3.4.1`, Toolchain-Bundle `8285d8ad56` unter `~/ncs/toolchains/`
- Board: `xiao_ble` (Zephyr upstream, `zephyr/boards/seeed/xiao_ble`)
- Build über die Kommandozeile, siehe README. Den Toolchain-Wrapper `nrfutil-sdk-manager toolchain launch` liefert die VS-Code-Extension mit.
- Flashen per UF2 macht der Nutzer selbst. Claude führt keine Flash-Befehle aus.
- Der Projektpfad darf keine Leerzeichen enthalten, sonst schlägt der Zephyr-Build fehl.

## Verbindliche Schnittstelle (nicht ändern ohne Sensorboard-Anpassung)

**Advertising-Format v1** (Hauptweg, passiver Scan): Beschreibung in der README, Abschnitt „Advertising-Format v1". Manufacturer Data 0xFFFF + 7 Byte: Version, Bruttogewicht int16 in 10 g, Akku uint16 mV, seq, Flags. Ein Burst aus 10 Paketen à 1 s pro Messzyklus.
- Gegenstelle: `firmware/sensorboard/main/network/gasbee_ble_scanner.c`. Sie scannt nur passiv und verbindet sich nie. Erkannt wird über den exakten Namen `GasBee` (6 Byte, im Advertising selbst), Company-ID 0xFFFF und Version 1. Die Zuordnung zu Flasche A/B läuft über die MAC, die im Sensorboard-NVS steht.
- Weiterverarbeitung: `sensors/gas_source.c` ins Gas-Topic, getestet am 2026-10-17.

GATT (nur für andere Clients wie Handy-Apps und für den Tare-Befehl, nicht für das Sensorboard):
- Service-UUID `4a5b6c7d-8e9f-0a1b-2c3d-4e5f60718293`
- Characteristics 0x2B01 (float kg), 0x2B02 (uint8 %), 0x2B03 (float kg), 0x2B04 (uint16 mV Akku, nur GasBee nRF), alle mit Notify. 0x2B10 ist der Tare-Write.
- Eine GATT-Verbindung unterdrückt die Bursts. Solange ein Client verbunden ist, bekommt das Sensorboard keine Werte.

## Entscheidungen

- **HX711-Treiber:** eigener GPIO-Port (`src/hx711.c`) statt des Zephyr-Treibers `hx711_spi`. Der Zephyr-Treiber liefert nur ganze µV (bei 3,3 V und Gain 128 sind das ca. 650 Rohwert-Schritte pro Stufe) und belegt drei SPI-Pins.
- **HX711-Timing:** 10 µs high/low unter `irq_lock()`. Ob die Interrupts des BLE-Controllers trotzdem dazwischenkommen, ist noch nicht auf Hardware geprüft.
- **Tare:** Das nächste Bruttogewicht wird als Tara übernommen und in Settings/NVS gespeichert. Die Übernahme passiert in der Mess-Schleife, nicht im BT-Kontext.
- **Status-LED:** RGB-LED über die Zephyr-GPIO-API (`DT_ALIAS(led0..2)` aus dem Boardfile), nur mit `CONFIG_GASBEE_STATUS_LED` (Projekt-`Kconfig`, eingeschaltet über `debug.conf`). Jede Farbe hat eine eigene Bedeutung: Blau = BLE, Rot = HX711, Grün = Messung/Tare. Das Blinken läuft über `k_timer`.
- **Akkumessung:** Zephyr-Treiber `voltage-divider` (Knoten `vbatt`, AIN7, 1 MΩ/510 kΩ laut Seeed-Schaltplan) **ohne** `power-gpios`. P0.14 wird per `gpio-hog` fest auf Low gesetzt. Mit `power-gpios` würde der Treiber P0.14 beim Init kurz und im PM-Suspend dauerhaft auf High setzen, und genau davor warnt Seeed (P0.31 kann dann über 3,6 V kommen). Gemessen wird im Messzyklus in `main.c`.
- **Messzyklus und Funk:** Intervall `CONFIG_GASBEE_MEASURE_INTERVAL_S` (300 s, `debug.conf` 60 s), feste Taktung ab Zyklusbeginn. Der Burst läuft über `bt_le_ext_adv_start()` mit `num_events = 10` (`CONFIG_BT_EXT_ADV`, Legacy-PDU, verbindbar). Bei bestehender Verbindung gibt es keinen Burst (`CONFIG_BT_MAX_CONN=1`). Der HX711 geht zwischen den Zyklen in den Power-Down, nach dem Aufwachen werden 4 Wandlungen verworfen.
- **Advertising-Gewicht = Brutto ohne Tara.** Tara und Gas rechnet das Sensorboard. Die GasBee-Tara (`gasbee/tare`) wirkt nur auf GATT 0x2B01..0x2B03.
- **Flag Bit 2 „kalibriert"** = Waage für diese Hardware kalibriert, also Offset und Scale aus NVS oder `GASBEE_HX711_CAL_MEASURED = 1` in `gasbee_config.h`. Das Sensorboard zeigt einen GasBee mit Bit 2 = 0 als „nc". Für einen neuen XIAO die Konstante auf 0 setzen, bis gemessen ist.
- **Stromsparen:**
  - `&uart0` ist deaktiviert. Der UARTE-Treiber startet sonst den Empfang beim Init und hält den HF-Takt an (`uart_nrfx_uarte.c`).
  - Der QSPI-Flash geht beim Start per `pm_device_action_run(SUSPEND)` in Deep Power-Down. Das geht nur mit `CONFIG_PM_DEVICE`, siehe `nrf_qspi_nor.c`.
  - P1.08 (IMU) und P1.10 (Mikrofon) sind per `gpio-hog` Low, weil wir für `xiao_ble` statt für die Sense-Variante bauen.
  - Gemessen ist der Strom noch nicht.
- **Ladestrom 100 mA:** P0.13 per `gpio-hog` `output-low` (Schaltplan: R12 2,7 kΩ von ISET nach P0.13 parallel zu R18 2,7 kΩ nach GND). P0.13 nie High setzen. Für Akkus unter 100 mAh den Hog entfernen, dann sind es 50 mA.
- **Ladestatus:** P0.17 (`~CHG`) über `zephyr,user chg-gpios` mit Pull-Up. `&pwm0` ist im Overlay deaktiviert, weil das Boardfile dort PWM auflegt.
- **Persistenz:** Zephyr Settings auf NVS, `storage_partition` @ 0xEC000. Schlüssel: `gasbee/offset`, `gasbee/scale`, `gasbee/tare`.

## Offen

- Grob-Kalibrierung erledigt (2026-10-18, siehe `src/gasbee_config.h`). Gegenprobe mit 14,285 kg steht aus. Feinkalibrierung pro MAC macht das Sensorboard.
- Ruhestrom im Akkubetrieb messen (Stromspar-Image vom 2026-10-18)
- Hardware-Stand: XIAO nRF52840 Sense, Bootloader 0.6.1 / S140 7.3.0. HX711, Akkubetrieb, Advertising v1 und der Empfang am Sensorboard funktionieren (2026-10-17/18).
- Das USB-Log liegt nicht fest auf `/dev/ttyACM0`. Den Port über `ID_SERIAL` zuordnen. Die ESP32-Ports nicht öffnen, weil das Öffnen sie resetten kann.
