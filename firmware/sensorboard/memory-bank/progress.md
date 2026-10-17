# Progress – WoMoHome Sensorboard

*Stand: 2026-10-17*

## Fertig (produktiv)

| Feature | Anmerkung |
|---|---|
| RS485-Protokoll v2 (Topic-basiert) | Round-Robin, ACK, Half-Duplex-Timing |
| BME680 (innen, 0x76) + BSEC | IAQ, CO2eq, VOCeq, Drucktrend |
| BME280/BME260 (außen, 0x77) | Drucktrend (1h/3h), 96-Sample-History |
| BNO055 IMU | Euler, Heading, Kalibrierungsstatus, NVS |
| HX711 Wägezellen (Gas A+B) | 3-Sample-Mittel, EMA-Rate, NVS-Persistenz |
| INA226 Strom/Leistung | Neu integriert (feature-Branch), `elec`-Topic |
| Analog ADC (Bat + Tank) | Median-of-3, Kalibrierung, NC-Erkennung |
| 12V Bordnetz Steuerung | Bistabiles Relais, Puls `SENSOR_PWR_12V_PULSE_MS` (500 ms), `ctrl.pwr_on` = GPIO14-Sense (echter Ist-Zustand) — **schaltet wegen Hardware-Bug noch nicht am EBL, s. u.** |
| Multimedia-Steuerung | N-MOSFET GPIO13 |
| Display 5V P-MOSFET | Sicherheitslogik (Hi-Z = AUS, nie HIGH!) |
| Deep-Sleep + Touch-Wakeup | GPIO6 kapazitiv, RTC-fähig |
| PCF8523 RTC | I2C 0x68, NTP-Sync nach WiFi-Connect |
| WiFi STA (RUTX11) | NVS-Credentials, mDNS, Retry-Logik |
| HTTP Web-Dashboard | `/api/*`, SPIFFS, mDNS `Womo-Sensor.local` |
| WiFi-Credential-Request via RS485 | `wifi_pass_request` → Display zeigt Dialog |
| Ruuvi BLE Scanner (Innen/Außen) | DF5-Decoding, Fallback für BME280/680, Batteriespannung |
| Ruuvi-MAC-Konfiguration via Display | `cmd:"ruuvi_config"` + `ruuvi_scan_request`/`ruuvi_scan`, NVS statt Compile-Time-Default |

## In Arbeit / Offen

| Feature | Status | Nächste Schritte |
|---|---|---|
| GasBee BLE (Advertising v1, Flasche A/B) | Läuft auf Hardware (2026-10-17, Flasche A) | Empfangsquote pro seq beobachten, Quellenwechsel HX711 ↔ GasBee testen, Flasche B, LiPo-Kennlinie verifizieren, GasBee kalibrieren. Alter GATT-Client entfernt (lieferte Werte nur ins Log). |
| INA226 Shunt-Kalibrierung | Hardware nicht eingebaut | Shunt messen → `SENSOR_INA226_SHUNT_MOHM` setzen |
| Original WoMo-Tanksensoren | Planung | **Pin-Konflikt (2026-08-30):** GPIO17/18 waren hierfür vorgesehen, sind aber jetzt für Licht/Pumpe belegt (siehe unten) — andere freie Pins wählen, Widerstandsnetzwerk, 4-Stufen Reed |
| Licht (GPIO17) + Pumpe (GPIO18) | Zukünftiges Feature — Pins reserviert & sicher initialisiert | Auf Hardware bereits verdrahtet (`SENSOR_LIGHT_GPIO`/`SENSOR_PUMP_GPIO` in `sensor_config.h`). Boot und beide Deep-Sleep-Pfade (`app_main.c`, `deep_sleep.c`) setzen sie bereits sauber auf OUTPUT LOW (analog Multimedia, kein Hold nötig). Noch offen: Kommando-Anbindung (`cmd` z. B. `light_on`/`pump_on`), RS485/ESP-NOW-Topic-Integration, Display-UI. |
| Kalibrierungs-Web-UI | Konzept | NVS-Layer + Web-Formular für HX711/Bat/Tank |
| Web-Dashboard mergen | Feature-Branch | Review → PR → main |
| Unterspannungsschutz 12V Bordnetz | Nicht implementiert | Bisher kein Software-Schutz: Sensorboard schaltet HS-Relais nur per Display-Kommando (`pwr_12v_on/off`), keine automatische Abschaltung bei Unterspannung der Wohnraumbatterie. Referenz: Schaudt-EBL-Elektroblöcke haben dafür einen "Batteriewächter mit Abschaltautomatik" (bei neueren Modellen z. B. 10,6V für >60s, beim EBL 262-3 nicht verifiziert). Schwelle/Zeitfenster für dieses Projekt noch festzulegen — vermutlich über die vorhandene Batteriespannungsmessung (`analog_sensor.c`, `bat`-Topic) realisierbar. |

## Bekannte Probleme / Einschränkungen

| Problem | Beschreibung | Workaround |
|---|---|---|
| **EBL-Relais schaltet nicht (2026-09, Ursache gefunden)** | Monatelang vermuteter Firmware-Bug (Pulslänge) war es nicht. Schaltplan-Analyse: Treiber Q1/Q2 für J7 Pin 1/2 („12V AUS"/„12V EIN") sind **IRLML2244 (P-Kanal)** in High-Side-Beschaltung (Source an +12V, Drain an J7-Pin, SS14-Freilauf nach +12V) — das legt bei „aktiv" **+12V** auf den Pin. Der EBL erwartet an diesen beiden Pins aber **Pull-to-GND** (die Relaisspule liegt EBL-intern schon auf +12V; der Original-Schaudt-Taster zieht Pin 9/12 kurz auf Masse). Mit +12V auf beiden Spulenenden liegt keine Spannung über der Spule → Relais schaltet nie, unabhängig von Pulslänge (100/500/1000 ms alle getestet) oder 12V-Sense. | **Fix (Hardware):** Q1/Q2 → **IRLML6244 (N-Kanal)**, sofern Source bereits an GND liegt und Gate direkt vom GPIO kommt (gleiches SOT-23-Pinout, vermutlich reiner Bestückungsfehler — Ein-Ziffer-Verwechslung IRLML2244/6244; betrifft evtl. auch Licht/Pumpe/Multimedia-Kanäle, dort aber laut Doku als N-Kanal *vorgesehen*). Vor Bestellung: Source-Anschluss von Q1/Q2 am Schaltplan verifizieren. Freilaufdioden (SS14) an Q1/Q2 können nach dem Tausch entfallen (EBL hat eigene) — bei Licht/Pumpe (induktive Last, v. a. Pumpenmotor) **behalten**. |
| 115200 Baud RS485 | Framing-Fehler durch DE-Toggle | Fix: 57600 Baud (aktuell korrekt) |
| GPIO7 HIGH verboten | P-MOSFET Querstrom ~300mA | Nur OUTPUT LOW oder INPUT (Hi-Z) |
| GPIO35-37 gesperrt | Octal-PSRAM intern (N16R8) | Pins nicht verwenden |
| INA226 Shunt-Wert | Platzhalter 1mΩ | Nach Einbau messen und anpassen |
| BSEC State nach Sensortausch | Alter State macht IAQ-Kalibrierung kaputt | `SENSOR_BME680_BSEC_STATE_VERSION` hochzählen |
| GPIO-Hold nach Deep Sleep (2026-08-30, behoben) | GPIO11/12 (12V EIN/AUS) wurden per `gpio_hold_en()` vor dem Schlafen eingefroren, aber `gpio_hold_dis()` fehlte in `app_main.c` komplett für diese Pins. Folge: nach jedem Touch-Wakeup blieben die Pins dauerhaft auf ihrem Vor-Schlaf-Pegel eingefroren, egal was die Software danach per `gpio_set_level()` setzte. Betraf potenziell auch die 12V-Bordnetz-Schaltung selbst. | Fix: `gpio_hold_dis(GPIO11/12)` jetzt unconditional am Boot-Anfang in `app_main.c`. Für GPIO17/18 (Licht/Pumpe) und GPIO8 (Legacy-RS485-DE) wurde der Hold-Mechanismus komplett entfernt (kein bistabiles Relais dahinter, Floating im Sleep unkritisch). |
| GPIO13 + GPIO17 (+ vermutlich GPIO11/12) hardwaredefekt (2026-08-30) | Echter Hardware-Schaden durch Verpolung an Steckverbinder J7 ("LT452 Malibu", 10-pol.): Multimedia-Leitung (GPIO13) lag auf GND, ESP32-Ausgang bei HIGH hart kurzgeschlossen (Ausgangstreiber vermutlich durchlegiert, bleibt bei 0V unabhängig von Software). GPIO17 (Licht) lag auf vertauschtem 12V-Kontroll-Signal — vermutlich Überspannung. GPIO18 (Pumpe) beim selben Vorfall unbeschädigt geblieben. "12V EIN"/"12V AUS" (GPIO11/12) liegen laut Schaltplan ebenfalls auf J7 (waren im Verpolungsbericht als WB/SB vertauscht beschrieben) — Nutzer-Messung mit MIN/MAX-Multimeter (Metrahit 28S, 0,05s Abtastrate) deutet auch hier auf einen Defekt hin, noch nicht abschließend verifiziert. Kein Software-Fix möglich. | Kein Pin-Remapping auf dieser Platine — Nutzer bestellt neue Platinenrevision (korrekter J7, RS485-Transceiver entfernt, Q5 richtig gepolt). `sensor_config.h` (GPIO13/17/18) gilt für die neue, unbeschädigte Platine. Da nun auch die sicherheitskritische 12V-Bordnetz-Steuerung (GPIO11/12) betroffen sein könnte, ist diese Platine ohnehin nicht mehr für normalen Betrieb nutzbar. |

## Versionsverlauf (Highlights)

| Datum | Änderung |
|---|---|
| 2026-05-16 | INA226 integriert, Web-Dashboard, Memory-Bank angelegt |
| 2026-05-03 | Batterie-ADC Neukalibrierung nach Q3-Platinentausch |
| 2026-02-13 | HX711 Kalibrierung (18kg Referenz) |
| 2026-02 | RS485-Protokoll v2 (Topic-basiert), Deep-Sleep, Touch-Wakeup |
