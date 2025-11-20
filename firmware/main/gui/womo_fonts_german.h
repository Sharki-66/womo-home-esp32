/**
 * @file womo_fonts_german.h
 * @brief Deutsche Montserrat-Schriftarten mit Umlauten
 * 
 * Generierte Schriftarten mit erweitertem Zeichensatz:
 * - ASCII (0x20-0x7F): A-Z, a-z, 0-9, Satzzeichen
 * - Latin-1 (0xC0-0xFF): ä,ö,ü,ß,é,è,à,ñ,ç,€,£,¥,©,®,°,µ,±,×,÷
 */

#ifndef WOMO_FONTS_GERMAN_H
#define WOMO_FONTS_GERMAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// Deutsche Montserrat-Schriftarten Deklarationen
extern const lv_font_t lv_font_montserrat_12_german;
extern const lv_font_t lv_font_montserrat_14_german;
extern const lv_font_t lv_font_montserrat_16_german;
extern const lv_font_t lv_font_montserrat_20_german;
extern const lv_font_t lv_font_montserrat_24_german;

// Font-Aliases für einfache Verwendung
#define WOMO_FONT_TINY          &lv_font_montserrat_12_german   // 12px - Debug/Status
#define WOMO_FONT_SMALL         &lv_font_montserrat_14_german   // 14px - Standard Labels
#define WOMO_FONT_MEDIUM        &lv_font_montserrat_16_german   // 16px - Sensor Labels
#define WOMO_FONT_LARGE         &lv_font_montserrat_20_german   // 20px - Wichtige Werte
#define WOMO_FONT_TITLE         &lv_font_montserrat_24_german   // 24px - Titel/Zeit

// Spezifische Font-Zuweisungen für UI-Elemente
#define WOMO_FONT_TIME          WOMO_FONT_TITLE     // Zeitanzeige
#define WOMO_FONT_DATE          WOMO_FONT_MEDIUM    // Datum mit Wochentag
#define WOMO_FONT_TEMP          WOMO_FONT_MEDIUM    // Temperatur-Labels
#define WOMO_FONT_SENSOR        WOMO_FONT_SMALL     // Sensor-Werte
#define WOMO_FONT_STATUS        WOMO_FONT_SMALL     // Status-Texte
#define WOMO_FONT_DEBUG         WOMO_FONT_TINY      // Debug/RS485

/**
 * Test-String mit deutschen Umlauten und Sonderzeichen
 */
#define WOMO_TEST_GERMAN_STRING "Größe: 25°C, Prüfung: äöüÄÖÜß €123"

/**
 * Hilfsfunktion: Font nach Größe auswählen
 */
const lv_font_t* womo_get_german_font_by_size(int size);

/**
 * Test-Funktion für deutsche Zeichen
 */
void womo_test_german_fonts(void);

#ifdef __cplusplus
}
#endif

#endif /* WOMO_FONTS_GERMAN_H */