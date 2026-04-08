/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#include "womo_gas_bottle.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "gas_bottle";

// Colors for the gas bottle (realistic camping bottle)
#define BOTTLE_GREY         lv_color_hex(0xD3D3D3)  // Light grey for bottle body
#define BOTTLE_GREY_DARK    lv_color_hex(0xA9A9A9)  // Dark grey for shadow/depth
#define BOTTLE_CAP_RED      lv_color_hex(0xE32636)  // Bright red for cap
#define BOTTLE_CAP_RED_DARK lv_color_hex(0xB81D24)  // Dark red for cap shadow
#define GAS_FILL_COLOR      lv_color_hex(0xFFD700)  // Yellow/Gold for gas fill
#define VALVE_METAL         lv_color_hex(0x708090)  // Slate grey for valve
#define RING_METAL          lv_color_hex(0x778899)  // Light steel grey for rings
#define SCALE_COLOR         lv_color_hex(0x000000)  // Black for scale markings

// Dimensions for 160px high bottle (realistic proportions)
#define BOTTLE_HEIGHT      78   // Main body height (-5%: 82 * 0.95)
#define BOTTLE_WIDTH       51   // Body width
#define HANDLE_HEIGHT      23   // Handle on top
#define HANDLE_WIDTH      18   // Handle width
#define SIDE_HANDLE_WIDTH  14   // Side handles width
#define SIDE_HANDLE_HEIGHT 12   // Side handles height
#define RING_FOOT_HEIGHT   19   // Ring foot at bottom
#define FILL_MARGIN        3    // Margin for fill indicator
#define CORNER_RADIUS_TOP  20   // Very round corners at top
#define CORNER_RADIUS_BOTTOM 10 // Half roundness at bottom (50% of top)

/**
 * @brief Calculate fill percentage from weight
 */
static uint8_t calculate_fill_percent(womo_gas_bottle_t *bottle)
{
    if (bottle->full_weight_kg <= bottle->empty_weight_kg) {
        return 0; // Invalid configuration
    }
    
    float gas_weight = bottle->current_weight_kg - bottle->empty_weight_kg;
    float max_gas_weight = bottle->full_weight_kg - bottle->empty_weight_kg;
    
    if (gas_weight <= 0) {
        return 0;
    }
    
    if (gas_weight >= max_gas_weight) {
        return 100;
    }
    
    float percent = (gas_weight / max_gas_weight) * 100.0f;
    return (uint8_t)(percent + 0.5f); // Round to nearest integer
}

/**
 * @brief Update the visual fill level
 */
static void update_fill_visual(womo_gas_bottle_t *bottle)
{
    if (!bottle) {
        return;
    }

    if (!bottle->has_valid_weight) {
        if (bottle->fill_bar) {
            lv_obj_set_size(bottle->fill_bar, BOTTLE_WIDTH - 2 * FILL_MARGIN + 3, 0);
        }
        if (bottle->label) {
            lv_label_set_text(bottle->label, bottle->no_conn ? "nc" : "-- %");
        }
        return;
    }
    
    // Calculate fill height based on percentage
    uint8_t percent = bottle->fill_percent;
    lv_coord_t usable_height = BOTTLE_HEIGHT - 2 * FILL_MARGIN;  // 78 - 6 = 72px
    lv_coord_t fill_height = usable_height * percent / 100;      // z.B. 72 * 73 / 100 = 52px
    
    // Update fill bar size and position (fills from bottom up)
    // Koordinaten sind relativ zum bottle_body (Child-Parent-Beziehung)
    if (bottle->fill_bar) {
        // Breite: volle Flaschenbreite minus Margins (plus 3px für bündigen Abschluss)
        lv_coord_t fill_width = BOTTLE_WIDTH - 2 * FILL_MARGIN + 3;
        lv_obj_set_size(bottle->fill_bar, fill_width, fill_height);
        
        // Position: zentriert horizontal, von unten nach oben
        lv_coord_t fill_x = FILL_MARGIN - 3;  // 3px nach links korrigieren
        lv_coord_t fill_y = (BOTTLE_HEIGHT + 3) - fill_height - FILL_MARGIN - 3;  // -3 für Korrektur nach oben
        lv_obj_set_pos(bottle->fill_bar, fill_x, fill_y);
    }
    
    // Update label text (only percentage in foot)
    if (bottle->label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u%%", percent);
        lv_label_set_text(bottle->label, buf);
    }
}

womo_gas_bottle_t* womo_gas_bottle_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y)
{
    if (!parent) {
        ESP_LOGE(TAG, "Parent object is NULL");
        return NULL;
    }
    
    // Allocate memory for gas bottle structure
    womo_gas_bottle_t *bottle = malloc(sizeof(womo_gas_bottle_t));
    if (!bottle) {
        ESP_LOGE(TAG, "Failed to allocate memory for gas bottle");
        return NULL;
    }
    
    // Initialize structure
    memset(bottle, 0, sizeof(womo_gas_bottle_t));
    bottle->empty_weight_kg = 10.1f;  // Empty bottle weight (0%)
    bottle->full_weight_kg = 21.0f;   // Full bottle weight (100%)
    bottle->current_weight_kg = 10.1f;
    bottle->fill_percent = 0;
    bottle->has_valid_weight = false;
    bottle->no_conn = false;
    
    // Create main container (taller for all elements, breiter für Skala rechts)
    bottle->container = lv_obj_create(parent);
    lv_obj_set_size(bottle->container, BOTTLE_WIDTH + 40, BOTTLE_HEIGHT + HANDLE_HEIGHT + RING_FOOT_HEIGHT + 35);
    lv_obj_set_pos(bottle->container, x, y);
    lv_obj_clear_flag(bottle->container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(bottle->container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(bottle->container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(bottle->container, 0, 0);
    lv_obj_set_style_clip_corner(bottle->container, false, 0);  // Kein Clipping!
    lv_obj_add_flag(bottle->container, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    
    // Create bottle body (main cylinder with round top, less round bottom)
    bottle->bottle_body = lv_obj_create(bottle->container);
    lv_obj_set_size(bottle->bottle_body, BOTTLE_WIDTH, BOTTLE_HEIGHT + 3);  // +3px to overlap into ring foot
    lv_obj_set_pos(bottle->bottle_body, 5, HANDLE_HEIGHT);
    lv_obj_set_style_bg_color(bottle->bottle_body, BOTTLE_GREY, 0);
    lv_obj_set_style_bg_grad_color(bottle->bottle_body, BOTTLE_GREY_DARK, 0);
    lv_obj_set_style_bg_grad_dir(bottle->bottle_body, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(bottle->bottle_body, 2, 0);
    lv_obj_set_style_border_color(bottle->bottle_body, BOTTLE_GREY_DARK, 0);
    lv_obj_set_style_radius(bottle->bottle_body, CORNER_RADIUS_TOP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bottle->bottle_body, true, 0);  // Clip children to rounded shape
    lv_obj_set_style_pad_all(bottle->bottle_body, 0, 0);  // Kein Padding!
    // Note: LVGL doesn't support different corner radii for top/bottom on same object
    // We use top radius for whole object, foot will have its own radius
    lv_obj_set_style_shadow_width(bottle->bottle_body, 8, 0);
    lv_obj_set_style_shadow_opa(bottle->bottle_body, LV_OPA_30, 0);
    lv_obj_clear_flag(bottle->bottle_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bottle->bottle_body, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_CLICKABLE);
    
    
    // Create yellow fill bar inside bottle (shows gas level based on weight)
    bottle->fill_bar = lv_obj_create(bottle->bottle_body);
    lv_obj_set_size(bottle->fill_bar, BOTTLE_WIDTH - 2 * FILL_MARGIN + 3, 0);  // Etwas breiter, damit bündig
    lv_obj_set_pos(bottle->fill_bar, FILL_MARGIN, BOTTLE_HEIGHT);
    lv_obj_set_style_bg_color(bottle->fill_bar, GAS_FILL_COLOR, 0);
    lv_obj_set_style_bg_opa(bottle->fill_bar, LV_OPA_70, 0);  // Semi-transparent
    lv_obj_set_style_border_opa(bottle->fill_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(bottle->fill_bar, 5, 0);  // Kleiner Radius (5px statt 20px)
    lv_obj_set_style_pad_all(bottle->fill_bar, 0, 0);  // Kein Padding
    lv_obj_clear_flag(bottle->fill_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bottle->fill_bar, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_CLICKABLE);
    
    // Create ring foot at bottom (8px Radius with black border)
    bottle->ring_foot = lv_obj_create(bottle->container);
    lv_obj_set_size(bottle->ring_foot, BOTTLE_WIDTH, RING_FOOT_HEIGHT);
    lv_obj_set_pos(bottle->ring_foot, 5, HANDLE_HEIGHT + BOTTLE_HEIGHT);
    lv_obj_set_style_bg_color(bottle->ring_foot, BOTTLE_GREY, 0);
    lv_obj_set_style_bg_grad_color(bottle->ring_foot, BOTTLE_GREY_DARK, 0);
    lv_obj_set_style_bg_grad_dir(bottle->ring_foot, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(bottle->ring_foot, 2, 0);
    lv_obj_set_style_border_color(bottle->ring_foot, BOTTLE_GREY_DARK, 0);  // Same as bottle body
    lv_obj_set_style_radius(bottle->ring_foot, 8, 0);  // 8px Radius (etwas weniger rund als oben)
    lv_obj_clear_flag(bottle->ring_foot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bottle->ring_foot, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_CLICKABLE);
    
    // Create scale markings - horizontal tick marks at bottle edge (no numbers, with intermediate ticks)
    // Tick 100 (top) - REMOVED per user request
    
    // Tick 75 (intermediate) - small
    lv_obj_t *tick_75 = lv_obj_create(bottle->container);
    lv_obj_set_size(tick_75, 5, 2);
    lv_obj_set_pos(tick_75, 5, HANDLE_HEIGHT + BOTTLE_HEIGHT / 4);
    lv_obj_set_style_bg_color(tick_75, SCALE_COLOR, 0);
    lv_obj_set_style_border_width(tick_75, 0, 0);
    lv_obj_set_style_radius(tick_75, 0, 0);
    
    // Tick 50 (middle) - large
    lv_obj_t *tick_50 = lv_obj_create(bottle->container);
    lv_obj_set_size(tick_50, 10, 2);
    lv_obj_set_pos(tick_50, 5, HANDLE_HEIGHT + BOTTLE_HEIGHT / 2);
    lv_obj_set_style_bg_color(tick_50, SCALE_COLOR, 0);
    lv_obj_set_style_border_width(tick_50, 0, 0);
    lv_obj_set_style_radius(tick_50, 0, 0);
    
    // Tick 25 (intermediate) - small
    lv_obj_t *tick_25 = lv_obj_create(bottle->container);
    lv_obj_set_size(tick_25, 5, 2);
    lv_obj_set_pos(tick_25, 5, HANDLE_HEIGHT + 3 * BOTTLE_HEIGHT / 4);
    lv_obj_set_style_bg_color(tick_25, SCALE_COLOR, 0);
    lv_obj_set_style_border_width(tick_25, 0, 0);
    lv_obj_set_style_radius(tick_25, 0, 0);
    
    // Tick 0 (bottom) - REMOVED per user request
    
    // No scale labels (numbers removed)
    bottle->scale_100 = NULL;
    bottle->scale_50 = NULL;
    bottle->scale_0 = NULL;
    
    // Create handle on top (roter Deckel)
    bottle->cap_handle = lv_obj_create(bottle->container);
    lv_obj_set_size(bottle->cap_handle, HANDLE_WIDTH, HANDLE_HEIGHT);
    lv_obj_set_pos(bottle->cap_handle, (BOTTLE_WIDTH + 10 - HANDLE_WIDTH) / 2, 0);
    lv_obj_set_style_bg_color(bottle->cap_handle, BOTTLE_CAP_RED, 0);
    lv_obj_set_style_bg_grad_color(bottle->cap_handle, BOTTLE_CAP_RED_DARK, 0);
    lv_obj_set_style_bg_grad_dir(bottle->cap_handle, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(bottle->cap_handle, 1, 0);
    lv_obj_set_style_border_color(bottle->cap_handle, lv_color_hex(0x8B0000), 0);
    lv_obj_set_style_radius(bottle->cap_handle, 4, 0);
    lv_obj_clear_flag(bottle->cap_handle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bottle->cap_handle, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_CLICKABLE);

    bottle->cap_label = lv_label_create(bottle->cap_handle);
    lv_label_set_text(bottle->cap_label, "");
    lv_obj_set_style_text_font(bottle->cap_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(bottle->cap_label, lv_color_black(), 0);
    lv_obj_center(bottle->cap_label);
    
    // Halter entfernt - werden nicht mehr erstellt!
    
    // Create label for percentage in foot area
    bottle->label = lv_label_create(bottle->ring_foot);
    lv_label_set_text(bottle->label, "-- %");
    lv_obj_set_style_text_font(bottle->label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bottle->label, lv_color_black(), 0);
    lv_obj_set_style_text_align(bottle->label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(bottle->label);  // Zentriert im Fuß
    
    ESP_LOGI(TAG, "Gas bottle widget created at (%d, %d)", x, y);
    
    // Initial update
    update_fill_visual(bottle);
    
    return bottle;
}

void womo_gas_bottle_update_weight(womo_gas_bottle_t *bottle, float weight_kg)
{
    if (!bottle) {
        ESP_LOGW(TAG, "Gas bottle is NULL");
        return;
    }
    
    bottle->current_weight_kg = weight_kg;
    bottle->has_valid_weight = true;
    bottle->no_conn = false;
    bottle->fill_percent = calculate_fill_percent(bottle);
    
    ESP_LOGD(TAG, "Updated weight: %.1f kg, fill: %u%%", weight_kg, bottle->fill_percent);
    
    update_fill_visual(bottle);
}

void womo_gas_bottle_set_percent(womo_gas_bottle_t *bottle, float percent)
{
    if (!bottle) {
        ESP_LOGW(TAG, "Gas bottle is NULL");
        return;
    }

    if (!isfinite(percent)) {
        womo_gas_bottle_set_no_data(bottle);
        return;
    }

    if (percent < 0.0f) {
        percent = 0.0f;
    } else if (percent > 100.0f) {
        percent = 100.0f;
    }

    bottle->fill_percent = (uint8_t)(percent + 0.5f);
    bottle->has_valid_weight = true;
    bottle->no_conn = false;
    bottle->no_conn = false;

    ESP_LOGD(TAG, "Updated percent: %.1f%% (rounded %u%%)", percent, bottle->fill_percent);

    update_fill_visual(bottle);
}

void womo_gas_bottle_set_empty_weight(womo_gas_bottle_t *bottle, float empty_weight_kg)
{
    if (!bottle) {
        ESP_LOGW(TAG, "Gas bottle is NULL");
        return;
    }
    
    bottle->empty_weight_kg = empty_weight_kg;
    bottle->no_conn = false;
    bottle->fill_percent = calculate_fill_percent(bottle);
    
    ESP_LOGI(TAG, "Set empty weight: %.1f kg", empty_weight_kg);
    
    update_fill_visual(bottle);
}

void womo_gas_bottle_set_full_weight(womo_gas_bottle_t *bottle, float full_weight_kg)
{
    if (!bottle) {
        ESP_LOGW(TAG, "Gas bottle is NULL");
        return;
    }
    
    bottle->full_weight_kg = full_weight_kg;
    bottle->no_conn = false;
    bottle->fill_percent = calculate_fill_percent(bottle);
    
    ESP_LOGI(TAG, "Set full weight: %.1f kg", full_weight_kg);
    
    update_fill_visual(bottle);
}

void womo_gas_bottle_set_no_data(womo_gas_bottle_t *bottle)
{
    if (!bottle) {
        ESP_LOGW(TAG, "Gas bottle is NULL");
        return;
    }

    bottle->has_valid_weight = false;
    bottle->no_conn = false;
    bottle->fill_percent = 0;
    update_fill_visual(bottle);
}

void womo_gas_bottle_set_nc(womo_gas_bottle_t *bottle)
{
    if (!bottle) {
        ESP_LOGW(TAG, "Gas bottle is NULL");
        return;
    }

    bottle->has_valid_weight = false;
    bottle->no_conn = true;
    bottle->fill_percent = 0;
    update_fill_visual(bottle);
}

uint8_t womo_gas_bottle_get_fill_percent(womo_gas_bottle_t *bottle)
{
    if (!bottle) {
        return 0;
    }
    
    return bottle->fill_percent;
}

void womo_gas_bottle_set_cap_label(womo_gas_bottle_t *bottle, const char *text)
{
    if (!bottle || !bottle->cap_label) {
        return;
    }

    if (!text) {
        text = "";
    }

    lv_label_set_text(bottle->cap_label, text);
}

void womo_gas_bottle_set_pos(womo_gas_bottle_t *bottle, lv_coord_t x, lv_coord_t y)
{
    if (!bottle || !bottle->container) {
        ESP_LOGW(TAG, "Gas bottle or container is NULL");
        return;
    }
    
    lv_obj_set_pos(bottle->container, x, y);
}

void womo_gas_bottle_set_visible(womo_gas_bottle_t *bottle, bool visible)
{
    if (!bottle || !bottle->container) {
        ESP_LOGW(TAG, "Gas bottle or container is NULL");
        return;
    }
    
    if (visible) {
        lv_obj_clear_flag(bottle->container, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(bottle->container, LV_OBJ_FLAG_HIDDEN);
    }
}

void womo_gas_bottle_set_status(womo_gas_bottle_t *bottle, womo_status_level_t status)
{
    if (!bottle || !bottle->ring_foot) {
        return;
    }

    // Fuß mit Prozentanzeige bleibt immer neutral, unabhängig vom Status.
    lv_color_t foot_main_color = BOTTLE_GREY;
    lv_color_t foot_grad_color = BOTTLE_GREY_DARK;

    // Rumpf bekommt Statusfarben, Füllstand bleibt Standard-Gelb.
    lv_color_t main_color = BOTTLE_GREY;
    lv_color_t grad_color = BOTTLE_GREY_DARK;

    switch (status) {
    case WOMO_STATUS_WARNING:
        main_color = lv_color_hex(0xFFA500); // Orange for warning
        grad_color = lv_color_hex(0xE68A00);
        break;
    case WOMO_STATUS_ERROR:
    case WOMO_STATUS_CRITICAL:
        main_color = lv_color_hex(0xE53935); // Red for critical/error
        grad_color = lv_color_hex(0xC62828);
        break;
    case WOMO_STATUS_OK:
    default:
        break;
    }

    // Fuß einfärben (immer Standardfarbe)
    lv_obj_set_style_bg_color(bottle->ring_foot, foot_main_color, 0);
    lv_obj_set_style_bg_grad_color(bottle->ring_foot, foot_grad_color, 0);

    // Rumpf einfärben
    if (bottle->bottle_body) {
        lv_obj_set_style_bg_color(bottle->bottle_body, main_color, 0);
        lv_obj_set_style_bg_grad_color(bottle->bottle_body, grad_color, 0);
    }

    // Füllstand immer in Standardfarbe (nicht Warn-/Fehlerfarbe)
    if (bottle->fill_bar) {
        lv_obj_set_style_bg_color(bottle->fill_bar, GAS_FILL_COLOR, 0);
    }
}

void womo_gas_bottle_delete(womo_gas_bottle_t *bottle)
{
    if (!bottle) {
        return;
    }
    
    // Delete LVGL objects (this will also delete child objects)
    if (bottle->container) {
        lv_obj_del(bottle->container);
    }
    
    // Free memory
    free(bottle);
    
    ESP_LOGI(TAG, "Gas bottle widget deleted");
}