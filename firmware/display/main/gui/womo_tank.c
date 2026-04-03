#include "womo_tank.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "tank_widget";

#define TANK_BODY_WIDTH        52
#define TANK_BODY_HEIGHT       82
#define TANK_BODY_BORDER        2
#define TANK_FILL_MARGIN        0
#define TANK_CONTAINER_EXTRA   36
#define TANK_BODY_OFFSET_X     10
#define TANK_BODY_OFFSET_Y      8

#define INLET_WIDTH            16
#define INLET_HEIGHT           10
#define OUTLET_SPOUT_WIDTH     18
#define OUTLET_SPOUT_HEIGHT     8

#define TANK_BODY_COLOR      lv_color_hex(0xD9DDE3)
#define TANK_BODY_BORDER_CLR lv_color_hex(0x8A929E)
#define CONNECTOR_COLOR      lv_color_hex(0x6C737F)
#define CONNECTOR_COLOR_DARK lv_color_hex(0x202020)
#define FRESH_WATER_COLOR    lv_color_hex(0x6AA9FF)  /* hellblau */
#define GREY_WATER_COLOR     lv_color_hex(0x70849B)  /* graublau */
#define SCALE_COLOR          lv_color_hex(0x000000)  /* schwarz für Skalenstriche */

// Hilfsmakro: einen Skalenstrich auf dem Container erzeugen
// x_pos: X relativ zu Container-Ursprung, y_off: Y-Offset ab Tankoberkante
#define MAKE_TICK(parent, x_pos, y_off, w, h) do { \
    lv_obj_t *_t = lv_obj_create(parent); \
    lv_obj_set_size(_t, w, h); \
    lv_obj_set_pos(_t, x_pos, (y_off)); \
    lv_obj_set_style_bg_color(_t, SCALE_COLOR, 0); \
    lv_obj_set_style_border_width(_t, 0, 0); \
    lv_obj_set_style_radius(_t, 0, 0); \
    lv_obj_clear_flag(_t, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE); \
} while(0)

static void update_fill_visual(womo_tank_t *tank)
{
    if (!tank || !tank->tank_body || !tank->fill) {
        return;
    }

    lv_coord_t usable_height = TANK_BODY_HEIGHT - 2 * TANK_FILL_MARGIN;

    if (!tank->has_valid_level) {
        lv_coord_t fill_width = TANK_BODY_WIDTH - 2 * TANK_FILL_MARGIN;
        lv_obj_set_size(tank->fill, fill_width, 0);
        lv_obj_set_pos(tank->fill,
                       TANK_FILL_MARGIN,
                       TANK_BODY_HEIGHT - TANK_FILL_MARGIN);
        if (tank->percent_label) {
            lv_label_set_text(tank->percent_label, "-- %");
        }
        return;
    }

    lv_coord_t fill_height = usable_height * tank->level_percent / 100;
    lv_coord_t fill_width = TANK_BODY_WIDTH - 2 * TANK_FILL_MARGIN;
    lv_coord_t fill_x = TANK_FILL_MARGIN;
    lv_coord_t fill_y = (TANK_BODY_HEIGHT - TANK_FILL_MARGIN) - fill_height;

    lv_obj_set_size(tank->fill, fill_width, fill_height);
    lv_obj_set_pos(tank->fill, fill_x, fill_y);

    if (tank->percent_label) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u%%", tank->level_percent);
        lv_label_set_text(tank->percent_label, buf);
    }
}

static lv_color_t water_color_for_type(womo_tank_type_t type)
{
    return (type == WOMO_TANK_GREY) ? GREY_WATER_COLOR : FRESH_WATER_COLOR;
}

static void apply_status_colors(womo_tank_t *tank, womo_status_level_t status)
{
    if (!tank || !tank->tank_body) {
        return;
    }

    lv_color_t body_main = TANK_BODY_COLOR;
    // Border bleibt immer im Standardgrau

    switch (status) {
    case WOMO_STATUS_WARNING:
        body_main = lv_color_hex(0xFFA500); // Orange warning
        break;
    case WOMO_STATUS_ERROR:
    case WOMO_STATUS_CRITICAL:
        body_main = lv_color_hex(0xE53935); // Red critical/error
        break;
    case WOMO_STATUS_OK:
    default:
        break;
    }

    lv_obj_set_style_bg_color(tank->tank_body, body_main, 0);

    // Füllstand bleibt immer in der Tank-Standardfarbe.
    if (tank->fill) {
        lv_color_t fill_base = water_color_for_type(tank->type);
        lv_obj_set_style_bg_color(tank->fill, fill_base, 0);
        lv_obj_set_style_bg_grad_color(tank->fill, lv_color_darken(fill_base, 40), 0);
    }
}

womo_tank_t *womo_tank_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, womo_tank_type_t type)
{
    if (!parent) {
        ESP_LOGE(TAG, "Parent is NULL");
        return NULL;
    }

    womo_tank_t *tank = calloc(1, sizeof(womo_tank_t));
    if (!tank) {
        ESP_LOGE(TAG, "Allocation failed");
        return NULL;
    }

    tank->type = type;
    tank->level_percent = 0;
    tank->has_valid_level = false;

    tank->container = lv_obj_create(parent);
    lv_obj_set_size(tank->container,
                    TANK_BODY_WIDTH + 30,
                    TANK_BODY_HEIGHT + TANK_CONTAINER_EXTRA);
    lv_obj_set_pos(tank->container, x, y);
    lv_obj_set_style_bg_opa(tank->container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(tank->container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(tank->container, 0, 0);
    lv_obj_clear_flag(tank->container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    tank->tank_body = lv_obj_create(tank->container);
    lv_obj_set_size(tank->tank_body, TANK_BODY_WIDTH, TANK_BODY_HEIGHT);
    lv_obj_set_pos(tank->tank_body, TANK_BODY_OFFSET_X, TANK_BODY_OFFSET_Y);
    lv_obj_set_style_bg_color(tank->tank_body, TANK_BODY_COLOR, 0);
    lv_obj_set_style_border_width(tank->tank_body, TANK_BODY_BORDER, 0);
    lv_obj_set_style_border_color(tank->tank_body, TANK_BODY_BORDER_CLR, 0);
    lv_obj_set_style_radius(tank->tank_body, 6, 0);
    lv_obj_set_style_pad_all(tank->tank_body, 0, 0);
    lv_obj_clear_flag(tank->tank_body, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    tank->fill = lv_obj_create(tank->tank_body);
    lv_obj_set_style_bg_color(tank->fill, water_color_for_type(type), 0);
    lv_obj_set_style_bg_grad_dir(tank->fill, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_color(tank->fill,
                                   lv_color_darken(water_color_for_type(type), 40),
                                   0);
    lv_obj_set_style_border_opa(tank->fill, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(tank->fill, 4, 0);
    lv_obj_set_style_pad_all(tank->fill, 0, 0);
    lv_obj_clear_flag(tank->fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Skalenstriche INNEN an der linken Wand des Tank-Körpers (wie bei Gasflaschen):
    // 75 % – kleiner Strich
    MAKE_TICK(tank->tank_body, 0, TANK_BODY_HEIGHT / 4,      5, 2);
    // 50 % – großer Strich (Mitte)
    MAKE_TICK(tank->tank_body, 0, TANK_BODY_HEIGHT / 2,     10, 2);
    // 25 % – kleiner Strich
    MAKE_TICK(tank->tank_body, 0, 3 * TANK_BODY_HEIGHT / 4,  5, 2);

    tank->inlet = lv_obj_create(tank->container);
    lv_obj_set_size(tank->inlet, INLET_WIDTH, INLET_HEIGHT);
    lv_obj_set_pos(tank->inlet,
                   TANK_BODY_OFFSET_X + (TANK_BODY_WIDTH / 3) - INLET_WIDTH / 2,
                   TANK_BODY_OFFSET_Y - INLET_HEIGHT + 2);
    lv_obj_set_style_bg_color(tank->inlet, CONNECTOR_COLOR_DARK, 0);
    lv_obj_set_style_border_width(tank->inlet, 0, 0);
    lv_obj_set_style_radius(tank->inlet, 2, 0);
    lv_obj_clear_flag(tank->inlet, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    tank->outlet_pipe = lv_obj_create(tank->tank_body);
    lv_obj_set_size(tank->outlet_pipe, 0, 0);
    lv_obj_add_flag(tank->outlet_pipe, LV_OBJ_FLAG_HIDDEN);

    tank->outlet_spout = lv_obj_create(tank->container);
    lv_obj_set_size(tank->outlet_spout, OUTLET_SPOUT_WIDTH, OUTLET_SPOUT_HEIGHT);
    lv_obj_set_pos(tank->outlet_spout,
                   TANK_BODY_OFFSET_X + TANK_BODY_WIDTH - OUTLET_SPOUT_WIDTH + 5,
                   TANK_BODY_OFFSET_Y + TANK_BODY_HEIGHT - OUTLET_SPOUT_HEIGHT);
    lv_obj_set_style_bg_color(tank->outlet_spout, CONNECTOR_COLOR_DARK, 0);
    lv_obj_set_style_border_width(tank->outlet_spout, 0, 0);
    lv_obj_set_style_radius(tank->outlet_spout, 2, 0);
    lv_obj_clear_flag(tank->outlet_spout, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(tank->outlet_spout);

    // Frischwasser: Auslauf ausblenden, Grauwasser: Zulauf ausblenden
    if (type == WOMO_TANK_FRESH) {
        lv_obj_add_flag(tank->outlet_spout, LV_OBJ_FLAG_HIDDEN);
    } else if (type == WOMO_TANK_GREY) {
        lv_obj_add_flag(tank->inlet, LV_OBJ_FLAG_HIDDEN);
    }

    tank->percent_label = lv_label_create(tank->tank_body);
    lv_label_set_text(tank->percent_label, "-- %");
    lv_obj_set_style_text_font(tank->percent_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tank->percent_label, lv_color_black(), 0);
    lv_obj_align(tank->percent_label, LV_ALIGN_CENTER, 0, 0);

    tank->caption_label = lv_label_create(tank->container);
    lv_label_set_text(tank->caption_label, "");
    lv_obj_set_style_text_font(tank->caption_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(tank->caption_label, lv_color_black(), 0);
    lv_obj_align(tank->caption_label, LV_ALIGN_BOTTOM_MID, 0, -4);

    update_fill_visual(tank);

    ESP_LOGI(TAG, "Tank widget created at (%d, %d)", x, y);
    return tank;
}

void womo_tank_delete(womo_tank_t *tank)
{
    if (!tank) {
        return;
    }

    if (tank->container) {
        lv_obj_del(tank->container);
    }
    free(tank);
}

void womo_tank_set_level(womo_tank_t *tank, uint8_t percent)
{
    if (!tank) {
        ESP_LOGW(TAG, "Tank is NULL");
        return;
    }

    if (percent > 100) {
        percent = 100;
    }

    tank->level_percent = percent;
    tank->has_valid_level = true;

    update_fill_visual(tank);
}

void womo_tank_set_no_data(womo_tank_t *tank)
{
    if (!tank) {
        ESP_LOGW(TAG, "Tank is NULL");
        return;
    }

    tank->has_valid_level = false;
    tank->level_percent = 0;

    update_fill_visual(tank);
}

void womo_tank_set_caption(womo_tank_t *tank, const char *text)
{
    if (!tank || !tank->caption_label) {
        return;
    }

    if (!text) {
        text = "";
    }

    lv_label_set_text(tank->caption_label, text);
}

void womo_tank_set_text_color(womo_tank_t *tank, lv_color_t color)
{
    if (!tank) {
        return;
    }

    if (tank->percent_label) {
        lv_obj_set_style_text_color(tank->percent_label, color, 0);
    }
    if (tank->caption_label) {
        lv_obj_set_style_text_color(tank->caption_label, color, 0);
    }
}

void womo_tank_set_pos(womo_tank_t *tank, lv_coord_t x, lv_coord_t y)
{
    if (!tank || !tank->container) {
        return;
    }

    lv_obj_set_pos(tank->container, x, y);
}

void womo_tank_set_status(womo_tank_t *tank, womo_status_level_t status)
{
    apply_status_colors(tank, status);
}
