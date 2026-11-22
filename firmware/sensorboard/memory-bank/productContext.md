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
[GasBee nRF52840] ──BLE-Advertising──→ [Sensorboard]
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

### → GasBee (BLE, passiver Scan)
- GasBee sendet alle 5 min einen Advertising-Burst (Format v1: Bruttogewicht, Akku, seq, Flags)
- Sensorboard scannt passiv, Zuordnung Flasche A/B per MAC (`gasbee_cfg`)
- GATT-Verbindung nur zur Kalibrierung, wenn die GasBee an USB hängt (`gasbee_gatt_cal.c`)
- HX711 am Kabel hat Vorrang (`gas_source.c`)

### → Browser (HTTP Web-Dashboard)
- Erreichbar unter `http://Womo-Sensor.local/` (mDNS)
- Zeigt alle Sensordaten, ermöglicht Steuerkommandos
- Statische Dateien aus SPIFFS-Partition `storage`
- API: `/api/status`, `/api/data`, `/api/imu`, `/api/cmd` (POST)

## Nutzerkontext
Das Fahrzeug ist ein **Fiat Ducato** (Reisemobil). Das System ist dauerhaft im Fahrzeug verbaut. Kein Display am Sensorboard selbst – Bedienung nur über Touch-Wakeup (GPIO6), ESP-NOW-Kommandos vom Display, oder Web-Dashboard.

## Steuerlogik

### 12V Bordnetz (LBE)
Bistabiles Relais: Ein-Puls / Aus-Puls (Rev 0.5: GPIO11/GPIO12, Rev 0.6: GPIO12/GPIO11,
gewählt über `SENSOR_BOARD_REV`), Pulslänge `SENSOR_PWR_12V_PULSE_MS`
(500 ms). Display sendet `pwr_12v_on`/`pwr_12v_off` per ESP-NOW-Kommando. `ctrl.pwr_on` meldet
den an GPIO14 gemessenen Ist-Zustand (12V-Sense) → Display-Button zeigt Realität.

⚠️ **Bekannter Hardware-Bug (2026-09):** Treiber Q1/Q2 (J7 „12V EIN"/„AUS") sind mit
IRLML2244 (P-Kanal, High-Side) bestückt statt IRLML6244 (N-Kanal, Low-Side). Der EBL
erwartet Pull-to-GND an diesen Pins — das Relais schaltet dadurch bisher nicht. Details:
`techContext.md` und `progress.md`. In Rev 0.6 behoben (Q6/Q7 IRLML6244 gegen GND an J5),
Hardware-Test steht aus.

### Multimedia
N-Kanal MOSFET (GPIO13): HIGH = EIN, nur wenn 12V aktiv. Display sendet `radio_on`/`radio_off`.

### AC-Sense (GPIO21)
Zeigt an ob 230V Landstrom vorhanden (passiver Eingang).

## Tankvolumen (Referenz)
- Frischwasser: 100 Liter
- Grauwasser: 92 Liter
- Tankfüllstand: Votronic kapazitiv (0–1V), direkt am ADC (kein Spannungsteiler);
  ab Rev 0.6 mit 4k7 in Reihe + Z-Diode 3,3V als Schutz (J9/J10)
- Ab Rev 0.6 zusätzlich Eingänge für die Original-Reed-Tanksensoren (J2/J3 → GPIO10/9), Firmware offen
