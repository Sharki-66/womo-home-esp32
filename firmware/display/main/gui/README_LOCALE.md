# WoMo Locale System

Multi-language support system for the WoMo Home Control display.

## Features

- **Runtime language switching** between German (DE) and English (EN)
- **Centralized string management** via enum-based string IDs
- **Weekday translation** helper function
- **Touch-activated language toggle** (bottom-left corner)

## Architecture

### Files
- `womo_locale.h` - Header with API definitions, enums, and function declarations
- `womo_locale.c` - Implementation with string tables and lookup functions

### String Tables
All UI strings are stored in a 2D array: `[locale][string_id]`
- Each string ID maps to localized text for each supported language
- Missing strings return "???" as fallback

### String IDs
```c
typedef enum {
    STR_TITLE,           // "WoMo Home Control"
    STR_MODE,            // "Mode:" / "Modus:"
    STR_STATUS,          // "Status:"
    STR_WIFI,            // "WiFi:"
    STR_TEMP,            // "Temp:" / "Temp:"
    STR_HUMID,           // "Humid:" / "Feuchte:"
    STR_PRESS,           // "Press:" / "Druck:"
    STR_GAS,             // "Gas:"
    STR_IMU,             // "IMU:"
    STR_WEIGHT,          // "Weight:" / "Gewicht:"
    STR_BATTERY,         // "Battery:" / "Batterie:"
    STR_TANKS,           // "Tanks:"
    STR_WEEKDAY_SUN,     // "So" / "Sun"
    STR_WEEKDAY_MON,     // "Mo" / "Mon"
    // ... etc
} womo_string_id_t;
```

## Usage

### Initialization
```c
void app_main(void) {
    womo_locale_init();  // Defaults to German (DE)
    // ...
}
```

### Getting Strings
```c
// Simple label
lv_label_set_text(label, womo_locale_get_string(STR_TITLE));

// Formatted string
char buf[40];
snprintf(buf, sizeof(buf), "%s %.1f°C", 
         womo_locale_get_string(STR_TEMP), 
         temperature);
lv_label_set_text(label, buf);

// Weekday (0=Sunday, 6=Saturday)
const char* weekday = womo_locale_get_weekday(day_index);
```

### Changing Language
```c
womo_locale_set(WOMO_LOCALE_EN);  // Switch to English
womo_locale_t current = womo_locale_get();  // Get current locale
```

### Touch Control
Touch the **bottom-left corner** (date area) to toggle between DE/EN.

## Implementation Details

### Default Language
- System starts in **German (DE)** by default
- Can be changed via `womo_locale_init()` or first `womo_locale_set()` call

### Thread Safety
- No internal locking
- If called from LVGL callbacks, caller must hold LVGL port lock

### Memory Footprint
- String tables are `const char*` arrays in flash (read-only)
- Runtime overhead: ~4 bytes (current locale enum)
- No dynamic memory allocation

## Adding New Languages

1. **Update `womo_locale.h`:**
   ```c
   typedef enum {
       WOMO_LOCALE_DE = 0,
       WOMO_LOCALE_EN = 1,
       WOMO_LOCALE_FR = 2,  // NEW
       WOMO_LOCALE_MAX
   } womo_locale_t;
   ```

2. **Update `womo_locale.c`:**
   ```c
   static const char* s_strings[WOMO_LOCALE_MAX][STR_MAX] = {
       [WOMO_LOCALE_DE] = { /* German strings */ },
       [WOMO_LOCALE_EN] = { /* English strings */ },
       [WOMO_LOCALE_FR] = { /* French strings */ },  // NEW
   };
   ```

3. **Update UI** for language selection (if more than 2 languages)

## Adding New Strings

1. **Add enum** in `womo_locale.h`:
   ```c
   typedef enum {
       // ...
       STR_NEW_FEATURE,  // NEW
       STR_MAX
   } womo_string_id_t;
   ```

2. **Add translations** in `womo_locale.c`:
   ```c
   [WOMO_LOCALE_DE] = {
       // ...
       [STR_NEW_FEATURE] = "Neue Funktion",
   },
   [WOMO_LOCALE_EN] = {
       // ...
       [STR_NEW_FEATURE] = "New Feature",
   },
   ```

3. **Use in code**:
   ```c
   lv_label_set_text(label, womo_locale_get_string(STR_NEW_FEATURE));
   ```

## Example: Dynamic Labels

When sensor data arrives via RS485:
```c
void rs485_data_received(const womo_sensor_data_t *data) {
    if (lvgl_port_lock(500)) {
        char buf[60];
        
        // Temperature with localized prefix
        snprintf(buf, sizeof(buf), "%s %.1f°C", 
                 womo_locale_get_string(STR_TEMP), 
                 data->bme680.temperature_c);
        lv_label_set_text(temp_label, buf);
        
        // Works in any language - string changes on language switch
        lvgl_port_unlock();
    }
}
```

## Testing

1. **Build and flash** Display firmware
2. **Touch bottom-left corner** (date area) to toggle DE ↔ EN
3. **Verify** all labels update immediately:
   - WiFi status
   - RS485 status  
   - Sensor labels (on next data packet)
   - Mode/Status labels (on next theme change)

## Future Enhancements

- [ ] Save language preference to NVS (persistent across reboots)
- [ ] Add more languages (French, Spanish, Italian, etc.)
- [ ] UI menu for language selection (instead of touch corner)
- [ ] Date format localization (DD.MM.YYYY vs MM/DD/YYYY)
- [ ] Number format localization (decimal comma vs period)
