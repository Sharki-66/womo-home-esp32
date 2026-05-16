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
18. gasbee_ble_client_start() – NimBLE Central (BLE-Gaswaage)
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
| `ctrl` | 2 s | `pwr_on`, `radio_on`, `ac_present` |
| `imu` | 5 s | `yaw_deg`, `pitch_deg`, `roll_deg`, `hdg`, `cal`, `calibrated` |
| `bat` | 10 s | `b1`, `b2` (V), `nc1`, `nc2` |
| `tank` | 10 s | `t1`, `t2` (%), `t1_l`, `t2_l` (L), Rate-Felder |
| `hx` | 10 s | `a`, `b` (kg), `sum`, `nc` |
| `gas` | 10 s | `active`, `net`, `rate1h`, `rate2h`, `rest_h`, ... |
| `bme` | 15 s | `"0x76"` (Indoor BME680), `"0x77"` (Outdoor BME280) |
| `elec` | 5 s | `v_bus`, `i_a`, `p_w`, `v_shunt_mv`; `nc=true` wenn INA226 fehlt |

## Sensor-Muster

### Analog (ADC, analog_sensor.c)
- Median-of-3: `analog_read_mv_avg(ch, &mv, 3)`
- Kalibrierung: `U_kal = U_gemessen * SCALE + OFFSET_mV`
- NC-Erkennung: < 1V → `nc=true` (nicht angeschlossen)
- Spannungsteiler: 100kΩ / 22kΩ → Faktor 122/22 für Batterien

### HX711 (hx711_sensor.c)
- Dual-Channel (A + B), 3 Samples gemittelt
- Kalibrierung: `Gewicht = (raw - OFFSET) * SCALE`
- Gasverbrauch: EMA-basierte Rate (1h + 2h Fenster), in NVS persistiert

### BME680 / BSEC (bme680_sensor.c)
- Bosch BSEC Bibliothek (3.3V, 3s Sample Rate, 4d)
- Temperatur-Offset: 4,0°C (Board-Eigenerwärmung, konfigurierbar)
- BSEC State: in NVS als `state_in_76_v<N>` gespeichert
- State-Version: Bei Sensortausch `SENSOR_BME680_BSEC_STATE_VERSION` hochzählen

### INA226 (ina226_sensor.c)
- Library: `k0i05/esp_ina226` v1.2.7
- Snapshot thread-safe via `ina226_app_get_snapshot()`
- Shunt: TODO → `SENSOR_INA226_SHUNT_MOHM` nach Hardwareeinbau setzen

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
