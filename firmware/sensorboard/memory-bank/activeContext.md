# Active Context – WoMoHome Sensorboard

*Stand: 2026-10-18*

## Aktueller Branch
`feature/espnow-migration`

## Laufende Arbeiten

### Gasverbrauch über Ausschalt-Lücke (2026-10-18, gebaut, nicht auf Hardware getestet)
`espnow_modem.c`: `gas_state_t` um `last_epoch_s`/`last_src` erweitert (Migration altes Layout), `gas_bridge_gap()` beim ersten Messwert nach Boot. Behebt nebenbei: `last_ts_us` (Laufzeit) aus NVS war nach Neustart unbrauchbar → Rate fror ein. Entscheidungen: Lücken-Rate direkt übernehmen, >1 Tag/ungültige Uhrzeit → Raten 0, gilt für HX711 + GasBee.
**Test:** Board über Nacht aus → Log `Gas-Lücke … überbrückt` bzw. `nicht überbrückt (Grund)`.
Risiko: RTC-Batterie leer + kein NTP → falsche, aber „gültige“ Uhrzeit.
Gleicher Fehler bei den Tanks (Rate nach Neustart eingefroren, im Log 2026-10-18 gesehen) → `tank_state_load_from_nvs()` setzt `last_ts_us = 0`, erster Messwert = neue Basis, Raten bleiben.

### Scan-Listen: ein Frame pro Gerät (2026-10-18, gebaut, nicht getestet)
`gasbee_scan` mit 2 GasBees war 301 Byte > 250 → Frame verworfen, Display bekam keine Liste. Jetzt ein Frame pro Gerät (`sid`/`idx`/`cnt`) für `ruuvi_scan` und `gasbee_scan`, Display setzt zusammen. GasBee-Feld `seq` → `bee_seq` (Kollision mit Frame-`seq`). **Sensorboard und Display gemeinsam flashen.**

### GasBee: nc vs. leer (2026-10-18, gebaut, nicht getestet)
GasBee < 1 kg → leer (0 %, Alarm) statt nc; nc nur ohne Werte oder unkalibriert (`gas_source.c`). HX711 am Kabel unverändert (< 1 kg = nc). Achtung: aktuelle XIAO-GasBee meldet „unkalibriert“ → Display zeigt jetzt nc, bis sie kalibriert ist.


### GasBee per BLE-Advertising (2026-10-17, auf Hardware getestet)
Zwei GasBees (Flasche A/B, `firmware/gasbee_nrf`) liefern per Advertising-Format v1
(passiv, kein Connect; Spezifikation in `README.md`). Neu: `network/gasbee_ble_scanner.c`
(ersetzt den alten GATT-Client `gasbee_ble_client`, dessen Werte nie verwendet wurden),
`sensors/gas_source.c` (HX711-Modul vorhanden → Vorrang, sonst GasBee, sonst nc).
Kommandos `gasbee_config`/`gasbee_scan_request`, Topic `gasbee_scan`, Felder `src` +
Akku/Flags in `hx`/`gas`. BLE-Scan jetzt passiv (`SENSOR_BLE_SCAN_ITVL/WINDOW`, jetzt 50/50 ms).
Display: Tab „RTC / Temp / Gas“ mit GasBee-Zuordnung.
**Test 2026-10-17:** XIAO-GasBee `F7:8A:BE:C9:83:CB` als Flasche A, ohne HX711-Modul
→ Quelle `ble`, Gewicht/Akku/Flags kommen im Display an. Fix dabei: Veraltungsprüfung
in `gas_source.c` verglich µs mit ms (Wert galt nach 0,61 s als veraltet).
**Scan-Fenster (2026-10-17):** Bei 30/50 ms kamen konstant nur 2/10 Pakete pro seq an
→ `SENSOR_BLE_SCAN_WINDOW` = Intervall (50/50 ms). Ergebnis noch prüfen.
HX711 ohne Modul warnt jetzt nur einmal (Rest DEBUG).
**Offen:** Pakete pro seq im Log (`[Flasche X] seq N: k/10 Pakete`) – fällt k dauerhaft
unter 2–3, Scan-Window auf Intervall anheben. LiPo-Kennlinie am echten Akku prüfen.
`LOG_LEVEL_GASBEE_BLE` temporär INFO – nach Test zurück auf WARN.

### EBL-12V-Relais schaltet nicht – Root Cause gefunden (2026-09-13)
Firmware-seitig erledigt: `SENSOR_PWR_12V_PULSE_MS = 500` (eine Konstante für Boot-/
Laufzeit-/Sleep-Puls), `ctrl.pwr_on` liest wieder GPIO14-Sense (`SENSOR_PWR_12V_SENSE_*`
in `sensor_config.h`, Grace 2500 ms nach Puls). Build ok, noch nicht auf Hardware verifiziert.

**Eigentliche Ursache war nie die Pulslänge:** Schaltplan-Analyse (J7 „LT452 Malibu")
zeigt Q1/Q2 (Treiber für „12V EIN"/„12V AUS") als **IRLML2244 (P-Kanal)** in
High-Side-Beschaltung – legt bei aktiv +12V auf den Pin. Der EBL erwartet dort aber
**Pull-to-GND** (Relaisspule EBL-intern auf +12V, Original-Taster zieht auf Masse).
Ergebnis: keine Spannung über der Spule, Relais schaltet nie – unabhängig von
Pulslänge/Sense. Details + Fix-Vorschlag (Q1/Q2 → IRLML6244 N-Kanal, gleiches Pinout):
`progress.md` („Bekannte Probleme") und `techContext.md` (Abschnitt 12V Bordnetz).
**Update (2026-09-13):** Bauteiltausch durchgeführt – Q1/Q2 (12V EIN/AUS) von
IRLML2244 auf **IRLML6244** (N-Kanal) umbestückt. Die übrigen IRLML2244
(Licht/Pumpe/Multimedia) wurden **entfernt, nicht ersetzt** – diese Kanäle sind
damit hardwareseitig stillgelegt, bis neu bestückt wird (`rs485_set_radio()`
schaltet GPIO13 weiterhin, es hängt aber nichts mehr dran).
**Noch offen:** Testergebnis, ob das EBL-Relais jetzt tatsächlich schaltet, steht
noch aus (nicht zurückgemeldet).

### Display-„Abstürze" erklärt: Portverwechslung, kein Hardware-/Brownout-Defekt (2026-09-13)
Über mehrere Sessions wiederholt aufgetretenes Bild: Display bootet nicht mehr,
Backlight/Buzzer tot, USB-Port „verschwindet". Ursache **gefunden und vom Nutzer
bestätigt**: `/dev/ttyACMx`-Nummern verschieben sich bei jedem Reconnect
(Linux-Standardverhalten) – dadurch wurde wiederholt **die Sensorboard-Firmware
versehentlich auf das Display-Board geflasht**, wenn der Sensorboard-Port gerade
nicht verfügbar war und die Portzuordnung nicht neu geprüft wurde. Erklärt das
scheinbar zufällige, mit Sensorboard-Flashes korrelierte "Zerschießen" des
Displays vollständig. **Eine zuvor diskutierte Brownout-Theorie (Spannungseinbruch
über die gemeinsame Stromversorgung beim Relais-Puls) ist damit hinfällig** –
wurde nirgends persistiert, nur zur Doku hier korrigiert.

**Fix (umgesetzt 2026-10-17, nach erneuter Verwechslung):** udev-Regel
`tools/udev/99-esp32-womo.rules` (installiert in `/etc/udev/rules.d/`) legt per
USB-Seriennummer feste Symlinks an: `/dev/ttyESP_SENSOR`, `/dev/ttyESP_DISPLAY`
(GasBee C3 / XIAO nRF: Platzhalter, Seriennummern noch auslesen). Ursache war
`idf.port` = `/dev/ttyACM0` in **beiden** `firmware/{display,sensorboard}/.vscode/settings.json`
(ESP-IDF-Extension-Buttons). Jetzt `idf.port` + Tasks je Board auf den festen
Symlink; Root-`.vscode/tasks.json` (Windows-Altlast COM5/6) gelöscht. Fehlt das
Board, fehlt der Symlink → Flash schlägt fehl statt falsches Board zu treffen.
Sensorboard-Seriennummer in der Regel war noch die der alten Platine
(`1C:DB:D4:74:8D:E4`) → korrigiert auf `44:1B:F6:CF:04:1C` (neue Platine, per
`udevadm` ausgelesen). Bei Platinentausch Regel nachziehen.
GasBee nRF (XIAO, beliebig viele): allgemeine Regel `ttyGASBEE_<Seriennummer>`
(VID/PID 2fe3:0004), Monitor mit Auswahl `tools/gasbee-monitor.sh`.

### Ruuvi-Außensensor-Debug (offen, 2026-08-25)
Außensensor wird nicht erkannt ("Keine Ruuvi-Tags gefunden!"). Bereits geklärt:
unkommitierter NVS-Config-Umbau (`ruuvi_ble_scanner.c/.h`) ist laut Nutzer
**nicht** die Ursache. Diagnose-Log eingebaut: `ruuvi_on_adv()` loggt jetzt
**jeden** erkannten Ruuvi-DF5-Tag unabhängig vom MAC-Filter (MAC, Temp, RSSI)
– markiert mit `TODO(debug, ...)`, nach Abschluss der Fehlersuche entfernen.
`LOG_LEVEL_RUUVI_BLE` in `sensor_config.h` temporär auf `ESP_LOG_INFO`
(vorher `ESP_LOG_WARN`) – nach Abschluss zurücksetzen.
Nächster Schritt: Nutzer flasht/beobachtet Monitor, prüft ob Außensensor-MAC
überhaupt in `[Scan ]`-Zeilen auftaucht.

**Ergänzender Befund (2026-08-25):** Erster Live-Test zeigte, dass gar kein
Ruuvi-Tag im Log auftaucht UND `ruuvi_scan_request` vom Display keine ACK
bekommt ("waiting for ACK for 3596 ms"), obwohl periodische Topics (bat/tank/
hb) normal ankommen. Nutzer widerspricht der zunächst vermuteten Ursache
"ESP-NOW-Kanal driftet während WiFi-Scans" (Router ist aus) – zu Recht, da
Topics nur einseitig (Sensor→Display, kein ACK nötig) funktionieren, während
`ruuvi_scan_request` einen vollen Umlauf (Display→Sensor Kommando + Sensor→
Display ACK) braucht. Fund: `LOG_LEVEL_SENSORBOARD` (steuert Tags
`espnow_modem` UND `womo_rs485_legacy`) stand auf `ESP_LOG_WARN` → die
Erfolgs-Logs für RX-Empfang (`RX: ...`) und ACK-Versand (`TX ack seq=...`)
waren unsichtbar, wir haben teilweise blind debuggt. Jetzt temporär auf
`ESP_LOG_INFO` gesetzt (sensor_config.h) – nach Abschluss zurücksetzen.
Nächster Schritt: Live-Test zeigt ob `ruuvi_scan_request` beim Sensorboard
ankommt (`RX: ...`) und ob der ACK abgeschickt wird (`TX ack seq=...`) –
daraus folgt ob reiner Funk-Paketverlust (kein Code-Bug) oder ein Empfangs-/
Sendefehler im Code vorliegt.

**Durchbruch (2026-08-25, zweiter Test):** ACK + `ruuvi_scan`-Antwort kamen
diesmal komplett + schnell an (Display-Log: `Sensor ACK für ruuvi_scan_request
(seq=12) status=ok`, 60ms später `Frame: ruuvi_scan`) — ESP-NOW-Transport
funktioniert also grundsätzlich, das war keine Transportstörung. Aber:
`segment_len=42` der Antwort entspricht einer **leeren** `tags:[]`-Liste.
Sensorboard-Bootlog bestätigt: `Ruuvi Scanner aktiv  Innen: deaktiviert
(nicht konfiguriert)  Außen: deaktiviert (nicht konfiguriert)` – NVS-Config
ist nach diesem Reboot komplett leer (beide Slots), nicht nur Außen. Wichtiger:
über das gesamte ~552s-Log seit Boot erscheint **keine einzige** `[Scan ]`-
Debug-Zeile – es wurde seit Boot kein einziges gültiges Ruuvi-DF5-Paket
empfangen, auch nicht vom früher zuverlässigen Innensensor. Verdacht: BLE-Scan
läuft evtl. gar nicht oder empfängt nichts – nicht (mehr) ein reines
Konfigurations-/Filterproblem, da die Scan-Liste unabhängig vom MAC-Filter
jeden Tag erfassen soll. `LOG_LEVEL_BLE_MANAGER` war noch auf `WARN` →
"Scan gestartet"/"Scan-Start fehlgeschlagen"-Logs aus `ble_manager.c` fehlten.
Jetzt ebenfalls auf `ESP_LOG_INFO` gesetzt.
Nächster Schritt: Live-Test zeigt ob "Scan gestartet" beim Boot erscheint und
ob danach irgendwann `[Scan ]`-Zeilen auftauchen (auch ohne Tastendruck,
periodisch bei jedem empfangenen Advertisement).

**Weiterer Fund (2026-08-25, dritter Test, Sensorboard-Bootlog):** Über 139s
seit Boot erscheint weder `ble_manager: BLE Manager initialisiert` noch
`Scan gestartet` noch `Scan-Start fehlgeschlagen` noch `[Scan ]`. Die
Fehler-Warnung `sensor_main: BLE Manager nicht gestartet (err=...)` (würde bei
`ble_manager_init() != ESP_OK` erscheinen) fehlt ebenfalls → `ble_manager_init()`
lief scheinbar durch (ESP_OK), aber sein eigenes Erfolgs-Log fehlt trotzdem.
Verdacht: `on_sync()` (NimBLE-Host-Sync-Callback, startet den eigentlichen Scan)
wird nie aufgerufen → Host kommt nie in synchronisierten Zustand. `on_sync()`
hatte bisher kein Log. Jetzt ergänzt: `ESP_LOGI(TAG, "NimBLE Host sync
abgeschlossen → starte Scan")` ganz am Anfang von `on_sync()`
(`ble_manager.c:150`, TODO-markiert). Test zeigt ob dieser Log jemals erscheint
– falls nicht, ist der NimBLE-Host-Sync selbst die Blockade (z. B. WiFi/BT-
Koexistenz beim gleichzeitigen Boot beider Radios, nicht Kanal-Hopping).

**ROOT CAUSE GEFUNDEN (2026-08-25, vierter Test):** `NimBLE Host sync
abgeschlossen → starte Scan` erscheint (t=2728ms), direkt gefolgt von
`BLE Manager initialisiert` — aber weder `Scan gestartet` noch
`Scan-Start fehlgeschlagen` je danach. Ursache: Race Condition in
`ble_manager_init()` (`ble_manager.c:171-184`) — `nimble_port_freertos_init()`
startet den NimBLE-Host-Task, der bei schnellem Sync sofort `on_sync()` →
`ble_manager_start_scan()` aufruft, **bevor** `s_ready = true` (stand danach)
gesetzt war. Der Not-Ready-Guard in `ble_manager_start_scan()` bricht dann
lautlos ab (kein Log) — der einzige Scan-Start-Versuch seit Boot verpuffte
dadurch immer, da `on_sync()` nur einmal feuert und sonst nichts (keine BLE-
Connects/Disconnects) `ble_manager_start_scan()` erneut aufruft.
**Fix:** `s_ready = true` jetzt VOR `nimble_port_freertos_init()` gesetzt.
Das erklärt vermutlich das komplette Ausbleiben von Ruuvi-Empfang seit der
ESP-NOW-Migration (Timing durch Refactor verändert, Race vorher evtl. nicht
oder seltener getroffen). Alle temporären Debug-Logs (`LOG_LEVEL_RUUVI_BLE`,
`LOG_LEVEL_BLE_MANAGER`, `LOG_LEVEL_SENSORBOARD` auf beiden Boards, `[Scan ]`-
Zeile in `ruuvi_on_adv()`, `on_sync()`-Log) nach erfolgreichem Test wieder
zurücksetzen/entfernen (alle mit `TODO(debug, ...)` markiert).
Nächster Schritt: Neu flashen, prüfen ob jetzt `Scan gestartet` erscheint und
ob danach `[Scan ]`-Zeilen für den Innensensor auftauchen.

**Race Condition bestätigt behoben (2026-08-25):** Scan läuft seitdem
zuverlässig, `[Scan ]`-Zeilen für beide Tags (Innen `E4:6B:66:33:35:08`,
weiterer Tag `CE:F1:47:5C:CC:C9`) erscheinen durchgehend. Zwei Anschluss-
Befunde beim Testen:
1. Vereinzelte `ruuvi_scan_request`/`ruuvi_config`-Kommandos gehen auf der
   Funkstrecke Display→Sensor verloren (per Log-Abgleich bestätigt: Anfrage
   erscheint nie als `RX:` auf dem Sensorboard) — echter, nicht deterministischer
   Paketverlust, kein Logikfehler. Dafür Retry im Display (`RUUVI_SCAN_MAX_RETRY`)
   ergänzt, siehe `display/memory-bank/activeContext.md`.
2. Nutzer berichtet: nach langem Testen findet der Scan irgendwann GAR KEINE
   Ruuvi-Tags mehr (dauerhafte Verschlechterung, nicht nur einzelne Anfragen).
   Verdacht: Speicherleck (BLE-Events bei `filter_duplicates=0` sehr häufig,
   auch von Fremdgeräten). Diagnose ergänzt: `rs485_publish_ctrl()` loggt jetzt
   bei jedem `ctrl`-Topic `esp_get_free_heap_size()` und
   `esp_get_minimum_free_heap_size()` (TODO-markiert, nach Abschluss entfernen).
   Nächster Schritt: Bei langem Testlauf beobachten ob `min_je_frei` kontinuierlich
   fällt (Leck) oder stabil bleibt (Ursache liegt dann woanders, z. B. NimBLE-
   interner Zustand/Scan-Liste).

### Web-Dashboard (in Review)
- SPIFFS-Dashboard (`spiffs/dashboard.html`) mit vollständiger Sensoranzeige und Steuerung
- HTTP-Endpunkte: `/`, `/dashboard.html`, `/horizon.html`, `/api/status`, `/api/data`, `/api/imu`, `/api/cmd`
- mDNS: `Womo-Sensor.local`

### INA226 (kalibriert 2026-05-18)
- `main/sensors/ina226_sensor.c` und `.h`
- Library: `k0i05/esp_ina226` v1.2.7
- `elec`-Topic (5s Intervall) sendet `v_bus`, `i_a`, `p_w`, `v_shunt_mv`
- Shunt: 10 mΩ, MAX_CURRENT: 3 A (Display+Board inkl. Relais, LSB=91 µA)
- Misst NUR Display/Sensorboard-Stromkreis, nicht Fahrzeug-Hauptleitung

## Geänderte Dateien (nicht committed)

| Datei | Änderung |
|---|---|
| `main/CMakeLists.txt` | INA226-Sensor hinzugefügt |
| `main/app_main.c` | `ina226_app_start()` integriert |
| `main/idf_component.yml` | `k0i05/esp_ina226` Dependency |
| `main/network/rs485_modem.c` | `rs485_publish_elec()` Topic-Publisher |
| `main/sensor_config.h` | INA226-Konfiguration |
| `main/sensors/bme680_sensor.c` | Drucktrend-Verbesserungen |
| `spiffs/dashboard.html` | Web-Dashboard |
| `README.md` | INA226, Web-Dashboard, aktualisierte Topic-Tabelle |

## HX711-Ausfall (2026-08-24) – Ursache: Kontaktproblem 3.3V-Stecker
HX711 (Gasfüllstand, DOUT=GPIO47/SCK=GPIO45) fiel mit
`gain switch fehlgeschlagen (ESP_ERR_TIMEOUT)` / `HX711 nicht gefunden` aus.
Zeitlich fiel das mit dem Ruuvi-BLE-Scanner-Commit (`cee65e9`, 2026-08-16)
zusammen, was zunächst auf Funk-/Scheduling-Konkurrenz zwischen NimBLE-Scan
und dem zeitkritischen HX711-Bit-Banging schließen ließ (Verdacht: Core-0-
Kontention mit NimBLE-Host, `sdkconfig: CONFIG_BT_NIMBLE_PINNED_TO_CORE_0`).

**Widerlegt durch Messung:** Der erste Ausfall trat bereits bei ~3 s Boot-Zeit
auf, vor `ble_manager_init()` – zu dem Zeitpunkt lief noch kein BLE-Scan.
Software-Gegenmaßnahmen (Core-1-Pinning für `hx711_task`, Scan-Pause um den
Lesezyklus) wurden getestet und zeigten keine Wirkung, wurden wieder
zurückgerollt. **Tatsächliche Ursache: Kontaktproblem am 3.3V-Stecker des
HX711-Moduls** (per Multimeter bestätigt) – reines Hardwareproblem, keine
Software-Ursache.

Bleibt erhalten: Ready-Guard (`s_ready`-Flag) in `ble_manager.c`, unabhängiger
Bugfix – verhindert NimBLE-Zugriffe vor `nimble_port_init()`, falls künftig
wieder ein Modul vor `ble_manager_init()` auf den Manager zugreift.

## Offene Entscheidungen / TODOs

1. **BNO055 (IMU):** Antwortet nicht im I2C-Scan (erwartet 0x28/0x29, gefunden: 0x40/0x68/0x76/0x77) → Hardwarefehler auf neuer Platine klären
2. **Original WoMo-Tanksensoren:** 4-Stufen Reed-Schwimmer (5 Leitungen) noch nicht implementiert. GPIO17/18 vorgesehen, Widerstandsnetzwerk-Schema in `sensor_config.h` dokumentiert
3. **Kalibrierungs-Web-UI:** HX711/Batterie/Tank-Kalibrierung aktuell hardcodiert in `sensor_config.h`, NVS-Layer + Web-UI fehlt noch
4. **Deep Sleep:** Noch nicht getestet auf neuer Platine

## Letzte Kalibrierungen
- **HX711:** 2026-02-13, Referenz 5×3,6 kg = 18,0 kg
  - Kanal A: OFFSET=-275500, SCALE=0.04004
  - Kanal B: OFFSET=77056, SCALE=-0.15674
- **Batterie ADC:** 2026-05-18 neue Platine
  - SCALE=1.012, OFFSET=0 (beide Kanäle)
  - Referenz: Multimeter 13,0V, Anzeige vorher 17,4V (alter SCALE=1.354 war Q3-Defekt-Ausreißer)
- **BME680 Temp-Offset:** 4,0°C (Board-Eigenerwärmung ESP32-S3)
- **BSEC State Version:** 3 (bei Sensortausch hochzählen!)

## Hardware-Status neue Platine (2026-05-18)
- INA226 ✓ (10 mΩ, 3 A)
- BME680/BME280 ✓
- HX711 ✓
- GPIO7-Logik: HIGH=EIN (Q5+Q4 Treiberkette), nicht mehr invertiert
- BNO055 ✗ — nicht im I2C-Scan, Hardwarefehler offen
