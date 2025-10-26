# Verkabelung & Schaltplan

Detaillierte Verkabelungs-Anleitung für das WoMo Home Control System.

---

## ⚡ Stromversorgung

### Hauptversorgung (12V Bordnetz)

```
12V Fahrzeugbatterie
    │
    ├──[Sicherung 5A]──┬──[+12V Rot]──→ Bauer DC/DC 5V USB-C
    │                  │                     │
    │                  └──[GND Schwarz]──────┴──→ USB-C Output
    │                                              │
    │                                              └─→ Waveshare ESP32-S3
    │
    └──[Sicherung 5A]──┬──[+12V Rot]──→ Buck DC/DC 12V→5V
                       │                     │
                       └──[GND Schwarz]──────┴──→ 5V Output
                                                  │
                                                  └─→ Walter Modem VIN
```

#### Kabel-Querschnitte
```
12V Hauptleitung:  1.5mm² (Rot/Schwarz)
5V zu Walter:      0.75mm² (Rot/Schwarz)
3.3V zu Sensoren:  0.5mm² (Rot/Schwarz)
```

#### Sicherungen
```
Position 1: 5A Blade (Waveshare-Zweig)
Position 2: 5A Blade (Walter-Zweig)

Montage: In der Nähe der Batterie
Zugänglichkeit: Serviceklappe
```

---

## 🔌 UART Verbindung (Waveshare ↔ Walter)

### Pin-Mapping

```
Waveshare ESP32-S3              Walter Modem
┌──────────────┐                ┌──────────────┐
│              │                │              │
│  TX (GPIO17) ├────────────────┤ RX (GPIO16)  │
│              │                │              │
│  RX (GPIO18) ├────────────────┤ TX (GPIO17)  │
│              │                │              │
│  GND         ├────────────────┤ GND          │
│              │                │              │
└──────────────┘                └──────────────┘

Kabel: 3x 0.25mm² Litze
Länge: ~30cm
Farben: Gelb (TX), Grün (RX), Schwarz (GND)
```

### UART Einstellungen
```c
// Beide Seiten identisch
Baudrate:   115200
Data Bits:  8
Stop Bits:  1
Parity:     None
Flow Ctrl:  None
```

---

## 📡 I2C Bus (Walter → PCA9548A → Sensoren)

### Hauptbus (Walter → PCA9548A)

```
Walter Modem                    PCA9548A
┌──────────────┐                ┌──────────────┐
│              │                │              │
│  SDA (GPIO21)├────────────────┤ SDA          │
│              │                │              │
│  SCL (GPIO22)├────────────────┤ SCL          │
│              │                │              │
│  3.3V        ├────────────────┤ VCC          │
│              │                │              │
│  GND         ├────────────────┤ GND          │
│              │                │              │
└──────────────┘                └──────────────┘

Kabel: 4x 0.5mm² Litze
Länge: ~20cm
Farben: Rot (3.3V), Schwarz (GND), Blau (SDA), Gelb (SCL)
```

### Pull-Up Widerstände
```
SDA & SCL: Je 4.7kΩ nach 3.3V

Montage: Direkt am PCA9548A
Position: Zwischen SDA/SCL und VCC
```

---

## 🔀 PCA9548A I2C Multiplexer

### Kanal-Zuordnung & Verkabelung

```
PCA9548A (0x70)
┌────────────────────────────────────┐
│                                    │
│  CH0 ──┬─→ SDA ──→ BME280 #1      │ (Innenraum)
│        └─→ SCL                     │
│                                    │
│  CH1 ──┬─→ SDA ──→ BME280 #2      │ (Außen)
│        └─→ SCL                     │
│                                    │
│  CH2 ──┬─→ SDA ──→ INA226 #1      │ (Solar)
│        └─→ SCL                     │
│                                    │
│  CH3 ──┬─→ SDA ──→ INA226 #2      │ (Batterie)
│        └─→ SCL                     │
│                                    │
│  CH4 ──┬─→ SDA ──→ BNO055         │ (IMU)
│        └─→ SCL                     │
│                                    │
│  CH5 ──┬─→ SDA ──→ ADS1115        │ (ADC)
│        └─→ SCL                     │
│                                    │
│  CH6 ──┬─→ SDA ──→ HX711 #1       │ (Gas 1)
│        └─→ SCL                     │
│                                    │
│  CH7 ──┬─→ SDA ──→ HX711 #2       │ (Gas 2)
│        └─→ SCL                     │
│                                    │
└────────────────────────────────────┘
```

### Standard-Verkabelung pro Sensor

```
Jeder Sensor erhält:
- VCC:  3.3V (Rot)
- GND:  GND (Schwarz)
- SDA:  SDA (Blau)
- SCL:  SCL (Gelb)

Kabel pro Sensor: 4x 0.25mm² Litze
Länge: Je nach Position (20-100cm)
Beschriftung: Sensor-Name + Kanal
```

---

## 🌡️ Sensor-Details

### BME280 #1 (Innenraum)

```
PCA9548A CH0 ──────→ BME280 (0x76)
                     ┌──────────┐
                     │   BME280 │
VCC (3.3V) ──────────┤ VCC      │
GND ─────────────────┤ GND      │
SDA (CH0) ───────────┤ SDA/SDI  │
SCL (CH0) ───────────┤ SCL/SCK  │
                     └──────────┘

Adresse: 0x76 (SDO → GND)
Montage: Innenraum (geschützt)
Kabel: 4x 0.25mm², ca. 50cm
```

### BME280 #2 (Außen)

```
PCA9548A CH1 ──────→ BME280 (0x77)
                     ┌──────────┐
                     │   BME280 │
VCC (3.3V) ──────────┤ VCC      │
GND ─────────────────┤ GND      │
SDA (CH1) ───────────┤ SDA/SDI  │
SCL (CH1) ───────────┤ SCL/SCK  │
                     └──────────┘

Adresse: 0x77 (SDO → VCC)
Montage: Außen (wasserdicht!)
Schutz: IP65 Gehäuse
Kabel: 4x 0.25mm², ca. 100cm
```

---

### INA226 #1 (Solar) mit Shunt

```
PCA9548A CH2 ──────→ INA226 (0x40)
                     ┌──────────┐
                     │  INA226  │
VCC (3.3V) ──────────┤ VCC      │
GND ─────────────────┤ GND      │
SDA (CH2) ───────────┤ SDA      │
SCL (CH2) ───────────┤ SCL      │
                     │          │
Solar (+) ───[Shunt]─┤ VIN+     │
Solar (-) ───────────┤ VIN-     │
                     └──────────┘

Shunt: 75mV/50A
Adresse: 0x40 (A0/A1 → GND)
Montage: Nähe Solar-Regler
Kabel: 1.5mm² für Hauptstrom
       0.25mm² für I2C
```

### INA226 #2 (Batterie) mit Shunt

```
PCA9548A CH3 ──────→ INA226 (0x41)
                     ┌──────────┐
                     │  INA226  │
VCC (3.3V) ──────────┤ VCC      │
GND ─────────────────┤ GND      │
SDA (CH3) ───────────┤ SDA      │
SCL (CH3) ───────────┤ SCL      │
                     │          │
Batt (+) ───[Shunt]──┤ VIN+     │
Batt (-) ────────────┤ VIN-     │
                     └──────────┘

Shunt: 75mV/50A
Adresse: 0x41 (A0 → VCC, A1 → GND)
Montage: Nähe Batterie
```

---

### BNO055 (IMU)

```
PCA9548A CH4 ──────→ BNO055 (0x28)
                     ┌──────────┐
                     │  BNO055  │
VCC (3.3V) ──────────┤ VIN      │
GND ─────────────────┤ GND      │
SDA (CH4) ───────────┤ SDA      │
SCL (CH4) ───────────┤ SCL      │
                     │          │
GND ─────────────────┤ PS0      │ (I2C Mode)
VCC ─────────────────┤ PS1      │
                     └──────────┘

Adresse: 0x28
Montage: Zentral im Fahrzeug
Ausrichtung: X=vorwärts, Y=rechts, Z=oben
Fixierung: Fest verschraubt (Vibration!)
```

---

### ADS1115 (ADC) mit Votronic Tank-Sensoren

```
PCA9548A CH5 ──────→ ADS1115 (0x48)
                     ┌──────────────┐
                     │   ADS1115    │
VCC (3.3V) ──────────┤ VDD          │
GND ─────────────────┤ GND          │
SDA (CH5) ───────────┤ SDA          │
SCL (CH5) ───────────┤ SCL          │
                     │              │
Tank Frisch (+) ─────┤ A0   ┌───────┤ GND
                     │      │       │
Tank Frisch (-) ─────┘  [Ref 3.3V] │
                     │              │
Tank Grau (+) ───────┤ A1   ┌───────┤ GND
                     │      │       │
Tank Grau (-) ───────┘  [Ref 3.3V] │
                     └──────────────┘

Adresse: 0x48 (ADDR → GND)
Votronic Sensoren: 0-180Ω
Spannung: 3.3V Referenz
Kabel zu Tanks: 2x 0.5mm² (abgeschirmt)
```

---

### HX711 + Wägezellen (Gas-Flaschen)

```
PCA9548A CH6 ──────→ HX711 #1 (0x20)
                     ┌──────────────┐
                     │    HX711     │
VCC (3.3V) ──────────┤ VCC          │
GND ─────────────────┤ GND          │
SDA (CH6) ───────────┤ SDA (I2C)    │
SCL (CH6) ───────────┤ SCL (I2C)    │
                     │              │
Wägezelle Red ───────┤ E+           │
Wägezelle Black ─────┤ E-           │
Wägezelle White ─────┤ A+           │
Wägezelle Green ─────┤ A-           │
                     └──────────────┘

CH7: Identisch für HX711 #2 (0x21)

Wägezellen: 50kg (halbe Brücke)
Montage: Unter Gas-Flaschen
Kalibrierung: Mit bekanntem Gewicht
Kabel: 4x 0.25mm² (abgeschirmt)
```

---

## 🎛️ PCF8575 GPIO Expander (Optional)

```
Walter I2C Bus ───→ PCF8575 (0x20)
                    ┌──────────────────┐
                    │     PCF8575      │
VCC (3.3V) ─────────┤ VCC              │
GND ────────────────┤ GND              │
SDA ────────────────┤ SDA              │
SCL ────────────────┤ SCL              │
                    │                  │
                    │  P0-P7  ─────────┤ → Relais-Modul (8 Kanäle)
                    │                  │
                    │  P8-P11 ─────────┤ → Digital-Eingänge
                    │                  │
                    │  P12-P15 ────────┤ → Status-LEDs
                    │                  │
                    └──────────────────┘

Verwendung:
P0: Licht Wohnraum
P1: Licht Außen
P2: Wasserpumpe
P3: Heizung
P4: Lüfter
P5-P7: Reserve

P8-P11: Türkontakte, Schalter
P12-P15: Status-LEDs (Power, LTE, GPS, Alarm)
```

---

## 📡 Antennen-Verkabelung

### Walter Modem → Dachantenne

```
Walter Modem               Panorama LGMM-7-60W
┌──────────────┐           ┌────────────────────┐
│              │           │                    │
│  LTE Main    ├───[5m]────┤ LTE MIMO 1 (Main)  │
│  (SMA)       │  RG174    │                    │
│              │           │                    │
│  LTE Aux     ├───[5m]────┤ LTE MIMO 2 (Aux)   │
│  (U.FL)      │  RG174    │                    │
│              │  +Adapter │                    │
│              │           │                    │
│  GPS         ├───[5m]────┤ GPS/GLONASS        │
│  (U.FL)      │  RG174    │                    │
│              │  +Adapter │                    │
└──────────────┘           └────────────────────┘

Kabel: RG174 (50Ω)
Länge: Je 5m
Adapter: U.FL zu SMA (falls nötig)
Montage: Kabel entlang Dachkante
Schutz: UV-beständige Kabelbinder
```

### Erdung Dachantenne

```
Antenne Masse ──→ Fahrzeugchassis
                   (kürzester Weg!)

Erdungskabel: 2.5mm² Kupfer
Länge: So kurz wie möglich
Befestigung: Direkter Metallkontakt
```

---

## 🔧 Montage-Tipps

### Kabel-Management

```
1. Farbcodierung:
   Rot:     VCC/+12V/+5V/+3.3V
   Schwarz: GND
   Blau:    SDA (I2C Data)
   Gelb:    SCL (I2C Clock)
   Grün:    RX (UART)
   Orange:  TX (UART)

2. Beschriftung:
   Alle Kabel mit Drucker-Etiketten
   Format: "SENSOR_NAME - PIN"
   Beispiel: "BME280_INNEN - SDA"

3. Bündelung:
   Kabelbinder alle 10cm
   Spiralschlauch für Hauptstränge
   Separate Wege für Power/Signal

4. Zugentlastung:
   Keine Kabel unter Spannung
   Schlaufen an Bewegungspunkten
   Fixierung an mehreren Stellen
```

### EMV & Störungen

```
1. Abschirmung:
   Signalkabel (I2C, UART) abschirmen
   Abschirmung nur einseitig erden
   Power-Kabel getrennt führen

2. Ferrite:
   Ferrit-Ringe an langen Kabeln
   Position: Nähe Sensor/Controller
   Mindestens 2 Wicklungen

3. Kondensatoren:
   100nF an jedem Sensor (VCC-GND)
   10µF an Buck-Convertern
   SMD Keramik (kurze Wege!)
```

---

## 📝 Checkliste Verkabelung

### Stromversorgung
- [ ] Sicherungen installiert (2x 5A)
- [ ] 12V Kabel verlegt (1.5mm²)
- [ ] Bauer DC/DC angeschlossen
- [ ] Buck DC/DC angeschlossen
- [ ] Walter VIN verbunden
- [ ] Alle Spannungen gemessen (12V, 5V, 3.3V)

### UART
- [ ] TX/RX gekreuzt verbunden
- [ ] GND verbunden
- [ ] Durchgang geprüft

### I2C Hauptbus
- [ ] SDA/SCL verbunden
- [ ] Pull-Up Widerstände (4.7kΩ)
- [ ] VCC/GND verbunden
- [ ] Bus-Scan durchgeführt (0x70 sichtbar)

### Sensoren (pro Sensor)
- [ ] VCC/GND verbunden
- [ ] SDA/SCL am richtigen PCA-Kanal
- [ ] Adresse konfiguriert
- [ ] 100nF Kondensator gelötet
- [ ] Beschriftung angebracht
- [ ] Funktionstest bestanden

### Antennen
- [ ] LTE Main Kabel verlegt
- [ ] LTE Aux Kabel verlegt
- [ ] GPS Kabel verlegt
- [ ] Alle SMA-Verbindungen fest
- [ ] Antenne geerdet
- [ ] Signal-Test durchgeführt

---

**Stand:** 2025-10-26  
**Version:** 1.0