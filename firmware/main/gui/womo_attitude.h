#ifndef WOMO_ATTITUDE_H
#define WOMO_ATTITUDE_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t *container;
    lv_obj_t *horizon;
    lv_obj_t *sky;
    lv_obj_t *ground;
    lv_obj_t *zero_axis;
    lv_obj_t *scale_left;
    lv_obj_t *scale_right;
    lv_obj_t *pitch_axis;
    lv_obj_t *vehicle;
    lv_obj_t *vehicle_body;
    lv_obj_t *vehicle_wheel_left;
    lv_obj_t *vehicle_wheel_right;
    lv_obj_t *vehicle_mirror_left;
    lv_obj_t *vehicle_mirror_right;
    lv_obj_t *vehicle_mirror_stem_left;
    lv_obj_t *vehicle_mirror_stem_right;
    lv_obj_t *vehicle_roof_line;
    lv_obj_t *pitch_label;
    lv_obj_t *roll_label;
    lv_coord_t diameter;
    lv_coord_t vehicle_height;
    lv_coord_t vehicle_center_offset;
    lv_coord_t vehicle_x_offset;
    float raw_roll_deg;
    float raw_pitch_deg;
    float roll_deg;
    float pitch_deg;
    float current_roll_deg;
    float current_pitch_deg;
} womo_attitude_t;

womo_attitude_t *womo_attitude_create(lv_obj_t *parent, lv_coord_t diameter);
void womo_attitude_update(womo_attitude_t *att, float roll_deg, float pitch_deg);
void womo_attitude_delete(womo_attitude_t *att);

#ifdef __cplusplus
}
#endif

#endif // WOMO_ATTITUDE_H
