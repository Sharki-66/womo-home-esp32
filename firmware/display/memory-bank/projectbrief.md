# Project Brief — WoMoHome Display

## Ziel
Digitales Cockpit-Display für ein Wohnmobil auf Fiat-Ducato-Basis. Das Display visualisiert alle Sensordaten des Fahrzeugs, zeigt Netzwerk- und Konnektivitätsstatus an und ermöglicht die Steuerung von 12V-Bordnetz, Multimedia-Radio und WLAN/LTE-Router.

## Abgrenzung (Non-goals)
- Sensordatenerfassung: Zuständigkeit des **Sensorboards** (ESP32-S3-WROOM-1, RS485)
- GPS-Server / WLAN-Access-Point: Zuständigkeit des **RUTX11-Routers** (OpenWRT)
- BLE-Gaswaage: Zuständigkeit des **GasBee** (ESP32-C3), Daten kommen über Sensorboard

## Erfolgskriterien (aktuell)
- Stabiler Dauerbetrieb ohne Abstürze oder Memory-Leaks
- Alle RS485-Topics werden korrekt empfangen und dargestellt (bat, tank, gas, imu, bme, ctrl, elec, hx)
- Konnektivitäts-Modal zeigt ESP32-WLAN, Router-WLAN und LTE korrekt an
- 12V-Bordnetz und Radio per Long-Press/Touch steuerbar
- Touch-Bedienung flüssig (keine LVGL-Blockaden)

_Update wenn sich der Scope ändert._
