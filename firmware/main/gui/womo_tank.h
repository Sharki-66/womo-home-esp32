#ifndef WOMO_TANK_H
#define WOMO_TANK_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WOMO_TANK_FRESH = 0,
    WOMO_TANK_GREY
} womo_tank_type_t;

typedef struct {
    lv_obj_t *container;
    lv_obj_t *tank_body;
    lv_obj_t *fill;
    lv_obj_t *inlet;
    lv_obj_t *outlet_pipe;
    lv_obj_t *outlet_spout;
    lv_obj_t *percent_label;
    lv_obj_t *caption_label;
    womo_tank_type_t type;
    uint8_t level_percent;
    bool has_valid_level;
} womo_tank_t;

womo_tank_t *womo_tank_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, womo_tank_type_t type);
void womo_tank_delete(womo_tank_t *tank);
void womo_tank_set_level(womo_tank_t *tank, uint8_t percent);
void womo_tank_set_no_data(womo_tank_t *tank);
void womo_tank_set_caption(womo_tank_t *tank, const char *text);
void womo_tank_set_text_color(womo_tank_t *tank, lv_color_t color);
void womo_tank_set_pos(womo_tank_t *tank, lv_coord_t x, lv_coord_t y);

#ifdef __cplusplus
}
#endif

#endif // WOMO_TANK_H
