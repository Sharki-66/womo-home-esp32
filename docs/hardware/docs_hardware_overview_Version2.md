# Hardware Übersicht

## System-Architektur

```
┌──────────────────────────────────────────────────────────────┐
│                    12V Fahrzeugbatterie                      │
└────────────┬─────────────────────────┬───────────────────────┘
             │                         │
             │                         │
    ┌────────▼────────┐       ┌────────▼────────┐
    │ Bauer DC/DC 5V  │       │ Buck DC/DC 5V   │
    │ 3A USB-C        │       │ 3A              │
    └────────┬────────┘       └────────┬────────┘
             │                         │
             │                         │
    ┌────────▼────────────┐   ┌────────▼────────────┐
    │ Waveshare ESP32-S3  │   │ Walter Modem        │
    │ AMOLED 7"           │   │ ESP32-S3            │
    │                     │   │                     │
    │ - Display (LVGL)    │   │ - LTE-M/NB-IoT     │
    │ - Touch Input       │   │ - GPS/GLONASS      │
    │ - WiFi AP/STA       │   │ - WiFi             │
    │ - User Interface    │   │ - I2C Bus Master   │
    └──────────┬──────────┘   └──────────┬─────────┘
               │                         │
               │ UART (JSON)             │ I2C
               │◄───────────────────────►│
               │                         │
                                         │
                           ┌─────────────▼──────────────┐
                           │     PCA9548A Multiplexer   │
                           │     (8x I2C Channels)      │
                           └┬──┬──┬──┬──┬──┬──┬──┬─────┘
                            │  │  │  │  │  │  │  │
        ┌───────────────────┘  │  │  │  │  │  │  └──────────┐
        │                      │  │  │  │  │  │             │
    ┌───▼───┐            ┌────▼──▼──▼──▼──▼──▼─────┐   ┌───▼────┐
    │BME280 │            │   Weitere Sensoren:      │   │PCF8575 │
    │Innen  │            │   - BME280 (Außen)       │   │GPIO    │
    └───────┘            │   - 2x INA226            │   │Expander│
                         │   - BNO055               │   └────────┘
                         │   - ADS1115              │
                         │   - 2x HX711             │
                         └──────────────────────────┘
```

---

## Zentrale Module

### 1. Waveshare ESP32-S3 AMOLED 7"

**Funktion:** Display & User Interface Controller

**Spezifikationen:**
- **Display:** 7" AMOLED 800×1280 Pixel
- **Touch:** Kapazitiver Touchscreen
- **MCU:** ESP32-S3-WROOM-1 (Octa-core, 240MHz)
- **RAM:** 512KB SRAM + 8MB PSRAM
- **Flash:** 16MB
- **Connectivity:** WiFi 802.11 b/g/n, Bluetooth 5.0
- **Interface:** USB-C (Power + Programming)

**Verwendung:**
```
✅ LVGL GUI (Hauptbildschirm)
✅ Touch-Bedienung
✅ WiFi Access Point / Station
✅ UART Master (Kommunikation mit Walter)
✅ Status-Anzeigen
✅ Sensor-Visualisierung
```

**Stromversorgung:** Bauer Electronics DC/DC 5V 3A via USB-C

---

### 2. DPTechnics Walter Modem

**Funktion:** Sensor Controller & LTE/GPS Gateway

**Spezifikationen:**
- **MCU:** ESP32-S3-WROOM-1
- **Modem:** Sequans Monarch 2 (GM02SP)
- **LTE:** LTE-M, NB-IoT (Cat-M1/NB2)
- **GPS:** Multi-GNSS (GPS, GLONASS, Galileo, BeiDou)
- **SIM:** Nano-SIM Slot
- **I2C:** Hardware I2C Master
- **GPIO:** Mehrere freie Pins

**Verwendung:**
```
✅ LTE-M/NB-IoT Konnektivität
✅ GPS/GLONASS Positionierung
✅ I2C Bus Master (alle Sensoren)
✅ UART Slave (Kommunikation mit Waveshare)
✅ Datensammlung & Preprocessing
✅ Cloud-Upload (MQTT/HTTP)
```

**Stromversorgung:** Buck DC/DC 5V 3A via VIN Pin

---

## Sensoren

### Klima-Sensoren

#### BME280 (2x)
```
Sensor #1: Innenraum
Sensor #2: Außen

Messungen:
- Temperatur: -40°C bis +85°C (±1°C)
- Luftfeuchtigkeit: 0-100% (±3%)
- Luftdruck: 300-1100 hPa (±1 hPa)

Interface: I2C
Adressen: 0x76, 0x77
Spannung: 3.3V (von Walter)
Verbrauch: ~3.6µA (sleep)
```

---

### Strom/Spannungs-Sensoren

#### INA226 (2x) mit 75mV/50A Shunts
```
Sensor #1: Solar-Panel Eingang
Sensor #2: Batterie Ausgang

Messungen:
- Spannung: 0-36V (±0.1%)
- Strom: 0-50A via Shunt (±0.5%)
- Leistung: berechnet (V × I)

Interface: I2C
Adressen: 0x40, 0x41
Spannung: 3.3V
Verbrauch: ~330µA
```

---

### Lage-Sensor

#### BNO055 (9-Achsen IMU)
```
Komponenten:
- 3-Achsen Gyroskop
- 3-Achsen Beschleunigungssensor
- 3-Achsen Magnetometer

Verwendung:
- Fahrzeug-Neigung (Pitch/Roll)
- Ausrichtung (Heading)
- Bewegungserkennung
- Stellplatz-Nivellierung

Interface: I2C
Adresse: 0x28 oder 0x29
Spannung: 3.3V
Verbrauch: ~12mA
```

---

### ADC Erweiterung

#### ADS1115 (16-bit ADC)
```
Eigenschaften:
- 4 Kanäle (Single-Ended oder 2x Differential)
- 16-bit Auflösung
- Programmable Gain Amplifier (PGA)
- Interner Oszillator

Verwendung:
- Analog-Eingänge (z.B. Votronic Tank-Sensoren)
- Spannungsmessung
- Erweiterte Analog-Erfassung

Interface: I2C
Adresse: 0x48
Spannung: 3.3V
```

---

### Füllstands-Sensoren

#### Votronic Tank-Sensoren (2x)
```
Sensor #1: Frischwasser
Sensor #2: Grauwasser

Eigenschaften:
- Widerstandsbasiert
- 0-180 Ohm (leer bis voll)
- Analog-Ausgang

Anschluss:
→ ADS1115 (Analog-Eingang)
→ Walter I2C Bus

Kalibrierung:
- 0Ω = 0% (leer)
- 180Ω = 100% (voll)
```

---

#### HX711 + Wägezellen (2x)
```
Gas-Flaschen Füllstand via Gewicht

Sensor #1: Gas-Flasche 1
Sensor #2: Gas-Flasche 2

HX711:
- 24-bit ADC für Wägezellen
- Gewichtsmessung: 0-50kg
- Kalibrierung auf Leergewicht

Interface: I2C (über HX711-I2C Adapter)
Adressen: 0x20, 0x21
Spannung: 3.3V
```

---

## I2C Infrastructure

### PCA9548A I2C Multiplexer

```
Funktion: 8-Kanal I2C Multiplexer

Warum benötigt?
- Mehrere Sensoren mit gleicher Adresse
- Elektrische Isolation
- Bus-Kapazitäts-Management

Kanäle:
CH0: BME280 #1 (Innen)    - 0x76
CH1: BME280 #2 (Außen)    - 0x77
CH2: INA226 #1 (Solar)    - 0x40
CH3: INA226 #2 (Batterie) - 0x41
CH4: BNO055 (IMU)         - 0x28
CH5: ADS1115 (ADC)        - 0x48
CH6: HX711 #1 (Gas)       - 0x20
CH7: HX711 #2 (Gas)       - 0x21

Interface: I2C
Adresse: 0x70
Spannung: 3.3V (von Walter)
```

---

### PCF8575 GPIO Expander

```
Funktion: 16-Port GPIO Erweiterung

Verwendung:
- Relais-Steuerung (8x)
- Digital-Eingänge (4x)
- LED-Anzeigen (4x)

Beispiel-Zuordnung:
P0-P7:  Relais (Licht, Pumpe, Heizung, etc.)
P8-P11: Digital-Eingänge (Türkontakte, Schalter)
P12-P15: Status-LEDs

Interface: I2C
Adresse: 0x20
Spannung: 3.3V
Verbrauch: ~100µA
```

---

## Stromversorgung

### Schema

```
12V Fahrzeugbatterie (Bordnetz)
    │
    ├─── [Sicherung 5A] ──→ Bauer DC/DC 5V 3A USB-C ──→ Waveshare
    │                                                    (isoliert)
    │
    └─── [Sicherung 5A] ──→ Buck DC/DC 5V 3A ──→ Walter VIN
                                                   │
                                                   └─ 3.3V Ausgang
                                                      │
                                                      └──→ Alle Sensoren
                                                           (PCA9548A, BME280, etc.)
```

### Details

#### Waveshare Versorgung
```
Eingang:  12V Bordnetz
Wandler:  Bauer Electronics DC/DC
Ausgang:  5V 3A via USB-C
Verbrauch: ~1.5A (Display aktiv)
Sicherung: 5A

✅ Isoliertes System
✅ USB-C für einfache Trennung
```

#### Walter + Sensoren Versorgung
```
Eingang:   12V Bordnetz
Wandler:   Buck DC/DC 12V→5V 3A
Ausgang:   5V → Walter VIN
Walter:    Interne 3.3V LDO → alle Sensoren
Verbrauch: ~800mA (LTE aktiv + Sensoren)
Sicherung: 5A

✅ Gemeinsame Masse (I2C!)
✅ Walter liefert 3.3V für Sensoren
```

---

## Antennen & RF

### Multifunktions-Dachantenne

```
Typ: Panorama LGMM-7-60W (oder ähnlich)

Antennen:
- 2x LTE (MIMO für besseren Empfang)
- 1x GPS/GLONASS

Anschlüsse:
- 2x SMA (LTE Main + LTE Aux)
- 1x SMA (GPS)

Montage:
- Dach-Montage
- Erdung zur Fahrzeugmasse
- Kabel-Durchführung wasserdicht

Kabel:
- 2x RG174 5m (LTE → Walter)
- 1x RG174 5m (GPS → Walter)
```

---

## Nächste Schritte

📖 **[Bauteilliste & Einkaufslinks →](components.md)**  
📖 **[Verkabelung & Schaltplan →](wiring.md)**  
📖 **[Stromversorgungs-Details →](power-supply.md)**

---

**Stand:** 2025-01-26