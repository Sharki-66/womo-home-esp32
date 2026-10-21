---
type: entry-point
title: Schnellstart
description: Einstiegspunkt für das WoMoHome-Wiki (Wohnmobil-Bordcomputer) — erklärt das Projektziel, das Drei-Knoten-System, die Repo-Struktur und verweist Leser zu den Architektur-, Hardware-, Workflow-, Operations- und Testing-Seiten für ihre jeweilige Aufgabe.
tags: [quickstart, overview, wohnmobil, sensorboard, display, router, esp32, getting-started]
verified:
  - by: openwiki/0.7.2
    at: 2026-10-19T20:49:57.319Z
sources:
  - id: openwiki-source-a2371d6362e5db4bc834ad03
    resource: repo://CLAUDE.md
  - id: openwiki-source-23775c3de52f3ab95a13cb8b
    resource: repo://README.md
generated: { by: "openwiki/0.7.2", at: "2026-10-19T20:49:57.319Z" }
---

# Schnellstart

**WoMoHome** ("WoMo Home ESP32") ist ein **privates Hobbyprojekt**: ein selbstgebautes
digitales Cockpit ("Bordcomputer") für ein Wohnmobil auf Fiat-Ducato-Basis. Es ersetzt
das originale Schaudt-artige EBL-Bedienpanel des Fahrzeugs durch ein selbstgebautes
IoT-System, das die Fahrzeugsensoren ausliest (Gasflaschengewicht, Wassertanks,
Batterie-/Shunt-Strom, Innen-/Außenklima, IMU/Nivellierung, GPS) und auf einem
7-Zoll-Touchscreen anzeigt, während es außerdem das 12V-Bordnetz und die
Multimedia-Stromversorgung steuert.

Zwei Dinge sollte man wissen, bevor man Code oder Dokumentation in diesem Repository
liest:

- **Die Firmware ist größtenteils "vibe coded"** (KI-unterstützt, iterativ generiert)
  statt vorab aus einer Spezifikation heraus von Hand entworfen. Behandeln Sie
  Quellcode und Tests als Ground Truth gegenüber der Prosa-Dokumentation, wenn beide
  voneinander abweichen — dieses Wiki benennt bekannte Diskrepanzen dort, wo sie
  gefunden wurden, explizit (siehe [Architektur-Überblick](architecture/overview.md)
  für die größte davon, den RS485-vs-ESP-NOW-Transport-Mismatch).
- **Die Codebasis und ihre Dokumentation sind vorwiegend auf Deutsch** (Code-Kommentare,
  Commit-Nachrichten, die Arbeitsregeln in `CLAUDE.md`/`AGENTS.md`, Firmware-READMEs).
  Dieses Wiki ist auf Deutsch verfasst, aber Datei-/Symbolnamen, Log-Strings und
  Config-Keys, denen Sie im Quellcode begegnen, bleiben Deutsch bzw. im Original.

Wenn Sie nur eine weitere Seite lesen, lesen Sie den
**[Architektur-Überblick](architecture/overview.md)** — er ist die Landkarte des
gesamten Systems. Wenn Sie vorhaben, an der Hardware zu arbeiten (Schaltpläne,
Pin-Belegungen, Bestückung von Bauteilen), lesen Sie zuerst
**[Bekannte Hardware-Fehler (Errata) und offene Hardware-Entscheidungen](hardware/errata-and-open-decisions.md)**
— dort werden bestätigte Hardware-Fehler und offene/unverifizierte Annahmen
aufgelistet, damit Sie keinen bekannten Fehler wiederholen oder auf einem
ungemessenen Wert aufbauen.

## Das System: drei physische Knoten + BLE-Peripheriegeräte

| Knoten | Hardware | Rolle |
|---|---|---|
| **Sensorboard** | ESP32-S3-WROOM-1 N16R8 (Heemol DevKitC-1) | Liest alle Sensoren aus (IMU, Klima, Tanks, Batterien, Shunt-Strom, Gasgewicht), stellt sie als topic-basierte JSON-Frames bereit, hostet ein lokales Wi-Fi-Web-Dashboard und steuert die 12V-/Multimedia-Relais an. |
| **Display** | Waveshare ESP32-S3-Touch-LCD-7 (800×480 Touch-LCD) | Betreibt die LVGL-Touch-GUI, führt alle Sensorboard-Topics zusammen und visualisiert sie, lässt den Nutzer das 12V-Bordnetz und Multimedia steuern und spricht mit dem Router und externen Internet-APIs. |
| **Router** | Teltonika RUTX11 (OpenWrt) | Stellt Wi-Fi/LTE-Internet, GNSS und einen eigenen Hotspot bereit; wird über UCI/ubus konfiguriert. Nicht Teil dieses Repositories (kommerzielles Gerät). |

Eine Familie batteriebetriebener **BLE-"GasBee"-Gaswaagen-Knoten** (plus
handelsübliche RuuviTag-Klimatags) bewirbt Daten passiv per Bluetooth Low Energy
gegenüber dem Sensorboard — siehe [BLE-Sensorknoten](architecture/ble-sensor-nodes.md).

Sensorboard und Display sind über das verbunden, was die gesamte Codebasis "das
RS485-Protokoll" nennt (ein topic-basiertes CRLF-JSON-Format) — aber die aktuell
kompilierte Firmware transportiert dieses Protokoll tatsächlich über **ESP-NOW**,
nicht über einen verdrahteten RS485-Bus; siehe
[Architektur-Überblick](architecture/overview.md) für die vollständige Erklärung
dieser Diskrepanz, bevor Sie davon ausgehen, dass Dokumentation oder
Schaltpläne aktuell sind.

## Repository-Struktur

```
womo-home-esp32/
├── firmware/
│   ├── sensorboard/    ← Sensorboard-Firmware (ESP-IDF, ESP32-S3)
│   ├── display/        ← Display-Firmware (ESP-IDF, ESP32-S3)
│   ├── gasbee/          ← GasBee BLE-Waage, Legacy ESP32-C3 (ESP-IDF)
│   ├── gasbee_nrf/      ← GasBee BLE-Waage, aktuell Seeed XIAO nRF52840 (nRF Connect SDK/Zephyr)
│   └── shared/          ← womo_config.h: von Sensorboard + Display gemeinsam genutzte Konstanten
├── hardware/
│   ├── schematics/      ← KiCad-Projekte für alle Platinen
│   └── datasheets/      ← Datenblätter der Komponenten
├── docs/                ← Handgeschriebene Hardware-/Firmware-Dokumentation
├── tests/               ← Eigenständige ESP-IDF-Hardware-Bringup-Tests
├── tools/                ← Unterstützende Werkzeuge (z. B. router-webui)
├── website/              ← Öffentliche Astro-Marketing-/Showcase-Website
└── openwiki/             ← Dieses generierte Wiki
```

Siehe [Platinen & KiCad](hardware/boards-and-kicad.md) dafür, was jedes
Hardware-Projekt enthält und wie es lizenziert ist bzw. von Upstream-Designs
abgeleitet ist (insbesondere der RuuviTag-Fork).

## Build-Grundlagen

Jedes Firmware-Teilprojekt baut unabhängig:

```bash
# Sensorboard / Display / GasBee (ESP32-C3) — ESP-IDF v5.5.2
source /home/hajo/esp/v5.5.2/esp-idf/export.sh
cd firmware/<sensorboard|display|gasbee>
idf.py build
```

GasBee nRF (`firmware/gasbee_nrf/`) nutzt stattdessen das nRF Connect SDK v3.4.1
(Zephyr); den zugehörigen Build-Befehl finden Sie in
[Build, Flash & Konfiguration](operations/build-flash-config.md). Gemäß den
Arbeitsregeln des Projekts werden **Builds/Flash-/Monitor-Sitzungen nur vom
menschlichen Entwickler über VS-Code-Tasks ausgeführt** — Agenten sollen diese
Schritte erklären, nicht selbst Flash-Befehle ausführen. Sensorboard und Display
kompilieren beide gegen den gemeinsamen Header `firmware/shared/womo_config.h`;
eine Änderung daran erfordert ein Neubauen **und** erneutes Flashen beider
Boards, da eine Abweichung die Protokollkompatibilität zwischen ihnen bricht.

## Wie geht es weiter, je nach Aufgabe

- **Die Gesamtarchitektur verstehen** → [Architektur-Überblick](architecture/overview.md)
  (die Systemlandkarte), dann die Seiten je Knoten:
  [Sensorboard](architecture/sensorboard.md),
  [Display](architecture/display.md),
  [Router und Konnektivität](architecture/router-and-connectivity.md),
  [BLE-Sensorknoten](architecture/ble-sensor-nodes.md).
- **Sensorboard- oder Display-Firmware ändern** → beginnen Sie beim
  [Architektur-Überblick](architecture/overview.md) und der jeweiligen Knoten-Seite
  oben, dann [Build, Flash & Konfiguration](operations/build-flash-config.md) für
  Toolchains, Partitionslayouts und NVS-Namespaces; prüfen Sie
  [RS485-Topic-Protokoll](workflows/rs485-protocol.md), falls Ihre Änderung die
  Sensorboard↔Display-Daten betrifft.
- **Hardware oder Pinbelegung ändern** → lesen Sie zuerst
  [Bekannte Hardware-Fehler (Errata) und offene Hardware-Entscheidungen](hardware/errata-and-open-decisions.md),
  dann [Platinen & KiCad](hardware/boards-and-kicad.md) und die Pin-Tabelle des
  jeweiligen Knotens (z. B. [Sensorboard](architecture/sensorboard.md)).
- **Das RS485-/BLE-Protokoll debuggen** →
  [RS485-Topic-Protokoll](workflows/rs485-protocol.md) für die
  Sensorboard↔Display-JSON-Frames, und
  [BLE-Payload-Formate und Datenfluss](workflows/ble-data-flow.md) für die
  GasBee-/Ruuvi-Advertising-Payloads und wie sie das Display erreichen.
- **Firmware bauen oder flashen** →
  [Build, Flash & Konfiguration](operations/build-flash-config.md); für isoliertes
  Peripherie-Bringup vor der Integration in die Haupt-Firmware siehe
  [Hardware-Bringup-Tests](testing/hardware-bringup-tests.md).
- **An der öffentlichen Website arbeiten** → [Website (Astro)](architecture/website.md)
  behandelt das separate, read-only `website/`-Showcase-Projekt und sein
  Netlify-Deployment.
- **Verwandte Workflows** — Gasflaschen-Einrichtung/Kalibrierung:
  [GasBee-Einrichtung & Kalibrierung](workflows/gas-calibration.md); 12V-Bus-/
  Relaissteuerung (einschließlich eines bekannten sicherheitsrelevanten
  Hardware-Defekts): [Stromversorgung und Leistungssteuerung](workflows/power-control.md);
  ausgehende Internet-Integrationen (Wetter, Geocoding, Unwetterwarnungen):
  [Externe Dienste](integrations/external-services.md).
