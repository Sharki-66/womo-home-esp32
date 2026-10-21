---
type: testing-guide
title: Hardware-Bringup-Tests
description: Überblick über die eigenständigen ESP-IDF-Testprojekte unter /tests, mit denen einzelne ESP32-Peripherien (I2C, RS485/UART, SD-Karte, ADC, UART-Echo, TWAI/CAN, LVGL-Display-Porting) isoliert validiert werden, bevor ihr Code in die Sensorboard-/Display-Hauptfirmware übernommen wird.
tags: [testing, esp-idf, hardware-bringup, i2c, rs485, uart, sd-card, adc, twai, can, lvgl, manual-testing]
verified:
  - by: openwiki/0.7.2
    at: 2026-10-19T20:49:57.319Z
sources:
  - id: openwiki-source-e30b31d81d8c57860f25bf33
    resource: repo://firmware/display/memory-bank/activeContext.md
  - id: openwiki-source-2a816c0aa0c4635503c8d066
    resource: repo://firmware/sensorboard/memory-bank/activeContext.md
  - id: openwiki-source-137e9f1c7e094cb98b662639
    resource: repo://tests/01_I2C_Test/CMakeLists.txt
  - id: openwiki-source-23712828cecca1a81d05323b
    resource: repo://tests/01_I2C_Test/main/cmd_i2ctools.c
  - id: openwiki-source-e5a7c1b8b75660d55d919bb4
    resource: repo://tests/01_I2C_Test/README.md
  - id: openwiki-source-b5e9de0a67de0e2dadcf3d7c
    resource: repo://tests/01_I2C_Test/sdkconfig.defaults
  - id: openwiki-source-eb789ca194fb4012d28d6114
    resource: repo://tests/02_RS485_Test/CMakeLists.txt
  - id: openwiki-source-d0f5263296c2d54acccf3030
    resource: repo://tests/02_RS485_Test/main/uart_echo_example_main.c
  - id: openwiki-source-d98c24eba15be5416eaa0043
    resource: repo://tests/02_RS485_Test/README.md
  - id: openwiki-source-b20432ae994b70eeacc018e2
    resource: repo://tests/03_SD_Test/CMakeLists.txt
  - id: openwiki-source-9c03c64abab53eec38ab0bcd
    resource: repo://tests/03_SD_Test/main/sd_card.c
  - id: openwiki-source-b102c805ca154d4107f77145
    resource: repo://tests/03_SD_Test/README.md
  - id: openwiki-source-cb29991b419c28d4130245f6
    resource: repo://tests/04_Sensor_AD/CMakeLists.txt
  - id: openwiki-source-3a7afff655605e50ef690d6b
    resource: repo://tests/04_Sensor_AD/main/oneshot_read_main.c
  - id: openwiki-source-817e79a46d132ffc61e2d817
    resource: repo://tests/04_Sensor_AD/README.md
  - id: openwiki-source-929798a749914e6f10a15828
    resource: repo://tests/05_UART_Test/CMakeLists.txt
  - id: openwiki-source-c4a3c8e84709064b137bc6c0
    resource: repo://tests/06_TWAItransmit/CMakeLists.txt
  - id: openwiki-source-f7ed94941c0834ef4532e6c0
    resource: repo://tests/06_TWAItransmit/main/main.c
  - id: openwiki-source-f7e573491561a17c6b3f90c8
    resource: repo://tests/06_TWAItransmit/main/waveshare_twai_port.h
  - id: openwiki-source-30eab1467d2bb7efa3b5452f
    resource: repo://tests/07_TWAIreceive/CMakeLists.txt
  - id: openwiki-source-f2e4be06595ef148d9ad734b
    resource: repo://tests/07_TWAIreceive/main/main.c
  - id: openwiki-source-e7d8caa8008e6fd88270bfdf
    resource: repo://tests/07_TWAIreceive/main/waveshare_twai_port.c
  - id: openwiki-source-154f37a49615d7ddc1717be5
    resource: repo://tests/08_lvgl_Porting/CMakeLists.txt
  - id: openwiki-source-697afa8c89255a10584882ea
    resource: repo://tests/08_lvgl_Porting/main/gas_bottle.h
  - id: openwiki-source-04ec494aaa93339dcab0b8c5
    resource: repo://tests/08_lvgl_Porting/main/idf_component.yml
  - id: openwiki-source-02267345cbee35bcf7798092
    resource: repo://tests/08_lvgl_Porting/main/lvgl_port.h
  - id: openwiki-source-10daf9ea35dd5f2ae53834a7
    resource: repo://tests/08_lvgl_Porting/main/main.c
  - id: openwiki-source-83693f3568c4a6e5b43bf6a8
    resource: repo://tests/08_lvgl_Porting/README.md
generated: { by: "openwiki/0.7.2", at: "2026-10-19T20:49:57.319Z" }
---

## Überblick

Das Verzeichnis `/tests` enthält acht unabhängige ESP-IDF-Projekte, von denen jedes der Inbetriebnahme und manuellen Erprobung einer einzelnen Hardware-Peripherie oder Teilkomponente dient, die irgendwo in der WoMoHome-Sensorboard-/Display-Hardware zum Einsatz kommt (I2C-Bus, RS485/UART-Strecke, SD-Karte über SPI, analoger/ADC-Eingang, UART-Echo, TWAI/CAN-Transceiver sowie LVGL + RGB-LCD + Touch-Porting). Sie sind **nicht** Teil des Sensorboard- oder Display-Firmware-Builds und **keine** automatisierte Regressions-/Unit-Test-Suite — jedes ist eine eigenständige ESP-IDF-Anwendung mit eigener `CMakeLists.txt`, `sdkconfig.defaults` und `main`-Komponente, die für sich allein mit `idf.py build flash monitor` gegen ein nacktes Entwicklungsboard (bzw. bei dem LVGL-Projekt gegen die Ziel-Waveshare-ESP32-S3-Boards) gebaut und geflasht wird, unabhängig von `firmware/sensorboard/` und `firmware/display/`. Siehe [Build, Flash and Config](../operations/build-flash-config.md) dazu, wie die vier Produktions-Firmware-Ziele gebaut werden; die `/tests`-Projekte folgen derselben ESP-IDF-Toolchain, werden aber als eigenständige Top-Level-Projekte konfiguriert und geflasht, nicht als Teil jenes Builds.

Die meisten dieser Projekte stammen aus Espressif-Beispielcode (`esp-idf`/`esp-iot-solution`-Beispiele wie `i2c-tools`, `uart_echo`, `sd_card`, `oneshot_read` sowie die LVGL-„Avoid-Tearing“-RGB-Demo), der weitgehend unverändert oder nur leicht angepasst übernommen wurde (z. B. umbenannte `waveshare_twai_port`-Hilfsfunktionen, ein eigenes `gas_bottle`-LVGL-Widget), um die tatsächliche Board-Verdrahtung und Peripheriewahl des Projekts zu validieren, bevor der eigentliche Produktionstreibercode geschrieben wird.

| # | Verzeichnis | Getestete Peripherie | Basiert auf |
|---|---|---|---|
| 1 | `tests/01_I2C_Test` | I2C-Master-Bus (scan/get/set/dump) | ESP-IDF-Beispiel `i2c-tools` |
| 2 | `tests/02_RS485_Test` | UART-Echo (RS485-Strecken-Inbetriebnahme) | ESP-IDF-Beispiel `uart_echo` |
| 3 | `tests/03_SD_Test` | SD-Karte über SPI (SDSPI + FATFS) | ESP-IDF-Beispiel `sd_card` (SDSPI) |
| 4 | `tests/04_Sensor_AD` | ADC-Oneshot-Lesen + Kalibrierung | ESP-IDF-ADC-Beispiel `oneshot_read` |
| 5 | `tests/05_UART_Test` | UART-Echo | ESP-IDF-Beispiel `uart_echo` |
| 6 | `tests/06_TWAItransmit` | TWAI/CAN-Bus, Master-/Sende-Rolle | ESP-IDF-TWAI-Netzwerkbeispiel, angepasst (`waveshare_twai_port`) |
| 7 | `tests/07_TWAIreceive` | TWAI/CAN-Bus, Empfangs-Rolle | ESP-IDF-TWAI-Netzwerkbeispiel, angepasst (`waveshare_twai_port`) |
| 8 | `tests/08_lvgl_Porting` | LVGL + RGB-LCD (ST7701) + kapazitives Touch (GT911) Porting | esp-iot-solution „RGB avoid tearing“-LVGL-Beispiel, erweitert um ein eigenes `gas_bottle`-Widget |

(Quelle: repo://tests/01_I2C_Test/CMakeLists.txt, repo://tests/02_RS485_Test/CMakeLists.txt, repo://tests/03_SD_Test/CMakeLists.txt, repo://tests/04_Sensor_AD/CMakeLists.txt, repo://tests/05_UART_Test/CMakeLists.txt, repo://tests/06_TWAItransmit/CMakeLists.txt, repo://tests/07_TWAIreceive/CMakeLists.txt, repo://tests/08_lvgl_Porting/CMakeLists.txt)

## Projektstruktur und Unabhängigkeit

Jedes Testverzeichnis ist ein in sich geschlossenes ESP-IDF-Projekt:

- Eine eigene `CMakeLists.txt` mit dem üblichen IDF-Projekt-Boilerplate (`cmake_minimum_required`, `include($ENV{IDF_PATH}/tools/cmake/project.cmake)`, `project(<name>)`), sodass es mit `idf.py` aus dem jeweiligen Verzeichnis heraus gebaut wird, unabhängig von `firmware/sensorboard/CMakeLists.txt` oder `firmware/display/CMakeLists.txt`.
- Eine eigene `sdkconfig.defaults`, die Board-Optionen (Zielchip, Flash-Modus/-Größe, PSRAM-Modus) und projektspezifische Kconfig-Werte (z. B. I2C-Pins) festlegt.
- Eine `main/`-Komponente mit eigener `idf_component.yml`, sofern externe verwaltete Komponenten benötigt werden (z. B. `lvgl/lvgl` und `esp_lcd_touch_gt911` für Projekt 8).
- Keines dieser Projekte referenziert `firmware/shared/womo_config.h` oder sonstigen Code aus `firmware/sensorboard/` oder `firmware/display/`; sie existieren ausschließlich, um eine Peripherie-/Bus-/Board-Kombination isoliert zu validieren, mit beispielhaften README-Anleitungen (Verdrahtungstabellen, `idf.py menuconfig`-Schritte, erwartete Monitor-Ausgaben) statt Assertions oder Pass/Fail-Testharness.

(Quelle: repo://tests/01_I2C_Test/CMakeLists.txt, repo://tests/01_I2C_Test/sdkconfig.defaults, repo://tests/01_I2C_Test/main/idf_component.yml, repo://tests/08_lvgl_Porting/main/idf_component.yml)

## Die einzelnen Projekte

### 01_I2C_Test — I2C-Bus-Werkzeuge

Implementiert das ESP-IDF-Beispiel `i2c-tools`: eine Kommandozeilen-Shell (aufbauend auf der ESP-IDF-Console-Komponente), die die Befehle `i2cconfig`, `i2cdetect`, `i2cget`, `i2cset` und `i2cdump` zum interaktiven Abfragen eines beliebigen, am Board verdrahteten I2C-Geräts bereitstellt (das README demonstriert dies an einem CCS811-Sensor). Mit `i2cconfig` kann der Bediener die SDA-/SCL-GPIOs sowie Port/Frequenz des Busses zur Laufzeit umkonfigurieren, statt einen Neubuild zu benötigen — nützlich bei der Inbetriebnahme neuer I2C-Sensorverdrahtung, ohne eigenen Firmware-Code zu schreiben. Die Standard-Pins werden über `CONFIG_I2C_MASTER_SCL`/`CONFIG_I2C_MASTER_SDA` in `sdkconfig.defaults` gesetzt (GPIO9/GPIO8 bei der hier mitgelieferten ESP32-S3-Konfiguration).

(Quelle: repo://tests/01_I2C_Test/README.md#L1-L18, repo://tests/01_I2C_Test/main/cmd_i2ctools.c, repo://tests/01_I2C_Test/sdkconfig.defaults)

### 02_RS485_Test — UART-Echo zur RS485-Inbetriebnahme

Trotz des Namens handelt es sich bei diesem Projekt um das unveränderte ESP-IDF-Beispiel `uart_echo`: Es konfiguriert einen UART-Port und sendet alle empfangenen Bytes zurück; hier genutzt, um die physische RS485/UART-Verdrahtung und den Transceiver zu validieren, bevor das Verbindungsprotokoll erprobt wird. Die Seite [Display-Architektur](../architecture/display.md) hält fest, dass dieser eigenständige Inbetriebnahme-Test getrennt von der produktiven Sensorboard↔Display-Verbindung gehalten wird, welche in der aktuellen Firmware-Revision über ESP-NOW statt über einen physischen RS485-UART-Bus läuft — der Testprojekt validiert nur den rohen UART-/RS485-Hardwarepfad, nicht das für diese Verbindung beschriebene JSON-über-RS485-Protokoll.

(Quelle: repo://tests/02_RS485_Test/README.md#L1-L30, repo://tests/02_RS485_Test/main/uart_echo_example_main.c)

### 03_SD_Test — SD-Karte über SPI

Unverändertes ESP-IDF-SDSPI-Beispiel: Es bindet eine SD-Karte über `esp_vfs_fat_sdspi_mount` ein (SDSPI-Peripherie-Initialisierung, Karten-Erkennung, FATFS-Mount, VFS-Registrierung — die Karte wird automatisch formatiert, falls das Mounten fehlschlägt), gibt dann Karteninformationen aus (Name, Typ, Kapazität, maximale Frequenz) und erprobt `fopen`/`fprintf`, `stat`, `unlink` und Umbenennen über die POSIX-/VFS-Schicht, um zu bestätigen, dass das Dateisystem nutzbar ist. Unterstützt SDSC-/SDHC-/SDXC-Karten und dokumentiert Standard-SPI-Pinbelegungen je Zielchip, konfigurierbar über `idf.py menuconfig` oder direkt im Quellcode.

(Quelle: repo://tests/03_SD_Test/README.md#L1-L47, repo://tests/03_SD_Test/main/sd_card.c, repo://tests/03_SD_Test/main/sd_card_example_main.c)

### 04_Sensor_AD — ADC-Oneshot-Lesen

Unverändertes ESP-IDF-`oneshot_read`-ADC-Beispiel, genutzt zur Validierung der analogen Sensorverdrahtung (z. B. Spannungsteiler oder analoge Sensoren, die einen ADC1-/ADC2-Kanal speisen). Demonstriert den ADC-Oneshot-Modus-Treiber zusammen mit der ADC-Kalibrierungs-API, um rohe ADC-Zählwerte unter Verwendung des Kurvenanpassungs-Kalibrierungsschemas in Millivolt umzurechnen; fällt mit einer Warnung auf rohe/unkalibrierte Messwerte zurück, falls die Kalibrierungs-eFuse-Bits des Boards nicht gebrannt sind. Die Pin-/Kanalauswahl erfolgt direkt in `oneshot_read_main.c`.

(Quelle: repo://tests/04_Sensor_AD/README.md#L1-L62, repo://tests/04_Sensor_AD/main/oneshot_read_main.c)

### 05_UART_Test — UART-Echo

Eine zweite Instanz des ESP-IDF-Beispiels `uart_echo` (gleicher Mechanismus wie 02_RS485_Test), genutzt zur Validierung einer einfachen UART-Schnittstelle — z. B. zum Zurücksenden von Bytes von einem externen USB-zu-Seriell-Adapter — einschließlich optionaler Hardware-Flusskontrolle (RTS/CTS)-Verdrahtung, unabhängig von der oben beschriebenen RS485-spezifischen Inbetriebnahme.

(Quelle: repo://tests/05_UART_Test/README.md#L1-L48)

### 06_TWAItransmit / 07_TWAIreceive — TWAI/CAN-Bus-Inbetriebnahme

Ein zusammengehöriges Paar von Projekten, abgeleitet vom ESP-IDF-TWAI-(CAN)-Netzwerkbeispiel, aufgeteilt in eine Sende-/Master-Rolle (`06_TWAItransmit`) und eine Empfangs-Rolle (`07_TWAIreceive`), die beide die ESP-IDF-`driver/twai.h`-API hinter einem kleinen `waveshare_twai_port`-Hilfsmodul (`waveshare_twai_init`, `waveshare_twai_transmit`/`waveshare_twai_receive`) kapseln. Die beiden Projekte sind dafür gedacht, gleichzeitig auf zwei Boards zu laufen, die an denselben CAN-Bus angeschlossen sind — eines mit dem Sendeprojekt, eines mit dem Empfangsprojekt geflasht —, um die TWAI-Transceiver-Verdrahtung und das Bus-Framing Ende-zu-Ende zu validieren. Der Port-Header zieht außerdem I2C-Treiber-Header/Definitionen (GPIO9/GPIO8, 400 kHz) neben der TWAI-Pin-Konfiguration hinzu, was nahelegt, dass dasselbe Board für die TWAI- und die I2C-Inbetriebnahme genutzt wird.

(Quelle: repo://tests/06_TWAItransmit/main/main.c, repo://tests/06_TWAItransmit/main/waveshare_twai_port.h, repo://tests/07_TWAIreceive/main/main.c, repo://tests/07_TWAIreceive/main/waveshare_twai_port.c)

### 08_lvgl_Porting — LVGL + RGB-LCD + Touch-Porting

Das umfangreichste der acht Projekte: ein esp-iot-solution-„RGB avoid tearing“-LVGL-Beispiel für ein ESP32-S3-Board, das ein **ST7701**-RGB-Interface-LCD-Panel (800×480) mit einem **GT911**-kapazitiven Touch-Controller ansteuert, festgelegt ausschließlich auf `idf_target = esp32s3`. Anders als die übrigen sieben Projekte deklariert es verwaltete Komponenten-Abhängigkeiten in `main/idf_component.yml` (`lvgl/lvgl` v8.3.x, `esp_lcd_touch_gt911`), die in einen lokalen `components/`-Baum (`espressif__esp_lcd_touch`, `espressif__esp_lcd_touch_gt911`, `lvgl__lvgl`) sowie eine `dependencies.lock`-Datei aufgelöst werden, und bringt neben `sdkconfig.defaults` auch einen eingecheckten `sdkconfig`-/`build/`-Baum mit.

Strukturell:
- `waveshare_rgb_lcd_port.c/.h` initialisiert das RGB-LCD-Panel und die Hintergrundbeleuchtung (`waveshare_esp32_s3_rgb_lcd_init`, `wavesahre_rgb_lcd_bl_on/off`).
- `lvgl_port.c/.h` stellt die generische LVGL-Porting-Schicht bereit: eine eigene FreeRTOS-Task treibt `lv_timer_handler()` über einen `esp_timer`-gesteuerten Takt an, abgesichert durch einen Mutex (`lvgl_port_lock`/`lvgl_port_unlock`), da LVGL-Aufrufe nicht threadsicher sind — jede andere Task, die LVGL-Objekte berührt, muss denselben Lock nehmen. Pufferplatzierung (PSRAM vs. interner SRAM) und der Anti-Tearing-Modus sind per Kconfig wählbar (`CONFIG_EXAMPLE_LVGL_PORT_BUF_PSRAM`/`_INTERNAL`, `CONFIG_EXAMPLE_LVGL_PORT_AVOID_TEAR_ENABLE`).
- `gas_bottle.c/.h` ist ein eigenes LVGL-Widget (nicht aus dem ursprünglichen Beispiel), das eine Gasflaschen-Füllstandsgrafik für ein 800×480-Panel rendert, mit einer Zuordnung von Gewicht zu Füllstandsprozent (grün >60 %, gelb 30–60 %, rot <30 %) und einem konfigurierbaren Maximalgewicht (11 kg). `main.c`s `app_main()` initialisiert das Panel, nimmt den LVGL-Lock und ruft einen lokalen `create_gas_display()`-Hilfsaufruf auf, der zwei `gas_bottle_t`-Widgets mit Beispielgewichten instanziiert — dies ist ein UI-/Porting-Prototyp für die Gasflaschen-Gewichtsanzeige, eigenständig gebaut und ausgeführt, bevor entsprechende LVGL-Bildschirme in die eigentliche Display-Firmware geschrieben werden, und keine wiederverwendbare Bibliothek, die mit `firmware/display/` geteilt wird.

(Quelle: repo://tests/08_lvgl_Porting/README.md#L1-L23, repo://tests/08_lvgl_Porting/main/idf_component.yml, repo://tests/08_lvgl_Porting/CMakeLists.txt, repo://tests/08_lvgl_Porting/main/lvgl_port.h#L1-L60, repo://tests/08_lvgl_Porting/main/gas_bottle.h, repo://tests/08_lvgl_Porting/main/main.c)

## Verhältnis zur Hauptfirmware und zur Testpraxis

- Keines dieser acht Projekte ist in einen Build von `firmware/sensorboard/` oder `firmware/display/` eingebunden; sie sind Prototyping-/Validierungs-Sandboxen. Code- oder Verdrahtungswissen, das hier validiert wird (Pin-Belegungen, Treiberkonfiguration, LVGL-Porting-Muster), fließt in die entsprechende Produktions-Firmware ein — wird dort aber nicht direkt wiederverwendet —, außer dort, wo Dateien von Hand kopiert/angepasst werden (wie beim `gas_bottle`-Widget und den `waveshare_twai_port`-Hilfsfunktionen).
- Das Repository verfügt über **keine automatisierte CI-Testsuite, die Firmware-Logik ausübt** — es gibt keinen von CI aufgerufenen Unit-Test- oder Hardware-in-the-Loop-Harness. Die Verifikation des Firmware-Verhaltens erfolgt manuell und hardwarenah: Ingenieure flashen ein Board, erproben es physisch und halten das Ergebnis als datierten Fortschrittseintrag in `memory-bank/activeContext.md` der jeweiligen Firmware fest, wobei Einträge explizit z. B. als „(auf Hardware getestet)“ gekennzeichnet werden, sobald sie hardwareverifiziert sind, versus „(gebaut, nicht auf Hardware getestet)“ bzw. „(gebaut, noch nicht auf Hardware getestet)“, wenn erst kompiliert, aber noch nicht verifiziert wurde.

(Quelle: repo://firmware/display/memory-bank/activeContext.md#L1-L11, repo://firmware/sensorboard/memory-bank/activeContext.md#L1-L26)

## Verwandte Seiten

- [Display-Architektur](../architecture/display.md) — produktive Sensorboard↔Display-Verbindung (ESP-NOW), welcher der RS485-Inbetriebnahme-Test (02) zeitlich vorausgeht bzw. die er unabhängig davon validiert.
- [Sensorboard-Architektur](../architecture/sensorboard.md) — Produktions-Firmware, in die diese Peripherie-Inbetriebnahmen einfließen, gegen die sie aber nicht gebaut werden.
- [Build, Flash and Config](../operations/build-flash-config.md) — wie die vier Produktions-Firmware-Ziele gebaut und geflasht werden, zum Vergleich mit diesen eigenständigen Testprojekten.
</content>
