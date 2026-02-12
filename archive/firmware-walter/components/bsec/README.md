# Bosch BSEC 1.4.9.2 (ESP32)

Enthält die proprietäre BSEC-Binary `lib/libalgobsec.a` und die offiziellen Header aus der BSEC-Arduino-Library (Bosch Sensortec, Lizenzhinweise in den Headern). Die Komponente stellt die BSEC-C-API bereit und linkt das Prebuilt für ESP32.

- Quelle: https://github.com/boschsensortec/BSEC-Arduino-library/tree/master/src/esp32
- Version: 1.4.9.2 (laut Upstream-Header)
- Nutzung: In Komponenten `#include "bsec_interface.h"`/`bsec_datatypes.h` und `${component_lib}` automatisch mit `libalgobsec.a` verlinkt.

Hinweise:
- BSEC ist Closed-Source. Prüfe die Lizenzbedingungen vor Verteilung.
- Die Library erwartet eine BME68x-Treiberintegration (I2C/SPI) und korrekte Zeitsynchronisierung (ns).
