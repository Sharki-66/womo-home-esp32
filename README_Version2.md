# WoMo Home Control - ESP32-S3

🚐 **Professionelles Wohnmobil Steuerungs- und Monitoring-System**

Intelligente Steuerung und Überwachung für dein Wohnmobil mit ESP32-S3, LTE-M/NB-IoT, GPS und 7" AMOLED Display.

---

## 📋 Übersicht

Dieses Projekt kombiniert zwei ESP32-S3 Module für ein leistungsstarkes WoMo-Kontrollsystem:

- **Waveshare ESP32-S3 AMOLED 7"** - Display & User Interface
- **Walter Modem** - LTE-M/NB-IoT, GPS, Sensorenanbindung

### Hauptfunktionen

✅ 7" AMOLED Touch-Display  
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
- **Waveshare ESP32-S3 AMOLED 7"** (Display & Touch)
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
```
Waveshare (Master)          Walter (Sensor Controller)
├─ Display (LVGL)          ├─ LTE-M/NB-IoT
├─ Touch Input             ├─ GPS/GLONASS
├─ User Interface          ├─ I2C Sensor Bus
└─ UART ←──JSON──→         └─ Datensammlung
```

### Komponenten
- **Walter Modem Library** (LTE/GPS)
- **LVGL v8.x** (Display GUI)
- **ESP-IDF Components** (WiFi, UART, I2C)
- **Sensor Drivers** (BME280, INA226, BNO055, etc.)

📖 **[Software-Dokumentation →](docs/software/architecture.md)**

---

## 🚀 Quick Start

### 1. Repository klonen
```bash
git clone https://github.com/Sharki-66/womo-home-esp32.git
cd womo-home-esp32
```

### 2. ESP-IDF Setup
```bash
# ESP-IDF installieren
# https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/

# Waveshare Projekt
cd firmware/waveshare-main
idf.py set-target esp32s3
idf.py menuconfig
idf.py build
idf.py flash monitor

# Walter Projekt
cd ../walter-sensor
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

📖 **[Detaillierte Setup-Anleitung →](docs/software/esp-idf-setup.md)**

---

## 📁 Projekt-Struktur

```
womo-home-esp32/
├── README.md
├── LICENSE
├── .gitignore
│
├── docs/
│   ├── hardware/
│   │   ├── overview.md
│   │   ├── components.md
│   │   ├── wiring.md
│   │   └── power-supply.md
│   │
│   └── software/
│       ├── architecture.md
│       ├── esp-idf-setup.md
│       ├── uart-protocol.md
│       └── i2c-sensors.md
│
├── firmware/
│   ├── waveshare-main/
│   └── walter-sensor/
│
└── hardware/
    └── schematics/
```

---

## 📊 Kosten

```
Hardware: ~243€

✅ Walter Modem                    ~100€
✅ Sensoren                        ~96€
✅ Stromversorgung & Verkabelung   ~29€
✅ Antennen & Kabel                ~18€
```

---

## 🔧 Status

🚧 **Projekt in Entwicklung** 🚧

- [x] Hardware-Planung
- [x] Komponenten-Auswahl
- [x] Repository-Setup
- [ ] Schaltplan & Verkabelung
- [ ] Waveshare Firmware
- [ ] Walter Firmware
- [ ] UART Kommunikation
- [ ] GUI (LVGL)
- [ ] Sensor-Integration
- [ ] LTE/GPS Integration

---

## 📝 Lizenz

MIT License - siehe [LICENSE](LICENSE)

---

## 👤 Autor

**Sharki-66**  
GitHub: [@Sharki-66](https://github.com/Sharki-66)

---

**Happy Camping! 🚐⛺**