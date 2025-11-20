#ifndef WOMO_BATTERY_H
#define WOMO_BATTERY_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Battery widget structure
 */
typedef struct {
    lv_obj_t *container;          // Main container
    lv_obj_t *battery_body;       // Main battery body (rectangular)
    lv_obj_t *connection_block;   // Connection block on top (overhangs left/right)
    lv_obj_t *terminal_plus;      // Positive terminal (right side)
    lv_obj_t *terminal_minus;     // Negative terminal (left side)
    lv_obj_t *fill_bar;          // Battery charge level indicator
    lv_obj_t *voltage_label;     // Voltage display on battery
    lv_obj_t *percent_label;     // Percentage label (optional)
    float voltage_v;             // Current voltage
    float min_voltage_v;         // Minimum voltage (0%)
    float max_voltage_v;         // Maximum voltage (100%)
    uint8_t charge_percent;      // Calculated charge percentage
    bool show_percent;           // Show percentage label
    bool has_valid_voltage;      // Indicates whether a real voltage has been received
} womo_battery_t;

/**
 * @brief Create battery widget
 * @param parent Parent object (screen)
 * @param x X position
 * @param y Y position
 * @return Battery widget instance or NULL on error
 */
womo_battery_t* womo_battery_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y);

/**
 * @brief Update battery voltage
 * @param battery Battery widget instance
 * @param voltage_v Voltage in Volts
 */
void womo_battery_set_voltage(womo_battery_t *battery, float voltage_v);

/**
 * @brief Set battery voltage range for percentage calculation
 * @param battery Battery widget instance
 * @param min_voltage_v Minimum voltage (0%)
 * @param max_voltage_v Maximum voltage (100%)
 */
void womo_battery_set_voltage_range(womo_battery_t *battery, float min_voltage_v, float max_voltage_v);

/**
 * @brief Mark battery as having no valid voltage yet (placeholder state)
 */
void womo_battery_set_no_data(womo_battery_t *battery);

/**
 * @brief Enable/disable percentage display
 * @param battery Battery widget instance
 * @param show_percent Show percentage label
 */
void womo_battery_set_show_percent(womo_battery_t *battery, bool show_percent);

/**
 * @brief Get current charge percentage
 * @param battery Battery widget instance
 * @return Charge percentage (0-100)
 */
uint8_t womo_battery_get_charge_percent(womo_battery_t *battery);

/**
 * @brief Set battery widget position
 * @param battery Battery widget instance
 * @param x X coordinate
 * @param y Y coordinate
 */
void womo_battery_set_pos(womo_battery_t *battery, lv_coord_t x, lv_coord_t y);

/**
 * @brief Set battery widget visibility
 * @param battery Battery widget instance
 * @param visible Visibility state
 */
void womo_battery_set_visible(womo_battery_t *battery, bool visible);

/**
 * @brief Delete battery widget
 * @param battery Battery widget instance
 */
void womo_battery_delete(womo_battery_t *battery);

#endif // WOMO_BATTERY_H