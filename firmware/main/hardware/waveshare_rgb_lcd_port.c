/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "womo_display";

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_GT911
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_ch422g_cfg_handle = NULL;
static i2c_master_dev_handle_t s_ch422g_data_handle = NULL;
static bool s_ch422g_backlight_on = true; // gespiegelt, damit CH422G-Schreibzugriffe (z. B. SD-CS) den BL-Zustand respektieren

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
    write_buf = 0x2C;
    if (ch422g_write_byte(s_ch422g_data_handle, write_buf) != ESP_OK)
    {
        ESP_LOGW(TAG, "Reset command 0x%02X failed", write_buf);
    }
    esp_rom_delay_us(100 * 1000);
    gpio_set_level(GPIO_INPUT_IO_4, 0);
    esp_rom_delay_us(100 * 1000);
    write_buf = 0x2E;
    if (ch422g_write_byte(s_ch422g_data_handle, write_buf) != ESP_OK)
    {
        ESP_LOGW(TAG, "Exit reset command 0x%02X failed", write_buf);
    }
    esp_rom_delay_us(200 * 1000);
}
#endif

// VSYNC event callback function
IRAM_ATTR static bool rgb_lcd_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx)
{
    return lvgl_port_notify_rgb_vsync();
}

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

    ESP_ERROR_CHECK(lvgl_port_init(panel_handle, tp_handle)); // Initialize LVGL with the panel and touch handles

    // Register callbacks for RGB panel events
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
#if EXAMPLE_RGB_BOUNCE_BUFFER_SIZE > 0
        .on_bounce_frame_finish = rgb_lcd_on_vsync_event, // Callback for bounce frame finish
#else
        .on_vsync = rgb_lcd_on_vsync_event, // Callback for vertical sync
#endif
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, NULL)); // Register event callbacks

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

/******************************* Example code **************************************/
static void draw_event_cb(lv_event_t *e) // Draw event callback function 
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e); // Get the draw part descriptor 
    if (dsc->part == LV_PART_ITEMS)
    {                                                                 // If drawing chart items 
        lv_obj_t *obj = lv_event_get_target(e);                       // Get the target object of the event 
        lv_chart_series_t *ser = lv_chart_get_series_next(obj, NULL); // Get the series of the chart 
        uint32_t cnt = lv_chart_get_point_count(obj);                 // Get the number of points in the chart 
        /* Make older values more transparent */
        dsc->rect_dsc->bg_opa = (LV_OPA_COVER * dsc->id) / (cnt - 1); // Set opacity based on the index 

        /* Make smaller values blue, higher values red  */
        lv_coord_t *x_array = lv_chart_get_x_array(obj, ser); // Get the X-axis array 
        lv_coord_t *y_array = lv_chart_get_y_array(obj, ser); // Get the Y-axis array 
        /* dsc->id is the drawing order, but we need the index of the point being drawn dsc->id  */
        uint32_t start_point = lv_chart_get_x_start_point(obj, ser); // Get the start point of the chart 
        uint32_t p_act = (start_point + dsc->id) % cnt;              // Calculate the actual index based on the start point 
        lv_opa_t x_opa = (x_array[p_act] * LV_OPA_50) / 200;         // Calculate X-axis opacity 
        lv_opa_t y_opa = (y_array[p_act] * LV_OPA_50) / 1000;        // Calculate Y-axis opacity 

        dsc->rect_dsc->bg_color = lv_color_mix(lv_palette_main(LV_PALETTE_RED), // Mix colors 
                                               lv_palette_main(LV_PALETTE_BLUE),
                                               x_opa + y_opa);
    }
}

static void add_data(lv_timer_t *timer) // Timer callback to add data to the chart 
{
    lv_obj_t *chart = timer->user_data;                                                                        // Get the chart associated with the timer 
    lv_chart_set_next_value2(chart, lv_chart_get_series_next(chart, NULL), lv_rand(0, 200), lv_rand(0, 1000)); // Add random data to the chart 
}

// This demo UI is adapted from LVGL official example: https://docs.lvgl.io/master/examples.html#scatter-chart
void example_lvgl_demo_ui() // LVGL demo UI initialization function 
{
    lv_obj_t *scr = lv_scr_act();                                              // Get the current active screen 
    lv_obj_t *chart = lv_chart_create(scr);                                    // Create a chart object 
    lv_obj_set_size(chart, 200, 150);                                          // Set chart size 
    lv_obj_align(chart, LV_ALIGN_CENTER, 0, 0);                                // Center the chart on the screen 
    lv_obj_add_event_cb(chart, draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL); // Add draw event callback 
    lv_obj_set_style_line_width(chart, 0, LV_PART_ITEMS);                      /* Remove chart lines  */

    lv_chart_set_type(chart, LV_CHART_TYPE_SCATTER); // Set chart type to scatter 

    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_X, 5, 5, 5, 1, true, 30);  // Set X-axis ticks 
    lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 10, 5, 6, 5, true, 50); // Set Y-axis ticks 

    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_X, 0, 200);  // Set X-axis range 
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000); // Set Y-axis range 

    lv_chart_set_point_count(chart, 50); // Set the number of points in the chart 

    lv_chart_series_t *ser = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y); // Add a series to the chart 
    for (int i = 0; i < 50; i++)
    {                                                                            // Add random points to the chart 
        lv_chart_set_next_value2(chart, ser, lv_rand(0, 200), lv_rand(0, 1000)); // Set X and Y values 
    }

    lv_timer_create(add_data, 100, chart); // Create a timer to add new data every 100ms 
}
