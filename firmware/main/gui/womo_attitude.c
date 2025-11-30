#include "womo_attitude.h"
#include <stdio.h>
#include <math.h>

#define ROLL_LIMIT_DEG        15.0f
#define PITCH_LIMIT_DEG       15.0f
#define SCALE_MINOR_STEP_DEG   5
#define SCALE_MAJOR_STEP_DEG   5
#define SCALE_TICK_LENGTH     14
#define SCALE_TICK_THICKNESS   2
#define ROLL_TICK_LENGTH_MAJOR 22
#define ROLL_TICK_LENGTH_MINOR 12
#define ROLL_TICK_LENGTH_SUB    ((ROLL_TICK_LENGTH_MINOR * 3) / 4)
#define ROLL_LABEL_DISTANCE    20
#define RAD_TO_DEG             57.29577951308232f
#define DEG_TO_RAD             0.017453292519943295f
#define PITCH_AXIS_WIDTH      66
#define PITCH_TICK_LENGTH_MAJOR 34
#define PITCH_TICK_LENGTH_MINOR 22
#define PITCH_LABEL_GAP         8
#define PITCH_TICK_LENGTH_SUB   ((PITCH_TICK_LENGTH_MINOR * 3) / 4)

static float clampf(float value, float min_val, float max_val)
{
    if (value < min_val) {
        return min_val;
    }
    if (value > max_val) {
        return max_val;
    }
    return value;
}

static float normalize_angle(float angle_deg)
{
    angle_deg = fmodf(angle_deg, 360.0f);
    if (angle_deg > 180.0f) {
        angle_deg -= 360.0f;
    } else if (angle_deg < -180.0f) {
        angle_deg += 360.0f;
    }
    return angle_deg;
}

static float normalize_level_angle(float angle_deg)
{
    angle_deg = normalize_angle(angle_deg);
    if (angle_deg > 90.0f) {
        angle_deg = 180.0f - angle_deg;
    } else if (angle_deg < -90.0f) {
        angle_deg = -180.0f - angle_deg;
    }
    return angle_deg;
}

static float display_magnitude(float angle_deg, float limit_deg)
{
    float folded = fabsf(normalize_level_angle(angle_deg));
    if (folded > limit_deg) {
        folded = limit_deg;
    }
    return folded;
}

static void create_scale_mark(lv_obj_t *parent,
                              lv_coord_t radius,
                              float y_offset,
                              int degree,
                              bool left_side,
                              bool show_label,
                              float tick_length_override)
{
    float radius_f = (float)radius;
    float y = y_offset;
    if (radius_f <= 0.0f) {
        return;
    }
    if (fabsf(y) > radius_f) {
        y = copysignf(radius_f, y);
    }

    float x = sqrtf(fmaxf(radius_f * radius_f - y * y, 0.0f));
    if (left_side) {
        x = -x;
    }

    float tick_length = tick_length_override > 0.0f
                             ? tick_length_override
                             : (show_label ? ROLL_TICK_LENGTH_MAJOR : ROLL_TICK_LENGTH_MINOR);
    float dir_x = (-x) / radius_f;
    float dir_y = (-y) / radius_f;

    float outer_x = radius_f + x;
    float outer_y = radius_f + y;
    float inner_x = outer_x + dir_x * tick_length;
    float inner_y = outer_y + dir_y * tick_length;
    float center_x = (outer_x + inner_x) * 0.5f;
    float center_y = (outer_y + inner_y) * 0.5f;

    lv_coord_t tick_len_px = (lv_coord_t)lroundf(tick_length);
    if (tick_len_px < 1) {
        tick_len_px = 1;
    }

    lv_obj_t *tick = lv_obj_create(parent);
    lv_obj_set_size(tick, tick_len_px, SCALE_TICK_THICKNESS);
    lv_obj_set_style_bg_color(tick, lv_color_hex(0xE0E3E7), 0);
    lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tick, 0, 0);
    lv_obj_set_style_pad_all(tick, 0, 0);
    lv_obj_set_style_radius(tick, SCALE_TICK_THICKNESS / 2, 0);
    lv_obj_clear_flag(tick, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_transform_pivot_x(tick, tick_len_px / 2, 0);
    lv_obj_set_style_transform_pivot_y(tick, SCALE_TICK_THICKNESS / 2, 0);

    float angle_deg = atan2f(dir_y, dir_x) * RAD_TO_DEG;
    int16_t angle_ddeg = (int16_t)lroundf(angle_deg * 10.0f);
    lv_obj_set_style_transform_angle(tick, angle_ddeg, 0);

    float offset_center_x = center_x - radius_f;
    float offset_center_y = center_y - radius_f;
    lv_obj_align(tick,
                 LV_ALIGN_CENTER,
                 (lv_coord_t)lroundf(offset_center_x),
                 (lv_coord_t)lroundf(offset_center_y));

    if (show_label) {
        lv_obj_t *label = lv_label_create(parent);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d°", degree);
        lv_label_set_text(label, buf);
        lv_obj_set_style_text_color(label, lv_color_hex(0xE0E3E7), 0);

        float label_distance = tick_length + ROLL_LABEL_DISTANCE;
        float offset_x = x + dir_x * label_distance;
        float offset_y = y + dir_y * label_distance;
        lv_obj_align(label,
                     LV_ALIGN_CENTER,
                     (lv_coord_t)lroundf(offset_x),
                     (lv_coord_t)lroundf(offset_y));
    }
}

static void create_scale_marks(lv_obj_t *parent, lv_coord_t diameter)
{
    const float radius = (float)diameter / 2.0f;
    const lv_coord_t radius_px = (lv_coord_t)lroundf(radius);

    const int roll_limit_deg = (int)ROLL_LIMIT_DEG;

    for (int degree = 0; degree <= roll_limit_deg; degree += SCALE_MINOR_STEP_DEG) {
        float ratio = (ROLL_LIMIT_DEG > 0.0f) ? ((float)degree / ROLL_LIMIT_DEG) : 0.0f;
        float offset = radius * ratio;
        bool show_label = (degree % SCALE_MAJOR_STEP_DEG == 0) && degree < roll_limit_deg;

        create_scale_mark(parent, radius_px, -offset, degree, true, show_label, 0.0f);
        if (degree > 0) {
            create_scale_mark(parent, radius_px, offset, degree, true, show_label, 0.0f);
        }
        create_scale_mark(parent, radius_px, -offset, degree, false, show_label, 0.0f);
        if (degree > 0) {
            create_scale_mark(parent, radius_px, offset, degree, false, show_label, 0.0f);
        }
    }

    for (int degree = 1; degree < roll_limit_deg; ++degree) {
        if (degree % SCALE_MINOR_STEP_DEG == 0) {
            continue;
        }
        float ratio = (ROLL_LIMIT_DEG > 0.0f) ? ((float)degree / ROLL_LIMIT_DEG) : 0.0f;
        float offset = radius * ratio;

        create_scale_mark(parent, radius_px, -offset, degree, true, false, ROLL_TICK_LENGTH_SUB);
        create_scale_mark(parent, radius_px, offset, degree, true, false, ROLL_TICK_LENGTH_SUB);
        create_scale_mark(parent, radius_px, -offset, degree, false, false, ROLL_TICK_LENGTH_SUB);
        create_scale_mark(parent, radius_px, offset, degree, false, false, ROLL_TICK_LENGTH_SUB);
    }
}

static void create_pitch_axis_mark(lv_obj_t *parent,
                                   float y_offset,
                                   int degree,
                                   bool show_label,
                                   lv_coord_t tick_length_override)
{
    lv_coord_t tick_length = tick_length_override > 0
                                 ? tick_length_override
                                 : (show_label ? PITCH_TICK_LENGTH_MAJOR : PITCH_TICK_LENGTH_MINOR);

    lv_obj_t *tick = lv_obj_create(parent);
    lv_obj_set_size(tick, tick_length, SCALE_TICK_THICKNESS);
    lv_obj_set_style_bg_color(tick, lv_color_hex(0xE0E3E7), 0);
    lv_obj_set_style_border_width(tick, 0, 0);
    lv_obj_set_style_pad_all(tick, 0, 0);
    lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
    lv_obj_align(tick, LV_ALIGN_CENTER, 0, (lv_coord_t)lroundf(y_offset));

    if (show_label) {
        lv_obj_t *label = lv_label_create(parent);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d°", degree);
        lv_label_set_text(label, buf);
        lv_obj_set_style_text_color(label, lv_color_hex(0xE0E3E7), 0);
        lv_coord_t offset_x = -(tick_length + PITCH_LABEL_GAP);
        lv_obj_align(label, LV_ALIGN_CENTER, offset_x, (lv_coord_t)lroundf(y_offset));
    }
}

static void create_pitch_axis_marks(lv_obj_t *parent, lv_coord_t diameter)
{
    const lv_coord_t half_height = diameter / 2;

    const int pitch_limit_deg = (int)PITCH_LIMIT_DEG;

    for (int degree = 0; degree <= pitch_limit_deg; degree += SCALE_MINOR_STEP_DEG) {
        float ratio = (PITCH_LIMIT_DEG > 0.0f) ? (float)degree / PITCH_LIMIT_DEG : 0.0f;
        float offset = ratio * half_height;
        bool show_label = (degree % SCALE_MAJOR_STEP_DEG == 0) && degree < pitch_limit_deg;

        if (degree == 0) {
            create_pitch_axis_mark(parent, 0.0f, degree, show_label, 0);
        } else {
            create_pitch_axis_mark(parent, -offset, degree, show_label, 0);
            create_pitch_axis_mark(parent, offset, degree, show_label, 0);
        }
    }

    for (int degree = 1; degree < pitch_limit_deg; ++degree) {
        if (degree % SCALE_MINOR_STEP_DEG == 0) {
            continue;
        }
        float ratio = (PITCH_LIMIT_DEG > 0.0f) ? (float)degree / PITCH_LIMIT_DEG : 0.0f;
        float offset = ratio * half_height;

        create_pitch_axis_mark(parent, -offset, degree, false, PITCH_TICK_LENGTH_SUB);
        create_pitch_axis_mark(parent, offset, degree, false, PITCH_TICK_LENGTH_SUB);
    }
}

womo_attitude_t *womo_attitude_create(lv_obj_t *parent, lv_coord_t diameter)
{
    if (!parent) {
        return NULL;
    }

    womo_attitude_t *att = lv_mem_alloc(sizeof(womo_attitude_t));
    if (!att) {
        return NULL;
    }

    att->diameter = diameter;
    att->raw_roll_deg = 0.0f;
    att->raw_pitch_deg = 0.0f;
    att->roll_deg = 0.0f;
    att->pitch_deg = 0.0f;
    att->current_roll_deg = 0.0f;
    att->current_pitch_deg = 0.0f;
    att->zero_axis = NULL;
    att->scale_left = NULL;
    att->scale_right = NULL;
    att->pitch_axis = NULL;

    lv_coord_t container_size = diameter;
    if (container_size < 120) {
        container_size = 120;
    }

    lv_coord_t outer_margin = container_size / 12;
    if (outer_margin < 24) {
        outer_margin = 24;
    }

    lv_coord_t circle_diameter = container_size - (outer_margin * 2);
    if (circle_diameter < 120) {
        circle_diameter = container_size - 48;
    }

    lv_coord_t horizon_size = circle_diameter;

    att->container = lv_obj_create(parent);
    lv_obj_set_size(att->container, container_size, container_size);
    lv_obj_set_style_pad_all(att->container, 0, 0);
    lv_obj_set_style_bg_color(att->container, lv_color_hex(0x0F1115), 0);
    lv_obj_set_style_bg_opa(att->container, LV_OPA_80, 0);
    lv_obj_set_style_border_color(att->container, lv_color_hex(0x3A3F45), 0);
    lv_obj_set_style_border_width(att->container, 2, 0);
    lv_obj_set_style_radius(att->container, 12, 0);
    lv_obj_clear_flag(att->container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(att->container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_flag(att->container, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(att->container, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(att->container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(att->container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_foreground(att->container);

    lv_obj_t *circle = lv_obj_create(att->container);
    lv_obj_set_size(circle, circle_diameter, circle_diameter);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(0x101419), 0);
    lv_obj_set_style_border_color(circle, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(circle, 2, 0);
    lv_obj_set_style_pad_all(circle, 0, 0);
    lv_obj_set_style_clip_corner(circle, true, 0);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, 0);

    att->horizon = lv_obj_create(circle);
    lv_obj_set_size(att->horizon, horizon_size, horizon_size);
    lv_obj_center(att->horizon);
    lv_obj_set_style_transform_pivot_x(att->horizon, horizon_size / 2, 0);
    lv_obj_set_style_transform_pivot_y(att->horizon, horizon_size / 2, 0);
    lv_obj_set_style_bg_opa(att->horizon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(att->horizon, 0, 0);
    lv_obj_set_style_pad_all(att->horizon, 0, 0);
    lv_obj_clear_flag(att->horizon, LV_OBJ_FLAG_SCROLLABLE);

    att->sky = lv_obj_create(att->horizon);
    lv_obj_set_size(att->sky, horizon_size, horizon_size / 2);
    lv_obj_align(att->sky, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(att->sky, lv_color_hex(0x2F69C2), 0);
    lv_obj_set_style_border_width(att->sky, 0, 0);
    lv_obj_set_style_pad_all(att->sky, 0, 0);

    att->ground = lv_obj_create(att->horizon);
    lv_obj_set_size(att->ground, horizon_size, horizon_size / 2);
    lv_obj_align(att->ground, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(att->ground, lv_color_hex(0x7A4F2A), 0);
    lv_obj_set_style_border_width(att->ground, 0, 0);
    lv_obj_set_style_pad_all(att->ground, 0, 0);

    att->zero_axis = lv_obj_create(att->horizon);
    lv_obj_set_size(att->zero_axis, horizon_size, 2);
    lv_obj_set_style_bg_color(att->zero_axis, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(att->zero_axis, LV_OPA_80, 0);
    lv_obj_set_style_border_width(att->zero_axis, 0, 0);
    lv_obj_set_style_pad_all(att->zero_axis, 0, 0);
    lv_obj_align(att->zero_axis, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_foreground(att->zero_axis);

    att->scale_left = lv_obj_create(circle);
    lv_obj_set_size(att->scale_left, circle_diameter, circle_diameter);
    lv_obj_set_style_bg_opa(att->scale_left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(att->scale_left, 0, 0);
    lv_obj_set_style_pad_all(att->scale_left, 0, 0);
    lv_obj_clear_flag(att->scale_left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(att->scale_left, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_foreground(att->scale_left);
    create_scale_marks(att->scale_left, circle_diameter);

    att->scale_right = NULL;

    att->pitch_axis = lv_obj_create(circle);
    lv_coord_t pitch_axis_width = circle_diameter - (outer_margin / 2);
    if (pitch_axis_width < PITCH_AXIS_WIDTH) {
        pitch_axis_width = PITCH_AXIS_WIDTH;
    }
    lv_obj_set_size(att->pitch_axis, pitch_axis_width, circle_diameter);
    lv_obj_set_style_bg_opa(att->pitch_axis, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(att->pitch_axis, 0, 0);
    lv_obj_set_style_pad_all(att->pitch_axis, 0, 0);
    lv_obj_clear_flag(att->pitch_axis, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(att->pitch_axis, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *pitch_axis_line = lv_obj_create(att->pitch_axis);
    lv_obj_set_size(pitch_axis_line, 2, circle_diameter - 12);
    lv_obj_set_style_bg_color(pitch_axis_line, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(pitch_axis_line, LV_OPA_80, 0);
    lv_obj_set_style_border_width(pitch_axis_line, 0, 0);
    lv_obj_set_style_pad_all(pitch_axis_line, 0, 0);
    lv_obj_align(pitch_axis_line, LV_ALIGN_CENTER, 0, 0);

    create_pitch_axis_marks(att->pitch_axis, circle_diameter);
    lv_obj_move_foreground(att->pitch_axis);

    lv_coord_t body_width = (lv_coord_t)(circle_diameter * 0.15f);
    if (body_width < 40) {
        body_width = 40;
    }
    lv_coord_t body_height = body_width * 2;
    lv_coord_t wheel_height = body_height / 4;
    lv_coord_t wheel_width = (lv_coord_t)(body_width * 0.3f) - 5;
    if (wheel_width < 16) {
        wheel_width = 16;
    }
    lv_coord_t mirror_width = body_width / 8;
    lv_coord_t vehicle_width = body_width + (mirror_width * 2) + 16;
    lv_coord_t vehicle_height = body_height + wheel_height + 18;
    att->vehicle_height = vehicle_height;

    att->vehicle = lv_obj_create(att->horizon);
    lv_obj_set_size(att->vehicle, vehicle_width, vehicle_height);
    lv_obj_set_style_bg_opa(att->vehicle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(att->vehicle, 0, 0);
    lv_obj_set_style_pad_all(att->vehicle, 0, 0);
    lv_obj_clear_flag(att->vehicle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_transform_pivot_x(att->vehicle, vehicle_width / 2, 0);
    lv_obj_set_style_transform_pivot_y(att->vehicle, vehicle_height, 0);
    lv_coord_t vehicle_x_offset = (lv_coord_t)(vehicle_width * 1.2f) - 30;
    lv_coord_t vehicle_center_offset = -(vehicle_height / 2);
    lv_obj_align(att->vehicle,
                 LV_ALIGN_CENTER,
                 vehicle_x_offset,
                 vehicle_center_offset);
    att->vehicle_center_offset = vehicle_center_offset;
    att->vehicle_x_offset = vehicle_x_offset;
    lv_obj_move_foreground(att->vehicle);

    att->vehicle_wheel_left = lv_obj_create(att->vehicle);
    lv_obj_set_size(att->vehicle_wheel_left, wheel_width, wheel_height);
    lv_obj_align(att->vehicle_wheel_left, LV_ALIGN_BOTTOM_LEFT, mirror_width + 9, 0);
    lv_obj_set_style_bg_color(att->vehicle_wheel_left, lv_color_hex(0x171A1E), 0);
    lv_obj_set_style_border_width(att->vehicle_wheel_left, 0, 0);
    lv_obj_set_style_radius(att->vehicle_wheel_left, wheel_height / 3, 0);

    att->vehicle_wheel_right = lv_obj_create(att->vehicle);
    lv_obj_set_size(att->vehicle_wheel_right, wheel_width, wheel_height);
    lv_obj_align(att->vehicle_wheel_right, LV_ALIGN_BOTTOM_RIGHT, -(mirror_width + 9), 0);
    lv_obj_set_style_bg_color(att->vehicle_wheel_right, lv_color_hex(0x171A1E), 0);
    lv_obj_set_style_border_width(att->vehicle_wheel_right, 0, 0);
    lv_obj_set_style_radius(att->vehicle_wheel_right, wheel_height / 3, 0);

    att->vehicle_body = lv_obj_create(att->vehicle);
    lv_obj_set_size(att->vehicle_body, body_width, body_height - 20);
    lv_obj_align(att->vehicle_body, LV_ALIGN_TOP_MID, 0, 54);
    lv_obj_set_style_bg_color(att->vehicle_body, lv_color_hex(0x191970), 0);
    lv_obj_set_style_border_color(att->vehicle_body, lv_color_hex(0x1B1F23), 0);
    lv_coord_t body_border_width = 2;
    lv_obj_set_style_border_width(att->vehicle_body, body_border_width, 0);
    lv_obj_set_style_radius(att->vehicle_body, 4, 0);

    lv_coord_t mirror_height = body_height / 4 - 3;

    att->vehicle_mirror_stem_left = lv_obj_create(att->vehicle);
    lv_obj_set_size(att->vehicle_mirror_stem_left, 6, mirror_height);
    lv_obj_set_style_bg_color(att->vehicle_mirror_stem_left, lv_color_hex(0x1B1F23), 0);
    lv_obj_set_style_border_width(att->vehicle_mirror_stem_left, 0, 0);
    lv_obj_set_style_radius(att->vehicle_mirror_stem_left, 2, 0);
    lv_obj_align_to(att->vehicle_mirror_stem_left, att->vehicle_body, LV_ALIGN_OUT_LEFT_MID, -4, 0);

    att->vehicle_mirror_stem_right = lv_obj_create(att->vehicle);
    lv_obj_set_size(att->vehicle_mirror_stem_right, 6, mirror_height);
    lv_obj_set_style_bg_color(att->vehicle_mirror_stem_right, lv_color_hex(0x1B1F23), 0);
    lv_obj_set_style_border_width(att->vehicle_mirror_stem_right, 0, 0);
    lv_obj_set_style_radius(att->vehicle_mirror_stem_right, 2, 0);
    lv_obj_align_to(att->vehicle_mirror_stem_right, att->vehicle_body, LV_ALIGN_OUT_RIGHT_MID, 4, 0);

    att->vehicle_mirror_left = NULL;
    att->vehicle_mirror_right = NULL;

    att->vehicle_roof_line = lv_obj_create(att->vehicle_body);
    lv_obj_set_size(att->vehicle_roof_line, body_width, body_border_width);
    lv_obj_align(att->vehicle_roof_line, LV_ALIGN_TOP_MID, 0, -15);
    lv_obj_set_style_bg_color(att->vehicle_roof_line, lv_color_hex(0x1B1F23), 0);
    lv_obj_set_style_border_width(att->vehicle_roof_line, 0, 0);

    lv_obj_move_foreground(att->vehicle_body);

    att->pitch_label = lv_label_create(att->container);
    lv_label_set_text(att->pitch_label, "Pitch 0.0°");
    lv_obj_set_style_text_color(att->pitch_label, lv_color_hex(0xE0E3E7), 0);
    lv_obj_align(att->pitch_label, LV_ALIGN_BOTTOM_LEFT, 28, -18);

    att->roll_label = lv_label_create(att->container);
    lv_label_set_text(att->roll_label, "Roll 0.0°");
    lv_obj_set_style_text_color(att->roll_label, lv_color_hex(0xE0E3E7), 0);
    lv_obj_align(att->roll_label, LV_ALIGN_BOTTOM_RIGHT, -28, -18);

    return att;
}

void womo_attitude_update(womo_attitude_t *att, float roll_deg, float pitch_deg)
{
    if (!att || !att->container) {
        return;
    }

    float sensor_roll = normalize_angle(roll_deg);
    float sensor_pitch = normalize_angle(pitch_deg);

    float roll_target_base = normalize_level_angle(sensor_roll);
    float pitch_target_base = normalize_level_angle(sensor_pitch);

    att->raw_roll_deg = sensor_roll;
    att->raw_pitch_deg = sensor_pitch;

    float roll_target = clampf(roll_target_base, -ROLL_LIMIT_DEG, ROLL_LIMIT_DEG);
    float pitch_target = clampf(pitch_target_base, -PITCH_LIMIT_DEG, PITCH_LIMIT_DEG);

    const float epsilon = 0.0f;
    float prev_roll = att->current_roll_deg;
    float prev_pitch = att->current_pitch_deg;

    att->roll_deg = roll_target;
    att->pitch_deg = pitch_target;
    att->current_roll_deg = roll_target;
    att->current_pitch_deg = pitch_target;

    bool roll_changed = fabsf(prev_roll - att->current_roll_deg) > epsilon;
    bool pitch_changed = fabsf(prev_pitch - att->current_pitch_deg) > epsilon;

    if ((roll_changed || pitch_changed) && att->horizon) {
        lv_coord_t horizon_size = lv_obj_get_width(att->horizon);
        float base_height = horizon_size / 2.0f;
        float offset = (-att->current_pitch_deg / PITCH_LIMIT_DEG) * base_height;
        lv_coord_t sky_height = (lv_coord_t)lroundf(base_height + offset);
        if (sky_height < 0) sky_height = 0;
        if (sky_height > horizon_size) sky_height = horizon_size;

        lv_obj_set_height(att->sky, sky_height);
        lv_obj_align(att->sky, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_height(att->ground, horizon_size - sky_height);
        lv_obj_align(att->ground, LV_ALIGN_BOTTOM_MID, 0, 0);

        float roll_ratio = 0.0f;
        if (ROLL_LIMIT_DEG > 0.0f) {
            roll_ratio = att->current_roll_deg / ROLL_LIMIT_DEG;
        }
        roll_ratio = clampf(roll_ratio, -1.0f, 1.0f);
        float roll_visual_deg = asinf(roll_ratio) * RAD_TO_DEG;
        int16_t angle = (int16_t)(-roll_visual_deg * 10.0f);
        lv_obj_set_style_transform_angle(att->horizon, angle, 0);

        if (att->vehicle) {
            float boundary_offset = (float)sky_height - (float)horizon_size / 2.0f;
            lv_coord_t vehicle_center_offset = att->vehicle_center_offset +
                                               (lv_coord_t)lroundf(boundary_offset);
            lv_obj_align(att->vehicle,
                         LV_ALIGN_CENTER,
                         att->vehicle_x_offset,
                         vehicle_center_offset);
        }

        lv_obj_invalidate(att->container);
    }

    const float dir_deadzone = 0.35f;
    float roll_display = display_magnitude(sensor_roll, ROLL_LIMIT_DEG);
    float pitch_display = display_magnitude(sensor_pitch, PITCH_LIMIT_DEG);

    char buf[32];

    if (roll_display <= dir_deadzone) {
        lv_label_set_text(att->roll_label, "Roll 0.0°");
    } else {
        const char *roll_dir = (att->roll_deg > 0.0f) ? "rechts" : "links";
        snprintf(buf, sizeof(buf), "Roll %s %.1f°", roll_dir, roll_display);
        lv_label_set_text(att->roll_label, buf);
    }

    if (pitch_display <= dir_deadzone) {
        lv_label_set_text(att->pitch_label, "Pitch 0.0°");
    } else {
        const char *pitch_dir = (att->pitch_deg > 0.0f) ? "vorn" : "hinten";
        snprintf(buf, sizeof(buf), "Pitch %s %.1f°", pitch_dir, pitch_display);
        lv_label_set_text(att->pitch_label, buf);
    }
}

void womo_attitude_delete(womo_attitude_t *att)
{
    if (!att) {
        return;
    }
    if (att->container) {
        lv_obj_del(att->container);
    }
    lv_mem_free(att);
}
