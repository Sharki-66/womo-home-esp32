#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui/ui.h" // EEZ Studio Header

static const char *TAG = "WaveShare_7_UI";

// LCD PINS (WaveShare ESP32-S3-Touch-7)
#define LCD_H_RES              800
#define LCD_V_RES              480
#define LCD_PIXEL_CLOCK_HZ     (16 * 1000 * 1000)

// I2C für CH422G (Backlight & Expander)
#define I2C_MASTER_SCL_IO      9
#define I2C_MASTER_SDA_IO      8

// Callback für LVGL 9 Display Flush
static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    int x1 = area->x1, y1 = area->y1, x2 = area->x2, y2 = area->y2;
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

void init_backlight() {
    // I2C Master Bus Setup
    i2c_master_bus_config_t i2c_bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &bus_handle));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x24,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

    // CH422G Befehl: Setze IO-Output um Backlight zu aktivieren
    uint8_t bk_cmd[2] = {0x08, 0x01}; 
    i2c_master_transmit(dev_handle, bk_cmd, 2, -1);
    ESP_LOGI(TAG, "Backlight enabled via CH422G");
}

#include "esp_lcd_touch_gt911.h"

esp_lcd_touch_handle_t touch_handle = NULL;

// Callback: LVGL fragt hier die Touch-Daten ab
static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    uint16_t touch_x[1];
    uint16_t touch_y[1];
    uint16_t touch_strength[1];
    uint8_t touch_cnt = 0;

    // Daten vom Chip lesen
    esp_lcd_touch_read_data(touch_handle);
    bool pressed = esp_lcd_touch_get_coordinates(touch_handle, touch_x, touch_y, touch_strength, &touch_cnt, 1);

    if (pressed && touch_cnt > 0) {
        data->point.x = touch_x[0];
        data->point.y = touch_y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void init_touch(i2c_master_bus_handle_t bus_handle) {
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = -1, // Wird oft über den CH422G gesteuert
        .int_gpio_num = -1, // Interrupt Pin (optional)
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr = 0x5D, // Standard GT911 Adresse
        .scl_speed_hz = 100000,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .lcd_param_bits = 8,
    };

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    // Wir nutzen den bereits existierenden I2C Bus
    esp_lcd_new_panel_io_i2c_v2(bus_handle, &tp_io_cfg, &tp_io_handle);
    esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_handle);
}

void app_main(void) {
    init_backlight();
// In app_main nach lvgl_port_init() einfügen:

// 1. Touch Hardware initialisieren
init_touch(bus_handle); 

// 2. Touch als Input Device bei LVGL 9 registrieren
lv_indev_t * indev = lv_indev_create();
lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
lv_indev_set_read_cb(indev, lvgl_touch_read_cb);

    // 1. RGB Panel konfigurieren
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 16,
        .psram_trans_align = 64,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num = -1,
        .pclk_gpio_num = 5,
        .vsync_gpio_num = 3,
        .hsync_gpio_num = 46,
        .de_gpio_num = 45,
        .data_gpio_nums = {15, 13, 12, 11, 10, 21, 47, 48, 45, 0, 39, 1, 2, 42, 41, 40},
        .timings = {
            .pclk_hz = LCD_PIXEL_CLOCK_HZ,
            .h_res = LCD_H_RES, .v_res = LCD_V_RES,
            .hsync_back_porch = 40, .hsync_front_porch = 48, .hsync_pulse_width = 40,
            .vsync_back_porch = 32, .vsync_front_porch = 13, .vsync_pulse_width = 1,
        },
        .flags.fb_in_psram = 1,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // 2. LVGL 9 Initialisierung
    lv_init();
    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    
    // Buffer im PSRAM (2 Zeilen a 800 Pixel als Beispiel für Teil-Update)
    size_t buf_size = LCD_H_RES * 50 * sizeof(lv_color16_t);
    void *buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    lv_display_set_buffers(disp, buf1, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(disp, panel_handle);
    lv_display_set_flush_cb(disp, lv_flush_cb);

    // 3. EEZ Studio UI starten
    ui_init();
    ESP_LOGI(TAG, "EEZ Studio UI initialized");

    // 4. Loop
    while (1) {
        uint32_t next_ms = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(next_ms > 0 ? next_ms : 1));
    }
}
