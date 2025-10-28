# Hardware Datasheets & Dokumentation

**Stand:** 2025-10-26

---

## 🖥️ Zentrale Module

### Waveshare ESP32-S3 Touch LCD/AMOLED 7"

**Offizielle Ressourcen:**
- 📄 **Wiki Touch AMOLED 7" (800×1280):** https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-7
- 🔧 **GitHub Waveshare Components:** https://github.com/waveshareteam/Waveshare-ESP32-components
- 🛒 **Product Page AMOLED:** https://www.waveshare.com/esp32-s3-touch-amoled-7.htm
- 📚 **LVGL Community Guide:** https://forum.lvgl.io/t/get-started-with-waveshare-esp32-s3-7inch-800x480-capacitive-i2c-interface-with-5-point-touch/19722

### Wichtige Spezifikationen

- **AMOLED:** 800×1280 AMOLED Display


---

### DPTechnics Walter Modem

**Offizielle Ressourcen:**
- 📥 **Datasheet (PDF):** https://www.quickspot.io/datasheet/walter_datasheet.pdf
- 📚 **Product Page:** https://www.dptechnics.com/en/products/walter.html
- 🔧 **ESP-IDF Component:** https://components.espressif.com/components/dptechnics/walter-modem
- 🔧 **GitHub Arduino Library:** https://github.com/QuickSpot/walter-arduino
- 📖 **Nordic Zephyr Docs:** https://docs.nordicsemi.com/bundle/ncs-2.9.0/page/zephyr/boards/dptechnics/walter/doc/index.html

---

## 🌡️ Sensoren

### BME280 (Temperature/Humidity/Pressure)

**Hersteller:** Bosch Sensortec

- 📥 **Datasheet (PDF):** https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
- 📚 **Product Page:** https://www.bosch-sensortec.com/products/environmental-sensors/humidity-sensors-bme280/
- 🔧 **Adafruit Library:** https://github.com/adafruit/Adafruit_BME280_Library

**Backup Links:**
- https://www.alldatasheet.com/datasheet-pdf/pdf/2163285/BOSCH/BME280.html
- https://datasheets.b-cdn.net/files/BME280-Bosch-Tools-datasheet-119732019.pdf

---

### INA226 (Current/Voltage Monitor)

**Hersteller:** Texas Instruments

- 📥 **Datasheet (PDF):** https://www.ti.com/lit/ds/symlink/ina226.pdf
- 📚 **Product Page:** https://www.ti.com/product/INA226
- 🔧 **GitHub Driver:** https://github.com/wollewald/INA226_WE

**Backup Links:**
- https://www.alldatasheet.com/datasheet-pdf/pdf/419932/ti1/ina226.html
- https://www.digikey.com/en/htmldatasheets/production/856663/0/0/1/ina226aidgst.html

---

### BNO055 (9-Axis IMU)

**Hersteller:** Bosch Sensortec

- 📥 **Datasheet (PDF):** https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bno055-ds000.pdf
- 📚 **Product Page:** https://www.bosch-sensortec.com/products/smart-sensors/bno055/
- 🔧 **Adafruit Library:** https://github.com/adafruit/Adafruit_BNO055

**Backup Links:**
- https://www.alldatasheet.com/datasheet-pdf/pdf/1132074/BOSCH/BNO055.html
- https://datasheets.b-cdn.net/files/BNO055-Bosch-Tools-datasheet-127364294.pdf

---

### ADS1115 (16-bit ADC)

**Hersteller:** Texas Instruments

- 📥 **Datasheet (PDF):** https://www.ti.com/lit/ds/symlink/ads1115.pdf
- 📚 **Product Page:** https://www.ti.com/product/ADS1115
- 🔧 **Adafruit Library:** https://github.com/adafruit/Adafruit_ADS1X15

**Backup Links:**
- https://cdn-shop.adafruit.com/datasheets/ads1115.pdf
- https://www.alldatasheet.com/datasheet-pdf/download/2001463/TI/ADS1115.html

---

### HX711 (24-bit ADC for Load Cells)

**Hersteller:** Avia Semiconductor

- 📥 **Datasheet (PDF):** https://cdn.sparkfun.com/datasheets/Sensors/ForceFlex/hx711_english.pdf
- 🔧 **GitHub Library:** https://github.com/bogde/HX711
- 🔧 **SparkFun GitHub:** https://github.com/sparkfun/HX711-Load-Cell-Amplifier

**Backup Links:**
- https://github.com/sparkfun/HX711-Load-Cell-Amplifier/blob/master/datasheets/hx711F_EN.pdf
- http://www.handsontec.com/dataspecs/module/HX711.pdf

---

## 🔌 I2C Infrastructure

### PCA9548A (8-Channel I2C Multiplexer)

**Hersteller:** Texas Instruments / NXP

- 📥 **Datasheet TI (PDF):** https://www.ti.com/lit/ds/symlink/pca9548a.pdf
- 📥 **Datasheet NXP (PDF):** https://www.nxp.com/docs/en/data-sheet/PCA9548A.pdf
- 🔧 **Adafruit Library:** https://github.com/adafruit/Adafruit_TCA9548A

**Backup Links:**
- https://www.alldatasheet.com/datasheet-pdf/pdf/113864/PHILIPS/PCA9548A.html
- https://cdn-learn.adafruit.com/downloads/pdf/adafruit-pca9548-8-channel-stemma-qt-qwiic-i2c-multiplexer.pdf

---

### PCF8575 (16-bit I2C GPIO Expander)

**Hersteller:** Texas Instruments

- 📥 **Datasheet (PDF):** https://www.ti.com/lit/ds/symlink/pcf8575.pdf
- 📚 **Product Page:** https://www.ti.com/product/PCF8575
- 🔧 **GitHub Library:** https://github.com/xreef/PCF8575_library

---

## ⚡ Stromversorgung

### Votronic Tank-Sensoren

**Hersteller:** Votronic

- 📚 **Product Info:** https://www.votronic.de/index.php/de/
- 📥 **Katalog (PDF):** https://www.votronic.de/downloads/katalog-deutsch.pdf

---

## 🔧 ESP32-S3 Ressourcen

### ESP32-S3 Official Documentation

- 📥 **Datasheet (PDF):** https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
- 📥 **Technical Reference (PDF):** https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf
- 📚 **ESP-IDF Programming Guide:** https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/
- 📚 **API Reference:** https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/
- 🔧 **GitHub ESP-IDF:** https://github.com/espressif/esp-idf
- 🔧 **Examples:** https://github.com/espressif/esp-idf/tree/master/examples

---

## 📚 Software Libraries

### LVGL (Display GUI Library)

- 📚 **Documentation:** https://docs.lvgl.io/
- 🔧 **GitHub:** https://github.com/lvgl/lvgl
- 📖 **Examples:** https://docs.lvgl.io/master/examples.html
- 📖 **Widgets:** https://docs.lvgl.io/master/widgets/index.html

### FreeRTOS

- 📚 **Documentation:** https://www.freertos.org/Documentation/RTOS_book.html
- 📚 **ESP-IDF FreeRTOS:** https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/freertos.html
- 🔧 **GitHub:** https://github.com/FreeRTOS/FreeRTOS-Kernel

### cJSON (JSON Parser)

- 🔧 **GitHub:** https://github.com/DaveGamble/cJSON
- 📚 **ESP-IDF Component:** Bereits integriert in ESP-IDF

---

## 📥 Download-Empfehlung

**Erstelle einen Ordner für PDFs:**
```bash
mkdir -p docs/hardware/datasheets
cd docs/hardware/datasheets
```

**Wichtigste Downloads:**
```bash
# ESP32-S3
wget https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf
wget https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf

# Walter Modem
wget https://www.quickspot.io/datasheet/walter_datasheet.pdf

# Sensoren
wget https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf
wget https://www.ti.com/lit/ds/symlink/ina226.pdf
wget https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bno055-ds000.pdf
wget https://www.ti.com/lit/ds/symlink/ads1115.pdf
wget https://cdn.sparkfun.com/datasheets/Sensors/ForceFlex/hx711_english.pdf

# I2C Infrastructure
wget https://www.ti.com/lit/ds/symlink/pca9548a.pdf
wget https://www.ti.com/lit/ds/symlink/pcf8575.pdf
```

**Speichere sie lokal** - dann hast du sie auch offline! 💾

---

## 📝 Zusätzliche Ressourcen

### Community & Support

- **ESP32 Forum:** https://esp32.com/
- **Reddit r/esp32:** https://reddit.com/r/esp32
- **LVGL Forum:** https://forum.lvgl.io/
- **Arduino Forum:** https://forum.arduino.cc/

### Tools

- **ESP Flash Download Tool:** https://www.espressif.com/en/support/download/other-tools
- **Fritzing Parts:** https://fritzing.org/
- **KiCad:** https://www.kicad.org/

---

**Stand:** 2025-10-26  
**Version:** 3.0 (vollständig korrigiert)  
**Autor:** Sharki-66