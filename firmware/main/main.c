/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "hardware/waveshare_rgb_lcd_port.h"
#include "time/womo_time.h"
#include "gui/womo_theme.h"
#include "gui/womo_locale.h"
#include "gui/womo_gas_bottle.h"
#include "gui/womo_weather.h"
#include "gui/womo_battery.h"
#include "gui/womo_tank.h"
#include "gui/womo_fonts_german.h"
#include "network/womo_wifi.h"
#include "network/womo_weather_http.h"
#include "storage/womo_sd.h"
#include "rs485/womo_rs485_display.h"
#include "i2cdev.h"  // i2cdev for CH422G GPIO expander on display
#include "sdkconfig.h"
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "womo_main";

#define RS485_MISSING_THRESHOLD 2

static const char *PLACEHOLDER_GAS = "Q -- kOhm";
static const char *PLACEHOLDER_PRESSURE = "---- hPa";
static const char *PLACEHOLDER_HUMIDITY = "--.-- %";
static const char *PLACEHOLDER_TEMPERATURE = "--.- °C";


// WiFi credentials from Kconfig
#define WIFI_SSID      CONFIG_WOMO_WIFI_SSID
#define WIFI_PASSWORD  CONFIG_WOMO_WIFI_PASSWORD
#define WIFI_MAX_RETRY CONFIG_WOMO_WIFI_MAX_RETRY

// Global LVGL objects
static lv_obj_t *title_label = NULL;
static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *wifi_label = NULL;
static lv_obj_t *temp_label = NULL;   // Temperature display
static lv_obj_t *humid_label = NULL;  // Humidity display
static lv_obj_t *press_label = NULL;  // Pressure display
static lv_obj_t *gas_label = NULL;    // Gas resistance display
static lv_obj_t *air_title_label = NULL; // Air value heading
static lv_obj_t *imu_label = NULL;    // Orientation display
static lv_obj_t *tank_label = NULL;   // Tank levels
static lv_obj_t *gps_label = NULL;    // GPS position
static lv_obj_t *bg_img = NULL;  // Background image
static lv_obj_t *rs485_debug_label = NULL; // RS485 debug status
static womo_weather_t *weather_widget = NULL; // Weather widget
static womo_battery_t *main_battery = NULL;   // Battery 1 widget
static womo_battery_t *secondary_battery = NULL; // Battery 2 widget

// Gas bottle widgets
static womo_gas_bottle_t *gas_bottle_a = NULL; // Gas bottle A (HX711 channel A)
static womo_gas_bottle_t *gas_bottle_b = NULL; // Gas bottle B (HX711 channel B)

// Water tank widgets
static womo_tank_t *fresh_water_tank = NULL;
static womo_tank_t *grey_water_tank = NULL;

// RS485 packet counter
static uint32_t rs485_packet_count = 0;

static struct {
    uint16_t hx711;
    uint16_t battery;
    uint16_t bme680;
    uint16_t tank;
} rs485_missing_counter = {0};

// RS485 data callback
static void rs485_data_received(const womo_sensor_data_t *data, void *user_data);
static void openweather_update_cb(const womo_weather_http_data_t *data, void *user_data);
static womo_weather_condition_t map_openweather_condition(int weather_id, bool is_night);
static womo_weather_condition_t map_day_condition_to_night(womo_weather_condition_t condition);

static void apply_text_theme_colors(void)
{
    lv_color_t text_color = womo_theme_is_daytime() ? lv_color_black() : lv_color_white();

    if (title_label) lv_obj_set_style_text_color(title_label, text_color, 0);
    if (time_label) lv_obj_set_style_text_color(time_label, text_color, 0);
    if (date_label) lv_obj_set_style_text_color(date_label, text_color, 0);
    if (wifi_label) lv_obj_set_style_text_color(wifi_label, text_color, 0);
    if (air_title_label) lv_obj_set_style_text_color(air_title_label, text_color, 0);
    if (gas_label) lv_obj_set_style_text_color(gas_label, text_color, 0);
    if (press_label) lv_obj_set_style_text_color(press_label, text_color, 0);
    if (humid_label) lv_obj_set_style_text_color(humid_label, text_color, 0);
    if (temp_label) lv_obj_set_style_text_color(temp_label, text_color, 0);
    if (imu_label) {
        lv_obj_set_style_text_color(imu_label, lv_color_white(), 0);
        lv_obj_set_style_bg_color(imu_label, lv_color_hex(0x1F3B6F), 0);
        lv_obj_set_style_bg_opa(imu_label, LV_OPA_COVER, 0);
    }
    if (tank_label) lv_obj_set_style_text_color(tank_label, text_color, 0);
    if (gps_label) lv_obj_set_style_text_color(gps_label, text_color, 0);
    if (fresh_water_tank) womo_tank_set_text_color(fresh_water_tank, text_color);
    if (grey_water_tank) womo_tank_set_text_color(grey_water_tank, text_color);
}


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
    
    if (code == LV_EVENT_CLICKED) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_get_act(), &point);
        
        ESP_LOGI(TAG, "Touch at: x=%d, y=%d", point.x, point.y);
        
        // Bottom left corner (date area): toggle language
        if (point.x < 200 && point.y > 400) {
            womo_locale_t current = womo_locale_get();
            womo_locale_t next = (current == WOMO_LOCALE_DE) ? WOMO_LOCALE_EN : WOMO_LOCALE_DE;
            womo_locale_set(next);
            ESP_LOGI(TAG, "Language switched to: %s", (next == WOMO_LOCALE_DE) ? "DE" : "EN");
            
            // Immediately update all static labels
            if (wifi_label && !womo_wifi_is_connected()) {
                lv_label_set_text(wifi_label, womo_locale_get_string(STR_WIFI_DISCONNECTED));
            }
            if (rs485_packet_count == 0 && rs485_debug_label) {
                lv_label_set_text(rs485_debug_label, womo_locale_get_string(STR_RS485_WAITING));
            }
            // Timer callback will update time/date with new language on next cycle
        }
        // Right side: cycle status (keep for system status simulation)
        else if (point.x > 400) {
            womo_theme_cycle_status();
            
            // Update status label with new status
            const womo_string_id_t status_ids[] = {STR_STATUS_OK, STR_STATUS_WARNING, STR_STATUS_ERROR, STR_STATUS_CRITICAL};
            char status_buf[40];
            snprintf(status_buf, sizeof(status_buf), "%s %s", 
                    womo_locale_get_string(STR_STATUS), 
                    womo_locale_get_string(status_ids[womo_theme_get_status()]));
            lv_label_set_text(status_label, status_buf);
        }
        // Note: Theme mode is now fully automatic based on real time - no manual control needed
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
    
    // Get current date with localized weekday (Mo 04.11.2025)
    char weekday_en[16];
    char full_date[128];
    
    if (womo_time_get_string(weekday_en, sizeof(weekday_en), "%w") == ESP_OK &&
        womo_time_get_string(date_str, sizeof(date_str), "%d.%m.%Y") == ESP_OK) {
        int day_index = atoi(weekday_en);  // 0=Sunday, 1=Monday, ...
        const char* weekday_str = womo_locale_get_weekday(day_index);
        snprintf(full_date, sizeof(full_date), "%s  %s", weekday_str, date_str);
        lv_label_set_text(date_label, full_date);
    }
    
    // Update WiFi status (2 lines: RSSI first, then SSID)
    if (womo_wifi_is_connected()) {
        char ssid[33];
        int8_t rssi = womo_wifi_get_rssi();
        char wifi_str[80];
        
        if (womo_wifi_get_ssid(ssid, sizeof(ssid)) == ESP_OK) {
            snprintf(wifi_str, sizeof(wifi_str), "%s %d dBm\n%s", 
                    womo_locale_get_string(STR_WIFI), rssi, ssid);
        } else {
            snprintf(wifi_str, sizeof(wifi_str), "%s %d dBm", 
                    womo_locale_get_string(STR_WIFI), rssi);
        }
        lv_label_set_text(wifi_label, wifi_str);
    } else {
        lv_label_set_text(wifi_label, womo_locale_get_string(STR_WIFI_DISCONNECTED));
    }
    
    // Update sensor data every 5 seconds (counter % 5 == 0)
    static uint32_t sensor_counter = 0;
    sensor_counter++;
    
    // Sensor data now comes via RS485 from Walter - no local sensor reading needed
    
    // Only update theme automatically if auto mode is enabled (but much less frequently!)
    // Update theme only every 30 seconds to reduce CPU load (30s is enough for smooth twilight)
    if (womo_theme_is_auto_mode() && (sensor_counter % 30 == 0)) {
        womo_theme_update(womo_theme_get_status());
        apply_text_theme_colors();
        womo_theme_apply_to_screen(NULL);
    }
}

static womo_weather_condition_t map_day_condition_to_night(womo_weather_condition_t condition)
{
    switch (condition) {
        case WEATHER_CLEAR:
        case WEATHER_SUNNY:
            return WEATHER_NT_CLEAR;
        case WEATHER_PARTLYSUNNY:
        case WEATHER_PARTLYCLOUDY:
            return WEATHER_NT_PARTLYCLOUDY;
        case WEATHER_MOSTLYSUNNY:
            return WEATHER_NT_MOSTLYSUNNY;
        case WEATHER_MOSTLYCLOUDY:
        case WEATHER_CLOUDY:
            return WEATHER_NT_CLOUDY;
        case WEATHER_CHANCERAIN:
            return WEATHER_NT_CHANCERAIN;
        case WEATHER_RAIN:
            return WEATHER_NT_RAIN;
        case WEATHER_CHANCESNOW:
            return WEATHER_NT_CHANCESNOW;
        case WEATHER_SNOW:
            return WEATHER_NT_SNOW;
        case WEATHER_CHANCESLEET:
            return WEATHER_NT_CHANCESLEET;
        case WEATHER_SLEET:
            return WEATHER_NT_SLEET;
        case WEATHER_CHANCETSTORMS:
            return WEATHER_NT_CHANCETSTORMS;
        case WEATHER_TSTORMS:
            return WEATHER_NT_TSTORMS;
        case WEATHER_FLURRIES:
            return WEATHER_NT_FLURRIES;
        case WEATHER_FOG:
            return WEATHER_NT_FOG;
        case WEATHER_HAZY:
            return WEATHER_NT_HAZY;
        default:
            return WEATHER_NT_UNKNOWN;
    }
}

static womo_weather_condition_t map_openweather_condition(int weather_id, bool is_night)
{
    womo_weather_condition_t condition = WEATHER_UNKNOWN;

    if (weather_id >= 200 && weather_id <= 232) {
        condition = WEATHER_TSTORMS;
    } else if (weather_id >= 300 && weather_id <= 321) {
        condition = WEATHER_CHANCERAIN;
    } else if (weather_id >= 500 && weather_id <= 504) {
        condition = WEATHER_RAIN;
    } else if (weather_id == 511) {
        condition = WEATHER_SLEET;
    } else if (weather_id >= 520 && weather_id <= 531) {
        condition = WEATHER_CHANCERAIN;
    } else if (weather_id >= 600 && weather_id <= 602) {
        condition = WEATHER_SNOW;
    } else if (weather_id >= 611 && weather_id <= 616) {
        condition = WEATHER_SLEET;
    } else if (weather_id >= 620 && weather_id <= 622) {
        condition = WEATHER_FLURRIES;
    } else if (weather_id == 701 || weather_id == 741) {
        condition = WEATHER_FOG;
    } else if (weather_id == 711 || weather_id == 721 || weather_id == 731 ||
               weather_id == 751 || weather_id == 761 || weather_id == 762) {
        condition = WEATHER_HAZY;
    } else if (weather_id == 771 || weather_id == 781) {
        condition = WEATHER_TSTORMS;
    } else if (weather_id == 800) {
        condition = WEATHER_SUNNY;
    } else if (weather_id == 801) {
        condition = WEATHER_PARTLYCLOUDY;
    } else if (weather_id == 802) {
        condition = WEATHER_MOSTLYSUNNY;
    } else if (weather_id == 803) {
        condition = WEATHER_MOSTLYCLOUDY;
    } else if (weather_id == 804) {
        condition = WEATHER_CLOUDY;
    } else {
        condition = WEATHER_UNKNOWN;
    }

    if (is_night) {
        return map_day_condition_to_night(condition);
    }
    return condition;
}

static void openweather_update_cb(const womo_weather_http_data_t *data, void *user_data)
{
    if (!data || !data->valid) {
        return;
    }

    womo_weather_condition_t condition = map_openweather_condition(data->weather_id, data->is_night);
    int16_t temperature = (int16_t)lrintf(data->temperature_c);

    ESP_LOGI(TAG, "OpenWeatherMap update: id=%d (%s), %.1f°C, night=%s",
             data->weather_id,
             data->description,
             data->temperature_c,
             data->is_night ? "yes" : "no");

    if (weather_widget && lvgl_port_lock(-1)) {
        womo_weather_set_condition(weather_widget, condition, data->is_night);
        if (weather_widget->temp_label) {
            womo_weather_set_temperature(weather_widget, temperature);
        }
        lvgl_port_unlock();
    }
}

void app_main()
{
    // Initialize i2cdev FIRST (needed by display for CH422G GPIO expander)
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
    
    // Initialize display (uses I2C for touch controller)
    waveshare_esp32_s3_rgb_lcd_init();
    
    // NOW initialize SD card (will use existing I2C bus for CH422G GPIO expander)
    ESP_LOGI(TAG, "Initializing SD card...");
    if (womo_sd_init() == ESP_OK) {
        ESP_LOGI(TAG, "SD card mounted successfully");
    } else {
        ESP_LOGW(TAG, "SD card mount failed - continuing without SD");
    }
    
    // Initialize RS485 communication (receives data from Walter)
    ESP_LOGI(TAG, "Initializing RS485 display receiver...");
    if (womo_rs485_display_init() == ESP_OK) {
        ESP_LOGI(TAG, "RS485 initialized - will receive data from Walter");
        womo_rs485_set_data_callback(rs485_data_received, NULL);
    } else {
        ESP_LOGW(TAG, "RS485 init failed - continuing without external sensors");
    }
    
    ESP_LOGI(TAG, "Display WoMo Home Control with Dynamic Theme");
    
    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (lvgl_port_lock(-1)) {
        
        // Get main screen and enable touch events
        lv_obj_t *screen = lv_scr_act();
        lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
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
        
        // Initialize locale system
        womo_locale_init();
        
        // Test deutsche Schriftarten
        womo_test_german_fonts();
        
    // Create title - höher positioniert
    title_label = lv_label_create(screen);
    lv_label_set_text(title_label, womo_locale_get_string(STR_TITLE));
    lv_obj_set_style_text_font(title_label, WOMO_FONT_TITLE, 0);
    lv_obj_set_style_text_color(title_label, lv_color_black(), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 5);
        
    // Create time display - direkt unter Titel, feste Breite damit stabil
    // Create license plate style frame for time display (smaller for 20px font)
    // Create time label below the title (no frame)
    time_label = lv_label_create(screen);
    lv_label_set_text(time_label, "--:--:--");
    lv_obj_set_style_text_font(time_label, WOMO_FONT_LARGE, 0);
    lv_obj_set_style_text_color(time_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(time_label, LV_ALIGN_TOP_MID, 0, 35);  // Direkt unter dem Titel
    lv_label_set_long_mode(time_label, LV_LABEL_LONG_CLIP);
        
        // Create date display - links unten (statt Mode)
        date_label = lv_label_create(screen);
        lv_label_set_text(date_label, "--.--.----");
        lv_obj_set_style_text_font(date_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(date_label, lv_color_black(), 0);
        lv_obj_align(date_label, LV_ALIGN_BOTTOM_LEFT, 20, -20);
        
        // Create WiFi status (top left) - moved from right
        wifi_label = lv_label_create(screen);
        lv_label_set_text(wifi_label, womo_locale_get_string(STR_WIFI_DISCONNECTED));
        lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(wifi_label, lv_color_black(), 0);
        lv_obj_set_style_text_align(wifi_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(wifi_label, LV_ALIGN_TOP_LEFT, 10, 10);
        
        // Weather data (top right) - Gas first, all one font size larger
    char init_buf[40];

    air_title_label = lv_label_create(screen);
    lv_label_set_text(air_title_label, "Luftwerte aussen");
    lv_obj_set_style_text_font(air_title_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(air_title_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(air_title_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(air_title_label, LV_ALIGN_TOP_RIGHT, -10, 10);

    gas_label = lv_label_create(screen);
    snprintf(init_buf, sizeof(init_buf), "Q -- kOhm");
    lv_label_set_text(gas_label, init_buf);
    lv_obj_set_style_text_font(gas_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(gas_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(gas_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(gas_label, LV_ALIGN_TOP_RIGHT, -10, 35);

    press_label = lv_label_create(screen);
    snprintf(init_buf, sizeof(init_buf), "---- hPa");
    lv_label_set_text(press_label, init_buf);
    lv_obj_set_style_text_font(press_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(press_label, lv_color_black(), 0);
    lv_obj_set_style_text_align(press_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(press_label, LV_ALIGN_TOP_RIGHT, -10, 60);

        humid_label = lv_label_create(screen);
    snprintf(init_buf, sizeof(init_buf), "--.-- %%");
        lv_label_set_text(humid_label, init_buf);
        lv_obj_set_style_text_font(humid_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(humid_label, lv_color_black(), 0);
        lv_obj_set_style_text_align(humid_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(humid_label, LV_ALIGN_TOP_RIGHT, -10, 85);

        temp_label = lv_label_create(screen);
    snprintf(init_buf, sizeof(init_buf), "--.- °C");
        lv_label_set_text(temp_label, init_buf);
        lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(temp_label, lv_color_black(), 0);
        lv_obj_set_style_text_align(temp_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(temp_label, LV_ALIGN_TOP_RIGHT, -10, 110);

    // Andere Sensoren in die Mitte
    imu_label = lv_label_create(screen);
    snprintf(init_buf, sizeof(init_buf), "%s --", womo_locale_get_string(STR_IMU));
    lv_label_set_text(imu_label, init_buf);
    lv_label_set_long_mode(imu_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(imu_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(imu_label, lv_color_white(), 0);
    lv_obj_set_width(imu_label, 280);
    lv_obj_align(imu_label, LV_ALIGN_CENTER, 0, 20);
    lv_obj_move_foreground(imu_label);
    lv_obj_set_style_bg_color(imu_label, lv_color_hex(0x1F3B6F), 0);
    lv_obj_set_style_bg_opa(imu_label, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(imu_label, 6, 0);
    lv_obj_set_style_pad_hor(imu_label, 8, 0);
    lv_obj_set_style_pad_ver(imu_label, 4, 0);
    lv_obj_set_style_border_width(imu_label, 0, 0);
    
    // Tank label
    tank_label = lv_label_create(screen);
    snprintf(init_buf, sizeof(init_buf), "%s -- / --", womo_locale_get_string(STR_TANKS));
    lv_label_set_text(tank_label, init_buf);
    lv_obj_set_style_text_font(tank_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tank_label, lv_color_black(), 0);
    lv_obj_align(tank_label, LV_ALIGN_CENTER, 0, 110);
    
    // GPS label
    gps_label = lv_label_create(screen);
    lv_label_set_text(gps_label, "GPS: --");
    lv_obj_set_style_text_font(gps_label, WOMO_FONT_SENSOR, 0);
    lv_obj_set_style_text_color(gps_label, lv_color_black(), 0);
    lv_obj_align(gps_label, LV_ALIGN_CENTER, 0, 140);
    
    // German test label removed per user request
    
    // Create water tank widgets (positioned above the gas bottles)
    fresh_water_tank = womo_tank_create(screen, 65, 110, WOMO_TANK_FRESH);
    if (fresh_water_tank) {
        womo_tank_set_caption(fresh_water_tank, "Frischwasser");
        womo_tank_set_no_data(fresh_water_tank);
    } else {
        ESP_LOGW(TAG, "Failed to create fresh water tank widget");
    }

    grey_water_tank = womo_tank_create(screen, 135, 110, WOMO_TANK_GREY);
    if (grey_water_tank) {
        womo_tank_set_caption(grey_water_tank, "Abwasser");
        womo_tank_set_no_data(grey_water_tank);
    } else {
        ESP_LOGW(TAG, "Failed to create grey water tank widget");
    }

    // Create gas bottle widgets (20px weiter nach links)
    // Display height is 480px, so 480 - 250 = 230px from top
    gas_bottle_a = womo_gas_bottle_create(screen, 70, 230);  // Gas bottle A (90 - 20)
    gas_bottle_b = womo_gas_bottle_create(screen, 140, 230);  // Gas bottle B (160 - 20)
    
    // Set weights for bottles: 10.1 kg = 0%, 21 kg = 100%
    if (gas_bottle_a) {
        womo_gas_bottle_set_empty_weight(gas_bottle_a, 10.1f);  // Empty bottle (0%)
        womo_gas_bottle_set_full_weight(gas_bottle_a, 21.0f);   // Full bottle (100%)
        womo_gas_bottle_set_cap_label(gas_bottle_a, "V");
    }
    if (gas_bottle_b) {
        womo_gas_bottle_set_empty_weight(gas_bottle_b, 10.1f);  // Empty bottle (0%)
        womo_gas_bottle_set_full_weight(gas_bottle_b, 21.0f);   // Full bottle (100%)
        womo_gas_bottle_set_cap_label(gas_bottle_b, "H");
    }
    
    // Create weather widget in top-right corner (icon sits beneath the weather values)
    weather_widget = womo_weather_create(screen);
    if (weather_widget) {
        // Set demo weather condition (cloudy with rain chance)
    bool theme_is_day = womo_theme_is_daytime();
    womo_weather_set_condition(weather_widget, WEATHER_CHANCERAIN, !theme_is_day);
        womo_weather_set_temperature(weather_widget, 18);  // 18°C demo temperature
        ESP_LOGI(TAG, "Weather widget created successfully");
    } else {
        ESP_LOGW(TAG, "Failed to create weather widget");
    }
    
    // Create first battery widget (Battery 1) - at bottom edge
    main_battery = womo_battery_create(screen, 250, 420);  // Bottom edge: y=420 (480-60 for battery height)
    if (main_battery) {
        // Set 12V battery voltage range: 10.5V (0%) to 14.4V (100%)
        womo_battery_set_voltage_range(main_battery, 10.5f, 14.4f);
        womo_battery_set_no_data(main_battery);
        womo_battery_set_show_percent(main_battery, false); // Hide percentage for clean look
        ESP_LOGI(TAG, "Battery 1 widget created successfully");
    } else {
        ESP_LOGW(TAG, "Failed to create battery 1 widget");
    }
    
    // Create second battery widget (Battery 2) - right next to first one at bottom
    secondary_battery = womo_battery_create(screen, 320, 420);  // Same y position, 70px to the right
    if (secondary_battery) {
        // Same voltage range as first battery
        womo_battery_set_voltage_range(secondary_battery, 10.5f, 14.4f);
        womo_battery_set_no_data(secondary_battery);
        womo_battery_set_show_percent(secondary_battery, false); // Hide percentage for clean look
        ESP_LOGI(TAG, "Battery 2 widget created successfully");
    } else {
        ESP_LOGW(TAG, "Failed to create battery 2 widget");
    }
    
    // RS485 Debug label (bottom left, above mode label)
    rs485_debug_label = lv_label_create(screen);
    lv_label_set_text(rs485_debug_label, womo_locale_get_string(STR_RS485_WAITING));
    lv_obj_set_style_text_font(rs485_debug_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rs485_debug_label, lv_color_make(255, 0, 0), 0); // Red for visibility
    lv_obj_align(rs485_debug_label, LV_ALIGN_BOTTOM_LEFT, 20, -60);
        
        // Mode label removed - theme now fully automatic based on real time
        
        // Create status label (right bottom - higher to avoid FPS overlay)
        status_label = lv_label_create(screen);
        char status_buf[40];
        snprintf(status_buf, sizeof(status_buf), "%s %s", 
                womo_locale_get_string(STR_STATUS), 
                womo_locale_get_string(STR_STATUS_OK));
        lv_label_set_text(status_label, status_buf);
        lv_obj_set_style_text_color(status_label, lv_color_black(), 0);
        lv_obj_align(status_label, LV_ALIGN_BOTTOM_LEFT, 553, -10);  // 20px weiter rechts: 533+20 = 553px, bottom -10px
        
    // Create LVGL timer to update time every second
    lv_timer_create(time_update_timer_cb, 1000, NULL);

    // Ensure text colors match the initial day/night theme
    apply_text_theme_colors();
        
        // Release the mutex
        lvgl_port_unlock();
    }
    
    // Connect to WiFi in background
    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);
    if (womo_wifi_connect(WIFI_SSID, WIFI_PASSWORD, WIFI_MAX_RETRY) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected successfully");
        
        esp_err_t weather_err = womo_weather_http_start(openweather_update_cb, NULL);
        if (weather_err != ESP_OK) {
            ESP_LOGW(TAG, "Online weather updates disabled: %s", esp_err_to_name(weather_err));
        }

        // Sync time via NTP (non-blocking)
        if (womo_time_sync_ntp(true) == ESP_OK) {
            ESP_LOGI(TAG, "Time synchronized via NTP");
        } else {
            ESP_LOGW(TAG, "NTP sync failed, using internal RTC");
        }
    } else {
        ESP_LOGW(TAG, "WiFi connection failed");
    }
    
    ESP_LOGI(TAG, "System running - Touch Control: Bottom-Left=Language, Right=Status, Auto-Theme=Enabled");
}

// RS485 data received callback - updates display labels
static void rs485_data_received(const womo_sensor_data_t *data, void *user_data)
{
    if (!data) return;
    
    // Increment packet counter
    rs485_packet_count++;
    
    ESP_LOGI(TAG, "rs485_data_received: packet %lu", rs485_packet_count);
    
    // Lock LVGL with longer timeout to avoid conflicts
    if (lvgl_port_lock(500)) {
        
        ESP_LOGI(TAG, "LVGL locked, updating labels...");
        
        // Update RS485 debug label
        if (rs485_debug_label) {
            char buf[60];
            snprintf(buf, sizeof(buf), womo_locale_get_string(STR_RS485_PACKETS), rs485_packet_count);
            lv_label_set_text(rs485_debug_label, buf);
            // Green color on successful reception
            lv_obj_set_style_text_color(rs485_debug_label, lv_color_make(0, 200, 0), 0);
        }
        
        // Update IMU label
        if (data->bno.valid && imu_label) {
            char buf[100];
            const char *dir = data->bno.direction[0] ? data->bno.direction : "?";
            ESP_LOGI(TAG, "RS485 IMU: %s %.1f° R:%.1f° P:%.1f°",
                     dir, data->bno.heading_deg, data->bno.roll_deg, data->bno.pitch_deg);
            snprintf(buf, sizeof(buf), "%s %s %.1f° R:%.1f° P:%.1f°",
                    womo_locale_get_string(STR_IMU), dir, data->bno.heading_deg,
                    data->bno.roll_deg, data->bno.pitch_deg);
            lv_label_set_text(imu_label, buf);
        }
        
        bool is_full_packet = (data->timestamp_ms != 0) ||
                              data->hx711.valid || data->battery.valid ||
                              data->bme680.valid || data->tank.valid || data->gps.valid;

        if (is_full_packet) {
            // Gas bottle weights via HX711
            if (data->hx711.valid) {
                if (rs485_missing_counter.hx711 > 0) {
                    ESP_LOGI(TAG, "RS485: HX711 data restored after %u missing packet(s)",
                             rs485_missing_counter.hx711);
                }
                rs485_missing_counter.hx711 = 0;

                if (gas_bottle_a) {
                    womo_gas_bottle_update_weight(gas_bottle_a, data->hx711.weight_a_kg);
                }
                if (gas_bottle_b) {
                    womo_gas_bottle_update_weight(gas_bottle_b, data->hx711.weight_b_kg);
                }
            } else {
                if (rs485_missing_counter.hx711 == 0) {
                    ESP_LOGW(TAG, "RS485: HX711 payload missing (packet %lu)", rs485_packet_count);
                }
                if (rs485_missing_counter.hx711 < UINT16_MAX) {
                    rs485_missing_counter.hx711++;
                }
                if (rs485_missing_counter.hx711 >= RS485_MISSING_THRESHOLD) {
                    if (rs485_missing_counter.hx711 == RS485_MISSING_THRESHOLD) {
                        ESP_LOGW(TAG, "RS485: HX711 data missing twice - resetting gas bottle UI");
                    }
                    if (gas_bottle_a) {
                        womo_gas_bottle_set_no_data(gas_bottle_a);
                    }
                    if (gas_bottle_b) {
                        womo_gas_bottle_set_no_data(gas_bottle_b);
                    }
                }
            }

            // Environmental data via BME680
            if (data->bme680.valid) {
                if (rs485_missing_counter.bme680 > 0) {
                    ESP_LOGI(TAG, "RS485: BME680 data restored after %u missing packet(s)",
                             rs485_missing_counter.bme680);
                }
                rs485_missing_counter.bme680 = 0;

                if (gas_label) {
                    char buf[60];
                    int gas_value = (int)roundf(data->bme680.gas_kohm);
                    snprintf(buf, sizeof(buf), "Q %d kOhm", gas_value);
                    lv_label_set_text(gas_label, buf);
                }
                if (press_label) {
                    char buf[40];
                    snprintf(buf, sizeof(buf), "%.1f hPa", data->bme680.pressure_hpa);
                    lv_label_set_text(press_label, buf);
                }
                if (humid_label) {
                    char buf[40];
                    snprintf(buf, sizeof(buf), "%.1f %%", data->bme680.humidity_percent);
                    lv_label_set_text(humid_label, buf);
                }
                if (temp_label) {
                    char buf[40];
                    snprintf(buf, sizeof(buf), "%.1f °C", data->bme680.temperature_c);
                    lv_label_set_text(temp_label, buf);
                }
            } else {
                if (rs485_missing_counter.bme680 == 0) {
                    ESP_LOGW(TAG, "RS485: BME680 payload missing (packet %lu)", rs485_packet_count);
                }
                if (rs485_missing_counter.bme680 < UINT16_MAX) {
                    rs485_missing_counter.bme680++;
                }
                if (rs485_missing_counter.bme680 >= RS485_MISSING_THRESHOLD) {
                    if (rs485_missing_counter.bme680 == RS485_MISSING_THRESHOLD) {
                        ESP_LOGW(TAG, "RS485: BME680 data missing twice - resetting air value labels");
                    }
                    if (gas_label) {
                        lv_label_set_text(gas_label, PLACEHOLDER_GAS);
                    }
                    if (press_label) {
                        lv_label_set_text(press_label, PLACEHOLDER_PRESSURE);
                    }
                    if (humid_label) {
                        lv_label_set_text(humid_label, PLACEHOLDER_HUMIDITY);
                    }
                    if (temp_label) {
                        lv_label_set_text(temp_label, PLACEHOLDER_TEMPERATURE);
                    }
                }
            }

            // Battery voltages
            if (data->battery.valid) {
                if (rs485_missing_counter.battery > 0) {
                    ESP_LOGI(TAG, "RS485: Battery data restored after %u missing packet(s)",
                             rs485_missing_counter.battery);
                }
                rs485_missing_counter.battery = 0;

                if (main_battery) {
                    womo_battery_set_voltage(main_battery, data->battery.battery1_v);
                }
                if (secondary_battery) {
                    womo_battery_set_voltage(secondary_battery, data->battery.battery2_v);
                }
            } else {
                if (rs485_missing_counter.battery == 0) {
                    ESP_LOGW(TAG, "RS485: Battery payload missing (packet %lu)", rs485_packet_count);
                }
                if (rs485_missing_counter.battery < UINT16_MAX) {
                    rs485_missing_counter.battery++;
                }
                if (rs485_missing_counter.battery >= RS485_MISSING_THRESHOLD) {
                    if (rs485_missing_counter.battery == RS485_MISSING_THRESHOLD) {
                        ESP_LOGW(TAG, "RS485: Battery data missing twice - resetting battery widgets");
                    }
                    if (main_battery) {
                        womo_battery_set_no_data(main_battery);
                    }
                    if (secondary_battery) {
                        womo_battery_set_no_data(secondary_battery);
                    }
                }
            }

            // Tank levels
            if (data->tank.valid) {
                if (rs485_missing_counter.tank > 0) {
                    ESP_LOGI(TAG, "RS485: Tank data restored after %u missing packet(s)",
                             rs485_missing_counter.tank);
                }
                rs485_missing_counter.tank = 0;

                if (fresh_water_tank) {
                    womo_tank_set_level(fresh_water_tank, data->tank.tank1_percent);
                }
                if (grey_water_tank) {
                    womo_tank_set_level(grey_water_tank, data->tank.tank2_percent);
                }
            } else {
                if (rs485_missing_counter.tank == 0) {
                    ESP_LOGW(TAG, "RS485: Tank payload missing (packet %lu)", rs485_packet_count);
                }
                if (rs485_missing_counter.tank < UINT16_MAX) {
                    rs485_missing_counter.tank++;
                }
                if (rs485_missing_counter.tank >= RS485_MISSING_THRESHOLD) {
                    if (rs485_missing_counter.tank == RS485_MISSING_THRESHOLD) {
                        ESP_LOGW(TAG, "RS485: Tank data missing twice - resetting tank widgets");
                    }
                    if (fresh_water_tank) {
                        womo_tank_set_no_data(fresh_water_tank);
                    }
                    if (grey_water_tank) {
                        womo_tank_set_no_data(grey_water_tank);
                    }
                    if (tank_label) {
                        char buf[50];
                        snprintf(buf, sizeof(buf), "%s -- / --",
                                 womo_locale_get_string(STR_TANKS));
                        lv_label_set_text(tank_label, buf);
                    }
                }
            }
        }
        
        // Update Tank label
        if (data->tank.valid && tank_label) {
            char buf[50];
            snprintf(buf, sizeof(buf), "%s %u%% / %u%%", 
                    womo_locale_get_string(STR_TANKS),
                    data->tank.tank1_percent, data->tank.tank2_percent);
            lv_label_set_text(tank_label, buf);
        }
        
        // Update GPS label
        if (data->gps.valid && gps_label) {
            char buf[80];
            // Format: "GPS: 51.5074°N 0.1278°W 45km/h ↗ 8sats"
            char lat_dir = (data->gps.latitude >= 0) ? 'N' : 'S';
            char lon_dir = (data->gps.longitude >= 0) ? 'E' : 'W';
            snprintf(buf, sizeof(buf), "GPS: %.4f°%c %.4f°%c %.0fkm/h %dsats", 
                    fabs(data->gps.latitude), lat_dir,
                    fabs(data->gps.longitude), lon_dir,
                    data->gps.speed_kmh, data->gps.satellites);
            lv_label_set_text(gps_label, buf);
        }
        
        lvgl_port_unlock();
        
        ESP_LOGI(TAG, "Display labels updated OK");
    } else {
        ESP_LOGW(TAG, "Failed to lock LVGL - skipping update");
    }
}

