# Womo-Home ESP32 Projektstruktur

## Übersicht

Das Projekt besteht aus drei ESP32-S3 Boards, die per RS485 miteinander kommunizieren:

1. **Display Board** (`firmware/`) - WaveShare ESP32-S3 LCD 7" Touch
2. **Walter Board** (`firmware-walter/`) - Sensor/Router Board (read-only Referenz)
3. **Modem Board** (`firmware-modem/`) - WaveShare ESP32-S3-A mit SIM7670G 4G-Modem

## Board-Zuständigkeiten

### Display Board (firmware/)
- **Hardware**: ESP32-S3, 7" RGB-LCD (ST7701), GT911 Touch, SD-Karte
- **Aufgaben**: 
  - Benutzer-Interface (LVGL)
  - Anzeige Sensordaten (von Walter via RS485)
  - WiFi-Client für Wetter/Zeit/Geocoding
  - Hintergrundbilder von SD-Karte
- **Kommunikation**: RS485 Slave (empfängt von Walter)

### Walter Board (firmware-walter/)
- **Hardware**: ESP32-S3, BNO055 IMU, 2× BME680, HX711, Analog-Eingänge
- **Aufgaben**:
  - Sensor-Datenerfassung (Gas, Batterien, Tanks, Umwelt, IMU)
  - WiFi-Router (SoftAP + STA mit NAT)
  - Datenaggregation und JSON-Serialisierung
- **Kommunikation**: RS485 Master (sendet an Display)

### Modem Board (firmware-modem/) - NEU
- **Hardware**: ESP32-S3-A, SIM7670G 4G-Modem, GPS
- **Aufgaben**:
  - LTE-Konnektivität (Backup/Hauptverbindung)
  - GPS-Position (höhere Genauigkeit als Modem in Walter)
  - Optional: Fernzugriff, Cloud-Anbindung
- **Kommunikation**: TBD (RS485, UART, oder WiFi zu Walter)

## Verzeichnisstruktur

```
womo-home-esp32/
├── firmware/                    # Display-Firmware
│   ├── main/
│   │   ├── main.c              # UI-Logik, RS485-Empfang
│   │   ├── gui/                # LVGL-Widgets
│   │   ├── hardware/           # LCD, Touch, SD
│   │   ├── network/            # WiFi, HTTP-Clients
│   │   ├── rs485/              # RS485-Display-Seite
│   │   ├── storage/            # SD-Karten-Zugriff
│   │   └── time/               # Zeit/Datum
│   ├── docs/
│   │   └── hardware/           # Display-Hardware-Docs
│   └── CMakeLists.txt
│
├── firmware-walter/             # Sensor-Board (Referenz)
│   ├── main/
│   ├── components/
│   │   ├── womo_rs485/         # RS485-Master
│   │   ├── womo_sensor_state/  # Shared State
│   │   └── ...                 # Sensor-Treiber
│   └── docs/
│       └── hardware_walter.md
│
├── firmware-modem/              # LTE-Modem-Board (neu)
│   ├── main/
│   ├── components/
│   │   └── sim7670g/           # Modem-Treiber
│   └── docs/
│       └── hardware/
│
└── docs/                        # Projekt-übergreifend
    ├── README.md               # Gesamtsystem-Dokumentation
    ├── system_architecture.md  # Architektur-Überblick
    └── hardware/
        ├── system_wiring.pdf   # Dein PCB/Verkabelung
        ├── connection_matrix.md
        └── power_budget.md
```

## Hardware-Dokumentation

### Pro Board (in `firmware*/docs/hardware/`)
- **Schematic PDF**: Offizieller Schaltplan vom Hersteller
- **Board-Specs Markdown**: Pin-Belegung, Besonderheiten
- **README.md**: Übersicht der Hardware-Dateien
- **datasheets/**: Datenblätter (gitignored, zu groß)

### System-übergreifend (in `docs/hardware/`)
- **system_wiring.pdf**: Dein Board-Layout für Verkabelung
- **connection_matrix.md**: Tabelle, welches Board welche Pins nutzt
- **power_budget.md**: Stromversorgung, Spannungen, Absicherung

## RS485-Netzwerk

```
┌──────────────┐     RS485      ┌───────────────┐
│    Walter    │────────────────▶│    Display    │
│  (Master)    │   Full-Frame    │   (Slave)     │
│              │      JSON        │               │
└──────────────┘                 └───────────────┘
       │
       │ (Future: RS485 oder UART zu Modem?)
       ▼
┌──────────────┐
│    Modem     │
│  (Optional)  │
└──────────────┘
```

## Nächste Schritte

1. **Modem-Firmware aufsetzen**:
   ```bash
   cd firmware-modem
   idf.py set-target esp32s3
   idf.py menuconfig  # SIM7670G konfigurieren
   ```

2. **Hardware-Dokumentation ergänzen**:
   - WaveShare SIM7670G Schematic besorgen
   - Eigenes Board-Layout (KiCad/PDF) in `docs/hardware/`
   - Connection-Matrix für Pin-Zuordnung

3. **Integration planen**:
   - Modem-Anbindung: RS485, UART oder WiFi?
   - GPS-Daten an Display weiterleiten
   - LTE-Status anzeigen (bereits vorbereitet in Display-UI)

## Wichtige Dateien

- **Copilot-Anweisungen**: `.github/copilot-instructions.md`
- **RS485-Protokoll**: `firmware/main/rs485/womo_rs485_display.h`
- **Sensor-Datenstruktur**: `womo_sensor_data_t`
- **Walter-Hardware**: `firmware-walter/docs/hardware_walter.md`

## Best Practices

1. **Keine Duplikate**: Gemeinsame Komponenten (z.B. `womo_rs485`) nur in einem Repo, andere referenzieren
2. **Hardware separat**: Board-spezifische Docs in jeweiligem `firmware*/docs/`
3. **System-Docs zentral**: Gesamtsystem in Root-`docs/`
4. **Datasheets lokal**: Zu große PDFs in `.gitignore`, aber Link zur Quelle
5. **Versionierung**: Git-Tags für Firmware-Releases pro Board

## Kontakt / Änderungen

Bei strukturellen Änderungen diese Datei aktualisieren und in `.github/copilot-instructions.md` referenzieren.
