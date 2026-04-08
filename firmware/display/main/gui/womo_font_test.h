/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file womo_font_test.h
 * @brief Font testing utilities - demonstrates extended character sets
 */

#ifndef WOMO_FONT_TEST_H
#define WOMO_FONT_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * Test strings to verify font character support
 */

// German umlauts and special characters
#define TEST_GERMAN_UMLAUTS "Größe: Temperatur über 20°C"
#define TEST_GERMAN_FULL "Prüfung: Wärme, Kühlung & Größenänderung"

// Extended Latin characters
#define TEST_ACCENTS "Café, naïve, résumé, piña"

// Various symbols available in DejaVu
#define TEST_SYMBOLS "←→↑↓ ±×÷ §©® °µΩ"

/**
 * Create a test label to verify font character rendering
 */
lv_obj_t* womo_font_test_create_label(lv_obj_t* parent, const char* test_text, const lv_font_t* font);

/**
 * Run font compatibility test - logs results
 */
void womo_font_test_compatibility(void);

#ifdef __cplusplus
}
#endif

#endif /* WOMO_FONT_TEST_H */