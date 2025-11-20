#ifndef WOMO_GAS_BOTTLE_H
#define WOMO_GAS_BOTTLE_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Gas bottle widget structure
 */
typedef struct {
    lv_obj_t *container;         // Main container
    lv_obj_t *bottle_body;       // Bottle body (grey cylinder)
    lv_obj_t *cap_handle;        // Handle on cap
    lv_obj_t *cap_label;         // Label on top of cap (e.g. V/H)
    lv_obj_t *ring_foot;         // Ring foot at bottom
    lv_obj_t *fill_bar;          // Yellow fill level indicator
    lv_obj_t *scale_0;           // Scale label 0
    lv_obj_t *scale_50;          // Scale label 50
    lv_obj_t *scale_100;         // Scale label 100
    lv_obj_t *label;             // Text label with weight/percentage
    
    float current_weight_kg; // Current weight
    float empty_weight_kg;   // Empty bottle weight (tare) - 10.1 kg
    float full_weight_kg;    // Full bottle weight - 21 kg
    uint8_t fill_percent;    // Current fill percentage
    bool has_valid_weight;   // True once we received valid load data
} womo_gas_bottle_t;

/**
 * @brief Create a new gas bottle widget
 * @param parent Parent object to attach to
 * @param x X position
 * @param y Y position
 * @return Pointer to gas bottle structure
 */
womo_gas_bottle_t* womo_gas_bottle_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y);

/**
 * @brief Update gas bottle with new weight data
 * @param bottle Pointer to gas bottle structure
 * @param weight_kg Current weight in kg
 */
void womo_gas_bottle_update_weight(womo_gas_bottle_t *bottle, float weight_kg);

/**
 * @brief Set the empty weight (tare) of the bottle
 * @param bottle Pointer to gas bottle structure
 * @param empty_weight_kg Empty bottle weight in kg
 */
void womo_gas_bottle_set_empty_weight(womo_gas_bottle_t *bottle, float empty_weight_kg);

/**
 * @brief Set the full weight of the bottle (empty + 11kg gas)
 * @param bottle Pointer to gas bottle structure
 * @param full_weight_kg Full bottle weight in kg
 */
void womo_gas_bottle_set_full_weight(womo_gas_bottle_t *bottle, float full_weight_kg);

/**
 * @brief Mark bottle as having no valid weight data (shows placeholder)
 */
void womo_gas_bottle_set_no_data(womo_gas_bottle_t *bottle);

/**
 * @brief Get current fill percentage
 * @param bottle Pointer to gas bottle structure
 * @return Fill percentage (0-100)
 */
uint8_t womo_gas_bottle_get_fill_percent(womo_gas_bottle_t *bottle);

/**
 * @brief Set cap label text (e.g. "V" or "H")
 */
void womo_gas_bottle_set_cap_label(womo_gas_bottle_t *bottle, const char *text);

/**
 * @brief Delete gas bottle widget and free memory
 * @param bottle Pointer to gas bottle structure
 */
void womo_gas_bottle_delete(womo_gas_bottle_t *bottle);

/**
 * @brief Set the position of the gas bottle
 * @param bottle Pointer to gas bottle structure
 * @param x X position
 * @param y Y position
 */
void womo_gas_bottle_set_pos(womo_gas_bottle_t *bottle, lv_coord_t x, lv_coord_t y);

/**
 * @brief Show/hide the gas bottle
 * @param bottle Pointer to gas bottle structure
 * @param visible true to show, false to hide
 */
void womo_gas_bottle_set_visible(womo_gas_bottle_t *bottle, bool visible);

#ifdef __cplusplus
}
#endif

#endif // WOMO_GAS_BOTTLE_H