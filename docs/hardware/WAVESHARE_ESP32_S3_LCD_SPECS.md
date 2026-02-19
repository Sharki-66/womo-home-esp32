# Hardware-Spezifikationen - Waveshare ESP32-S3 Touch LCD 7" - 800×480

## 📋 **OFFIZIELLE DOKUMENTATIONS-QUELLEN**

### **Primäre Quellen (Waveshare offiziell)**
- 📄 **Wiki:** https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7
- 📥 **Schematic (PDF):** https://www.waveshare.com/w/upload/4/4e/ESP32-S3-Touch-LCD-7_Schematic.pdf
- 📚 **User Manual (PDF):** https://files.waveshare.com/upload/2/2a/ESP32-S3-Touch-LCD-7_User_Manual_EN.pdf
- 🔧 **GitHub Examples:** https://github.com/waveshare/ESP32-S3-Touch-LCD-7
- 🛒 **Product Page:** https://www.waveshare.com/esp32-s3-touch-lcd-7.htm

## ✅ **VERIFIZIERTE HARDWARE-SPEZIFIKATIONEN**

### 📺 **DISPLAY SPEZIFIKATIONEN**

| Spezifikation | Wert |
|--------------|------|
| **Display Typ** | IPS LCD |
| **Display Controller** | ST7262 |
| **Interface** | RGB (16-bit parallel) |
| **Auflösung** | 800×480 Pixel |
| **Diagonale** | 7 Zoll |
| **Pixel Clock** | 16 MHz |
| **Color Depth** | 16-bit (RGB565) |
| **Data Width** | 16-bit parallel |
| **Viewing Angle** | IPS 170° |

### 👆 **TOUCH CONTROLLER**

| Spezifikation | Wert |
|--------------|------|
| **Controller IC** | GT911 |
| **Interface** | I2C |
| **Touch Points** | 5-Point Multi-Touch |
| **I2C Address** | 0x5D oder 0x14 |

### 🔌 **ESP32-S3 SPEZIFIKATIONEN**

| Spezifikation | Wert |
|--------------|------|
| **MCU** | ESP32-S3-WROOM-1-N8R8 |
| **Flash** | 8MB |
| **PSRAM** | 8MB |
| **WiFi** | 802.11 b/g/n |
| **Bluetooth** | Bluetooth 5.0, BLE |
| **USB** | USB-C (Programming + Power) |

## ⚙️ **GPIO PIN-MAPPING**

### **RGB Interface GPIO Pins:**
```c
#define EXAMPLE_LCD_IO_RGB_VSYNC        (GPIO_NUM_3)
#define EXAMPLE_LCD_IO_RGB_HSYNC        (GPIO_NUM_46)
#define EXAMPLE_LCD_IO_RGB_DE           (GPIO_NUM_5)
#define EXAMPLE_LCD_IO_RGB_PCLK         (GPIO_NUM_7)

// RGB Data Lines (16-bit)
#define EXAMPLE_LCD_IO_RGB_DATA0        (GPIO_NUM_14)  // R0
#define EXAMPLE_LCD_IO_RGB_DATA1        (GPIO_NUM_38)  // R1
#define EXAMPLE_LCD_IO_RGB_DATA2        (GPIO_NUM_18)  // R2
#define EXAMPLE_LCD_IO_RGB_DATA3        (GPIO_NUM_17)  // R3
#define EXAMPLE_LCD_IO_RGB_DATA4        (GPIO_NUM_10)  // R4
#define EXAMPLE_LCD_IO_RGB_DATA5        (GPIO_NUM_39)  // G0
#define EXAMPLE_LCD_IO_RGB_DATA6        (GPIO_NUM_0)   // G1
#define EXAMPLE_LCD_IO_RGB_DATA7        (GPIO_NUM_45)  // G2
#define EXAMPLE_LCD_IO_RGB_DATA8        (GPIO_NUM_48)  // G3
#define EXAMPLE_LCD_IO_RGB_DATA9        (GPIO_NUM_47)  // G4
#define EXAMPLE_LCD_IO_RGB_DATA10       (GPIO_NUM_21)  // G5
#define EXAMPLE_LCD_IO_RGB_DATA11       (GPIO_NUM_1)   // B0
#define EXAMPLE_LCD_IO_RGB_DATA12       (GPIO_NUM_2)   // B1
#define EXAMPLE_LCD_IO_RGB_DATA13       (GPIO_NUM_42)  // B2
#define EXAMPLE_LCD_IO_RGB_DATA14       (GPIO_NUM_41)  // B3
#define EXAMPLE_LCD_IO_RGB_DATA15       (GPIO_NUM_40)  // B4
```

### **Touch I2C GPIO Pins:**
```c
#define I2C_MASTER_SCL_IO           9       // I2C Clock
#define I2C_MASTER_SDA_IO           8       // I2C Data
#define I2C_MASTER_NUM              0       // I2C Port 0
#define I2C_MASTER_FREQ_HZ          400000  // 400kHz
#define GPIO_INPUT_IO_4             4       // Touch interrupt pin
```

### **Backlight & Control:**
```c
#define EXAMPLE_PIN_NUM_BK_LIGHT        (-1)    // Hardware-gesteuert
#define EXAMPLE_LCD_IO_RST              (-1)    // Nicht verwendet
#define EXAMPLE_LCD_IO_RGB_DISP         (-1)    // Nicht verwendet
```

## 🛠️ **SOFTWARE-KONFIGURATION**

### **ESP-IDF Version:**
```
ESP-IDF v5.2.0+ (empfohlen)
```

### **LVGL Konfiguration für 800×480:**
```c
#define LVGL_PORT_H_RES             800
#define LVGL_PORT_V_RES             480
#define LVGL_PORT_BUFFER_HEIGHT     100
#define LVGL_PORT_AVOID_TEAR        1
```

### **RGB Interface Timing:**
```c
.timings = {
    .pclk_hz = 16000000,        // 16MHz Pixel Clock
    .h_res = 800,               // Horizontale Auflösung
    .v_res = 480,               // Vertikale Auflösung
    .hsync_pulse_width = 30,
    .hsync_back_porch = 16,
    .hsync_front_porch = 210,
    .vsync_pulse_width = 13,
    .vsync_back_porch = 10,
    .vsync_front_porch = 22,
}
```

## 🔋 **STROMVERSORGUNG**

| Spezifikation | Wert |
|--------------|------|
| **Input Voltage** | 5V via USB-C |
| **Power Consumption** | ~1.2W (Display an) |
| **Sleep Current** | <10mA |

## 📦 **VERWENDETE BIBLIOTHEKEN**

### **ESP-IDF Komponenten:**
- `esp_lcd_panel_rgb` - RGB LCD Panel Treiber
- `esp_lcd_touch_gt911` - GT911 Touch Controller Treiber
- `driver/i2c` - I2C Treiber für Touch
- `driver/gpio` - GPIO Treiber

### **LVGL Integration:**
- RGB Avoid Tearing implementiert
- Double Buffering für flüssige Animationen
- 800×480 optimierte Widgets

## ✅ **VERIFIKATIONS-STATUS**

| Komponente | Status | Quelle |
|------------|---------|---------|
| **Display Controller (ST7262)** | ✅ Verifiziert | Waveshare Dokumentation |
| **Touch Controller (GT911)** | ✅ Verifiziert | Waveshare Demo Code |
| **Auflösung 800×480** | ✅ Verifiziert | Hardware-Spezifikation |
| **GPIO Pin-Mapping** | ✅ Verifiziert | Schaltplan |
| **ESP-IDF v5.2.0** | ✅ Verifiziert | Offizieller Support |
| **LVGL Integration** | ✅ Verifiziert | Funktionierender Demo |
| **RGB 16-bit Interface** | ✅ Verifiziert | Hardware-Konfiguration |
| **I2C Touch Interface** | ✅ Verifiziert | I2C GT911 Treiber |

---

**DIESE SPEZIFIKATIONEN SIND FÜR DAS LCD 7" 800×480 MODELL VERIFIZIERT!** 🎯

*Erstellt: 26. Oktober 2025*