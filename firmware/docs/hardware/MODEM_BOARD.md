# WaveShare ESP32-S3-A mit SIM7670G - Hardware-Notizen

## Übersicht

- **Board**: WaveShare ESP32-S3-A-LTE (mit SIM7670G-4G-Modem)
- **MCU**: ESP32-S3-WROOM-1 (8MB Flash, 2MB PSRAM)
- **Modem**: SIMCom SIM7670G (LTE Cat-1, GNSS)
- **Konnektivität**: 4G LTE, GPS/GLONASS/Galileo/BeiDou
- **Stromversorgung**: 5V via USB-C oder Schraubklemme, min. 2A empfohlen

## Pin-Belegung (Vorläufig)

> **TODO**: Offizielle WaveShare-Dokumentation prüfen und hier ergänzen!

### Modem-Schnittstelle (UART)

| ESP32-S3 GPIO | Funktion | SIM7670G Pin | Beschreibung |
|---------------|----------|--------------|--------------|
| GPIO17 | UART TX | RXD | Daten vom ESP32 zum Modem |
| GPIO18 | UART RX | TXD | Daten vom Modem zum ESP32 |
| GPIO4 | PWRKEY | PWRKEY | Power-On Trigger (LOW-Puls 1s) |
| GPIO5 | STATUS | STATUS | Modem-Status (HIGH=ON) |
| GPIO6 | RESET | RESET | Hardware-Reset (LOW=Reset) |

### GPS-Antenne

- **Anschluss**: SMA-Buchse auf Board
- **Typ**: Aktive GPS-Antenne (3.3V LNA-Power vom Modem)
- **Empfehlung**: WaveShare GPS-Antenne oder kompatibel (50Ω)

### LTE-Antenne

- **Anschluss**: SMA-Buchse auf Board
- **Frequenzbänder**: 
  - LTE-FDD: B1/B3/B5/B7/B8/B20/B28
  - LTE-TDD: B38/B40/B41
- **Empfehlung**: Breitband-LTE-Antenne für Europa (700-2700 MHz)

### SIM-Karte

- **Slot**: Nano-SIM (Push-in)
- **Spannung**: 1.8V oder 3V (auto-detect)
- **PIN-Lock**: Im Code deaktivieren oder PIN senden

### USB

- **Typ**: USB-C
- **Funktion**: 
  - Stromversorgung (5V/2-3A)
  - Debug/Flash via USB-JTAG (ESP32-S3 integriert)
  - Nicht für Modem-AT-Befehle (nutze UART-Schnittstelle)

### I2C (falls frei)

| ESP32-S3 GPIO | Funktion | Verbindung |
|---------------|----------|------------|
| GPIO8 | SDA | Frei für externe Sensoren (falls nicht belegt) |
| GPIO9 | SCL | Frei für externe Sensoren |

> **Hinweis**: I2C-Pins eventuell vom Modem-Interface belegt – Schaltplan checken!

## Stromversorgung

### Anforderungen

| Betriebszustand | Typisch | Peak |
|-----------------|---------|------|
| Idle (ESP32 only) | 80 mA | 150 mA |
| Modem Sleep | 100 mA | 200 mA |
| LTE Search | 400 mA | 1 A |
| LTE Connected (Idle) | 150 mA | 300 mA |
| LTE TX (max) | 800 mA | **2 A** |
| GPS aktiv | +50 mA | +80 mA |

### Netzteil-Empfehlung

- **Minimum**: 5V/2A (für kurze LTE-TX-Bursts ausreichend)
- **Empfohlen**: 5V/3A (sicherer Betrieb bei schlechter Netzabdeckung)
- **Kondensator**: 1000µF Low-ESR am 5V-Eingang (auf Board vorhanden?)

### Spannungsregler

- **5V → 3.8V** (für SIM7670G Hauptversorgung)
- **3.3V Rail** (ESP32-S3 + Peripherie)
- Beide Regler on-board (keine externe Beschaltung nötig)

## Software-Integration

### AT-Befehlssatz (SIM7670G)

Kommunikation über UART (115200 baud default):

```c
// Modem einschalten
gpio_set_level(GPIO_PWRKEY, 0);
vTaskDelay(pdMS_TO_TICKS(1200)); // 1.2s LOW
gpio_set_level(GPIO_PWRKEY, 1);

// Warten auf Initialisierung (STATUS=HIGH)
while (!gpio_get_level(GPIO_STATUS)) {
    vTaskDelay(pdMS_TO_TICKS(100));
}

// AT-Befehle senden
uart_write_bytes(uart_num, "AT\r\n", 4);
// Erwarte "OK\r\n"

uart_write_bytes(uart_num, "AT+CPIN?\r\n", 10);
// Erwarte "+CPIN: READY\r\nOK\r\n"

uart_write_bytes(uart_num, "AT+CGATT?\r\n", 11);
// Erwarte "+CGATT: 1\r\nOK\r\n" (attached)

// GPS aktivieren
uart_write_bytes(uart_num, "AT+CGNSPWR=1\r\n", 14);

// GPS-Position lesen
uart_write_bytes(uart_num, "AT+CGNSINF\r\n", 12);
// Erwarte "+CGNSINF: 1,1,<timestamp>,<lat>,<lon>,<alt>,...\r\nOK\r\n"
```

### ESP-IDF Komponenten

- **UART**: `driver/uart.h`
- **GPIO**: `driver/gpio.h`
- **Modem-Bibliothek**: 
  - Option 1: [esp-modem](https://github.com/espressif/esp-protocols/tree/master/components/esp_modem) (offiziell)
  - Option 2: Custom AT-Parser (für einfache Anwendungen)

### Beispiel-Initialisierung

```c
// UART-Config
uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
};
uart_param_config(UART_NUM_1, &uart_config);
uart_set_pin(UART_NUM_1, GPIO_TX, GPIO_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
uart_driver_install(UART_NUM_1, 2048, 2048, 0, NULL, 0);

// GPIO für Modem-Control
gpio_set_direction(GPIO_PWRKEY, GPIO_MODE_OUTPUT);
gpio_set_direction(GPIO_STATUS, GPIO_MODE_INPUT);
gpio_set_level(GPIO_PWRKEY, 1); // Idle HIGH

// Modem einschalten
modem_power_on();
```

## GPS-Nutzung

### Positionsgenauigkeit

- **Cold Start**: 30-120s bis First Fix (im Freien)
- **Warm Start**: 10-30s
- **Hot Start**: 1-5s
- **Genauigkeit**: Typisch 2.5m CEP (bei gutem Signal)

### AT-Befehle

```
AT+CGNSPWR=1        // GPS einschalten
AT+CGNSINF          // Position, Geschwindigkeit, Heading, Sats
AT+CGNSURC=2        // URC-Modus (unaufgeforderte Positionsmeldungen)
AT+CGNSTST=1        // Self-Test (Antenne prüfen)
```

### Datenformat

```
+CGNSINF: <GNSS run status>,<Fix status>,<UTC date & Time>,<Latitude>,<Longitude>,
          <MSL Altitude>,<Speed Over Ground>,<Course Over Ground>,<Fix Mode>,
          <Reserved1>,<HDOP>,<PDOP>,<VDOP>,<Reserved2>,<GNSS Satellites in View>,
          <GNSS Satellites Used>,<GLONASS Satellites Used>,<Reserved3>,<C/N0 max>,
          <HPA>,<VPA>
```

Beispiel:
```
+CGNSINF: 1,1,20260108123045.000,52.520008,13.404954,50.0,0.0,0.0,1,,1.2,1.8,1.0,,12,10,2,,35,2.5,1.5
```

## LTE-Konfiguration

### APN-Einstellungen

```c
// Beispiel für deutsche Provider
AT+CGDCONT=1,"IP","internet.telekom"        // Telekom
AT+CGDCONT=1,"IP","internet.vodafone.de"   // Vodafone
AT+CGDCONT=1,"IP","internet.eplus.de"      // O2/Eplus
```

### Netzsuche

```
AT+COPS=?          // Verfügbare Netze scannen (dauert 60s!)
AT+COPS=0          // Automatische Netzwahl
AT+CREG?           // Registrierungsstatus
AT+CSQ             // Signal Quality (0-31, >10 = ok)
```

### Datenverbindung

```
AT+CGACT=1,1       // PDP Context aktivieren
AT+CIFSR           // IP-Adresse abfragen
AT+CIPSTART="TCP","example.com",80  // TCP-Socket öffnen
AT+CIPSEND=<len>   // Daten senden
AT+CIPCLOSE        // Socket schließen
```

## Mechanische Integration

### Abmessungen (geschätzt)

- **Board**: ~65mm × 90mm
- **Höhe**: ~12mm (ohne Antennen)
- **Befestigung**: 4× M3-Bohrlöcher

### Antennen-Platzierung

- **LTE**: Mindestens 20cm Abstand zu Metall/GND-Flächen
- **GPS**: Freie Sicht nach oben (Fenster oder Außenmontage)
- **Abstand**: LTE ↔ GPS mindestens 10cm (Interferenz vermeiden)

### Kühlung

- SIM7670G wird bei LTE-TX warm (bis 60°C)
- Belüftung empfohlen bei Dauerbetrieb in geschlossenem Gehäuse
- Ggf. Kühlkörper auf Modem-Chip

## Bekannte Probleme & Workarounds

### Modem startet nicht

- **Check**: PWRKEY-Puls mindestens 1s LOW
- **Check**: Stromversorgung ausreichend? (min. 2A)
- **Workaround**: Hardware-Reset via RESET-Pin (100ms LOW)

### GPS kein Fix

- **Check**: Antenne angeschlossen?
- **Check**: Im Freien? (Indoor meist kein Fix)
- **Workaround**: A-GPS nutzen (AT+CGNSCOLD für Cold Start mit Netz-Unterstützung)

### LTE verbindet nicht

- **Check**: SIM-Karte entsperrt? (AT+CPIN=<PIN>)
- **Check**: APN korrekt? (AT+CGDCONT)
- **Check**: Signal vorhanden? (AT+CSQ, Wert >10)
- **Workaround**: Netzsuche forcieren (AT+COPS=0)

## Datenblätter & Links

- **SIM7670G AT-Befehlsreferenz**: [SIMCom Website](https://www.simcom.com/product/SIM7670G.html)
- **ESP32-S3 Datasheet**: [Espressif](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- **WaveShare Wiki**: [TBD - Link ergänzen nach Recherche]
- **ESP-Modem Bibliothek**: [GitHub](https://github.com/espressif/esp-protocols/tree/master/components/esp_modem)

## Integration ins Womo-System

### Rolle im Gesamtsystem

1. **LTE-Konnektivität**: Backup oder Haupt-Internet-Verbindung
2. **GPS-Provider**: Höhere Genauigkeit als Modem in Walter (falls vorhanden)
3. **Remote-Access**: Optional VPN oder SSH-Tunnel für Fernwartung

### Kommunikation mit Walter

**Empfohlene Methode**: UART direkt
- Modem sendet GPS-Daten + LTE-Status als JSON an Walter
- Walter aggregiert mit eigenen Sensordaten
- Display empfängt alles via RS485 von Walter

**Alternatives**: Modem baut WiFi-Verbindung zu Walter-AP
- HTTP-API oder WebSocket
- Flexibler, aber höherer Overhead

### Datenformat (Vorschlag)

```json
{
  "lte": {
    "registered": true,
    "operator": "Telekom",
    "rssi": -75,
    "signal_percent": 85
  },
  "gps": {
    "valid": true,
    "latitude": 52.520008,
    "longitude": 13.404954,
    "altitude_m": 50.0,
    "speed_kmh": 0.0,
    "heading_deg": 0.0,
    "satellites": 10,
    "hdop": 1.2
  }
}
```

## TODO

- [ ] Offizielle WaveShare-Pinout verifizieren
- [ ] Schaltplan vom Board besorgen (WaveShare-Wiki)
- [ ] Test-Firmware schreiben (AT-Command-Tool)
- [ ] GPS-Fix-Zeit im Fahrzeug messen (Metal-Dach-Einfluss)
- [ ] LTE-Antennen-Positionen testen (RSSI-Optimierung)
- [ ] Stromverbrauch messen (Idle vs. TX)
- [ ] Integration mit Walter-UART definieren (Protokoll)
- [ ] Failover-Logik: Wann LTE, wann Walter-WiFi?

## Changelog

| Datum | Änderung | Autor |
|-------|----------|-------|
| 2026-01-08 | Initial Draft | System |
| TBD | Pinout verifiziert | - |
| TBD | Test-Ergebnisse | - |

---

**Status**: 🟡 Work in Progress – Informationen vorläufig, Verifizierung ausstehend!
