# Projektbrief – RuuviTag Gas (WoMoHome)

## Zweck
Hardware-Fork des **RuuviTag Rev.B8** (nRF52832) zu einem BLE-Beacon, der zusätzlich
über einen **HX711** eine Gasflaschen-Waage ausliest und das Gewicht im
BLE-Advertisement mitsendet.

## Herkunft & Lizenz
Basis: `ruuvi/ruuvitag_hw` @ `afa1b03`, Verzeichnis `ruuvitag_revb8`.
Lizenz CC BY-SA 4.0 (geerbt).

## Fixierte Entscheidungen
- Außenmaße = Original Rev.B8, 4-lagig. Gehäuse wird unter die Wiegeplatte geklebt.
- Sensoren: nur **SHTC3** behalten; TMP117 / DPS310 / LIS2DH12 entfernen.
- **HX711** aus dem `GasBee`-Projekt übernehmen (`Analog_ADC:HX711`), Speisung aus `VDD`,
  interner Regler, ratiometrische Brückenspeisung, Power-Down über `PD_SCK` (kein Load-Switch).
- Wägezelle: Vollbrücke aus 4× 50 kg, **extern vorkombiniert** → 4 SMD-Lötpads E+/E-/A+/A-.
- Speisung: CR2477-Clip bleibt + 2 DNP-Pads (VBAT/GND) für optionalen externen Pack.
- KiCad-Workflow: Rev.B8 migrieren, dann erweitern (nicht neu zeichnen).
- Firmware: eigene nRF52-FW (Nordic SDK/Zephyr), **nicht** ESP-IDF – separates Paket.

## Offen
- Wäge-/Advertising-Intervall (Firmware).
- Genaue P0.x-Zuordnung für HX711 (aus migriertem Schaltplan ablesen).
- Freie Fläche/Routing für HX711 nach Sensor-Rückbau – in KiCad 9 prüfen.
- Auflösung: 4× 50 kg = 200 kg Vollausschlag ist für eine Gasflasche grob.
