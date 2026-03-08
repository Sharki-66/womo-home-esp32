# LVGL v8 → v9 Migration (Display Firmware)

> Branch: `feature/lvgl9-migration` | Stand: 06.03.2026 | Status: **noch nicht gestartet**

## Ist-Zustand

- LVGL **8.4.0** als lokale Kopie in `components/lvgl__lvgl/`, konfiguriert über **Kconfig** (`LV_CONF_SKIP=y`, kein `lv_conf.h`).
- Display: RGB LCD 800×480, 16-bit RGB565, Tear-Avoidance Mode 3 (Double-Buffer + Direct-Mode), LVGL-Task Core 1, Prio 4, 4–12 ms Delay.
- Touch: GT911 (I2C) via `esp_lcd_touch`.
- Custom `lvgl_port.c` (640 Zeilen): eigener Flush-Callback (5 Varianten), Dirty-Area-Tracking mit internen LVGL-Strukturen (`disp->inv_p`, `_lv_refr_get_disp_refreshing()`), eigener LVGL-Task, Indev-Driver.
- Custom Fonts: 6× Montserrat-German (12/14/16/20/24 px) in `main/gui/fonts/`, v8-Format.

## Ziel

- LVGL **v9** + **`espressif/esp_lvgl_port` v2** (aktuell v2.7.1) als Managed Component.
- `esp_lvgl_port` ersetzt den kompletten custom `lvgl_port.c` → ~640 Zeilen werden zu ~20 Zeilen Config.
- Referenz-Beispiel: `esp-bsp/components/esp_lvgl_port/examples/rgb_lcd/` (800×480 + GT1151, nahezu identisch zu unserem Setup).

## Phasen / Aufwand (geschätzt 3–4 Tage)

### Phase 1 – Dependencies + Driver-Layer (1,5 Tage) ← **Startpunkt**
1. **`idf_component.yml`**: lokale `lvgl__lvgl`-Kopie entfernen, `lvgl ^9` + `espressif/esp_lvgl_port ^2.7.1` als Managed Components eintragen.
2. **`lvgl_port.c`** (~640 Zeilen → ~20 Zeilen): kompletter Rewrite mit `lvgl_port_add_disp_rgb()` + `lvgl_port_add_touch()`. CH422G-Backlight und Touch-Wake-Callback separat halten.
3. **Kconfig / sdkconfig**: LVGL-v9-Menuconfig, Farbformat RGB565. `lv_api_map_v8.h` Compatibility-Layer aktivieren, damit der Rest des Projekts zunächst ohne weitere Änderungen kompiliert.

> ✅ Validierungspunkt: Build erfolgreich + Display läuft fehlerfrei auf Hardware (kein Tearing).

### Phase 2 – Custom Fonts (0,5 Tage)
- Alle 6 Fonts in `main/gui/fonts/` mit dem **v9-Font-Converter** neu generieren (Format hat sich geändert):
  - `lv_font_montserrat_12_german.c`
  - `lv_font_montserrat_14_german.c`
  - `lv_font_montserrat_16_german.c`
  - `lv_font_montserrat_20_german.c`
  - `lv_font_montserrat_24_german.c`
  - `lv_font_material_16.c`

### Phase 3 – Widget-Umbenennungen (1 Tag)
Mechanische Ersetzungen (~190 Stellen), schrittweise Ablösung des `lv_api_map_v8.h`-Layers:

| API alt → neu | Anzahl Stellen | Dateien |
|---|---|---|
| `lv_coord_t` → `int32_t` | 45 | 9 |
| `lv_obj_clear_flag` → `lv_obj_remove_flag` | 63 | 8 |
| `lv_btn_create` → `lv_button_create` | 18 | 3 |
| `lv_scr_act()` → `lv_screen_active()` | 14 | 6 |
| `LV_BTNMATRIX_CTRL_*` → `LV_BUTTONMATRIX_CTRL_*` | 15 | 1 |
| `lv_obj_del` → `lv_obj_delete` | 9 | 6 |
| `lv_event_get_target` → `lv_event_get_target_obj` | 6 | 3 |
| `lv_img_*` → `lv_image_*` | 13 | 2 |
| `lv_msgbox` (API komplett neu) | 9 | 1 |
| Sonstige (je 1×) | 6 | div. |

### Phase 4 – Spezialfälle (0,5 Tage)
- `lv_spinner_create(parent, speed, arc)` → `lv_spinner_create(parent)` + `lv_spinner_set_anim_params()`
- `lv_msgbox` API komplett überarbeitet
- `lv_keyboard_set_map()` Signatur geändert
- Flush-Callback Signatur: `lv_color_t *color_map` → `uint8_t *px_map`
- `lv_color_t` intern jetzt RGB888 (3 Byte), Farbformat per `lv_display_set_color_format()`
- **Touch-Wake-Callback**: `indev->driver->read_timer` existiert in v9 nicht mehr → muss neu gelöst werden (z. B. über LVGL-v9-Indev-Callbacks).

## Betroffene Dateien

### 🔴 Aufwand HOCH – kompletter Rewrite / Ersatz
| Datei | Grund |
|---|---|
| `main/hardware/lvgl_port.c` | ~640 Zeilen komplett durch `esp_lvgl_port` v2 ersetzen. Nutzt `lv_disp_drv_t`, `_lv_refr_get_disp_refreshing()`, `disp->inv_p`, `indev->driver->read_timer`, `lv_color_t *color_map` Flush-Callback – alles v9-inkompatibel. |
| `main/hardware/waveshare_rgb_lcd_port.c/.h` | `lv_coord_t` (2×), `lv_scr_act()` (1×), `lv_event_get_target` (1×), direkter Framebuffer-Zugriff |

### 🟠 Aufwand MITTEL – viele Umbenennungen
| Datei | Betroffene APIs |
|---|---|
| `main/main.c` | `lv_coord_t` (17×), `lv_btn_create` (5×), `lv_obj_clear_flag` (21×), `lv_obj_del` (4×), `lv_scr_act()` (8×), `lv_disp_get_hor_res` (1×), `lv_indev_get_act` (1×), `lv_timer_del` (1×), `lv_msgbox` (9×), `lv_img_*` (9×), `LV_LABEL_LONG_DOT` (1×), `lv_obj_set_style_img_opa` (1×) |
| `main/gui/womo_connectivity_modal.c` | `lv_btn_create` (6×), `lv_obj_clear_flag` (12×), `LV_BTNMATRIX_CTRL_*` (15×), `lv_obj_del` (1×), `lv_event_get_target` (2×), `lv_scr_act()` (1×), `lv_indev_get_act` (1×), `lv_spinner_create` (1×), `lv_keyboard_set_map` (2×), `LV_LABEL_LONG_DOT` (1×) |
| `main/gui/womo_settings_modal.c` | `lv_btn_create` (7×), `lv_obj_clear_flag` (5×), `lv_event_get_target` (3×) |

### 🟡 Aufwand GERING – wenige Stellen
| Datei | Betroffene APIs |
|---|---|
| `main/gui/womo_battery.c/.h` | `lv_coord_t` (4×), `lv_obj_clear_flag` (7×), `lv_obj_del` (1×) |
| `main/gui/womo_gas_bottle.c/.h` | `lv_coord_t` (9×), `lv_obj_clear_flag` (6×), `lv_obj_del` (1×) |
| `main/gui/womo_tank.c/.h` | `lv_coord_t` (10×), `lv_obj_clear_flag` (6×), `lv_obj_del` (1×) |
| `main/gui/womo_weather.c/.h` | `lv_coord_t` (2×), `lv_obj_clear_flag` (3×), `lv_img_*` (4×) |
| `main/gui/womo_router_leds_modal.c` | `lv_obj_clear_flag` (3×), `lv_obj_del` (1×), `lv_scr_act()` (2×) |
| `main/gui/womo_theme.c` | `lv_scr_act()` (1×) |

### 🔵 Fonts – Neugenerierung nötig (v8-Format → v9-Format)
Alle 6 Custom-Fonts in `main/gui/fonts/` müssen mit dem v9-Font-Converter neu gebaut werden.

### ⚪ Nicht betroffen
`main/network/`, `main/rs485/`, `main/storage/`, `main/time/`

## Risiken

| Risiko | Einschätzung | Maßnahme |
|---|---|---|
| Tearing nach Flush-Umstellung | Hoch | Sofort auf Hardware validieren nach Phase 1 |
| Touch-Wake-Callback (`read_timer` weg) | Mittel | v9-Indev API prüfen; notfalls eigener Timer |
| Font-Compiler v9 Toolchain | Niedrig | `lv_font_conv` npm-Tool, gut dokumentiert |
| `lv_msgbox` komplett neu | Niedrig | Nur 1 Datei (`main.c`), 9 Aufrufe |
| Kein v9-Beispiel für Waveshare LCD-7 | Niedrig | Espressif RGB-LCD-Beispiel nahezu identisch |

## Git-Workflow

```bash
# Zur Migration wechseln
git checkout feature/lvgl9-migration

# Zurück zum lauffähigen v8-Stand
git checkout main

# Aktuellen Branch anzeigen
git branch

# Nach erfolgreichem Phase-1-Build pushen
git push origin feature/lvgl9-migration
```

> ⚠️ Vor dem Branch-Wechsel sicherstellen, dass keine ungespeicherten Änderungen offen sind (`git status`).

## Nützliche Referenzen

- [esp_lvgl_port v2 README](https://github.com/espressif/esp-bsp/tree/master/components/esp_lvgl_port)
- [esp_lvgl_port RGB-LCD Beispiel](https://github.com/espressif/esp-bsp/tree/master/components/esp_lvgl_port/examples/rgb_lcd)
- [LVGL v9 Migration Guide (offiziell)](https://docs.lvgl.io/9.0/migration_guides/v8_v9.html)
- [lv_api_map_v8.h (Kompatibilitäts-Layer)](https://github.com/lvgl/lvgl/blob/master/src/lv_api_map_v8.h)
