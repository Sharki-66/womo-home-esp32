# Product Context – WoMoHome Sensorboard

## Rolle im Gesamtsystem

```
[RUTX11 Router] ←WiFi/LTE/GNSS→ [Internet / Mobilfunk]
      ↑ WiFi STA                        ↑ HTTP/UCI (Display → Router)
      │
[Sensorboard ESP32-S3]
      │ RS485 (57600 8N1, JSON)
      ↓
[Display ESP32-S3]
      ↑ BLE
[GasBee ESP32-C3]  ──BLE──→ [Sensorboard]
```

Das Sensorboard ist **kein** UI-Gerät – es sammelt Daten und sendet sie weiter. Das Display ist für alle Visualisierung und Nutzerinteraktion zuständig.

## Schnittstellen

### → Display (RS485)
Primärkanal. Das Sensorboard ist der **Sender**, das Display der **Empfänger**.
- Topics im Round-Robin (alle paar Sekunden)
- Display sendet Steuerkommandos zurück (`pwr_12v_on`, `radio_on`, `tare_a`, ...)
- Handshake: Sensorboard sendet `hello` bis Display `display_ready` schickt

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
Das Fahrzeug ist ein **Fiat Ducato** (Reisemobil). Das System ist dauerhaft im Fahrzeug verbaut. Kein Display am Sensorboard selbst – Bedienung nur über Touch-Wakeup (GPIO6), RS485-Kommandos vom Display, oder Web-Dashboard.

## Steuerlogik

### 12V Bordnetz (LBE)
Bistabiles Relais: Ein-Puls (GPIO11, 100ms) schaltet alles ein, Aus-Puls (GPIO12, 100ms) schaltet alles aus. Display sendet `pwr_12v_on`/`pwr_12v_off` per RS485-Kommando.

### Multimedia
N-Kanal MOSFET (GPIO13): HIGH = EIN, nur wenn 12V aktiv. Display sendet `radio_on`/`radio_off`.

### AC-Sense (GPIO21)
Zeigt an ob 230V Landstrom vorhanden (passiver Eingang).

## Tankvolumen (Referenz)
- Frischwasser: 100 Liter
- Grauwasser: 92 Liter
- Tankfüllstand: Votronic kapazitiv (0–1V), direkt am ADC (kein Spannungsteiler)
