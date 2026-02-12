# Hardware Interconnection Matrix

Dieses Dokument beschreibt die physische Verkabelung zwischen den drei ESP32-S3 Boards im Womo-Home-System.

## Board-Übersicht

| Board | Bezeichnung | Hauptfunktion | Stromversorgung |
|-------|-------------|---------------|-----------------|
| Display | WaveShare ESP32-S3 LCD 7" | UI, RS485 Slave | 5V USB-C (2A) |
| Walter | ESP32-S3 DevKit | Sensoren, RS485 Master | 5V USB (3A empfohlen) |
| Modem | WaveShare ESP32-S3-A/SIM7670G | LTE, GPS | 5V (2A min, 3A bei LTE-TX) |

## RS485-Verkabelung (Display ↔ Walter)

### Physische Verbindung

| Signal | Walter GPIO | Walter Funktion | Display GPIO | Display Funktion | Kabel |
|--------|-------------|-----------------|--------------|------------------|-------|
| RS485-A | GPIO17 (TX) via Transceiver | UART2 TX → A | GPIO18 via Transceiver | UART1 RX ← A | Twisted Pair (+) |
| RS485-B | GPIO15 (RX) via Transceiver | UART2 RX ← B | GPIO17 via Transceiver | UART1 TX → B | Twisted Pair (−) |
| DE/RE | GPIO16 | Transmit Enable (HIGH=TX) | GPIO16 | Receive Enable (LOW=RX) | Einzelader |
| GND | GND | Common Ground | GND | Common Ground | GND-Kabel |

### RS485-Transceiver (z.B. MAX485, SN65HVD72)

**Walter-Seite (Master)**:
```
ESP32-S3 GPIO17 (TX) ──▶ DI (Transceiver) ──▶ A ─┐
ESP32-S3 GPIO15 (RX) ◀── RO (Transceiver) ◀── B ─┤ Twisted Pair
ESP32-S3 GPIO16 ───────▶ DE/RE (HIGH beim Senden) │
                                                   │
Walter GND ──────────────────────────────────────┼─── Common GND
```

**Display-Seite (Slave)**:
```
                                                   │
Display GND ─────────────────────────────────────┼─── Common GND
                                                   │
ESP32-S3 GPIO18 ◀── RO (Transceiver) ◀── A ───────┘
ESP32-S3 GPIO17 ──▶ DI (Transceiver) ──▶ B
ESP32-S3 GPIO16 ───▶ DE/RE (meist LOW, nur HIGH bei ACK)
```

### Termination

- **120Ω Abschlusswiderstand** zwischen A und B an **beiden Enden** der Busleitung
- Bei kurzen Kabeln (<5m) oft optional, aber empfohlen für Stabilität
- Alternativ: 120Ω + 10nF Kondensator parallel (Fail-Safe-Bias)

### Kabeltyp

- **Empfohlen**: Cat5e/Cat6 Twisted Pair (ein Paar für A/B, Rest für GND/Reserve)
- **Max. Länge**: Bis 10m problemlos, darüber 120Ω-Termination zwingend
- **Schirmung**: Optional, bei EMV-Problemen Schirm einseitig auf GND

## Stromversorgung

### Display Board
- **Eingang**: USB-C, 5V/2A
- **Verbrauch**: ~1.2A (Display Backlight max), ~0.5A idle
- **Besonderheit**: SD-Karten-Slot braucht stabiles 3.3V (vom Onboard-Regler)

### Walter Board
- **Eingang**: USB Micro, 5V/3A empfohlen
- **Verbrauch**: ~0.8A (Sensoren + WiFi), Peaks bis 1.5A bei WiFi-TX
- **3.3V Rail**: Versorgt BNO055, BME680, HX711
- **Hinweis**: USB-Y-Kabel möglich, wenn gemeinsame 5V-Quelle (≥3A)

### Modem Board
- **Eingang**: USB-C oder Schraubklemme, 5V/2-3A
- **Verbrauch**: 
  - Idle: ~0.3A
  - LTE-Suche: ~1A
  - LTE-TX Peak: bis 2A (kurzzeitig)
- **GPS**: +50mA bei aktiver Positionsbestimmung
- **Wichtig**: SIM7670G braucht min. 2A für LTE-Registrierung

### Common Ground

⚠️ **WICHTIG**: Alle drei Boards müssen **gemeinsame Masse (GND)** haben:
- RS485 GND zwischen Display und Walter
- Bei separaten Netzteilen: GND-Verbindung zwischen allen Boards
- Sonst: Potentialunterschiede → RS485-Fehler oder Schäden

## Modem-Anbindung (zukünftig)

### Option A: RS485 an Walter (erweitert)
- Walter wird Multi-Drop RS485-Master
- Modem als zweiter Slave (Adresse 2)
- **Pro**: Einheitliches Protokoll, Display sieht auch Modem-Daten
- **Contra**: Walter-Firmware-Anpassung nötig

### Option B: UART direkt Walter ↔ Modem
- UART3 an Walter (GPIO 43/44 frei?)
- Einfaches AT-Protokoll oder JSON
- **Pro**: Weniger RS485-Last, direkter Zugriff
- **Contra**: Display muss über Walter gehen

### Option C: WiFi
- Modem baut AP auf oder verbindet sich mit Walter-AP
- HTTP-API oder MQTT
- **Pro**: Flexibel, keine Kabel nötig
- **Contra**: WiFi-Overhead, Latenz

**Empfehlung**: **Option B** für Stabilität, Option A wenn Display LTE-Status direkt braucht.

## Pin-Zuordnung Zusammenfassung

### Display (ESP32-S3 LCD 7")
| GPIO | Funktion | Richtung | Verbindung zu |
|------|----------|----------|---------------|
| 18 | UART1 RX | Input | Walter GPIO17 (via RS485-A) |
| 17 | UART1 TX | Output | Walter GPIO15 (via RS485-B) |
| 16 | RS485 DE/RE | Output | Transceiver Direction Control |
| 8 | I2C SDA | Bidir | GT911 Touch |
| 9 | I2C SCL | Output | GT911 Touch |
| 10 | SD CMD | Bidir | SD-Karte |
| 11-16 | RGB LCD | Output | ST7701 Display |

### Walter (ESP32-S3 DevKit)
| GPIO | Funktion | Richtung | Verbindung zu |
|------|----------|----------|---------------|
| 17 | UART2 TX | Output | Display GPIO18 (via RS485-A) |
| 15 | UART2 RX | Input | Display GPIO17 (via RS485-B) |
| 16 | RS485 DE/RE | Output | Transceiver Direction Control |
| 8 | I2C SDA | Bidir | BNO055, 2× BME680 |
| 9 | I2C SCL | Output | Sensoren-Bus |
| 4 | HX711 DOUT | Input | Waage Daten |
| 5 | HX711 SCK | Output | Waage Clock |
| 6 | ADC Battery 1 | Input | Batterie-Spannungsteiler |
| 7 | ADC Battery 2 | Input | Batterie-Spannungsteiler |
| 1 | ADC Tank 1 | Input | Frischwasser-Sensor |
| 2 | ADC Tank 2 | Input | Grauwasser-Sensor |

### Modem (ESP32-S3-A + SIM7670G)
| GPIO | Funktion | Richtung | Verbindung zu |
|------|----------|----------|---------------|
| TBD | UART Modem TX | Output | SIM7670G RX |
| TBD | UART Modem RX | Input | SIM7670G TX |
| TBD | Modem Power | Output | SIM7670G PWRKEY |
| TBD | Modem Status | Input | SIM7670G STATUS |
| TBD | UART Walter TX | Output | Optional: Walter RX |
| TBD | UART Walter RX | Input | Optional: Walter TX |

> **TODO**: WaveShare ESP32-S3-A Pinout recherchieren und hier eintragen

## Mechanischer Aufbau

```
┌─────────────────────────────────────┐
│          Display (Front)            │  ← Sichtbar für Nutzer
│    7" Touchscreen + UI              │
└───────────┬─────────────────────────┘
            │ USB-C Power + RS485
            ▼
┌───────────────────────────────────────┐
│          Walter (Zentral)             │  ← Sensor-Hub
│  IMU, BME680, HX711, ADCs             │
│  RS485 Master, WiFi-Router            │
└───────────┬───────────────────────────┘
            │ UART oder RS485 (optional)
            ▼
┌───────────────────────────────────────┐
│          Modem (Hinten)               │  ← LTE-Antenne nach außen
│  SIM7670G, GPS-Antenne                │
└───────────────────────────────────────┘
```

## Kabellängen & EMV

| Verbindung | Typ | Max. Länge | Schirmung |
|------------|-----|------------|-----------|
| Display ↔ Walter RS485 | Twisted Pair | 10m | Optional |
| Walter ↔ Sensoren I2C | Flachband | 30cm | Nicht nötig |
| Walter ↔ HX711 | 2-adrig | 1m | Nicht nötig |
| Modem ↔ Walter UART | 3-adrig (TX/RX/GND) | 2m | Empfohlen bei >1m |
| LTE-Antenne | Koax (SMA) | 3m | Koax-Kabel |
| GPS-Antenne | Koax (SMA) | 3m | Koax-Kabel |

## Test-Checkliste

- [ ] Durchgangsprüfung: GND zwischen allen Boards
- [ ] RS485: Twisted Pair korrekt (A↔A, B↔B, nicht gekreuzt)
- [ ] RS485: 120Ω-Termination an beiden Enden
- [ ] Stromversorgung: Jedes Board einzeln testen
- [ ] Walter sendet JSON an Display (Monitor auf UART1)
- [ ] Display empfängt und parst Daten (LVGL aktualisiert)
- [ ] Modem registriert sich im LTE-Netz
- [ ] GPS-Fix erfolgreich (TTF <60s im Freien)

## Troubleshooting

### RS485-Fehler
- **Symptom**: Display zeigt "RS485 Timeout"
- **Check**: 
  1. Kabel richtig angeschlossen? (A/B nicht vertauscht)
  2. GND zwischen Boards vorhanden?
  3. Termination 120Ω vorhanden?
  4. Transceiver-Power (3.3V) ok?

### LTE verbindet nicht
- **Check**:
  1. SIM-Karte eingelegt und entsperrt?
  2. Antenne angeschlossen?
  3. Stromversorgung min. 2A?
  4. Netzabdeckung vorhanden? (Outdoor-Test)

### GPS kein Fix
- **Check**:
  1. GPS-Antenne angeschlossen?
  2. Im Freien oder am Fenster?
  3. Cold Start braucht 30-120s

## Updates

| Datum | Änderung | Autor |
|-------|----------|-------|
| 2026-01-08 | Initial | System |
| TBD | Modem-Pinout ergänzen | - |
| TBD | Fotos vom Aufbau | - |

---

**Hinweis**: Dieses Dokument ist Work-in-Progress. Bei Änderungen am Verkabelungsplan hier aktualisieren!
