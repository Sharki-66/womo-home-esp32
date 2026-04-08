/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file womo_fonts_german.c
 * @brief Implementation der deutschen Font-Hilfsfunktionen
 */

#include "womo_fonts_german.h"
#include "esp_log.h"

static const char *TAG = "womo_fonts_german";

const lv_font_t* womo_get_german_font_by_size(int size)
{
    switch(size) {
        case 12: return &lv_font_montserrat_12_german;
        case 14: return &lv_font_montserrat_14_german;
        case 16: return &lv_font_montserrat_16_german;
        case 20: return &lv_font_montserrat_20_german;
        case 24: return &lv_font_montserrat_24_german;
        default:
            ESP_LOGW(TAG, "Unsupported font size %d, using 14px", size);
            return &lv_font_montserrat_14_german;
    }
}

void womo_test_german_fonts(void)
{
    ESP_LOGI(TAG, "=== Deutsche Schriftarten Test ===");
    ESP_LOGI(TAG, "Verfügbare Größen: 12, 14, 16, 20, 24 px");
    ESP_LOGI(TAG, "Deutsche Umlaute: äöüÄÖÜß");
    ESP_LOGI(TAG, "Akzente: éèàñçÉÈÀÑÇ");
    ESP_LOGI(TAG, "Symbole: €£¥©®°µ±×÷");
    ESP_LOGI(TAG, "Test-String: \"%s\"", WOMO_TEST_GERMAN_STRING);
    ESP_LOGI(TAG, "Deutsche Montserrat-Schriftarten bereit!");
}