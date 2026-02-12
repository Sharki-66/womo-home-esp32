# WoMo Home Control - ESP32-S3

🚐 **Professionelles Wohnmobil Steuerungs- und Monitoring-System**

Intelligente Steuerung und Überwachung für dein Wohnmobil mit ESP32-S3, LTE-M/NB-IoT, GPS und 7" LCD Touch Display.

---

## 📋 Übersicht

Dieses Projekt kombiniert zwei ESP32-S3 Module für ein leistungsstarkes WoMo-Kontrollsystem:

- **Waveshare ESP32-S3 Touch LCD 7"** - 800×480 IPS Display & User Interface
- **Walter Modem** - LTE-M/NB-IoT, GPS, Sensorenanbindung

### Hauptfunktionen

✅ 7" IPS LCD Touch-Display (800×480)
✅ LTE-M/NB-IoT Konnektivität  
✅ GPS/GLONASS Tracking  
✅ Klima-Monitoring (Innen/Außen)  
✅ Batterie & Solar-Überwachung  
✅ Füllstands-Sensoren (Wasser, Gas)  
✅ 9-Achsen Lagesensor  
✅ WiFi Hotspot & Netzwerk-Management  
✅ Relais-Steuerung (Licht, Pumpe, etc.)

---

## 🛠️ Hardware

### Zentrale Module
- **Waveshare ESP32-S3 Touch LCD 7"** (800×480 IPS Display & Touch)
- **DPTechnics Walter Modem** (ESP32-S3 + Sequans Monarch 2 LTE-M/NB-IoT + GPS)

### Sensoren
- 2x **BME280** (Temperatur, Luftfeuchtigkeit, Luftdruck)
- 2x **INA226** (Strom/Spannungsmessung mit 75mV/50A Shunts)
- 1x **BNO055** (9-Achsen IMU + Magnetometer)
- 1x **ADS1115** (16-bit ADC)
- 2x **Votronic Tank-Sensoren** (Frischwasser/Grauwasser)
- 2x **HX711 + Wägezellen** (Gas-Flaschen Füllstand)

### I2C Infrastructure
- **PCA9548A** I2C Multiplexer (8 Kanäle)
- **PCF8575** GPIO Expander (16 Ports für Relais & Aktoren)

### Stromversorgung
- Bauer Electronics DC/DC 5V 3A USB-C (Waveshare)
- DC-DC Buck 12V→5V 3A (Walter)
- Multifunktions-Dachantenne (2x LTE + GPS)

📖 **[Detaillierte Hardware-Dokumentation →](docs/hardware/overview.md)**

---

## 💻 Software

### Entwicklungsumgebung
- **ESP-IDF v5.x** (Espressif IoT Development Framework)
- **VS Code** mit ESP-IDF Extension
- **C/C++** (native ESP32 Entwicklung)

### Architektur
