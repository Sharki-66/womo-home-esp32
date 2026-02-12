# WoMo Home Control – ESP32-S3

🚐 **Wohnmobil Steuerungs- und Monitoring-System**

Intelligente Steuerung und Überwachung für ein Fiat Ducato Wohnmobil mit ESP32-S3, RS485-Bus, 7" Touch-Display und Teltonika RUTX11 Router (LTE/WLAN/GNSS).

---

## 📋 Überblick

| Modul | Board | Funktion |
|-------|-------|----------|
| **Display** | Waveshare ESP32-S3 Touch LCD 7" (800×480) | GUI, LVGL v8, Router-Anbindung (HTTP/SSH) |
| **Sensorboard** | Heemol ESP32-S3 N16R8 DevKitC-1 | Sensorik, Aktorik, RS485-TX |
| **Router** | Teltonika RUTX11 | WLAN, LTE, GNSS, Hotspot |

Kommunikation Display ↔ Sensorboard: **RS485 Half-Duplex** (115200 8N1, JSON-Lines, Topic-basiert).

### Sensoren & Aktoren

- 2× BME680 (Temperatur, Feuchte, Druck, Gas – innen/außen)
- BNO055 (9-Achsen IMU + Kompass)
- 2× HX711 + Wägezellen (Gasfüllstand in kg)
- 2× Votronic Tanksensoren (Frisch-/Grauwasser, kapazitiv)
- 2× Batterie-Messung (Board/Kfz)
- Relais-Steuerung (12V Bordnetz, Radio)

---

## 📁 Projektstruktur

```
womo-home-esp32/
├── firmware/
│   ├── display/          ← Waveshare 7" LCD Firmware (LVGL, Router-Poll)
│   └── sensorboard/      ← Sensorboard Firmware (RS485-TX, Sensoren)
├── hardware/
│   ├── schematics/        ← KiCad-Schaltpläne (HAT-Board)
│   └── datasheets/        ← PDFs, Datenblätter, Schaltpläne
├── docs/                  ← Dokumentation mit Querverweisen
│   ├── README.md          ← Doku-Index
│   ├── hardware/          ← Hardware-Beschreibungen (.md)
│   └── software-architecture.md
├── sdcard/                ← SD-Karten-Inhalt fürs Display
│   ├── images/            ← Ducato-Bilder, Wetter-Icons
│   └── config/            ← Konfigurationsdateien
├── tests/                 ← Hardware-Test-Sketche (I2C, SPI, LVGL, …)
├── archive/               ← Abgelöste Firmware-Versionen
│   ├── firmware-modem/    ← USB-Modem-Version (→ RS485 3.3V Käfer)
│   └── firmware-walter/   ← DPTechnics Walter v1.0 (abgelöst)
├── .github/
│   └── copilot-instructions.md  ← KI-Regeln, RS485-Protokoll v2
├── womo-sensor.code-workspace   ← VS Code Workspace (Sensorboard)
└── womo-display.code-workspace  ← VS Code Workspace (Display)
```

---

## 🚀 Schnellstart

### Voraussetzungen
- [ESP-IDF v5.5.x](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
- VS Code + [ESP-IDF Extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension)

### Sensorboard bauen & flashen
```bash
cd firmware/sensorboard
idf.py set-target esp32s3
idf.py build flash monitor -p /dev/ttyACM2
```

### Display bauen & flashen
```bash
cd firmware/display
idf.py set-target esp32s3
idf.py build flash monitor -p COM5
```

Oder: entsprechenden `.code-workspace` öffnen → Tasks nutzen (Ctrl+Shift+B).

---

## 📖 Dokumentation

→ **[docs/README.md](docs/README.md)** — Kompletter Doku-Index mit Querverweisen

Wichtige Einstiegspunkte:
- [Hardware Interconnection Matrix](docs/hardware/CONNECTION_MATRIX.md)
- [RS485-Protokoll v2 (Topic-basiert)](.github/copilot-instructions.md)
- [Software-Architektur](docs/software-architecture.md)

---

## 🔧 VS Code Workspaces

| Workspace | Zweck |
|-----------|-------|
| `womo-sensor.code-workspace` | Sensorboard-Entwicklung (MAIN) + Display (REF) |
| `womo-display.code-workspace` | Display-Entwicklung (MAIN) + Sensorboard (REF) |

---

## 📜 Lizenz

Siehe [LICENSE](LICENSE).
