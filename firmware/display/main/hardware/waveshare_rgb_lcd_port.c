/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "esp_lcd_panel_rgb.h"
#include "womo_theme.h"

static const char *TAG = "womo_display";

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_ch422g_cfg_handle = NULL;
static i2c_master_dev_handle_t s_ch422g_data_handle = NULL;
static bool s_ch422g_backlight_on = false; // Boot mit BL=off; wird erst nach dem ersten vollständigen LVGL-Render true

#define GT911_I2C_FREQ_HZ        400000

#define CH422G_ADDR_CFG   0x24
#define CH422G_ADDR_DATA  0x38

static esp_err_t ch422g_write_byte(i2c_master_dev_handle_t handle, uint8_t value)
{
    const int max_attempts = 3;
    for (int attempt = 1; attempt <= max_attempts; ++attempt)
    {
        esp_err_t err = i2c_master_transmit(handle, &value, 1, I2C_MASTER_TIMEOUT_MS);
        if (err == ESP_OK)
        {
            return ESP_OK;
        }

        ESP_LOGW(TAG, "CH422G write 0x%02X failed (try %d/%d): %s", value, attempt, max_attempts, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_FAIL;
}

static esp_err_t ensure_i2c_bus(void)
{
    if (s_i2c_bus)
    {
        return ESP_OK;
    }

    const i2c_master_bus_config_t bus_cfg =
    {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags =
        {
            .enable_internal_pullup = true,
        },
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

static esp_err_t ensure_ch422g_handles(void)
{
    esp_err_t err = ensure_i2c_bus();
    if (err != ESP_OK)
    {
        return err;
    }

    if (!s_ch422g_cfg_handle)
    {
        const i2c_device_config_t cfg =
        {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = CH422G_ADDR_CFG,
            .scl_speed_hz = CH422G_I2C_FREQ_HZ,
        };
        err = i2c_master_bus_add_device(s_i2c_bus, &cfg, &s_ch422g_cfg_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to add CH422G cfg device: %s", esp_err_to_name(err));
            return err;
        }
    }

    if (!s_ch422g_data_handle)
    {
        const i2c_device_config_t cfg =
        {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = CH422G_ADDR_DATA,
            .scl_speed_hz = CH422G_I2C_FREQ_HZ,
        };
        err = i2c_master_bus_add_device(s_i2c_bus, &cfg, &s_ch422g_data_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to add CH422G data device: %s", esp_err_to_name(err));
            return err;
        }
    }

    return ESP_OK;
}

esp_err_t womo_ch422g_assert_sd_cs(void)
{
    if (ensure_ch422g_handles() != ESP_OK)
    {
        ESP_LOGE(TAG, "Unable to assert SD-CS because CH422G handles are unavailable");
        return ESP_FAIL;
    }

    uint8_t cfg = 0x01;
    esp_err_t err = ch422g_write_byte(s_ch422g_cfg_handle, cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "CH422G cfg write failed for SD-CS: %s", esp_err_to_name(err));
        return err;
    }

    // EXIO4 low für SD-CS, Backlight-Bit gemäß aktuellem Zustand lassen
    uint8_t data = s_ch422g_backlight_on ? 0x0E : 0x0A;
    err = ch422g_write_byte(s_ch422g_data_handle, data);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "CH422G data write failed for SD-CS: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "CH422G SD-CS asserted via EXIO4 (data=0x%02X)", data);
    return ESP_OK;
}

// Reset the touch screen
void waveshare_esp32_s3_touch_reset()
{
    if (ensure_ch422g_handles() != ESP_OK)
    {
        ESP_LOGE(TAG, "Unable to reset touch controller because CH422G handles are unavailable");
        return;
    }

    uint8_t write_buf = 0x01;
    if (ch422g_write_byte(s_ch422g_cfg_handle, write_buf) != ESP_OK)
    {
        ESP_LOGW(TAG, "Unable to configure CH422G for touch reset (0x%02X)", write_buf);
    }

    // Reset the touch screen. It is recommended to reset the touch screen before using it.
    // EXIO2 = Backlight: Bit 2 bewusst LOW lassen (0x28 statt 0x2C),
    // damit das Backlight während des Boot-Vorgangs aus bleibt.
    write_buf = 0x28;
    if (ch422g_write_byte(s_ch422g_data_handle, write_buf) != ESP_OK)
    {
        ESP_LOGW(TAG, "Reset command 0x%02X failed", write_buf);
    }
    esp_rom_delay_us(100 * 1000);
    gpio_set_level(GPIO_INPUT_IO_4, 0);
    esp_rom_delay_us(100 * 1000);
    write_buf = 0x2A;
    if (ch422g_write_byte(s_ch422g_data_handle, write_buf) != ESP_OK)
    {
        ESP_LOGW(TAG, "Exit reset command 0x%02X failed", write_buf);
    }
    esp_rom_delay_us(200 * 1000);
}
#endif

// GPIO initialization
void gpio_init(void)
{
    // Zero-initialize the config structure
    gpio_config_t io_conf = {};
    // Disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    // Bit mask of the pins, use GPIO4 here
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    // Set as input mode
    io_conf.mode = GPIO_MODE_OUTPUT;

    gpio_config(&io_conf);
}

// Initialize RGB LCD
esp_err_t waveshare_esp32_s3_rgb_lcd_init()
{
    ESP_LOGI(TAG, "Install RGB LCD panel driver"); // Log the start of the RGB LCD panel driver installation
    esp_lcd_panel_handle_t panel_handle = NULL; // Declare a handle for the LCD panel
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT, // Set the clock source for the panel
        .timings =  {
            .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ, // Pixel clock frequency
            .h_res = EXAMPLE_LCD_H_RES, // Horizontal resolution
            .v_res = EXAMPLE_LCD_V_RES, // Vertical resolution
            .hsync_pulse_width = 4, // Horizontal sync pulse width
            .hsync_back_porch = 8, // Horizontal back porch
            .hsync_front_porch = 8, // Horizontal front porch
            .vsync_pulse_width = 4, // Vertical sync pulse width
            .vsync_back_porch = 8, // Vertical back porch
            .vsync_front_porch = 8, // Vertical front porch
            .flags = {
                .pclk_active_neg = 1, // Active low pixel clock
            },
        },
        .data_width = EXAMPLE_RGB_DATA_WIDTH, // Data width for RGB
        .bits_per_pixel = EXAMPLE_RGB_BIT_PER_PIXEL, // Bits per pixel
        .num_fbs = LVGL_PORT_LCD_RGB_BUFFER_NUMS, // Number of frame buffers
        .bounce_buffer_size_px = EXAMPLE_RGB_BOUNCE_BUFFER_SIZE, // Bounce buffer size in pixels
        .sram_trans_align = 4, // SRAM transaction alignment
        .psram_trans_align = 64, // PSRAM transaction alignment
        .hsync_gpio_num = EXAMPLE_LCD_IO_RGB_HSYNC, // GPIO number for horizontal sync
        .vsync_gpio_num = EXAMPLE_LCD_IO_RGB_VSYNC, // GPIO number for vertical sync
        .de_gpio_num = EXAMPLE_LCD_IO_RGB_DE, // GPIO number for data enable
        .pclk_gpio_num = EXAMPLE_LCD_IO_RGB_PCLK, // GPIO number for pixel clock
        .disp_gpio_num = EXAMPLE_LCD_IO_RGB_DISP, // GPIO number for display
        .data_gpio_nums = {
            EXAMPLE_LCD_IO_RGB_DATA0,
            EXAMPLE_LCD_IO_RGB_DATA1,
            EXAMPLE_LCD_IO_RGB_DATA2,
            EXAMPLE_LCD_IO_RGB_DATA3,
            EXAMPLE_LCD_IO_RGB_DATA4,
            EXAMPLE_LCD_IO_RGB_DATA5,
            EXAMPLE_LCD_IO_RGB_DATA6,
            EXAMPLE_LCD_IO_RGB_DATA7,
            EXAMPLE_LCD_IO_RGB_DATA8,
            EXAMPLE_LCD_IO_RGB_DATA9,
            EXAMPLE_LCD_IO_RGB_DATA10,
            EXAMPLE_LCD_IO_RGB_DATA11,
            EXAMPLE_LCD_IO_RGB_DATA12,
            EXAMPLE_LCD_IO_RGB_DATA13,
            EXAMPLE_LCD_IO_RGB_DATA14,
            EXAMPLE_LCD_IO_RGB_DATA15,
        },
        .flags = {
            .fb_in_psram = 1, // Use PSRAM for framebuffer
        },
    };

    // Create a new RGB panel with the specified configuration
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));

    ESP_LOGI(TAG, "Initialize RGB LCD panel"); // Log the initialization of the RGB LCD panel
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle)); // Initialize the LCD panel

    // ── Framebuffer mit Theme-Hintergrundfarbe vorfüllen ──────────────
    // Das LCD-Panel streamt jetzt bereits aus den PSRAM-Framebuffern via
    // DMA.  Ohne Prefill wäre der Inhalt schwarz (calloc).  Wir füllen
    // beide Framebuffer mit der aktuellen Theme-Farbe (Tag=Hellblau,
    // Nacht=Dunkelblau), sodass das LCD von Beginn an die richtige Farbe
    // zeigt – auch bevor LVGL überhaupt gestartet wird.
    {
        lv_color_t bg = womo_theme_get_background_color();
        /* LVGL v9: lv_color_t → RGB565 via lv_color_to_u16() */
        uint16_t rgb565 = lv_color_to_u16(bg);

        size_t fb_size_bytes = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * sizeof(uint16_t);
#if LVGL_PORT_LCD_RGB_BUFFER_NUMS >= 2
        void *fb0 = NULL, *fb1 = NULL;
        esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &fb0, &fb1);
        if (fb0) {
            uint16_t *p = (uint16_t *)fb0;
            for (size_t i = 0; i < fb_size_bytes / 2; i++) p[i] = rgb565;
        }
        if (fb1) {
            uint16_t *p = (uint16_t *)fb1;
            for (size_t i = 0; i < fb_size_bytes / 2; i++) p[i] = rgb565;
        }
        ESP_LOGI(TAG, "Framebuffer prefilled with theme color 0x%04X", rgb565);
#else
        void *fb0 = NULL;
        esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 1, &fb0);
        if (fb0) {
            uint16_t *p = (uint16_t *)fb0;
            for (size_t i = 0; i < fb_size_bytes / 2; i++) p[i] = rgb565;
        }
        ESP_LOGI(TAG, "Framebuffer prefilled with theme color 0x%04X", rgb565);
#endif
    }

    esp_lcd_touch_handle_t tp_handle = NULL; // Declare a handle for the touch panel
#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911
    ESP_LOGI(TAG, "Initialize I2C bus"); // Log the initialization of the I2C bus
    ESP_ERROR_CHECK(ensure_ch422g_handles()); // Initialize the I2C master and CH422G handles
    ESP_LOGI(TAG, "Initialize GPIO"); // Log GPIO initialization
    gpio_init(); // Initialize GPIO pins
    ESP_LOGI(TAG, "Initialize Touch LCD"); // Log touch LCD initialization
    waveshare_esp32_s3_touch_reset(); // Reset the touch panel

    esp_lcd_panel_io_handle_t tp_io_handle = NULL; // Declare a handle for touch panel I/O
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG(); // Configure I2C for GT911 touch controller
    tp_io_config.scl_speed_hz = GT911_I2C_FREQ_HZ; // Use conservative GT911 clock to avoid NACKs on shared bus

    ESP_LOGI(TAG, "Initialize I2C panel IO"); // Log I2C panel I/O initialization
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_config, &tp_io_handle)); // Create new I2C panel I/O

    ESP_LOGI(TAG, "Initialize touch controller GT911"); // Log touch controller initialization
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES, // Set maximum X coordinate
        .y_max = EXAMPLE_LCD_V_RES, // Set maximum Y coordinate
        .rst_gpio_num = EXAMPLE_PIN_NUM_TOUCH_RST, // GPIO number for reset
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT, // GPIO number for interrupt
        .levels = {
            .reset = 0, // Reset level
            .interrupt = 0, // Interrupt level
        },
        .flags = {
            .swap_xy = 0, // No swap of X and Y
            .mirror_x = 0, // No mirroring of X
            .mirror_y = 0, // No mirroring of Y
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp_handle)); // Create new I2C GT911 touch controller
#endif // CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911

    /* esp_lvgl_port registriert den VSync/Bounce-Callback intern in
     * lvgl_port_add_disp_rgb() – kein eigener Callback mehr nötig. */
    ESP_ERROR_CHECK(womo_lvgl_port_init(panel_handle, tp_handle));

    return ESP_OK; // Return success 
}

/******************************* Turn on the screen backlight **************************************/
esp_err_t wavesahre_rgb_lcd_bl_on()
{
    ESP_RETURN_ON_ERROR(ensure_ch422g_handles(), TAG, "CH422G handles not ready for backlight on");

    s_ch422g_backlight_on = true;

    ESP_LOGI(TAG, "Backlight ON (CH422G)");

    // Configure CH422G to output mode
    uint8_t write_buf = 0x01;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_ch422g_cfg_handle, &write_buf, 1, I2C_MASTER_TIMEOUT_MS), TAG, "CH422G mode write failed");

    // Pull the backlight pin high to light the screen backlight
    write_buf = 0x1E;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_ch422g_data_handle, &write_buf, 1, I2C_MASTER_TIMEOUT_MS), TAG, "CH422G data write failed");
    return ESP_OK;
}

/******************************* Turn off the screen backlight **************************************/
esp_err_t wavesahre_rgb_lcd_bl_off()
{
    ESP_RETURN_ON_ERROR(ensure_ch422g_handles(), TAG, "CH422G handles not ready for backlight off");

    s_ch422g_backlight_on = false;

    ESP_LOGI(TAG, "Backlight OFF (CH422G)");

    // Configure CH422G to output mode
    uint8_t write_buf = 0x01;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_ch422g_cfg_handle, &write_buf, 1, I2C_MASTER_TIMEOUT_MS), TAG, "CH422G mode write failed");

    // Turn off the screen backlight by pulling the backlight pin low
    write_buf = 0x1A;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_ch422g_data_handle, &write_buf, 1, I2C_MASTER_TIMEOUT_MS), TAG, "CH422G data write failed");
    return ESP_OK;
}


