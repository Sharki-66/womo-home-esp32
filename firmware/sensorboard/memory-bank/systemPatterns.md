# System Patterns – WoMoHome Sensorboard

## Boot-Sequenz (app_main.c)

```
1. sensor_log_init()          – Log-Level aus sensor_config.h setzen
2. rtc_gpio_hold_dis(GPIO7)   – Display-MOSFET Hold nach Deep-Sleep aufheben
3. GPIO7 = OUTPUT LOW         – Display 5V aus (Q5 sperrt → Q4 sperrt)
4. GPIO13 = OUTPUT LOW        – Multimedia AUS (kein Hardware-Pulldown!)
5. deep_sleep_init()          – Touch immer zuerst initialisieren
6. deep_sleep_wakeup_by_touch() prüfen
7. pwr_12v_on_early()         – Display 5V EIN + 12V-Relais-Puls
8. rgb_led_off()              – WS2812 GPIO48 ausschalten
9. nvs_flash_init()
10. time_sync_init()          – RTC + I2C-Bus
11. power_mode_init()        – Takt 80–240 MHz (esp_pm, ohne Light Sleep)
    sensor_wifi_init()        – STA zum RUTX11
12. sensor_http_start()       – HTTP-Server (Port 80, SPIFFS)
13. time_sync_start_ntp()     – NTP → setzt RTC nach Connect
14. analog_init()             – ADC (Batterien + Tanks)
15. bme680_app_start()        – BME680 + BME280 + BSEC
16. hx711_app_start()         – Wägezellen (Gas)
17. ina226_app_start()        – Strom-/Leistungsmessung
18. ble_manager_init() + gasbee_ble_scanner_init() + gasbee_gatt_cal_init() + ruuvi_ble_scanner_init() – passiver BLE-Scan, GATT nur zur GasBee-Kalibrierung
19. vTaskDelay(1000ms)        – BNO055 braucht kurze Pause
20. bno055_app_start()        – IMU
21. espnow_modem_init()       – ESP-NOW-Transport + Topic-Scheduler starten
22. deep_sleep_start_monitor() – Touch-Debug-Log
23. while(1) vTaskDelay(1s)   – Idle
```

## ESP-NOW Task-Architektur (espnow_modem.c + espnow_transport.c)

Seit 2026-06 (Commit `e1eae83`) ESP-NOW statt RS485. Interne Funktionen in `espnow_modem.c` mit Präfix `modem_*` (bis 2026-10 `rs485_*`).

**Transport (`espnow_transport.c`):** Ein JSON-Frame pro ESP-NOW-Paket, max. `WOMO_ESPNOW_MAX_PAYLOAD` (250 Byte), Voll-Duplex (keine TX-Sperren mehr). Display-MAC im NVS, nur verschlüsselter Unicast von/zu ihr; Kopplung per Beacon (Details `firmware/sensorboard/CLAUDE.md`). Empfang über Queue + RX-Task (Stack 8192 Byte).

**Scheduler-Task:** Round-Robin über alle Topics (100 ms-Zyklus, max. 1 Topic pro Zyklus). Sobald `espnow_transport_peer_known()` → `s_display_ready = true` + Initial-Burst. Scan-Listen und `gasbee_cal_result` haben Vorrang vor dem Round-Robin.

**RX:** ACK (`ack`/`cmd_ack`) → `s_pending[]` aufräumen; `hb` vom Display ohne ACK (Peer-Erkennung, Feld `eco` → `power_mode_set_requested()`); Commands (`pwr_12v_on/off`, `radio_on/off`, `tare_a/b`, `wifi_config`, ...) direkt ausführen und mit `cmd_ack` quittieren.

**State-Flags:**
```c
s_display_ready     // true sobald Display-Peer per ESP-NOW bekannt
s_tx_seq            // monoton steigender Sequenzzähler (u32, skip 0)
s_last_rx_seq       // letzter vom Display empfangener seq
s_last_ack_seq      // letzter bestätigter seq
```

## Topic-Scheduler

Jedes Topic hat ein eigenes Intervall (`WOMO_TOPIC_*_INTERVAL_MS` in `firmware/shared/womo_config.h`). Sobald der Display-Peer bekannt ist: Initial-Burst aller Topics sofort.
Sofort-Ctrl: Wenn `pwr_12v_on/off` oder `radio_on/off` ausgeführt wird, wird `ctrl`-Topic unmittelbar nochmal gesendet.

| Topic | Intervall | Felder (Kernfelder) |
|---|---|---|
| `ctrl` | 2 s | `pwr_on` (12V-Sense GPIO14, s. Abschnitt „12V Bordnetz"), `radio_on`, `ac_present`, `uptime`, `heap` (ehemals `hb`), `ssid`, optional `eco` (nur `true`), `rtc_bat_low`, `rtc_bat_switched`, `rssi`, `wifi_trial` |
| `imu` | 2 s | `yaw_deg`, `pitch_deg`, `roll_deg`, `hdg`, `cal`, `calibrated` |
| `bat` | 5 s | `b1`, `b2` (V), `nc1`, `nc2` |
| `tank` | 10 s | `t1`, `t2` (%), `t1_l`, `t2_l` (L), Rate-Felder |
| `hx` | 10 s | `src` (`hx`/`ble`/`none`), `a`, `b` (kg), `sum`, `nc`; bei `src:"ble"` `batt_a_mv`, `batt_a_pct`, `chg_a`, `cal_a`, `cfg_a` (nur wenn an USB/Konfigurationsmodus, analog `_b`) |
| `gas` | 10 s | `src`, `active`, `net`, `rate1h`, `rate2h`, `rest_h`, ... |
| `bme_in` | 10 s | `chip` (`bme680`/`bme280`/`bme260`/`ruuvi`), `addr`, `temp_c`, `rh_pct`, `press_hpa`, `ts`; nur BME680: `gas_kohm` (wenn Gas gültig) sowie – erst wenn BSEC einen IAQ-Wert liefert (nach Burn-in) – `iaq`, `iaq_acc` (0–3), `eco2_ppm`, `bvoc_ppm`; Ruuvi-Fallback zusätzlich `batt_mv` |
| `bme_out` | 10 s | `chip`, `addr`, `temp_c`, `rh_pct`, `press_hpa`, `ts`; bei Trenddaten `press_trend_state` (`fall_fast`/`fall_slow`/`steady`/`rise_slow`/`rise_fast`) + `press_trend_hpa_h`; nur BME680: `gas_kohm` (wenn Gas gültig); **kein IAQ/BSEC** für den Außensensor; Ruuvi-Fallback zusätzlich `batt_mv` |
| `elec` | 1 s | `v_bus`, `i_a`, `p_w`, `v_shunt_mv`; `nc=true` wenn INA226 fehlt |
| `ruuvi_scan` | nur auf Anfrage | ein Frame pro Tag: `sid`, `idx`, `cnt` (0 = keiner) + `mac`, `rssi`, `temp_c`, `has_pressure`, `age_s` – alle gesehenen Ruuvi-Tags (ungefiltert), Antwort auf `cmd:"ruuvi_scan_request"` |
| `gasbee_scan` | nur auf Anfrage | ein Frame pro GasBee: `sid`, `idx`, `cnt` (0 = keine) + `mac`, `rssi`, `weight_kg`, `batt_mv`, `batt_pct`, `chg`, `cal`, `cfg`, `bee_seq`, `age_s` – alle gesehenen GasBees (ungefiltert), Antwort auf `cmd:"gasbee_scan_request"` |
| `gasbee_cal_result` | nach `gasbee_cal` | `step` (`zero`/`ref`), `ok`, `err` (0 ok, > 0 ATT-Fehler der GasBee, < 0 Sensorboard-Fehler, s. `gasbee_gatt_cal.h`), `msg` |

## Energiesparen (hal/power_mode.c)

- Immer: dynamische Taktanpassung 80–240 MHz (`esp_pm_configure`, `light_sleep_enable = false` – Light Sleep würde ESP-NOW-Empfang und BLE-Scan lückenhaft machen).
- Eco = `hb.eco` vom Display (Hintergrundlicht aus) **und** kein Landstrom. `power_mode_tick()` läuft alle 100 ms im TX-Task und gleicht den Soll-Zustand ab (idempotente Setter, keine Flankenlogik):
  - `ble_manager_set_scan_enabled()`: Scan nur, wenn `gasbee_ble_scanner_burst_expected()` oder Ruuvi-Fenster offen. Innerhalb der Fenster bleibt das Tastverhältnis 100 % (bei 30/50 ms kamen nur 2/10 GasBee-Pakete an).
  - GasBee-Takt: Bezug = erstes Paket einer neuen seq, Intervall = Abstand / seq-Differenz (zählt verpasste Bursts mit), plausibel 15–3600 s, sonst unbekannt. Konfigurationsmodus (USB) → unbekannt. Fenster −5 s … +15 s um den erwarteten Burst, nach 2 verpassten Fenstern Dauer-Scan bis zur nächsten seq.
  - Ruuvi: alle 60 s ein Fenster (max. `SENSOR_ECO_RUUVI_WINDOW_S`), endet sobald alle konfigurierten Tags ein Paket geliefert haben.
  - `sensor_wifi_set_paused()`: trennt, keine Reconnects, setzt den zuletzt verbundenen Kanal fest (ESP-NOW). Nicht während `wifi_trial`.
  - `bno055_app_set_suspended()`: Umschaltung im BNO055-Task, letzter Snapshot bleibt stehen, beim Aufwecken NDOF + Kalibrierung aus NVS.
  - Topics mit `eco_interval_ms`. Wechsel zurück auf Normal → `modem_publish_all()`.

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
- **MAC-Filter vom Display konfigurierbar** (seit MAC-Config-Umbau): kein Compile-Time-Default mehr, sondern per ESP-NOW `cmd:"ruuvi_config"` (`indoor_mac`, `outdoor_mac`) gesetzt und in NVS (`SENSOR_RUUVI_NVS_NAMESPACE`/`_KEY_INDOOR`/`_KEY_OUTDOOR` in `sensor_config.h`) persistiert; `ruuvi_ble_scanner_set_macs()` aktualisiert den Filter zur Laufzeit ohne Reboot. Leer/unkonfiguriert = Slot deaktiviert.
- Scan-Liste (`ruuvi_ble_scanner_get_scan_list()`): sammelt bis zu `SENSOR_RUUVI_SCAN_MAX_TAGS` zuletzt gesehene Tags unabhängig vom Filter (MAC, RSSI, Temp, Alter), für die Auswahlliste im Display; wird auf `cmd:"ruuvi_scan_request"` als `ruuvi_scan`-Topic gesendet
- Kabel-Sensor (BME680/BME280) hat Vorrang; Ruuvi wird in `espnow_modem.c` (`modem_publish_bme_in/out`) nur als Fallback genutzt
- Felder: `temp_c`, `humidity_pct`, `pressure_hpa` (nur wenn `has_pressure`), `battery_mv` (nur wenn `battery_valid`, Byte 15-16 des DF5-Payloads, 11-Bit + 1600mV Offset)

### GasBee BLE Scanner (gasbee_ble_scanner.c) + Gas-Quelle (gas_source.c)
- Advertising-Format v1 der GasBee (`firmware/gasbee_nrf`), Spezifikation in `README.md`: Name „GasBee“, Company-ID 0xFFFF, Version 1, Bruttogewicht (10 g), Akku-mV, `seq`, Flags
- Kein Connect, kein GATT; Scan passiv (`ble_manager`, `SENSOR_BLE_SCAN_ITVL/WINDOW`), `filter_duplicates=0`
- Pro Messzyklus 10 Pakete mit gleicher `seq` → nur erstes Paket einer neuen `seq` übernommen; Paketzahl pro `seq` wird als Koexistenz-Diagnose geloggt
- Zwei Slots (Flasche A/B) per MAC, `cmd:"gasbee_config"`, NVS `gasbee_cfg`, leer = deaktiviert (Muster wie Ruuvi)
- Zuordnung für Nutzer über Bit 3 (USB): Display-Assistent fragt `gasbee_scan_request` mit `cfg_only` ab, keine MAC-Auswahl
- Kalibrierung: `gasbee_gatt_cal.c` (einziger Connection-Handler des BLE-Managers), Ergebnis asynchron → Frame `gasbee_cal_result` aus dem TX-Task. Während `gasbee_cal_start`…`_end` ist die Flasche `nc` und die Verbrauchsrate ruht (sonst Gas-Alarm/falsche Rate durch leere Waage bzw. Referenzgewicht)
- Veraltet nach `SENSOR_GASBEE_STALE_MS` (2 Zyklen + Sendefenster) ohne neue `seq`
- **Scan-Listen (`ruuvi_scan`/`gasbee_scan`):** Ein Frame pro Gerät (`sid`/`idx`/`cnt`), ein Frame pro Durchlauf des TX-Tasks (100 ms, Round-Robin setzt dann aus). Grund: Die Liste als ein Frame überschritt mit 2 GasBees `WOMO_ESPNOW_MAX_PAYLOAD` (250 Byte). Die Liste zeigt bewusst **alle** Geräte (bis `SENSOR_*_SCAN_MAX_TAGS` = 8), die Zuordnung erfolgt explizit per MAC (Stellplatz mit Nachbarfahrzeugen).
- Akku-% per LiPo-Tabelle `SENSOR_GASBEE_LIPO_CURVE` (linear interpoliert, noch nicht am Akku verifiziert)
- `gas_source_get_snapshot()`: HX711-Modul vorhanden (`valid_a || valid_b`) → HX711 (Vorrang); sonst GasBee A→Kanal A, B→Kanal B; sonst nc. Alle Gas-Publisher (`hx`, `gas`, Web-Snapshot) nutzen nur noch diese Funktion. Fehlende Wägezelle bei vorhandenem Modul ist nicht erkennbar (Mischbetrieb nicht vorgesehen).
- **nc vs. leer (`gas`-Topic `nc_a`/`nc_b`):** GasBee: `nc` nur, wenn keine aktuellen Werte kommen (veraltet, HX711-Fehler-Flag oder **unkalibriert**); gültiger Wert unter `SENSOR_GAS_NO_BOTTLE_THRESHOLD_KG` (1 kg, keine Flasche auf der Waage) = **leer** → `pct_a` 0, Gas-Alarm im Display. HX711 am Kabel: unter Schwelle weiterhin `nc` (abgesteckte Wägezelle sonst nicht erkennbar). Leere Waage wird weder als aktive Flasche gewählt noch als Voll-Referenz übernommen.
- Wechsel HX711 ↔ GasBee setzt die Rate-Basis zurück (`last_ts_us = 0`), Voll-Referenzen bleiben

### BNO055 (bno055_sensor.c)
- Achsen-Remapping für Fahrzeugeinbaulage (sensor_config.h)
  - Sensor-Z → logisch X (Roll/Quer)
  - Sensor-Y negiert → logisch Y (Pitch/Längs)
  - Sensor-X → logisch Z (Heading/Hoch)
- Kalibrierung in NVS (`bno055` Namespace)

## WiFi-Credential-Flow
Wenn kein SSID in NVS: Sensorboard sendet `type:"wifi_pass_request"` per ESP-NOW → Display zeigt Passwort-Dialog → sendet Credentials per `cmd` zurück → Sensorboard verbindet.

`wifi_config` (auch vom AP-Tab des Displays) ist eine Probeverbindung: NVS erst nach GOT_IP, nach `SENSOR_WIFI_RETRY_MAX` Fehlversuchen zurück auf die gespeicherten Daten (`sensor_wifi.c`, `s_trial_*`).

## Pin-Übersicht (Kurzfassung)
```
GPIO1  Tank1 Frisch (ADC1_CH0)     GPIO9  frei (ehem. RS485 TX)
GPIO2  Tank2 Grau  (ADC1_CH1)      GPIO10 frei (ehem. RS485 RX)
GPIO4  Batt1 Kfz  (ADC1_CH3)       GPIO8  ehem. RS485 DE, im Deep-Sleep LOW
GPIO5  Batt2 Board (ADC1_CH4)      GPIO11 12V LBE EIN (Relais-Puls)
GPIO6  Touch-Wakeup (TOUCH_PAD6)   GPIO12 12V LBE AUS (Relais-Puls)
GPIO7  Display 5V (HIGH=ein, Q5→Q4) GPIO13 Multimedia (N-MOSFET)
GPIO14 12V Sense (Eingang)         GPIO15 I2C SCL
GPIO16 I2C SDA                     GPIO21 AC 230V Sense
GPIO45 HX711 SCK                   GPIO47 HX711 DOUT
GPIO48 WS2812 RGB-LED (onboard)
GPIO35,36,37 GESPERRT (PSRAM intern!)
```
