# Walter Hardware Notes

## Overview
- ESP32-S3 module running the Walter firmware as dual-mode Wi-Fi router (SoftAP + upstream STA) with IPv4 NAT.
- Intended peripherals: Bosch BNO055 IMU, two Bosch BME680 environmental sensors, HX711 load-cell ADC, and an RS485 interface for fieldbus communication.
- All external sensor signals are user-assignable; defaults align with the current `menuconfig` settings for quick bring-up.

## Power Strategy
- Recommended: USB-Y cable supplying both the ESP32-S3 DevKit and the attached sensor stack from the same 5 V source (≥3 A) to avoid ground potential differences.
- BME680/BNO055/HX711 modules expected to operate at 3V3 logic; ensure their VCC rails share the ESP32 3V3 output.
- Add dedicated 5 V to 3V3 regulators if total draw of sensors exceeds the ESP32 board’s onboard regulator capacity.

## Connectivity Summary
- Single I2C bus shared by BNO055 and both BME680 sensors.
- HX711 uses a two-wire proprietary interface (DOUT/SCK) with optional gain selection via firmware.
- RS485 implemented on a spare UART using DE/RE direction control (active-high transmit enable).
- USB is used strictly for power and flashing; Wi-Fi handles primary data paths.

## Sensor-Task-Architektur
- Jeder Sensor läuft in einer eigenen FreeRTOS-Task: getrennte Tasks für HX711, Analog-Eingänge (10 s Polling, identisch zu HX711/BNO055), je BME680-Instanz (0x77/0x76) sowie den BNO055 (IMU).
- Alle Sensor-Tasks aktualisieren einen gemeinsamen, mutex-geschützten Zustand (`sensor_shared_state_t`). Aufgaben wie RS485 greifen ausschließlich auf diese Snapshots zu – direkte Mehrfachzugriffe auf die Hardware entfallen.
- Der I²C-Bus (Port 0, GPIO8/9, 100 kHz) wird einmalig über den neuen ESP-IDF-Master-Bus (`i2c_new_master_bus`) initialisiert und an jede BME680-/BNO055-Task weitergereicht.
- Analogwerte werden ohne zusätzliche Plausibilitätsfilter übernommen; die Firmware markiert Messungen als gültig, sobald eine ADC-Wandlung erfolgreich war.
- RS485-TX erstellt seine JSON-Nutzdaten anhand dieses Zustands (inklusive Zeitstempel in Mikrosekunden, Fallback-Flags, IAQ-Werten sowie dem neuen `imu`-Objekt mit Heading, Euler-Winkeln, Linearbeschleunigung, Gravitation und Kalibrierstatus).
- Die Aufgabenpriorität orientiert sich an `WALTER_SENSOR_TASK_PRIORITY`; Stackgrößen lassen sich zentral über `WALTER_SENSOR_TASK_STACK` anpassen.

## Default Pin Assignment
| Function | ESP32-S3 GPIO | Notes |
|----------|---------------|-------|
| I2C SDA | GPIO8 | Shared by BNO055 + BME680 sensors. |
| I2C SCL | GPIO9 | 400 kHz by default. Add 4k7 pull-ups if external board lacks them. |
| BME680 #0 Address | 0x77 | Select via solder jumper on sensor board. |
| BME680 #1 Address | 0x76 | Second sensor; change in Kconfig if only one unit present. |
| BNO055 Address | 0x28 | Default MCU-mode address. |
| HX711 DOUT | GPIO4 | Data output (ESD-protected digital input advised). |
| HX711 SCK | GPIO5 | Clock output from ESP32. |
| HX711 Gain Mode | Config option | `WALTER_HX711_GAIN_SETTING` (1=128× A, 2=32× B, 3=64× A). |
| RS485 TX | GPIO17 | UART2 TX by default (can be remapped). |
| RS485 RX | GPIO18 | UART2 RX. |
| RS485 DE/RE | GPIO16 | Drive high during transmit; low keeps transceiver in receive mode. |
| Battery Sense #1 | GPIO6 (ADC1_CH5) | Sensor outputs 0–3 V mapping to 11.0–14.5 V battery voltage. |
| Battery Sense #2 | GPIO7 (ADC1_CH6) | Identical 0–3 V → 11.0–14.5 V mapping. |
| Tank Level #1 | GPIO1 (ADC1_CH0) | 0–3 V range corresponds to 0–100% fill level. |
| Tank Level #2 | GPIO2 (ADC1_CH1) | Same 0–3 V → 0–100% calibration as channel #1. |

> **Note:** All pin references in this table use ESP32-S3 GPIO numbering. On the DevKit headers these map to the physical pin labels (e.g. GPIO8 → header pin 23, GPIO9 → header pin 24). Cross-check with the layout when wiring to avoid mixing GPIO numbers with silk-screen pin indices.

### Spare/Reserved Pins
- Keep strapping pins (GPIO0, GPIO2, GPIO46, etc.) untouched unless absolutely required.
- USB-JTAG pins (GPIO19/GPIO20) remain tied to the on-board debugger; avoid repurposing for external sensors unless JTAG is disconnected.

## Software Configuration Hooks
- `idf.py menuconfig → Walter configuration → Walter sensors` exposes I2C pins, HX711 pins, gain, and polling intervals.
- `WALTER_BME680_SENSOR_COUNT` allows enabling only one sensor if needed.
- RS485 pin assignment currently handled in code (future work: add Kconfig entries if the wiring needs to be flexible).
- NAT, DNS advertisement, and Wi-Fi credentials located in the top-level `Walter configuration` menu.

## Integration Checklist
- Verify 3V3 pull-ups on SDA/SCL; firmware will log warnings if bus handshake fails.
- Calibrate HX711 offset and scale within the application before using weight readings.
- BNO055: Sensor nach Strom-on einmalig kalibrieren (alle vier Statuswerte = 3); der aktuelle Kalibrierstatus wird über RS485 im `imu.cal`-Block ausgegeben.
- Confirm RS485 transceiver direction control matches the board (some modules tie RE low internally).
- Battery/tank sensing: ensure the attached sensors deliver 0–3 V; firmware maps this range to 0–100% (tanks) and 11.0–14.5 V (batteries). Add RC filtering as required to suppress noise.
- After wiring changes, update Kconfig and rebuild with `idf.py reconfigure` followed by `idf.py build flash`.

## Future Enhancements
1. Add Kconfig toggles for RS485 pinout and baud rate.
2. Expose ADC channel selection and calibration parameters in firmware (battery/tank inputs).
3. Provide optional CAN/TWAI mapping if the Walter design enables it later.
4. Extend documentation with photos or wiring diagrams once the physical harness is finalized.
