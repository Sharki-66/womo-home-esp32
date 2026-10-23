# Product Context – WoMoHome Sensorboard

## Rolle im Gesamtsystem

```
[RUTX11 Router] ←WiFi/LTE/GNSS→ [Internet / Mobilfunk]
      ↑ WiFi STA                        ↑ HTTP/UCI (Display → Router)
      │
[Sensorboard ESP32-S3]
      │ ESP-NOW (JSON, ≤ 250 Byte/Frame)
      ↓
[Display ESP32-S3]
      ↑ BLE
[GasBee ESP32-C3]  ──BLE──→ [Sensorboard]
```

Das Sensorboard ist **kein** UI-Gerät – es sammelt Daten und sendet sie weiter. Das Display ist für alle Visualisierung und Nutzerinteraktion zuständig.

## Schnittstellen

### → Display (ESP-NOW, bis 2026-06 RS485)
Primärkanal. Das Sensorboard ist der **Sender**, das Display der **Empfänger**.
- Topics im Round-Robin (alle paar Sekunden)
- Display sendet Steuerkommandos zurück (`pwr_12v_on`, `radio_on`, `tare_a`, ...)
- Kein Handshake mehr: Display sendet alle 2 s `hb`, Sensorboard lernt daraus die Display-MAC und startet die Topics

### → Router (WiFi STA)
- Verbindet sich als WLAN-Client mit RUTX11
- NTP-Zeit-Sync über Router
- GPS/LTE-Daten werden **nicht** vom Sensorboard geholt – das macht das Display direkt

### → GasBee (BLE Central)
- Sensorboard ist NimBLE Central, GasBee ist Peripheral
- Auto-Reconnect bei Verbindungsabbruch
- Empfängt Gewichtsdaten der Gasflaschen

### → Browser (HTTP Web-Dashboard)
- Erreichbar unter `http://Womo-Sensor.local/` (mDNS)
- Zeigt alle Sensordaten, ermöglicht Steuerkommandos
- Statische Dateien aus SPIFFS-Partition `storage`
- API: `/api/status`, `/api/data`, `/api/imu`, `/api/cmd` (POST)

## Nutzerkontext
Das Fahrzeug ist ein **Fiat Ducato** (Reisemobil). Das System ist dauerhaft im Fahrzeug verbaut. Kein Display am Sensorboard selbst – Bedienung nur über Touch-Wakeup (GPIO6), ESP-NOW-Kommandos vom Display, oder Web-Dashboard.

## Steuerlogik

### 12V Bordnetz (LBE)
Bistabiles Relais: Ein-Puls (GPIO11) / Aus-Puls (GPIO12), Pulslänge `SENSOR_PWR_12V_PULSE_MS`
(500 ms). Display sendet `pwr_12v_on`/`pwr_12v_off` per ESP-NOW-Kommando. `ctrl.pwr_on` meldet
den an GPIO14 gemessenen Ist-Zustand (12V-Sense) → Display-Button zeigt Realität.

⚠️ **Bekannter Hardware-Bug (2026-09):** Treiber Q1/Q2 (J7 „12V EIN"/„AUS") sind mit
IRLML2244 (P-Kanal, High-Side) bestückt statt IRLML6244 (N-Kanal, Low-Side). Der EBL
erwartet Pull-to-GND an diesen Pins — das Relais schaltet dadurch bisher nicht. Details:
`techContext.md` und `progress.md`.

### Multimedia
N-Kanal MOSFET (GPIO13): HIGH = EIN, nur wenn 12V aktiv. Display sendet `radio_on`/`radio_off`.

### AC-Sense (GPIO21)
Zeigt an ob 230V Landstrom vorhanden (passiver Eingang).

## Tankvolumen (Referenz)
- Frischwasser: 100 Liter
- Grauwasser: 92 Liter
- Tankfüllstand: Votronic kapazitiv (0–1V), direkt am ADC (kein Spannungsteiler)
