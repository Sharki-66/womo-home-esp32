#include "womo_weather.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "weather";

// Weather icon base directory on SD card (path: /sdcard/SDCARD/images/weather-icons/)
#define WEATHER_ICON_BASE_PATH   "/sdcard/SDCARD/images/weather-icons"
#define WEATHER_ICON_DIR_BLACK   WEATHER_ICON_BASE_PATH "/black"
#define WEATHER_ICON_DIR_WHITE   WEATHER_ICON_BASE_PATH "/white"

// Weather icon file names (same for black/white variants)
static const char* weather_icon_files[] = {
    [WEATHER_UNKNOWN] = "nt_unknown.png",
    [WEATHER_CLEAR] = "clear.png",
    [WEATHER_SUNNY] = "nt_sunny.png",  // Use night version as default
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
    [WEATHER_TSTORMS] = "nt_tstorms.png",
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
    
    const char *color_dir = use_white_theme ? WEATHER_ICON_DIR_WHITE : WEATHER_ICON_DIR_BLACK;
    ESP_LOGI(TAG, "Using weather icon directory: %s", color_dir);

    // Build full path: /sdcard/SDCARD/images/weather-icons/{black|white}/filename
    snprintf(weather->icon_path, sizeof(weather->icon_path), "%s/%s", color_dir, filename);
    
    ESP_LOGI(TAG, "Loading weather icon: %s", weather->icon_path);
    
    // List files in target directory for debugging
    // Check if file exists first
    FILE *test_file = fopen(weather->icon_path, "r");
    if (test_file) {
        fclose(test_file);
        ESP_LOGI(TAG, "Weather icon file exists: %s", weather->icon_path);
    } else {
        ESP_LOGW(TAG, "Weather icon file NOT FOUND: %s", weather->icon_path);
        
        // Try with different case
        char alt_path[128];
    snprintf(alt_path, sizeof(alt_path), "%s/chanceregen.png", color_dir);
        FILE *alt_file = fopen(alt_path, "r");
        if (alt_file) {
            fclose(alt_file);
            ESP_LOGI(TAG, "Alternative file found: %s", alt_path);
        }
        
        return;
    }
    
    // Load PNG file into memory (same method as Ducato background)
    FILE *fp = fopen(weather->icon_path, "rb");
    if (!fp) {
        ESP_LOGW(TAG, "Failed to open weather icon file");
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
    size_t bytes_read = fread(png_data, 1, file_size, fp);
    fclose(fp);
    
    if (bytes_read != file_size) {
        ESP_LOGE(TAG, "Read only %zu of %ld bytes for weather icon", bytes_read, file_size);
        heap_caps_free(png_data);
        return;
    }
    
    ESP_LOGI(TAG, "Weather PNG loaded: %ld bytes", file_size);
    
    // Create LVGL image descriptor for PNG (same as Ducato method)
    static lv_img_dsc_t weather_img_dsc;
    weather_img_dsc.header.always_zero = 0;
    weather_img_dsc.header.w = 0;  // PNG decoder will determine size
    weather_img_dsc.header.h = 0;  // PNG decoder will determine size
    weather_img_dsc.data_size = file_size;
    weather_img_dsc.header.cf = LV_IMG_CF_RAW_ALPHA;  // Let PNG decoder handle it
    weather_img_dsc.data = png_data;
    
    // Set image source to memory descriptor
    lv_img_set_src(weather->weather_icon, &weather_img_dsc);
    
    ESP_LOGI(TAG, "Weather icon PNG set successfully");
    
    // Note: png_data will be kept in memory as LVGL references it
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
    lv_obj_align(weather->container, LV_ALIGN_TOP_RIGHT, -70, 70);
    lv_obj_clear_flag(weather->container, LV_OBJ_FLAG_SCROLLABLE);
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
    
    // Load default icon (cloudy as safe fallback, day scheme)
    load_weather_icon(weather, weather_icon_files[WEATHER_CLOUDY], false);
    
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