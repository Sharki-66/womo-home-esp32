# Dokumentation – WoMo Home ESP32

Übersicht über alle Projektdokumente mit Querverweisen.

## 📐 Hardware

| Dokument | Beschreibung |
|----------|-------------|
| [CONNECTION_MATRIX.md](hardware/CONNECTION_MATRIX.md) | Verkabelung zwischen den drei ESP32-S3 Boards |
| [MODEM_BOARD.md](hardware/MODEM_BOARD.md) | WaveShare ESP32-S3-A + SIM7670G Hardware-Notizen |
| [esp32-s3-touch-lcd-7.md](hardware/esp32-s3-touch-lcd-7.md) | Waveshare Display-Board Übersicht |
| [Hardware-Übersicht](hardware/docs_hardware_overview_Version2.md) | Systemweite Hardware-Beschreibung |
| [Komponenten](hardware/docs_hardware_components_Version2.md) | Detaillierte Komponentenliste |
| [Verdrahtung](hardware/docs_hardware_wiring_Version2.md) | Pin-Belegungen und Verkabelung |
| [Waveshare LCD Specs](hardware/WAVESHARE_ESP32_S3_LCD_SPECS.md) | LCD-Spezifikationen |
| [Waveshare ESP32 Specs](hardware/WAVESHARE_ESP32_S3_SPECS.md) | ESP32-S3 Modul-Spezifikationen |

→ Datenblätter (PDFs) und Schaltpläne liegen in [hardware/datasheets/](../hardware/datasheets/).

## 💻 Software

| Dokument | Beschreibung |
|----------|-------------|
| [Software-Architektur](software-architecture.md) | Gesamtarchitektur des Systems |
| [Copilot Instructions](../.github/copilot-instructions.md) | Regeln für KI-Assistenz, RS485-Protokoll v2, Geplante LVGL v9 Migration |

## 🔗 Firmware-Dokumentation

Jedes Firmware-Projekt hat eigene Dokumente:

- **Display**: [firmware/display/](../firmware/display/) — Waveshare 7" Touch LCD, LVGL v8
- **Sensorboard**: [firmware/sensorboard/](../firmware/sensorboard/) — Heemol ESP32-S3, Sensoren, RS485

## 🗄️ Archiv

Ältere/abgelöste Projekte:

- [archive/firmware-modem/](../archive/firmware-modem/) — USB-Modem-Version (wird durch RS485 3.3V abgelöst)
- [archive/firmware-walter/](../archive/firmware-walter/) — DPTechnics Walter v1.0 (Dez 2025, abgelöst)

## 📁 Weitere Ressourcen

| Ordner | Beschreibung |
|--------|-------------|
| [hardware/schematics/](../hardware/schematics/) | KiCad-Schaltpläne |
| [hardware/datasheets/](../hardware/datasheets/) | Datenblätter, Schaltpläne (PDF) |
| [sdcard/](../sdcard/) | SD-Karten-Inhalt für das Display (Ducato-Bilder, Wetter-Icons, Config) |
| [tests/](../tests/) | Hardware-Test-Sketche (I2C, SPI, LVGL, Touch, …) |
