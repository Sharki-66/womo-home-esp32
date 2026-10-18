# System Patterns – WoMoHome Sensorboard

## Boot-Sequenz (app_main.c)

```
1. sensor_log_init()          – Log-Level aus sensor_config.h setzen
2. rtc_gpio_hold_dis(GPIO7)   – Display-MOSFET Hold nach Deep-Sleep aufheben
3. GPIO7 = INPUT (Hi-Z)       – Display 5V sperren (P-MOSFET AUS)
4. GPIO13 = OUTPUT LOW        – Multimedia AUS (kein Hardware-Pulldown!)
5. deep_sleep_init()          – Touch immer zuerst initialisieren
6. deep_sleep_wakeup_by_touch() prüfen
7. pwr_12v_on_early()         – Display 5V EIN + 12V-Relais-Puls
8. rgb_led_off()              – WS2812 GPIO48 ausschalten
9. nvs_flash_init()
10. time_sync_init()          – RTC + I2C-Bus
11. sensor_wifi_init()        – STA zum RUTX11
12. sensor_http_start()       – HTTP-Server (Port 80, SPIFFS)
13. time_sync_start_ntp()     – NTP → setzt RTC nach Connect
14. analog_init()             – ADC (Batterien + Tanks)
15. bme680_app_start()        – BME680 + BME280 + BSEC
16. hx711_app_start()         – Wägezellen (Gas)
17. ina226_app_start()        – Strom-/Leistungsmessung
18. ble_manager_init() + gasbee_ble_scanner_init() + ruuvi_ble_scanner_init() – passiver BLE-Scan
19. vTaskDelay(1000ms)        – BNO055 braucht kurze Pause
20. bno055_app_start()        – IMU
21. rs485_modem_init()        – RS485 TX+RX Tasks starten
22. deep_sleep_start_monitor() – Touch-Debug-Log
23. while(1) vTaskDelay(1s)   – Idle
```

## RS485 Task-Architektur (rs485_modem.c)

**TX-Task:** Round-Robin über alle Topics (100ms-Zyklus, max. 1 Topic pro Zyklus)
- Vor TX: `s_last_rx_us` prüfen → 150ms TX-Sperre nach RX (Empfangsfenster)
- Mindestabstand TX→TX: 80ms (`RS485_MIN_TX_GAP_US`)
- `s_tx_mutex` schützt Bus-Zugriff

**RX-Task:** Liest UART-Bytes, assembliert CRLF-terminierte JSON-Zeilen
- Echo-Unterdrückung: eigene TX-Bytes auf Half-Duplex-Bus werden ignoriert
- ACK verarbeiten → `s_pending[]`-Array aufräumen
- Commands (`display_ready`, `pwr_12v_on/off`, `radio_on/off`, `tare_a/b`, ...) direkt ausführen

**State-Flags:**
```c
s_display_ready     // true nach erstem display_ready CMD
s_tx_seq            // monoton steigender Sequenzzähler (u32, skip 0)
s_last_rx_seq       // letzter vom Display empfangener seq
s_last_ack_seq      // letzter bestätigter seq
```

## Topic-Scheduler

Jedes Topic hat ein eigenes Intervall. Nach `display_ready`: Initial-Burst aller Topics sofort.
Sofort-Ctrl: Wenn `pwr_12v_on/off` oder `radio_on/off` ausgeführt wird, wird `ctrl`-Topic unmittelbar nochmal gesendet.

| Topic | Intervall | Felder (Kernfelder) |
|---|---|---|
| `hello` | bis display_ready | `fw`, `board`, `uptime`, `ts` |
| `hb` | 30 s | `uptime`, `ts`, `heap` |
| `ctrl` | 2 s | `pwr_on` (12V-Sense GPIO14, s. Abschnitt „12V Bordnetz"), `radio_on`, `ac_present` |
| `imu` | 5 s | `yaw_deg`, `pitch_deg`, `roll_deg`, `hdg`, `cal`, `calibrated` |
| `bat` | 10 s | `b1`, `b2` (V), `nc1`, `nc2` |
| `tank` | 10 s | `t1`, `t2` (%), `t1_l`, `t2_l` (L), Rate-Felder |
| `hx` | 10 s | `src` (`hx`/`ble`/`none`), `a`, `b` (kg), `sum`, `nc`; bei `src:"ble"` `batt_a_mv`, `batt_a_pct`, `chg_a`, `cal_a` (analog `_b`) |
| `gas` | 10 s | `src`, `active`, `net`, `rate1h`, `rate2h`, `rest_h`, ... |
| `bme_in` | 15 s | `chip` (`bme680`/`bme280`/`bme260`/`ruuvi`), `addr`, `temp_c`, `rh_pct`, `press_hpa`, `ts`; nur BME680: `gas_kohm` (wenn Gas gültig) sowie – erst wenn BSEC einen IAQ-Wert liefert (nach Burn-in) – `iaq`, `iaq_acc` (0–3), `eco2_ppm`, `bvoc_ppm`; Ruuvi-Fallback zusätzlich `batt_mv` |
| `bme_out` | 15 s | `chip`, `addr`, `temp_c`, `rh_pct`, `press_hpa`, `ts`; bei Trenddaten `press_trend_state` (`fall_fast`/`fall_slow`/`steady`/`rise_slow`/`rise_fast`) + `press_trend_hpa_h`; nur BME680: `gas_kohm` (wenn Gas gültig); **kein IAQ/BSEC** für den Außensensor; Ruuvi-Fallback zusätzlich `batt_mv` |
| `elec` | 5 s | `v_bus`, `i_a`, `p_w`, `v_shunt_mv`; `nc=true` wenn INA226 fehlt |
| `ruuvi_scan` | nur auf Anfrage | `tags:[{mac, rssi, temp_c, has_pressure, age_s}, …]` – alle gesehenen Ruuvi-Tags (ungefiltert), Antwort auf `cmd:"ruuvi_scan_request"` |
| `gasbee_scan` | nur auf Anfrage | `tags:[{mac, rssi, weight_kg, batt_mv, batt_pct, chg, cal, seq, age_s}, …]` – alle gesehenen GasBees (ungefiltert), Antwort auf `cmd:"gasbee_scan_request"` |

## Sensor-Muster

### Analog (ADC, analog_sensor.c)
- Median-of-3: `analog_read_mv_avg(ch, &mv, 3)`
- Kalibrierung: `U_kal = U_gemessen * SCALE + OFFSET_mV`
- NC-Erkennung: < 1V → `nc=true` (nicht angeschlossen)
- Spannungsteiler: 100kΩ / 22kΩ → Faktor 122/22 für Batterien

### HX711 (hx711_sensor.c)
- Dual-Channel (A + B), 3 Samples gemittelt
- Kalibrierung: `Gewicht = (raw - OFFSET) * SCALE`
- Gasverbrauch: EMA-basierte Rate (1h + 2h Fenster), in NVS persistiert (gilt für HX711 und GasBee)
- Ausschalt-Lücke: `gas_state` speichert Netto A/B mit Uhrzeit (RTC/NTP) und Quelle; beim ersten Messwert nach dem Boot wird der mittlere Verbrauch über die Lücke direkt als `rate1h`/`rate2h` übernommen (gleiche Quelle, Lücke ≤ `SENSOR_GAS_GAP_MAX_S` = 1 Tag, Abnahme ≥ `SENSOR_GAS_ACTIVE_EPS_KG`), sonst Raten auf 0 und neue Basis.

### BME680 / BSEC (bme680_sensor.c)
- Bosch BSEC Bibliothek (3.3V, 3s Sample Rate, 4d)
- Temperatur-Offset: 4,0°C (Board-Eigenerwärmung, konfigurierbar)
- BSEC State: in NVS als `state_in_76_v<N>` gespeichert
- State-Version: Bei Sensortausch `SENSOR_BME680_BSEC_STATE_VERSION` hochzählen
- IAQ-Burn-in: `iaq`/`eco2`/`bvoc` erst gültig nach Stabilisierung (~5 min, fabrikneu 20–30 min);
  `iaq_acc` 0→3 (0 = nicht stabilisiert, 1 = unsicher, 2 = mittel, 3 = kalibriert), Stufe 3 typ.
  nach Stunden, Auto-Kalibrierung voll eingeschwungen nach ~4 Tagen Dauerbetrieb (4d-Config).
  Nach erstem Erreichen von Accuracy ≥ 3 wird der State gespeichert → nächster Boot startet fast dort.
- Display wertet IAQ erst ab `iaq_acc >= 2` für den Fehlerstapel aus (`main.c`)

### INA226 (ina226_sensor.c)
- Library: `k0i05/esp_ina226` v1.2.7
- Snapshot thread-safe via `ina226_app_get_snapshot()`
- Shunt: TODO → `SENSOR_INA226_SHUNT_MOHM` nach Hardwareeinbau setzen

### Ruuvi BLE Scanner (ruuvi_ble_scanner.c)
- Dekodiert Ruuvi Data Format 5 (RAWv2) aus Advertisement-Manufacturer-Data, registriert als Handler beim zentralen `ble_manager` (kein eigenes NimBLE)
- Zwei Slots (Indoor/Outdoor), Routing ausschließlich per MAC-Filter
- **MAC-Filter vom Display konfigurierbar** (seit MAC-Config-Umbau): kein Compile-Time-Default mehr, sondern per RS485 `cmd:"ruuvi_config"` (`indoor_mac`, `outdoor_mac`) gesetzt und in NVS (`SENSOR_RUUVI_NVS_NAMESPACE`/`_KEY_INDOOR`/`_KEY_OUTDOOR` in `sensor_config.h`) persistiert; `ruuvi_ble_scanner_set_macs()` aktualisiert den Filter zur Laufzeit ohne Reboot. Leer/unkonfiguriert = Slot deaktiviert.
- Scan-Liste (`ruuvi_ble_scanner_get_scan_list()`): sammelt bis zu `SENSOR_RUUVI_SCAN_MAX_TAGS` zuletzt gesehene Tags unabhängig vom Filter (MAC, RSSI, Temp, Alter), für die Auswahlliste im Display; wird auf `cmd:"ruuvi_scan_request"` als `ruuvi_scan`-Topic gesendet
- Kabel-Sensor (BME680/BME280) hat Vorrang; Ruuvi wird in `espnow_modem.c` (`rs485_publish_bme_in/out`) nur als Fallback genutzt
- Felder: `temp_c`, `humidity_pct`, `pressure_hpa` (nur wenn `has_pressure`), `battery_mv` (nur wenn `battery_valid`, Byte 15-16 des DF5-Payloads, 11-Bit + 1600mV Offset)

### GasBee BLE Scanner (gasbee_ble_scanner.c) + Gas-Quelle (gas_source.c)
- Advertising-Format v1 der GasBee (`firmware/gasbee_nrf`), Spezifikation in `README.md`: Name „GasBee“, Company-ID 0xFFFF, Version 1, Bruttogewicht (10 g), Akku-mV, `seq`, Flags
- Kein Connect, kein GATT; Scan passiv (`ble_manager`, `SENSOR_BLE_SCAN_ITVL/WINDOW`), `filter_duplicates=0`
- Pro Messzyklus 10 Pakete mit gleicher `seq` → nur erstes Paket einer neuen `seq` übernommen; Paketzahl pro `seq` wird als Koexistenz-Diagnose geloggt
- Zwei Slots (Flasche A/B) per MAC, `cmd:"gasbee_config"`, NVS `gasbee_cfg`, leer = deaktiviert (Muster wie Ruuvi)
- Veraltet nach `SENSOR_GASBEE_STALE_MS` (2 Zyklen + Sendefenster) ohne neue `seq`
- Akku-% per LiPo-Tabelle `SENSOR_GASBEE_LIPO_CURVE` (linear interpoliert, noch nicht am Akku verifiziert)
- `gas_source_get_snapshot()`: HX711-Modul vorhanden (`valid_a || valid_b`) → HX711 (Vorrang); sonst GasBee A→Kanal A, B→Kanal B; sonst nc. Alle Gas-Publisher (`hx`, `gas`, Web-Snapshot) nutzen nur noch diese Funktion. Fehlende Wägezelle bei vorhandenem Modul ist nicht erkennbar (Mischbetrieb nicht vorgesehen).
- Wechsel HX711 ↔ GasBee setzt die Rate-Basis zurück (`last_ts_us = 0`), Voll-Referenzen bleiben

### BNO055 (bno055_sensor.c)
- Achsen-Remapping für Fahrzeugeinbaulage (sensor_config.h)
  - Sensor-Z → logisch X (Roll/Quer)
  - Sensor-Y negiert → logisch Y (Pitch/Längs)
  - Sensor-X → logisch Z (Heading/Hoch)
- Kalibrierung in NVS (`bno055` Namespace)

## WiFi-Credential-Flow
Wenn kein SSID in NVS: Sensorboard sendet `type:"wifi_pass_request"` per RS485 → Display zeigt Passwort-Dialog → sendet Credentials per `cmd` zurück → Sensorboard speichert in NVS und verbindet.

## Pin-Übersicht (Kurzfassung)
```
GPIO1  Tank1 Frisch (ADC1_CH0)     GPIO9  RS485 TX
GPIO2  Tank2 Grau  (ADC1_CH1)      GPIO10 RS485 RX
GPIO4  Batt1 Kfz  (ADC1_CH3)       GPIO8  RS485 DE/RTS
GPIO5  Batt2 Board (ADC1_CH4)      GPIO11 12V LBE EIN (Relais-Puls)
GPIO6  Touch-Wakeup (TOUCH_PAD6)   GPIO12 12V LBE AUS (Relais-Puls)
GPIO7  Display 5V (P-MOSFET Q4)    GPIO13 Multimedia (N-MOSFET)
GPIO14 12V Sense (Eingang)         GPIO15 I2C SCL
GPIO16 I2C SDA                     GPIO21 AC 230V Sense
GPIO45 HX711 SCK                   GPIO47 HX711 DOUT
GPIO48 WS2812 RGB-LED (onboard)
GPIO35,36,37 GESPERRT (PSRAM intern!)
```
