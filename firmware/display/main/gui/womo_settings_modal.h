/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once
#include "lvgl.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Einstellungs-Modal öffnen.
 * @param parent  Eltern-Objekt (lv_scr_act())
 */
void womo_settings_modal_show(lv_obj_t *parent);

/**
 * @brief Prüfen ob das Einstellungs-Modal gerade offen ist.
 */
bool womo_settings_modal_is_open(void);

/**
 * @brief Gespeicherte AP-Konfiguration aus NVS laden (Namespace „womo_ap_cfg").
 *
 * @param ssid     Ausgabepuffer für die SSID (mind. 33 Bytes).
 * @param ssid_sz  Größe des ssid-Puffers.
 * @param pass     Ausgabepuffer für das Passwort (mind. 65 Bytes).
 * @param pass_sz  Größe des pass-Puffers.
 * @return ESP_OK wenn SSID gefunden, ESP_ERR_NVS_NOT_FOUND wenn kein Eintrag.
 */
esp_err_t womo_ap_cfg_load(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz);
