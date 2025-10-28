/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "hardware/waveshare_rgb_lcd_port.h"
#include "time/womo_time.h"
#include "gui/womo_theme.h"
#include "network/womo_wifi.h"
#include "storage/womo_sd.h"
#include "sensors/womo_bme680.h"
#include "i2cdev.h"  // i2cdev initialization for esp-idf-lib
#include "sdkconfig.h"
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

// WiFi credentials from Kconfig
#define WIFI_SSID      CONFIG_WOMO_WIFI_SSID
#define WIFI_PASSWORD  CONFIG_WOMO_WIFI_PASSWORD
#define WIFI_MAX_RETRY CONFIG_WOMO_WIFI_MAX_RETRY

// Global LVGL objects
static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *mode_label = NULL;
static lv_obj_t *wifi_label = NULL;
static lv_obj_t *temp_label = NULL;   // Temperature display
static lv_obj_t *humid_label = NULL;  // Humidity display
static lv_obj_t *press_label = NULL;  // Pressure display
static lv_obj_t *bg_img = NULL;  // Background image

// Load background image from SD card
static bool load_background_image(lv_obj_t *screen)
{
    if (!womo_sd_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted, skipping background image");
        return false;
    }
    
    // Try to load Ducato.png from SD card
    const char* img_path = "/sdcard/SDCARD/images/Ducato.png";
    struct stat st;
    
    ESP_LOGI(TAG, "Loading background image: %s", img_path);
    
    if (stat(img_path, &st) != 0) {
        ESP_LOGW(TAG, "Background image not found: %s", img_path);
        return false;
    }
    
    // Open file with ESP-IDF FATFS
    FILE *fp = fopen(img_path, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open image file: %s", img_path);
        return false;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > 5 * 1024 * 1024) {  // Max 5MB
        ESP_LOGE(TAG, "Invalid file size: %ld bytes", file_size);
        fclose(fp);
        return false;
    }
    
    // Allocate buffer for PNG data
    uint8_t *png_data = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!png_data) {
        ESP_LOGE(TAG, "Failed to allocate %ld bytes for PNG", file_size);
        fclose(fp);
        return false;
    }
    
    // Read file into buffer
    size_t bytes_read = fread(png_data, 1, file_size, fp);
    fclose(fp);
    
    if (bytes_read != file_size) {
        ESP_LOGE(TAG, "Read only %d of %ld bytes", bytes_read, file_size);
        heap_caps_free(png_data);
        return false;
    }
    
    ESP_LOGI(TAG, "PNG file loaded: %ld bytes", file_size);
    
    // Create LVGL image descriptor for PNG
    static lv_img_dsc_t img_dsc;
    img_dsc.header.always_zero = 0;
    img_dsc.header.w = 0;  // PNG decoder will determine size
    img_dsc.header.h = 0;  // PNG decoder will determine size
    img_dsc.data_size = file_size;
    img_dsc.header.cf = LV_IMG_CF_RAW_ALPHA;  // Let PNG decoder handle it
    img_dsc.data = png_data;
    
    // Create image object
    bg_img = lv_img_create(screen);
    
    // IMPORTANT: Set as background layer with explicit Z-index
    lv_obj_move_background(bg_img);
    lv_obj_set_style_pad_all(bg_img, 0, 0);
    
    // Position and size
    lv_obj_set_size(bg_img, 800, 480);
    lv_obj_align(bg_img, LV_ALIGN_CENTER, 0, 0);
    
    // Set image source (PNG will be decoded once)
    lv_img_set_src(bg_img, &img_dsc);
    
    // Use PNG's own transparency: Ducato = opaque, free areas = transparent
    // No additional opacity - let PNG alpha channel control transparency
    lv_obj_set_style_img_opa(bg_img, LV_OPA_COVER, 0);
    
    // Ensure image stays in background
    lv_obj_move_to_index(bg_img, 0);
    
    ESP_LOGI(TAG, "Background image applied to screen");
    return true;
}

// Touch event handler
static void screen_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * screen = lv_event_get_target(e);
    
    if (code == LV_EVENT_CLICKED) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_get_act(), &point);
        
        ESP_LOGI(TAG, "Touch at: x=%d, y=%d", point.x, point.y);
        
        // Left side: cycle theme mode
        if (point.x < 400) {
            womo_theme_cycle_mode();
        }
        // Right side: cycle status
        else {
            womo_theme_cycle_status();
        }
        
        // Apply new theme
        womo_theme_apply_to_screen(screen);
        
        // Update labels
        const char* mode_text[] = {"Mode: DAY", "Mode: NIGHT", "Mode: SUNRISE", "Mode: SUNSET"};
        const char* status_text[] = {"Status: OK", "Status: WARNING", "Status: ERROR", "Status: CRITICAL"};
        
        lv_label_set_text(mode_label, mode_text[womo_theme_get_mode()]);
        lv_label_set_text(status_label, status_text[womo_theme_get_status()]);
    }
}

// Timer callback for updating time display
static void time_update_timer_cb(lv_timer_t *timer)
{
    char time_str[32];
    char date_str[32];
    
    // Get current time as string
    if (womo_time_get_string(time_str, sizeof(time_str), "%H:%M:%S") == ESP_OK) {
        lv_label_set_text(time_label, time_str);
    }
    
    // Get current date
    if (womo_time_get_string(date_str, sizeof(date_str), "%d.%m.%Y") == ESP_OK) {
        lv_label_set_text(date_label, date_str);
    }
    
    // Update WiFi status
    if (womo_wifi_is_connected()) {
        int8_t rssi = womo_wifi_get_rssi();
        char wifi_str[32];
        snprintf(wifi_str, sizeof(wifi_str), "WiFi: %d dBm", rssi);
        lv_label_set_text(wifi_label, wifi_str);
    } else {
        lv_label_set_text(wifi_label, "WiFi: Disconnected");
    }
    
    // Update sensor data every 5 seconds (counter % 5 == 0)
    static uint32_t sensor_counter = 0;
    sensor_counter++;
    
    if (sensor_counter % 5 == 0 && womo_bme680_is_initialized()) {
        womo_bme680_data_t sensor_data;
        if (womo_bme680_read(&sensor_data) == ESP_OK && sensor_data.valid) {
            char sensor_str[32];
            
            // Update temperature
            snprintf(sensor_str, sizeof(sensor_str), "%.1f°C", sensor_data.temperature);
            lv_label_set_text(temp_label, sensor_str);
            
            // Update humidity
            snprintf(sensor_str, sizeof(sensor_str), "%.0f%%", sensor_data.humidity);
            lv_label_set_text(humid_label, sensor_str);
            
            // Update pressure
            snprintf(sensor_str, sizeof(sensor_str), "%.0fhPa", sensor_data.pressure);
            lv_label_set_text(press_label, sensor_str);
        }
    }
    
    // Only update theme automatically if auto mode is enabled
    if (womo_theme_is_auto_mode()) {
        womo_theme_update(womo_theme_get_status());
        womo_theme_apply_to_screen(NULL);
    }
}

void app_main()
{
    // Initialize i2cdev FIRST (creates mutexes for all I2C ports)
    // This is needed for esp-idf-lib components (like BME680) that use i2cdev
    ESP_LOGI(TAG, "Initializing i2cdev...");
    ESP_ERROR_CHECK(i2cdev_init());
    
    // Initialize time management
    womo_time_init();
    
    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    womo_wifi_init();
    
    // Initialize theme (default location: Central Europe)
    // TODO: Get location from GPS (Walter Modem)
    womo_theme_init(50.0, 10.0);  // Approximate Germany
    
    // Set sunrise/sunset for Central Europe winter
    womo_theme_set_sun_times(7, 30, 17, 0);
    
    // Initialize display SECOND (this installs I2C bus on I2C_NUM_0 for GT911 touch)
    // i2cdev already created the mutex, now display installs the actual driver
    waveshare_esp32_s3_rgb_lcd_init();
    
    // NOW initialize SD card (will use existing I2C bus for CH422G GPIO expander)
    ESP_LOGI(TAG, "Initializing SD card...");
    if (womo_sd_init() == ESP_OK) {
        ESP_LOGI(TAG, "SD card mounted successfully");
    } else {
        ESP_LOGW(TAG, "SD card mount failed - continuing without SD");
    }
    
    // Initialize BME680 sensor (uses existing I2C bus)
    ESP_LOGI(TAG, "Initializing BME680 sensor...");
    if (womo_bme680_init() == ESP_OK) {
        ESP_LOGI(TAG, "BME680 sensor initialized successfully");
    } else {
        ESP_LOGW(TAG, "BME680 sensor init failed - continuing without sensor");
    }
    
    ESP_LOGI(TAG, "Display WoMo Home Control with Dynamic Theme");
    
    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (lvgl_port_lock(-1)) {
        
        // Get main screen and enable touch events
        lv_obj_t *screen = lv_scr_act();
        lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(screen, screen_event_handler, LV_EVENT_CLICKED, NULL);
        
        // Try to load background image from SD card
        load_background_image(screen);
        
        // Apply initial theme (screen background = full opacity for theme colors)
        womo_theme_update(WOMO_STATUS_OK);
        if (bg_img != NULL) {
            // Screen background stays SOLID (LV_OPA_COVER) to show theme colors
            // Ducato image is semi-transparent (LV_OPA_60) to let colors through
            lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        }
        womo_theme_apply_to_screen(screen);
        
        // Create title
        lv_obj_t * title = lv_label_create(screen);
        lv_label_set_text(title, "WoMo Home Control");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(title, lv_color_black(), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
        
        // Create time display (center)
        time_label = lv_label_create(screen);
        lv_label_set_text(time_label, "--:--:--");
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(time_label, lv_color_black(), 0);
        lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -20);
        
        // Create date display (below time)
        date_label = lv_label_create(screen);
        lv_label_set_text(date_label, "--.--.----");
        lv_obj_set_style_text_font(date_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(date_label, lv_color_black(), 0);
        lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 15);
        
        // Create WiFi status (top right)
        wifi_label = lv_label_create(screen);
        lv_label_set_text(wifi_label, "WiFi: Disconnected");
        lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(wifi_label, lv_color_black(), 0);
        lv_obj_align(wifi_label, LV_ALIGN_TOP_RIGHT, -10, 10);
        
        // Create sensor displays (top left)
        temp_label = lv_label_create(screen);
        lv_label_set_text(temp_label, "--.-°C");
        lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(temp_label, lv_color_black(), 0);
        lv_obj_align(temp_label, LV_ALIGN_TOP_LEFT, 10, 10);
        
        humid_label = lv_label_create(screen);
        lv_label_set_text(humid_label, "--%");
        lv_obj_set_style_text_font(humid_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(humid_label, lv_color_black(), 0);
        lv_obj_align(humid_label, LV_ALIGN_TOP_LEFT, 10, 30);
        
        press_label = lv_label_create(screen);
        lv_label_set_text(press_label, "----hPa");
        lv_obj_set_style_text_font(press_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(press_label, lv_color_black(), 0);
        lv_obj_align(press_label, LV_ALIGN_TOP_LEFT, 10, 50);
        
        // Create mode label (left bottom)
        mode_label = lv_label_create(screen);
        lv_label_set_text(mode_label, "Mode: DAY");
        lv_obj_set_style_text_color(mode_label, lv_color_black(), 0);
        lv_obj_align(mode_label, LV_ALIGN_BOTTOM_LEFT, 20, -20);
        
        // Create status label (right bottom - higher to avoid FPS overlay)
        status_label = lv_label_create(screen);
        lv_label_set_text(status_label, "Status: OK");
        lv_obj_set_style_text_color(status_label, lv_color_black(), 0);
        lv_obj_align(status_label, LV_ALIGN_BOTTOM_RIGHT, -20, -60);
        
        // Create touch hint
        lv_obj_t * hint = lv_label_create(screen);
        lv_label_set_text(hint, "Links: Mode | Rechts: Status");
        lv_obj_set_style_text_color(hint, lv_color_black(), 0);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
        lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 60);
        
        // Create LVGL timer to update time every second
        lv_timer_create(time_update_timer_cb, 1000, NULL);
        
        // Release the mutex
        lvgl_port_unlock();
    }
    
    // Connect to WiFi in background
    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);
    if (womo_wifi_connect(WIFI_SSID, WIFI_PASSWORD, WIFI_MAX_RETRY) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected successfully");
        
        // Sync time via NTP (non-blocking)
        if (womo_time_sync_ntp(true) == ESP_OK) {
            ESP_LOGI(TAG, "Time synchronized via NTP");
        } else {
            ESP_LOGW(TAG, "NTP sync failed, using internal RTC");
        }
    } else {
        ESP_LOGW(TAG, "WiFi connection failed");
    }
    
    ESP_LOGI(TAG, "System running - Touch Control: Left=Mode, Right=Status");
}


