/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#include "womo_weather.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include "hardware/waveshare_rgb_lcd_port.h"  // for CH422G SD-CS reassert
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "misc/cache/instance/lv_image_cache.h"  // lv_image_cache_drop (LVGL private)
#include "storage/womo_sd.h"

static const char *TAG = "weather";

// Weather icon base directory on SD card (path: /sdcard/images/weather-icons/)
#define WEATHER_ICON_BASE_PATH   "/sdcard/images/weather-icons"
#define WEATHER_ICON_DIR_BLACK   WEATHER_ICON_BASE_PATH "/black"
#define WEATHER_ICON_DIR_WHITE   WEATHER_ICON_BASE_PATH "/white"

// Weather icon file names (same for black/white variants)
static const char* weather_icon_files[] = {
    [WEATHER_UNKNOWN] = "nt_unknown.png",
    [WEATHER_CLEAR] = "clear.png",
    [WEATHER_SUNNY] = "sunny.png",
    [WEATHER_PARTLYSUNNY] = "partlysunny.png",
    [WEATHER_PARTLYCLOUDY] = "partlycloudy.png",
    [WEATHER_MOSTLYSUNNY] = "mostlysunny.png",
    [WEATHER_MOSTLYCLOUDY] = "mostlycloudy.png",
    [WEATHER_CLOUDY] = "cloudy.png",
    [WEATHER_CHANCERAIN] = "chancerain.png",
    [WEATHER_RAIN] = "rain.png",
    [WEATHER_CHANCESNOW] = "chancesnow.png",
    [WEATHER_SNOW] = "snow.png",
    [WEATHER_CHANCESLEET] = "chancesleet.png",
    [WEATHER_SLEET] = "sleet.png",
    [WEATHER_CHANCETSTORMS] = "chancetstorms.png",
    [WEATHER_TSTORMS] = "tstorms.png",
    [WEATHER_FLURRIES] = "flurries.png",
    [WEATHER_FOG] = "fog.png",
    [WEATHER_HAZY] = "hazy.png",
    // Night versions
    [WEATHER_NT_CLEAR] = "nt_clear.png",
    [WEATHER_NT_SUNNY] = "nt_sunny.png",
    [WEATHER_NT_PARTLYSUNNY] = "nt_partlysunny.png",
    [WEATHER_NT_PARTLYCLOUDY] = "nt_partlycloudy.png",
    [WEATHER_NT_MOSTLYSUNNY] = "nt_mostlysunny.png",
    [WEATHER_NT_MOSTLYCLOUDY] = "nt_mostlycloudy.png",
    [WEATHER_NT_CLOUDY] = "nt_cloudy.png",
    [WEATHER_NT_CHANCERAIN] = "nt_chancerain.png",
    [WEATHER_NT_RAIN] = "nt_rain.png",
    [WEATHER_NT_CHANCESNOW] = "nt_chancesnow.png",
    [WEATHER_NT_SNOW] = "nt_snow.png",
    [WEATHER_NT_CHANCESLEET] = "nt_chancesleet.png",
    [WEATHER_NT_SLEET] = "nt_sleet.png",
    [WEATHER_NT_CHANCETSTORMS] = "nt_chancetstorms.png",
    [WEATHER_NT_TSTORMS] = "nt_tstorms.png",
    [WEATHER_NT_CHANCEFLURRIES] = "nt_chanceflurries.png",
    [WEATHER_NT_FLURRIES] = "nt_flurries.png",
    [WEATHER_NT_FOG] = "nt_fog.png",
    [WEATHER_NT_HAZY] = "nt_hazy.png",
    [WEATHER_NT_UNKNOWN] = "nt_unknown.png"
};

// Weather condition names for debugging
static const char* weather_condition_names[] = {
    [WEATHER_UNKNOWN] = "Unknown",
    [WEATHER_CLEAR] = "Clear",
    [WEATHER_SUNNY] = "Sunny",
    [WEATHER_PARTLYSUNNY] = "Partly Sunny",
    [WEATHER_PARTLYCLOUDY] = "Partly Cloudy",
    [WEATHER_MOSTLYSUNNY] = "Mostly Sunny",
    [WEATHER_MOSTLYCLOUDY] = "Mostly Cloudy",
    [WEATHER_CLOUDY] = "Cloudy",
    [WEATHER_CHANCERAIN] = "Chance Rain",
    [WEATHER_RAIN] = "Rain",
    [WEATHER_CHANCESNOW] = "Chance Snow",
    [WEATHER_SNOW] = "Snow",
    [WEATHER_CHANCESLEET] = "Chance Sleet",
    [WEATHER_SLEET] = "Sleet",
    [WEATHER_CHANCETSTORMS] = "Chance Storms",
    [WEATHER_TSTORMS] = "Storms",
    [WEATHER_FLURRIES] = "Flurries",
    [WEATHER_FOG] = "Fog",
    [WEATHER_HAZY] = "Hazy",
    // Night versions
    [WEATHER_NT_CLEAR] = "Clear (Night)",
    [WEATHER_NT_SUNNY] = "Sunny (Night)",
    [WEATHER_NT_PARTLYSUNNY] = "Partly Sunny (Night)",
    [WEATHER_NT_PARTLYCLOUDY] = "Partly Cloudy (Night)",
    [WEATHER_NT_MOSTLYSUNNY] = "Mostly Sunny (Night)",
    [WEATHER_NT_MOSTLYCLOUDY] = "Mostly Cloudy (Night)",
    [WEATHER_NT_CLOUDY] = "Cloudy (Night)",
    [WEATHER_NT_CHANCERAIN] = "Chance Rain (Night)",
    [WEATHER_NT_RAIN] = "Rain (Night)",
    [WEATHER_NT_CHANCESNOW] = "Chance Snow (Night)",
    [WEATHER_NT_SNOW] = "Snow (Night)",
    [WEATHER_NT_CHANCESLEET] = "Chance Sleet (Night)",
    [WEATHER_NT_SLEET] = "Sleet (Night)",
    [WEATHER_NT_CHANCETSTORMS] = "Chance Storms (Night)",
    [WEATHER_NT_TSTORMS] = "Storms (Night)",
    [WEATHER_NT_CHANCEFLURRIES] = "Chance Flurries (Night)",
    [WEATHER_NT_FLURRIES] = "Flurries (Night)",
    [WEATHER_NT_FOG] = "Fog (Night)",
    [WEATHER_NT_HAZY] = "Hazy (Night)",
    [WEATHER_NT_UNKNOWN] = "Unknown (Night)"
};

/**
 * @brief Load weather icon from SD card
 */
static void load_weather_icon(womo_weather_t *weather, const char *filename, bool use_white_theme)
{
    if (!weather || !weather->weather_icon || !filename) {
        ESP_LOGW(TAG, "Invalid parameters for loading weather icon");
        return;
    }

    if (!womo_sd_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted, cannot load weather icon");
        return;
    }
    
    const char *chosen_dir = use_white_theme ? WEATHER_ICON_DIR_WHITE : WEATHER_ICON_DIR_BLACK;
    ESP_LOGI(TAG, "Using weather icon directory: %s (theme %s)",
             chosen_dir,
             use_white_theme ? "white" : "black");

    snprintf(weather->icon_path, sizeof(weather->icon_path), "%s/%s", chosen_dir, filename);
    ESP_LOGI(TAG, "Loading weather icon: %s", weather->icon_path);
    
    womo_ch422g_assert_sd_cs();
    FILE *test_file = fopen(weather->icon_path, "r");
    if (!test_file && errno == EIO) {
        ESP_LOGW(TAG, "Weather icon open EIO, retrying once: %s", weather->icon_path);
        vTaskDelay(pdMS_TO_TICKS(50));
        womo_ch422g_assert_sd_cs();
        test_file = fopen(weather->icon_path, "r");
    }
    if (test_file) {
        fclose(test_file);
        ESP_LOGI(TAG, "Weather icon file exists: %s", weather->icon_path);
    } else {
        int err = errno;
        ESP_LOGW(TAG, "Weather icon file NOT FOUND: %s (errno=%d: %s)",
                 weather->icon_path,
                 err,
                 strerror(err));
        return;
    }
    
    // Load PNG file into memory (same method as Ducato background)
    womo_ch422g_assert_sd_cs();
    FILE *fp = fopen(weather->icon_path, "rb");
    if (!fp && errno == EIO) {
        ESP_LOGW(TAG, "Weather icon fopen EIO, retrying once: %s", weather->icon_path);
        vTaskDelay(pdMS_TO_TICKS(50));
        womo_ch422g_assert_sd_cs();
        fp = fopen(weather->icon_path, "rb");
    }
    if (!fp) {
        ESP_LOGW(TAG, "Failed to open weather icon file (errno=%d: %s)", errno, strerror(errno));
        return;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > 1024 * 1024) {  // Max 1MB for weather icon
        ESP_LOGE(TAG, "Invalid weather icon file size: %ld bytes", file_size);
        fclose(fp);
        return;
    }
    
    // Allocate buffer for PNG data (use SPIRAM if available)
    uint8_t *png_data = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!png_data) {
        ESP_LOGE(TAG, "Failed to allocate %ld bytes for weather PNG", file_size);
        fclose(fp);
        return;
    }
    
    // Read file into buffer
    womo_ch422g_assert_sd_cs();
    size_t bytes_read = fread(png_data, 1, file_size, fp);
    if (bytes_read != file_size && errno == EIO) {
        ESP_LOGW(TAG, "Weather icon fread EIO, retrying once");
        vTaskDelay(pdMS_TO_TICKS(50));
        fseek(fp, 0, SEEK_SET);
        womo_ch422g_assert_sd_cs();
        bytes_read = fread(png_data, 1, file_size, fp);
    }
    fclose(fp);
    
    if (bytes_read != file_size) {
        ESP_LOGE(TAG, "Read only %zu of %ld bytes for weather icon (errno=%d: %s)",
                 bytes_read,
                 file_size,
                 errno,
                 strerror(errno));
        heap_caps_free(png_data);
        return;
    }
    
    ESP_LOGI(TAG, "Weather PNG loaded: %ld bytes", file_size);

    // Alten Cache-Eintrag und Puffer freigeben, bevor der Descriptor überschrieben wird.
    // WICHTIG: Da img_dsc eine feste Adresse im Struct hat, würde LVGL sonst das gecachte
    // alte Bild zurückliefern, obwohl der Descriptor-Inhalt bereits geändert wurde.
    lv_image_cache_drop(&weather->img_dsc);
    if (weather->png_buf) {
        heap_caps_free(weather->png_buf);
        weather->png_buf = NULL;
    }

    // LVGL image descriptor im Struct befüllen (eindeutige Adresse pro Widget-Instanz)
    weather->img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    weather->img_dsc.header.w = 0;  // PNG decoder will determine size
    weather->img_dsc.header.h = 0;  // PNG decoder will determine size
    weather->img_dsc.data_size = file_size;
    weather->img_dsc.header.cf = LV_COLOR_FORMAT_RAW_ALPHA;  // Let PNG decoder handle it
    weather->img_dsc.data = png_data;
    weather->png_buf = png_data;

    // Set image source to memory descriptor
    lv_img_set_src(weather->weather_icon, &weather->img_dsc);

    ESP_LOGI(TAG, "Weather icon PNG set successfully");
}

womo_weather_t* womo_weather_create(lv_obj_t *parent)
{
    if (!parent) {
        ESP_LOGE(TAG, "Parent object is NULL");
        return NULL;
    }
    
    // Allocate memory for weather widget structure
    womo_weather_t *weather = malloc(sizeof(womo_weather_t));
    if (!weather) {
        ESP_LOGE(TAG, "Failed to allocate memory for weather widget");
        return NULL;
    }
    
    // Initialize structure
    memset(weather, 0, sizeof(womo_weather_t));
    weather->condition = WEATHER_UNKNOWN;
    weather->temperature_c = -99;  // Invalid temperature
    weather->is_night = false;
    
    // Create main container (64x64 px for weather icon + small temp)
    weather->container = lv_obj_create(parent);
    lv_obj_set_size(weather->container, 80, 80);
    // Position 70px from the right edge and 70px from the top edge
    lv_obj_align(weather->container, LV_ALIGN_TOP_RIGHT, -160, 10);
    lv_obj_clear_flag(weather->container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(weather->container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(weather->container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(weather->container, 2, 0);
    
    // Create weather icon (64x64 px with auto-scaling)
    weather->weather_icon = lv_img_create(weather->container);
    lv_obj_set_size(weather->weather_icon, 64, 64);
    lv_obj_align(weather->weather_icon, LV_ALIGN_TOP_MID, 0, 0);
    
    // Icons will be 64x64px natively - no scaling needed
    
    // Temperature label removed per user request
    weather->temp_label = NULL;
    
    ESP_LOGI(TAG, "Weather widget created in top-right corner");
    
    // Load default icon as explicit night-unknown to see later updates clearly
    load_weather_icon(weather, weather_icon_files[WEATHER_NT_UNKNOWN], true);
    
    return weather;
}

void womo_weather_set_condition(womo_weather_t *weather, womo_weather_condition_t condition, bool is_night)
{
    if (!weather) {
        ESP_LOGW(TAG, "Weather widget is NULL");
        return;
    }
    
    weather->condition = condition;
    weather->is_night = is_night;
    
    // Validate condition range
    if (condition >= (sizeof(weather_icon_files) / sizeof(weather_icon_files[0]))) {
        ESP_LOGW(TAG, "Invalid weather condition: %d", condition);
        condition = WEATHER_UNKNOWN;
    }
    
    ESP_LOGI(TAG, "Setting weather condition: %s", womo_weather_get_condition_name(condition));
    
    // Load appropriate icon
    const char *filename = weather_icon_files[condition];
    if (filename) {
        load_weather_icon(weather, filename, weather->is_night);
    } else {
        ESP_LOGW(TAG, "No icon file for condition: %d", condition);
        load_weather_icon(weather, weather_icon_files[WEATHER_UNKNOWN], weather->is_night);
    }
}

void womo_weather_set_temperature(womo_weather_t *weather, int16_t temperature_c)
{
    if (!weather || !weather->temp_label) {
        ESP_LOGW(TAG, "Weather widget or temp label is NULL");
        return;
    }
    
    weather->temperature_c = temperature_c;
    
    char temp_str[16];
    if (temperature_c == -99) {
        snprintf(temp_str, sizeof(temp_str), "--°");
    } else {
        snprintf(temp_str, sizeof(temp_str), "%d°", temperature_c);
    }
    
    lv_label_set_text(weather->temp_label, temp_str);
    
    ESP_LOGD(TAG, "Updated temperature: %d°C", temperature_c);
}

void womo_weather_set_pos(womo_weather_t *weather, lv_coord_t x, lv_coord_t y)
{
    if (!weather || !weather->container) {
        ESP_LOGW(TAG, "Weather widget or container is NULL");
        return;
    }
    
    lv_obj_set_pos(weather->container, x, y);
}

void womo_weather_set_visible(womo_weather_t *weather, bool visible)
{
    if (!weather || !weather->container) {
        ESP_LOGW(TAG, "Weather widget or container is NULL");
        return;
    }
    
    if (visible) {
        lv_obj_clear_flag(weather->container, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(weather->container, LV_OBJ_FLAG_HIDDEN);
    }
}

void womo_weather_delete(womo_weather_t *weather)
{
    if (!weather) {
        return;
    }

    // Cache-Eintrag und PNG-Puffer freigeben
    lv_image_cache_drop(&weather->img_dsc);
    if (weather->png_buf) {
        heap_caps_free(weather->png_buf);
        weather->png_buf = NULL;
    }

    // Delete LVGL objects (this will also delete child objects)
    if (weather->container) {
        lv_obj_del(weather->container);
    }

    // Free memory
    free(weather);

    ESP_LOGI(TAG, "Weather widget deleted");
}

const char* womo_weather_get_condition_name(womo_weather_condition_t condition)
{
    if (condition >= (sizeof(weather_condition_names) / sizeof(weather_condition_names[0]))) {
        return "Invalid";
    }
    
    const char *name = weather_condition_names[condition];
    return name ? name : "Unknown";
}

void womo_weather_set_warnings(womo_weather_t *weather, uint8_t count, uint8_t max_severity)
{
    if (!weather || !weather->container) {
        return;
    }

    weather->warn_count        = count;
    weather->warn_max_severity = max_severity;

    /* Badge erstmalig erzeugen */
    if (!weather->warn_badge) {
        weather->warn_badge = lv_label_create(weather->container);
        lv_obj_set_style_text_font(weather->warn_badge, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_opa(weather->warn_badge, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(weather->warn_badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_hor(weather->warn_badge, 4, 0);
        lv_obj_set_style_pad_ver(weather->warn_badge, 1, 0);
        /* Rechts oben am Icon, knapp über dem Container-Rand */
        lv_obj_align(weather->warn_badge, LV_ALIGN_TOP_RIGHT, 2, -2);
        lv_obj_set_style_text_color(weather->warn_badge, lv_color_white(), 0);
    }

    if (count == 0) {
        lv_obj_add_flag(weather->warn_badge, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* Farbe je nach Stufe */
    lv_color_t badge_color;
    if (max_severity >= 4) {
        badge_color = lv_color_make(200, 0, 0);   // Rot (Extreme)
    } else if (max_severity == 3) {
        badge_color = lv_color_make(200, 0, 0);   // Rot (Severe)
    } else {
        badge_color = lv_color_make(230, 100, 0); // Orange (Moderate)
    }

    lv_obj_set_style_bg_color(weather->warn_badge, badge_color, 0);

    char buf[8];
    if (count <= 9) {
        snprintf(buf, sizeof(buf), " %u ", count);
    } else {
        snprintf(buf, sizeof(buf), " 9+ ");
    }
    lv_label_set_text(weather->warn_badge, buf);
    lv_obj_clear_flag(weather->warn_badge, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Warn-Badge: %u Warnungen, Stufe %u", count, max_severity);
}
