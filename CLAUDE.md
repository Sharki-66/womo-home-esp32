# Claude Code — WoMoHome ESP32 Projekt

## Arbeitsweise (verbindlich)

### Optionen vor Implementierung
Bei jeder Entscheidung die eine Auswahl erfordert (Libraries, Frameworks, Architektur, Tools, APIs):
1. **Recherche** — verfügbare Optionen ermitteln
2. **Präsentation** — Optionen mit Vor-/Nachteilen vorlegen, Empfehlung begründen
3. **Warten** — auf explizite Freigabe durch den Nutzer
4. **Erst dann umsetzen**

Das gilt auch wenn eine Option klar besser erscheint. Keine eigenständige Vorauswahl mit nachträglicher Begründung.

### Nur Fakten, kein Raten
- Keine Annahmen über Hardware, Pinbelegung, Protokolle oder Werte — immer aus Code, Schaltplan oder Messung ableiten
- Unbekannte Werte als TODO kennzeichnen mit Erklärung wie der korrekte Wert ermittelt wird
- Kalibrierungswerte und Hardware-Parameter immer aus echten Messungen ableiten, nie schätzen

### Libraries bevorzugen
- Vor jeder Eigenimplementierung nach fertigen, gepflegten Libraries suchen (ESP Component Registry, GitHub)
- Bewertungskriterien: ESP-IDF v5.x Kompatibilität, Aktivität, Lizenz, Abhängigkeiten, Projektpassung
- Eigenimplementierung nur wenn: keine passende Library existiert, Library-Overhead nicht vertretbar, oder Nutzer es explizit wünscht

### Dokumentation mitpflegen
Bei jeder Code-Änderung die betroffenen Dateien mitaktualisieren:
- `CLAUDE.md` im jeweiligen Firmware-Verzeichnis (Protokoll, Konfiguration)
- `README.md` im jeweiligen Firmware-Verzeichnis (Hardware, Struktur, Topics)
- `sensor_config.h` / analoge Konfigurationsdateien

---

## Projektüberblick

**Ziel:** Digitales Cockpit für ein Wohnmobil (Ducato-Basis). Sensordaten erfassen, anzeigen und Bordnetz steuern.

| Teilprojekt | MCU | Verzeichnis | Aufgabe |
|---|---|---|---|
| **Sensorboard** | ESP32-S3-WROOM-1 N16R8 | `firmware/sensorboard/` | Sensoren erfassen, RS485 → Display, Webdashboard |
| **Display** | ESP32-S3-Touch-LCD-7 | `firmware/display/` | LVGL-GUI, Visualisierung, Steuerung |
| **GasBee** | ESP32-C3 | `firmware/gasbee/` | BLE-Gaswaage (Flaschenwägung) |
| **Router** | Teltonika RUTX11 | — | WLAN/LTE/GNSS, OpenWRT |

## Build-System

```bash
source /home/hajo/esp/v5.5.2/esp-idf/export.sh
cd firmware/<sensorboard|display|gasbee>
idf.py build
```

Build, Flash und Monitor werden vom Nutzer über VS Code Tasks gestartet — Claude erklärt nur, führt keine Flash-Befehle aus.

## Kommunikation zwischen Teilprojekten

- **RS485** (57600 8N1, Half-Duplex): Sensorboard → Display, JSON Topic-basiert
- **BLE** (NimBLE): GasBee → Sensorboard (Gewichtsdaten)
- **HTTP/UCI**: Display → RUTX11 Router (WiFi/LTE/GPS)
- **WiFi**: Sensorboard im RUTX11-Netz (NTP, Web-Dashboard)

## Geteilte Konfiguration

`firmware/shared/womo_config.h` — RS485-Baudrate, Topic-Intervalle, WiFi-Credentials.
Änderungen dort wirken sich auf Sensorboard **und** Display aus.

## Hardware

- **Fahrzeug:** Fiat Ducato
- **Stromversorgung:** 12V Bordnetz (Blei-Säure / LiFePO4)
- **Shunt-Messung:** INA226 in der Hauptleitung (I2C 0x40)
- **Netzwerk:** Teltonika RUTX11 (OpenWRT), Dachantenne 2×WLAN + 2×LTE + GNSS

## Sprache & Stil

- Kommunikation: **Deutsch**
- Commit-Messages: **Deutsch**
- Kommentare im Code: **Deutsch**
- Keine Emojis außer auf explizite Anfrage
