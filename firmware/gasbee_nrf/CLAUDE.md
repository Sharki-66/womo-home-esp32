# Claude Code – GasBee nRF (XIAO nRF52840)

Ersatz für `firmware/gasbee` (ESP32-C3). Details zu Hardware, Protokoll und Kalibrierung stehen in der [README.md](README.md).

## Toolchain

- nRF Connect SDK **v3.4.1** unter `~/ncs/v3.4.1`, Toolchain-Bundle `8285d8ad56` unter `~/ncs/toolchains/`
- Board: `xiao_ble` (Zephyr upstream, `zephyr/boards/seeed/xiao_ble`)
- Build über die Kommandozeile, siehe README. Den Toolchain-Wrapper `nrfutil-sdk-manager toolchain launch` liefert die VS-Code-Extension mit.
- Flashen per UF2 macht der Nutzer selbst. Claude führt keine Flash-Befehle aus.
- Der Projektpfad darf keine Leerzeichen enthalten, sonst schlägt der Zephyr-Build fehl.

## Verbindliche Schnittstelle (nicht ändern ohne Sensorboard-Anpassung)

**Advertising-Format v1** (Hauptweg, passiver Scan): Beschreibung in der README, Abschnitt „Advertising-Format v1". Manufacturer Data 0xFFFF + 7 Byte: Version, Bruttogewicht int16 in 10 g, Akku uint16 mV, seq, Flags. Ein Burst aus 10 Paketen à 1 s pro Messzyklus. Das Sensorboard wird in einem anderen Arbeitsbereich angepasst.

GATT (Kompatibilität mit dem heutigen `firmware/sensorboard/main/network/gasbee_ble_client.c`):
- Das Sensorboard verbindet nur, wenn im **Advertising** exakt der Name `GasBee` steht (6 Byte).
- Service-UUID `4a5b6c7d-8e9f-0a1b-2c3d-4e5f60718293`
- Characteristics 0x2B01 (float kg), 0x2B02 (uint8 %), 0x2B03 (float kg), alle mit Notify. 0x2B10 ist der Tare-Write.
- 0x2B04 (uint16 mV, Akku, Notify) ist eine Erweiterung, die nur der GasBee nRF hat. Das Sensorboard muss damit umgehen, dass sie beim ESP32-C3-GasBee fehlt. Sensorboard und Display werden in einem anderen Arbeitsbereich angepasst.

## Entscheidungen

- **HX711-Treiber:** eigener GPIO-Port (`src/hx711.c`) statt des Zephyr-Treibers `hx711_spi`. Der Zephyr-Treiber liefert nur ganze µV (bei 3,3 V und Gain 128 sind das ca. 650 Rohwert-Schritte pro Stufe) und belegt drei SPI-Pins.
- **HX711-Timing:** 10 µs high/low unter `irq_lock()`. Ob die Interrupts des BLE-Controllers trotzdem dazwischenkommen, ist noch nicht auf Hardware geprüft.
- **Tare:** Das nächste Bruttogewicht wird als Tara übernommen und in Settings/NVS gespeichert. Die Übernahme passiert in der Mess-Schleife, nicht im BT-Kontext.
- **Status-LED:** RGB-LED über die Zephyr-GPIO-API (`DT_ALIAS(led0..2)` aus dem Boardfile), nur mit `CONFIG_GASBEE_STATUS_LED` (Projekt-`Kconfig`, eingeschaltet über `debug.conf`). Jede Farbe hat eine eigene Bedeutung: Blau = BLE, Rot = HX711, Grün = Messung/Tare. Das Blinken läuft über `k_timer`.
- **Akkumessung:** Zephyr-Treiber `voltage-divider` (Knoten `vbatt`, AIN7, 1 MΩ/510 kΩ laut Seeed-Schaltplan) **ohne** `power-gpios`. P0.14 wird per `gpio-hog` fest auf Low gesetzt. Mit `power-gpios` würde der Treiber P0.14 beim Init kurz und im PM-Suspend dauerhaft auf High setzen, und genau davor warnt Seeed (P0.31 kann dann über 3,6 V kommen). Gemessen wird im Messzyklus in `main.c`.
- **Messzyklus und Funk:** Intervall `CONFIG_GASBEE_MEASURE_INTERVAL_S` (300 s, `debug.conf` 60 s), feste Taktung ab Zyklusbeginn. Der Burst läuft über `bt_le_ext_adv_start()` mit `num_events = 10` (`CONFIG_BT_EXT_ADV`, Legacy-PDU, verbindbar). Bei bestehender Verbindung gibt es keinen Burst (`CONFIG_BT_MAX_CONN=1`). Der HX711 geht zwischen den Zyklen in den Power-Down, nach dem Aufwachen werden 4 Wandlungen verworfen.
- **Advertising-Gewicht = Brutto ohne Tara.** Tara und Gas rechnet das Sensorboard. Die GasBee-Tara (`gasbee/tare`) wirkt nur auf GATT 0x2B01..0x2B03.
- **Ladestrom 100 mA:** P0.13 per `gpio-hog` `output-low` (Schaltplan: R12 2,7 kΩ von ISET nach P0.13 parallel zu R18 2,7 kΩ nach GND). P0.13 nie High setzen. Für Akkus unter 100 mAh den Hog entfernen, dann sind es 50 mA.
- **Ladestatus:** P0.17 (`~CHG`) über `zephyr,user chg-gpios` mit Pull-Up. `&pwm0` ist im Overlay deaktiviert, weil das Boardfile dort PWM auflegt.
- **Persistenz:** Zephyr Settings auf NVS, `storage_partition` @ 0xEC000. Schlüssel: `gasbee/offset`, `gasbee/scale`, `gasbee/tare`.

## Offen

- Kalibrierung (OFFSET/SCALE) an der echten Hardware messen, siehe TODO in `src/gasbee_config.h`
- Stand 2026-10-17: HX711, Akkubetrieb und Empfang am Sensorboard funktionieren. Allerdings ruft das Sensorboard `gasbee_ble_client_get_snapshot()` nirgends auf. Die GasBee-Werte stehen dort nur im Log, das Gas-Topic nimmt den eigenen HX711 (Kanäle A/B). Das wird in einem anderen Arbeitsbereich geklärt.
- Erster Hardwaretest ist erledigt (2026-10-17, XIAO nRF52840 Sense, Bootloader 0.6.1 / S140 7.3.0): Firmware, NVS und Advertising laufen. Offen sind der Betrieb mit HX711 und die Bestätigung der Sensorboard-Verbindung im Sensorboard-Log.
- Das USB-Log liegt nicht fest auf `/dev/ttyACM0`. Den Port über `ID_SERIAL` zuordnen. Die ESP32-Ports nicht öffnen, weil das Öffnen sie resetten kann.
