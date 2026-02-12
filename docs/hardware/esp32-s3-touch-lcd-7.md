# ESP32-S3-Touch-LCD-7 (Waveshare)

Kurzübersicht des Boards basierend auf den Waveshare-Wiki-Informationen und den bereits kopierten Inhalten.

## Bilder
- Onboard-Ressourcen (beschriftet): ![Onboard-Ansicht](./ESP32-S3-LCD-7-02.png)

## Kernmerkmale
- ESP32-S3-WROOM-1-N16R8 (16 MB Flash, 8 MB PSRAM, 512 KB SRAM, 384 KB ROM), LX7 Dual-Core bis 240 MHz.
- 7" IPS, 800×480, 65k Farben, kapazitiver Touch (5-Punkt, I²C, Interrupt).
- WLAN 2.4 GHz b/g/n, BLE 5 (onboard Antenne).
- Schnittstellen: CAN, RS485, I²C, UART (umschaltbar), TF/SD, USB Full-Speed (OTG), PH2.0 Li-Ion (Boost auf 5 V, Laderegler CS8501).
- CH422G IO-Expander steuert u. a. Backlight, Touch-Reset, SD-CS, USB/CAN-Select.

## Onboard-Ressourcen (Nummern aus der Waveshare-Grafik)
1. ESP32-S3-WROOM-1-N16R8 (8 MB PSRAM, 16 MB Flash)
2. Display-FPC (LCD)
3. Touch-FPC
4. TF/SD-Slot
5. USB Type-C (USB-OTG)
6. USB-UART Type-C
7. UART-Klemmen (gemeinsam mit 6, umschaltbar per Schalter 15)
8. Sensor-Klemme
9. CAN-Klemme
10. I²C-Klemme
11. RS485-Klemme + Backlight-Boost
12. PH2.0 Li-Ion-Eingang (3.7 V), Boost auf 5 V, Ladestrom via R45 konfigurierbar
13. CAN-Abschlusswahl
14. RS485-Abschlusswahl
15. UART-Select (UART1 / UART2)
16. BOOT-Taster
17. RESET-Taster
18. I²C-Level-Translator (3.3 V / 5 V)
19. DONE (Laden fertig)
20. CHG (Ladevorgang)
21. PWR (Power-LED)

## Pinbelegungen (Auszug)
### LCD (RGB + Sync + Backlight)
- VSYNC: GPIO3
- HSYNC: GPIO46
- PCLK: GPIO7
- DE: GPIO5
- Daten: R3–R7 (GPIO1,2,40,41,42), G2–G7 (GPIO39,45,48,47,21), B3–B7 (GPIO14,38,18,17,10)
- Backlight Enable: CH422G EXIO2 (Druckpunkt DISP)

### Touch (I²C)
- TP_IRQ: GPIO4
- TP_SDA: GPIO8
- TP_SCL: GPIO9
- TP_RST: CH422G EXIO1

### USB (OTG/Device)
- USB_D−: GPIO19
- USB_D+: GPIO20
- USB_SEL: CH422G EXIO5 (Low = USB, sonst CAN)

### TF/SD (SPI)
- MOSI: GPIO11
- SCK: GPIO12
- MISO: GPIO13
- SD_CS: CH422G EXIO4 (Low aktiv)

### RS485
- TXD: GPIO15
- RXD: GPIO16
- Auto-DE/RE-Schaltung onboard

### CAN
- CANTX: GPIO20
- CANRX: GPIO19
- CAN_SEL: CH422G EXIO5 (High = CAN, sonst USB)

### I²C (Shared Bus)
- SDA: GPIO8
- SCL: GPIO9
- Nutzung: Touch, CH422G IO-Expander, externe I²C-Buchse

### Stromversorgung
- PH2.0 Li-Ion 3.7 V, Boost auf 5 V (CS8501, ca. 580 mA Standard-Ladestrom; änderbar via R45).
- LEDs: DONE (voll), CHG (Lädt), PWR (Versorgt).

## Hinweise
- Touch-Reset, SD-CS, Backlight und USB/CAN-Select laufen über den CH422G – immer dessen Pegel berücksichtigen.
- USB/CAN teilen sich GPIO19/20; EXIO5 entscheidet den Modus.
- Backlight Enable über EXIO2 (aktiviert/abschaltet Versorgung des Panels).
- Bei eigener Verkabelung: Level-Translator (Punkt 18) kann 3.3 V oder 5 V bereitstellen.
