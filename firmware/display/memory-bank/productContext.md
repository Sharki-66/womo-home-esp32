# Product Context — WoMoHome Display

## Problem
Ein Wohnmobil hat viele verteilte Systeme (Batterien, Tanks, Gas, Heizung, Netzwerk), die bisher über separate analoge Anzeigen oder gar nicht überwacht wurden. Ziel ist ein zentrales, übersichtliches Cockpit das alles auf einen Blick zeigt und Steuerung per Touch ermöglicht.

## Primäre Nutzer
Fahrer und Reisende (Hajo) — keine technischen Vorkenntnisse nötig für die Bedienung.

## UX-Prinzipien
- **Auf einen Blick lesbar**: Kritische Werte (Batterie, Tanks, Gas) immer sichtbar ohne Menü
- **Touch-first**: Alle Aktionen per Touch, keine physischen Knöpfe außer 12V-Long-Press
- **Farbcodierung**: Grün = OK, Gelb = Warnung, Rot = Alarm — konsistent im gesamten System
- **Statusanzeigen passiv, Aktionen explizit**: Sensordaten aktualisieren sich automatisch; Schalten (12V, Radio) erfordert bewusste Touch-Geste (Long-Press für 12V)
- **Konnektivität transparent**: WiFi/LTE-Pill-Button oben links zeigt permanent AP-Status (grün), WiFi- und LTE-Symbol

## Constraints
- **Stromversorgung**: 12V Bordnetz (Blei-Säure oder LiFePO4), kein stabiles 5V-Netzteil
- **Display**: ESP32-S3-Touch-LCD-7, 800×480 px, kapazitiver Touch (GT911)
- **Performance**: LVGL auf Core 1, RS485-Parser auf Core 0, kein Blocking im UI-Thread
- **Speicher**: PSRAM für große Allokationen (Framebuffer, LVGL-Heap, Icons), SRAM knapp
- **Offline-fähig**: Display muss ohne Router funktionieren (Konnektivitätsfehler anzeigen, nicht abstürzen)
