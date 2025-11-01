# Community Open Hardware Plattformen

## Übersicht

Diese Dokumentation listet offene und freie Hardware-Plattformen auf, die von der Community für ESP32-basierte Wohnmobil-Steuerungssysteme und IoT-Projekte verwendet werden.

---

## ESP32-S3 Basierte Plattformen

### 1. Waveshare ESP32-S3 Touch LCD Displays

**Verfügbare Modelle:**
- **ESP32-S3 Touch LCD 4.3"** (480×272 IPS)
- **ESP32-S3 Touch LCD 5"** (800×480 IPS)
- **ESP32-S3 Touch LCD 7"** (800×480 IPS)
- **ESP32-S3 Touch AMOLED 7"** (800×1280 AMOLED)

**Spezifikationen:**
- MCU: ESP32-S3-WROOM-1/1U
- RAM: 512KB SRAM + 8MB PSRAM
- Flash: 16MB
- Display: IPS LCD oder AMOLED mit kapazitivem Touch
- Connectivity: WiFi 802.11 b/g/n, Bluetooth 5.0
- USB-C für Programmierung und Stromversorgung

**Community Nutzung:**
- ✅ Home Automation Dashboards
- ✅ Wohnmobil Kontrollsysteme
- ✅ IoT Bedienoberflächen
- ✅ Industrielle HMI Panels

**Vorteile:**
- Integriertes Display und Touch
- Leistungsstarker ESP32-S3 mit PSRAM
- LVGL GUI Framework Unterstützung
- Open Source Beispiele und Dokumentation

**Quellen:**
- Website: https://www.waveshare.com
- GitHub: https://github.com/waveshare
- Dokumentation: https://www.waveshare.com/wiki/

---

### 2. LilyGO T-Display Serie

**Verfügbare Modelle:**
- **T-Display S3** (ESP32-S3 + 1.9" LCD)
- **T-Display S3 AMOLED** (ESP32-S3 + 1.91" AMOLED)
- **T-Display S3 Pro** (ESP32-S3 + 2.33" IPS)

**Spezifikationen:**
- MCU: ESP32-S3FN16R8 (16MB Flash, 8MB PSRAM)
- Display: 1.9" - 2.33" IPS/AMOLED
- Batterie: Integrierter LiPo Lade-Controller
- USB-C: Programmierung und Laden

**Community Nutzung:**
- ✅ Portable IoT Geräte
- ✅ Batteriebetriebene Sensoren mit Display
- ✅ Wearable Displays
- ✅ Smart Home Bedienelemente

**Vorteile:**
- Kompakte Bauform
- Integrierte Batterie-Verwaltung
- Aktive Open Source Community
- Günstig und gut verfügbar

**Quellen:**
- GitHub: https://github.com/Xinyuan-LilyGO
- Shop: http://www.lilygo.cn

---

### 3. M5Stack Serie

**Verfügbare Modelle:**
- **M5Stack CoreS3** (ESP32-S3 + 2" LCD + Sensoren)
- **M5Stack Core2** (ESP32 + 2" LCD + Touch)
- **M5Stack Atom S3** (ESP32-S3 kompakt)

**Spezifikationen:**
- MCU: ESP32 oder ESP32-S3
- Display: 2" IPS LCD (320×240) mit Touch
- Sensoren: IMU, Mikrofon, RTC
- Erweiterbar: I2C/UART/GPIO Module
- Gehäuse: Robustes Kunststoffgehäuse

**Community Nutzung:**
- ✅ Modulare IoT Prototypen
- ✅ Sensor-Stationen
- ✅ Robotik Steuerungen
- ✅ Bildungs-Projekte

**Vorteile:**
- Komplettes System im Gehäuse
- Große Auswahl an Erweiterungsmodulen
- Sehr aktive Community
- UIFlow (visuelles Programming)

**Quellen:**
- Website: https://m5stack.com
- GitHub: https://github.com/m5stack
- Dokumentation: https://docs.m5stack.com

---

## ESP32 Classic Plattformen

### 4. ESP32 DevKit C

**Spezifikationen:**
- MCU: ESP32-WROOM-32
- RAM: 520KB SRAM
- Flash: 4MB
- GPIO: 30 Pins verfügbar
- USB-to-Serial: CP2102 oder CH340

**Community Nutzung:**
- ✅ General Purpose ESP32 Entwicklung
- ✅ Sensor-Netzwerke
- ✅ WiFi/Bluetooth Gateways
- ✅ Prototyping

**Vorteile:**
- Standard ESP32 Entwicklungsboard
- Günstig und weit verbreitet
- Viele GPIO Pins
- Breadboard-kompatibel

**Quellen:**
- Espressif: https://www.espressif.com
- Zahlreiche Clone-Hersteller

---

### 5. Adafruit ESP32 Feather

**Spezifikationen:**
- MCU: ESP32-PICO-D4
- RAM: 520KB SRAM
- Flash: 4MB
- LiPo-Charger: Integriert
- Feather Format: Kompatibel mit Feather Wings

**Community Nutzung:**
- ✅ Batteriebetriebene IoT Geräte
- ✅ Wearables
- ✅ Sensor-Logger
- ✅ Low-Power Anwendungen

**Vorteile:**
- Feather Ecosystem kompatibel
- Integriertes Batterie-Management
- Hochwertige Fertigung
- Ausführliche Dokumentation

**Quellen:**
- Website: https://www.adafruit.com
- GitHub: https://github.com/adafruit
- Learn: https://learn.adafruit.com

---

## LTE/GPS Kombinationen

### 6. DPTechnics Walter Modem

**Spezifikationen:**
- MCU: ESP32-S3-WROOM-1
- Modem: Sequans Monarch 2 (GM02SP)
- LTE: LTE-M, NB-IoT (Cat-M1/NB2)
- GPS: Multi-GNSS (GPS, GLONASS, Galileo, BeiDou)
- SIM: Nano-SIM Slot

**Community Nutzung:**
- ✅ Remote Monitoring Systeme
- ✅ Asset Tracking
- ✅ Wohnmobil Connectivity
- ✅ Off-Grid IoT

**Vorteile:**
- Integriertes LTE-M/NB-IoT Modem
- GPS/GNSS auf dem Board
- Niedriger Stromverbrauch
- Ideal für mobile Anwendungen

**Quellen:**
- Website: https://dptechnics.com
- Dokumentation: https://docs.dptechnics.com

---

### 7. LilyGO T-SIM7000G

**Spezifikationen:**
- MCU: ESP32-WROVER-B
- Modem: SIM7000G (GSM/GPRS/LTE)
- GPS: Integriert
- RAM: 8MB PSRAM
- Antenne: Externe GPS und GSM Antennen

**Community Nutzung:**
- ✅ GPS Tracker
- ✅ Remote Sensoren mit GSM
- ✅ Notfall-Kommunikation
- ✅ Fahrzeug-Telemetrie

**Vorteile:**
- Günstige LTE/GPS Kombination
- Open Source Hardware Design
- Aktive Community
- Weltweite GSM Abdeckung

**Quellen:**
- GitHub: https://github.com/Xinyuan-LilyGO/LilyGO-T-SIM7000G

---

## Sensorik & I/O Erweiterungen

### 8. Adafruit Sensoren & Breakouts

**Beliebte Module:**
- BME280/BME680 (Klima-Sensoren)
- INA219/INA226 (Strom/Spannungsmessung)
- BNO055 (9-Achsen IMU)
- ADS1115 (16-bit ADC)
- PCA9685 (16-Kanal PWM)

**Vorteile:**
- Hochwertige Fertigungsqualität
- STEMMA QT (Qwiic kompatibel) I2C Stecker
- Ausführliche Dokumentation und Libraries
- Open Source Hardware Designs

**Quellen:**
- Website: https://www.adafruit.com
- GitHub: https://github.com/adafruit

---

### 9. SparkFun Qwiic System

**Beliebte Module:**
- Qwiic Mux (TCA9548A I2C Multiplexer)
- Qwiic GPIO (SX1509 I/O Expander)
- Qwiic Environmental Combo (BME280 + CCS811)
- Qwiic Scale (NAU7802 für Wägezellen)

**Vorteile:**
- Werkzeuglose I2C Verkettung
- Große Auswahl an Sensoren
- Open Source Hardware
- Kompatibel mit STEMMA QT

**Quellen:**
- Website: https://www.sparkfun.com/qwiic
- GitHub: https://github.com/sparkfun

---

## Display-Systeme

### 10. Nextion HMI Displays

**Verfügbare Größen:**
- 2.4" bis 7" und größer
- Resistiver oder kapazitiver Touch
- UART Interface

**Community Nutzung:**
- ✅ Home Automation Panels
- ✅ Maschinen-Bedienfelder
- ✅ Wohnmobil Kontroll-Displays
- ✅ Industrielle Anwendungen

**Vorteile:**
- Einfache UART Kommunikation
- Grafischer Editor für GUI Design
- Keine LVGL oder Display-Treiber nötig
- Geringe Last auf ESP32

**Nachteile:**
- Proprietäre Software (Editor)
- Nicht vollständig Open Source

**Quellen:**
- Website: https://nextion.tech

---

### 11. SquareLine Studio + Generic Displays

**Unterstützte Displays:**
- ILI9341 (2.8" - 3.2" TFT)
- ST7789 (1.3" - 2.4" TFT)
- ST7796 (3.5" - 4.0" TFT)

**Community Nutzung:**
- ✅ Custom LVGL GUI Designs
- ✅ Kostengünstige TFT Displays
- ✅ Prototyping
- ✅ DIY Projekte

**Vorteile:**
- LVGL-basierte Open Source GUI
- SquareLine Studio für GUI Design
- Günstige Standard-Displays
- Volle Kontrolle über Software

**Quellen:**
- LVGL: https://lvgl.io
- SquareLine: https://squareline.io

---

## Power Management

### 12. Pololu Spannungsregler

**Beliebte Module:**
- Pololu 5V, 3A Step-Down (D24V22F5)
- Pololu 3.3V, 3A Step-Down (D24V22F3)
- Pololu Dual High-Power Motor Driver

**Vorteile:**
- Effiziente DC-DC Wandler
- Kompakte Bauform
- Hohe Qualität
- Open Hardware Designs verfügbar

**Quellen:**
- Website: https://www.pololu.com

---

### 13. Bauer Electronics Automotive DC/DC

**Spezifikationen:**
- Eingangsspannung: 6-32V (Automotive)
- Ausgangsspannung: 5V, 12V, 24V
- Leistung: bis 3A
- Schutzfunktionen: Überspannung, Überstrom, Kurzschluss

**Community Nutzung:**
- ✅ Wohnmobil-Elektronik
- ✅ Automotive Applications
- ✅ Mobile Power Supplies
- ✅ Solar-Systeme

**Vorteile:**
- Automotive-Grade Qualität
- Weiter Eingangsspannungsbereich
- Robuste Bauweise
- Made in Germany

---

## Relay & Aktor Module

### 14. PCF8575 GPIO Expander Boards

**Spezifikationen:**
- 16 GPIO Pins via I2C
- 5V Tolerant
- Adressierbar (bis zu 8 Geräte)

**Community Nutzung:**
- ✅ Relais-Steuerung (8-16 Relais)
- ✅ LED Matrix Steuerung
- ✅ Digital-Eingänge
- ✅ Aktor-Ansteuerung

**Vorteile:**
- Einfache I2C Integration
- Viele GPIOs ohne zusätzliche ESP32 Pins
- Günstig verfügbar
- Gute Library-Unterstützung

---

## Empfehlungen für Wohnmobil-Projekte

### Minimal Setup (Budget)
```
✅ ESP32 DevKit C (~5€)
✅ Nextion 3.5" Display (~25€)
✅ BME280 Sensoren (~3€/Stück)
✅ Relais-Module (~5€)
✅ DC-DC Step-Down (~3€)

Gesamt: ~50€
```

### Standard Setup (Empfohlen)
```
✅ Waveshare ESP32-S3 Touch LCD 7" (~70€)
✅ BME280, INA226 Sensoren (~20€)
✅ PCA9548A I2C Multiplexer (~3€)
✅ PCF8575 GPIO Expander (~3€)
✅ Bauer DC/DC 5V 3A (~20€)

Gesamt: ~120€
```

### Professional Setup (Dieses Projekt)
```
✅ Waveshare ESP32-S3 Touch LCD 7" (~70€)
✅ Walter Modem (LTE-M + GPS) (~120€)
✅ Komplette Sensorik (~80€)
✅ I2C Infrastructure (~15€)
✅ Power Management (~30€)

Gesamt: ~315€
```

---

## Community Ressourcen

### Foren & Diskussionen
- **ESP32 Forum:** https://esp32.com
- **Reddit r/esp32:** https://reddit.com/r/esp32
- **Arduino Forum:** https://forum.arduino.cc

### Deutsche Communities
- **Mikrocontroller.net:** https://www.mikrocontroller.net
- **Promobil Forum:** https://forum.promobil.de
- **Wohnmobil-Forum.de:** https://www.wohnmobil-forum.de

### GitHub Repositories
- **Awesome ESP32:** https://github.com/agucova/awesome-esp32
- **ESP32 Projects:** https://github.com/topics/esp32
- **LVGL Projects:** https://github.com/lvgl

### YouTube Kanäle
- **Andreas Spiess** (Swiss Accent)
- **GreatScott!** (Deutsch/English)
- **DroneBot Workshop**
- **Electronoobs**

---

## Lizenz & Open Hardware

### OSHWA Zertifizierung
Viele der aufgelisteten Plattformen sind nach **Open Source Hardware Association (OSHWA)** Standards entwickelt:
- ✅ Schaltpläne verfügbar
- ✅ PCB Layouts verfügbar
- ✅ Bill of Materials (BOM) verfügbar
- ✅ Software Open Source

### Lizenzen
Typische Lizenzen für Open Hardware:
- **CERN OHL** (CERN Open Hardware License)
- **TAPR OHL** (TAPR Open Hardware License)
- **MIT License** (Software & Hardware)
- **Apache 2.0** (Software)
- **CC BY-SA** (Creative Commons)

---

## Auswahlkriterien

Bei der Auswahl einer Hardware-Plattform sollten folgende Kriterien beachtet werden:

### Technische Kriterien
✅ **Performance:** CPU-Leistung, RAM, Flash  
✅ **Connectivity:** WiFi, Bluetooth, LTE, GPS  
✅ **I/O:** Anzahl GPIO Pins, I2C, SPI, UART  
✅ **Display:** Größe, Auflösung, Touch  
✅ **Power:** Stromverbrauch, Batterie-Management  
✅ **Sensoren:** Integrierte Sensoren

### Community Kriterien
✅ **Dokumentation:** Qualität und Vollständigkeit  
✅ **Code-Beispiele:** Verfügbarkeit und Qualität  
✅ **Forum-Support:** Aktive Community  
✅ **Library-Support:** Arduino, ESP-IDF, PlatformIO  
✅ **Update-Frequenz:** Regelmäßige Firmware-Updates

### Praktische Kriterien
✅ **Verfügbarkeit:** Lieferbarkeit und Lieferzeit  
✅ **Preis:** Kosten-Nutzen-Verhältnis  
✅ **Hersteller:** Zuverlässigkeit und Support  
✅ **Formfaktor:** Größe und Gehäuse-Optionen  
✅ **Erweiterbarkeit:** Module und Add-ons

---

**Stand:** 2024-11-01  
**Version:** 1.0  
**Autor:** WoMo Home ESP32 Projekt
