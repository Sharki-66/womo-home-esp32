#include "womo_battery.h"
#include "womo_fonts_german.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "battery";

// Battery colors (same as gas bottles)
#define BATTERY_BODY_COLOR      lv_color_hex(0xD3D3D3)  // Light grey (same as gas bottle)
#define BATTERY_BODY_GRAD       lv_color_hex(0xA9A9A9)  // Dark grey gradient (same as gas bottle)
#define BATTERY_TERMINAL_COLOR  lv_color_hex(0x708090)  // Slate grey (same as valve metal)
#define BATTERY_BORDER_COLOR    lv_color_hex(0xA9A9A9)  // Dark grey border (same as gas bottle)
#define BATTERY_FILL_BLUE       lv_color_hex(0x3B82F6)  // Blue for charging
#define BATTERY_FILL_GREEN      lv_color_hex(0x22C55E)  // Green for good charge
#define BATTERY_FILL_ORANGE     lv_color_hex(0xF97316)  // Orange for medium
#define BATTERY_FILL_RED        lv_color_hex(0xDC2626)  // Red for low charge
#define BATTERY_TEXT_COLOR      lv_color_hex(0x000000)  // Black text (better on light background)
#define BATTERY_PLACEHOLDER_COLOR lv_color_hex(0xE5E7EB) // Light grey when no data

// Battery dimensions (20% smaller, with connection block design)
#define BATTERY_WIDTH           48   // Main body width (20% kleiner: 60*0.8)
#define BATTERY_HEIGHT          32   // Main body height (20% kleiner: 40*0.8)
#define CONNECTION_BLOCK_WIDTH  56   // Connection block width (übersteht links/rechts)
#define CONNECTION_BLOCK_HEIGHT 8    // Connection block height (flach)
#define TERMINAL_WIDTH          8    // Terminal width (Pol)
#define TERMINAL_HEIGHT         6    // Terminal height (Pol)
#define FILL_MARGIN             2    // Margin for fill indicator
#define CORNER_RADIUS           3    // Rounded corners

#define BATTERY_PLACEHOLDER_TEXT "--.- V"

/**
 * @brief Calculate charge percentage from voltage
 */
static uint8_t calculate_charge_percent(womo_battery_t *battery)
{
    if (battery->max_voltage_v <= battery->min_voltage_v) {
        return 0; // Invalid configuration
    }
    
    float voltage_range = battery->max_voltage_v - battery->min_voltage_v;
    float current_range = battery->voltage_v - battery->min_voltage_v;
    
    if (current_range <= 0) {
        return 0;
    }
    
    if (current_range >= voltage_range) {
        return 100;
    }
    
    float percent = (current_range / voltage_range) * 100.0f;
    return (uint8_t)(percent + 0.5f); // Round to nearest integer
}

/**
 * @brief Update the visual charge level and colors
 */
static void update_battery_visual(womo_battery_t *battery)
{
    if (!battery) {
        return;
    }

    if (!battery->has_valid_voltage) {
        if (battery->battery_body) {
            lv_obj_set_style_bg_color(battery->battery_body, BATTERY_PLACEHOLDER_COLOR, 0);
            lv_obj_set_style_bg_grad_color(battery->battery_body, lv_color_darken(BATTERY_PLACEHOLDER_COLOR, LV_OPA_30), 0);
        }
        if (battery->voltage_label) {
            lv_label_set_text(battery->voltage_label, BATTERY_PLACEHOLDER_TEXT);
        }
        if (battery->percent_label) {
            lv_label_set_text(battery->percent_label, "--%");
            lv_obj_add_flag(battery->percent_label, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    
    // Calculate charge percentage
    battery->charge_percent = calculate_charge_percent(battery);

    // Determine body color based on voltage levels
    lv_color_t fill_color = BATTERY_FILL_GREEN;
    if (battery->voltage_v >= (battery->max_voltage_v - 0.2f)) {
        fill_color = BATTERY_FILL_BLUE;  // Charging / high voltage
    } else if (battery->voltage_v <= (battery->min_voltage_v + 0.2f)) {
        fill_color = BATTERY_FILL_RED;   // Critical low voltage
    } else if (battery->voltage_v <= (battery->min_voltage_v + 1.0f)) {
        fill_color = BATTERY_FILL_ORANGE; // Medium / warning
    }

    if (battery->battery_body) {
        lv_obj_set_style_bg_color(battery->battery_body, fill_color, 0);
        lv_obj_set_style_bg_grad_color(battery->battery_body, lv_color_darken(fill_color, LV_OPA_30), 0);
    }

    // Update voltage label on battery
    if (battery->voltage_label) {
        char voltage_text[16];
        snprintf(voltage_text, sizeof(voltage_text), "%.1fV", battery->voltage_v);
        lv_label_set_text(battery->voltage_label, voltage_text);
    }
    
    // Update percentage label (if enabled)
    if (battery->percent_label && battery->show_percent) {
        char percent_text[8];
        snprintf(percent_text, sizeof(percent_text), "%u%%", battery->charge_percent);
        lv_label_set_text(battery->percent_label, percent_text);
        lv_obj_clear_flag(battery->percent_label, LV_OBJ_FLAG_HIDDEN);
    } else if (battery->percent_label) {
        lv_obj_add_flag(battery->percent_label, LV_OBJ_FLAG_HIDDEN);
    }
}

womo_battery_t* womo_battery_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y)
{
    if (!parent) {
        ESP_LOGE(TAG, "Parent object is NULL");
        return NULL;
    }
    
    // Allocate memory for battery structure
    womo_battery_t *battery = malloc(sizeof(womo_battery_t));
    if (!battery) {
        ESP_LOGE(TAG, "Failed to allocate memory for battery widget");
        return NULL;
    }
    
    // Initialize structure with 12V car battery defaults
    memset(battery, 0, sizeof(womo_battery_t));
    battery->voltage_v = 12.6f;        // Current voltage
    battery->min_voltage_v = 10.5f;    // Empty battery (0%)
    battery->max_voltage_v = 14.4f;    // Full charged (100%)
    battery->charge_percent = 0;
    battery->show_percent = false;
    battery->has_valid_voltage = false;
    
    // Create main container
    battery->container = lv_obj_create(parent);
    lv_obj_set_size(battery->container, CONNECTION_BLOCK_WIDTH, BATTERY_HEIGHT + CONNECTION_BLOCK_HEIGHT + TERMINAL_HEIGHT + 10);
    lv_obj_set_pos(battery->container, x, y);
    lv_obj_clear_flag(battery->container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(battery->container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(battery->container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(battery->container, 0, 0);
    
    // Create main battery body (rectangular, gas bottle colors)
    battery->battery_body = lv_obj_create(battery->container);
    lv_obj_set_size(battery->battery_body, BATTERY_WIDTH, BATTERY_HEIGHT);
    lv_obj_set_pos(battery->battery_body, (CONNECTION_BLOCK_WIDTH - BATTERY_WIDTH) / 2, CONNECTION_BLOCK_HEIGHT + TERMINAL_HEIGHT);
    lv_obj_set_style_bg_color(battery->battery_body, BATTERY_BODY_COLOR, 0);
    lv_obj_set_style_bg_grad_color(battery->battery_body, BATTERY_BODY_GRAD, 0);
    lv_obj_set_style_bg_grad_dir(battery->battery_body, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(battery->battery_body, 2, 0);
    lv_obj_set_style_border_color(battery->battery_body, BATTERY_BORDER_COLOR, 0);
    lv_obj_set_style_radius(battery->battery_body, CORNER_RADIUS, 0);
    lv_obj_set_style_shadow_width(battery->battery_body, 4, 0);
    lv_obj_set_style_shadow_opa(battery->battery_body, LV_OPA_30, 0);
    lv_obj_clear_flag(battery->battery_body, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create connection block on top (overhangs left/right, 2px into the body)
    battery->connection_block = lv_obj_create(battery->container);
    lv_obj_set_size(battery->connection_block, CONNECTION_BLOCK_WIDTH, CONNECTION_BLOCK_HEIGHT);
    lv_obj_set_pos(battery->connection_block, 0, TERMINAL_HEIGHT + 2);  // 2px into the battery body
    lv_obj_set_style_bg_color(battery->connection_block, BATTERY_TERMINAL_COLOR, 0);
    lv_obj_set_style_border_width(battery->connection_block, 1, 0);
    lv_obj_set_style_border_color(battery->connection_block, BATTERY_BORDER_COLOR, 0);
    lv_obj_set_style_radius(battery->connection_block, 2, 0);
    lv_obj_clear_flag(battery->connection_block, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create positive terminal (right side, 3 pixels more inward)
    battery->terminal_plus = lv_obj_create(battery->container);
    lv_obj_set_size(battery->terminal_plus, TERMINAL_WIDTH, TERMINAL_HEIGHT);
    lv_obj_set_pos(battery->terminal_plus, CONNECTION_BLOCK_WIDTH - TERMINAL_WIDTH - 7, 0);  // 3 pixels more inward
    lv_obj_set_style_bg_color(battery->terminal_plus, BATTERY_TERMINAL_COLOR, 0);
    lv_obj_set_style_border_width(battery->terminal_plus, 1, 0);
    lv_obj_set_style_border_color(battery->terminal_plus, BATTERY_BORDER_COLOR, 0);
    lv_obj_set_style_radius(battery->terminal_plus, 1, 0);
    lv_obj_clear_flag(battery->terminal_plus, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create negative terminal (left side, 3 pixels more inward)
    battery->terminal_minus = lv_obj_create(battery->container);
    lv_obj_set_size(battery->terminal_minus, TERMINAL_WIDTH, TERMINAL_HEIGHT);
    lv_obj_set_pos(battery->terminal_minus, 7, 0);  // 3 pixels more inward
    lv_obj_set_style_bg_color(battery->terminal_minus, BATTERY_TERMINAL_COLOR, 0);
    lv_obj_set_style_border_width(battery->terminal_minus, 1, 0);
    lv_obj_set_style_border_color(battery->terminal_minus, BATTERY_BORDER_COLOR, 0);
    lv_obj_set_style_radius(battery->terminal_minus, 1, 0);
    lv_obj_clear_flag(battery->terminal_minus, LV_OBJ_FLAG_SCROLLABLE);
    
    // No fill bar needed per user request
    battery->fill_bar = NULL;
    
    // Create voltage label centered inside the battery body
    battery->voltage_label = lv_label_create(battery->battery_body);
    lv_label_set_text(battery->voltage_label, BATTERY_PLACEHOLDER_TEXT);
    lv_obj_set_style_text_font(battery->voltage_label, &lv_font_montserrat_12, 0);  // Kleinere Font für den kleineren Block
    lv_obj_set_style_text_color(battery->voltage_label, BATTERY_TEXT_COLOR, 0);
    lv_obj_set_style_text_align(battery->voltage_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(battery->voltage_label, LV_ALIGN_CENTER, 0, 0);
    
    // Create optional percentage label (initially hidden)
    battery->percent_label = lv_label_create(battery->container);
    lv_label_set_text(battery->percent_label, "75%");
    lv_obj_set_style_text_font(battery->percent_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(battery->percent_label, lv_color_black(), 0);
    lv_obj_align(battery->percent_label, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(battery->percent_label, LV_OBJ_FLAG_HIDDEN); // Hidden by default
    
    ESP_LOGI(TAG, "Battery widget created at (%d, %d)", x, y);
    
    // Initial visual update
    update_battery_visual(battery);
    
    return battery;
}

void womo_battery_set_voltage(womo_battery_t *battery, float voltage_v)
{
    if (!battery) {
        ESP_LOGW(TAG, "Battery widget is NULL");
        return;
    }
    
    battery->has_valid_voltage = true;
    battery->voltage_v = voltage_v;
    
    ESP_LOGD(TAG, "Updated battery voltage: %.1fV", voltage_v);
    
    update_battery_visual(battery);
}

void womo_battery_set_no_data(womo_battery_t *battery)
{
    if (!battery) {
        ESP_LOGW(TAG, "Battery widget is NULL");
        return;
    }

    battery->has_valid_voltage = false;
    battery->voltage_v = 0.0f;
    update_battery_visual(battery);
}

void womo_battery_set_voltage_range(womo_battery_t *battery, float min_voltage_v, float max_voltage_v)
{
    if (!battery) {
        ESP_LOGW(TAG, "Battery widget is NULL");
        return;
    }
    
    battery->min_voltage_v = min_voltage_v;
    battery->max_voltage_v = max_voltage_v;
    
    ESP_LOGI(TAG, "Set battery voltage range: %.1fV - %.1fV", min_voltage_v, max_voltage_v);
    
    update_battery_visual(battery);
}

void womo_battery_set_show_percent(womo_battery_t *battery, bool show_percent)
{
    if (!battery) {
        ESP_LOGW(TAG, "Battery widget is NULL");
        return;
    }
    
    battery->show_percent = show_percent;
    
    update_battery_visual(battery);
}

uint8_t womo_battery_get_charge_percent(womo_battery_t *battery)
{
    if (!battery) {
        return 0;
    }
    
    return battery->charge_percent;
}

void womo_battery_set_pos(womo_battery_t *battery, lv_coord_t x, lv_coord_t y)
{
    if (!battery || !battery->container) {
        ESP_LOGW(TAG, "Battery widget or container is NULL");
        return;
    }
    
    lv_obj_set_pos(battery->container, x, y);
}

void womo_battery_set_visible(womo_battery_t *battery, bool visible)
{
    if (!battery || !battery->container) {
        ESP_LOGW(TAG, "Battery widget or container is NULL");
        return;
    }
    
    if (visible) {
        lv_obj_clear_flag(battery->container, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(battery->container, LV_OBJ_FLAG_HIDDEN);
    }
}

void womo_battery_delete(womo_battery_t *battery)
{
    if (!battery) {
        return;
    }
    
    // Delete LVGL objects (this will also delete child objects)
    if (battery->container) {
        lv_obj_del(battery->container);
    }
    
    // Free memory
    free(battery);
    
    ESP_LOGI(TAG, "Battery widget deleted");
}