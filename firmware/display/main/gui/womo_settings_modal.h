#pragma once
#include "lvgl.h"
#include <stdbool.h>

/**
 * @brief Einstellungs-Modal öffnen.
 * @param parent  Eltern-Objekt (lv_scr_act())
 */
void womo_settings_modal_show(lv_obj_t *parent);

/**
 * @brief Prüfen ob das Einstellungs-Modal gerade offen ist.
 */
bool womo_settings_modal_is_open(void);
