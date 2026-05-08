/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * WoMo Theme Manager
 * 
 * Verwaltet Hintergrundfarben und Themes basierend auf:
 * - Tageszeit (Tag/Nacht)
 * - Systemstatus (normal/warnung/kritisch)
 * - Ressourcenlevel (Wasser, Gas, Batterie)
 */

#ifndef WOMO_THEME_H
#define WOMO_THEME_H

#include "lvgl.h"
#include "esp_err.h"
#include <stdbool.h>

// Theme modes
typedef enum {
    WOMO_THEME_DAY,           // Tagsüber (nach Sonnenaufgang)
    WOMO_THEME_NIGHT,         // Nachts (nach Sonnenuntergang)
    WOMO_THEME_SUNRISE,       // Sonnenaufgangsübergang
    WOMO_THEME_SUNSET         // Sonnenuntergangsübergang
} womo_theme_mode_t;

// System status levels
typedef enum {
    WOMO_STATUS_OK,           // Alle Systeme normal (blau/grün)
    WOMO_STATUS_WARNING,      // Niedrige Ressourcen (orange/gelb)
    WOMO_STATUS_CRITICAL,     // Kritisches Niveau (rot)
    WOMO_STATUS_ERROR         // Systemfehler (dunkelrot)
} womo_status_level_t;

// Color definitions for different states
#define WOMO_COLOR_DAY_NORMAL       lv_color_hex(0x87CEEB)  // Sky Blue (Hellblau)
#define WOMO_COLOR_NIGHT_NORMAL     lv_color_hex(0x191970)  // Midnight Blue (Dunkelblau)
#define WOMO_COLOR_SUNRISE          lv_color_hex(0xFF6347)  // Tomato (orange-red)
#define WOMO_COLOR_SUNSET           lv_color_hex(0xFF8C00)  // Dark Orange

// Dauer der bürgerlichen Dämmerung (Minuten vor/nach Sonnenzeiten für sanfte Übergänge)
#define WOMO_CIVIL_TWILIGHT_MINUTES 45  // Dämmerungsfenster: 45 min vor/nach Sonnenauf-/-untergang

#define WOMO_COLOR_ERROR            lv_color_hex(0xFF0000)  // Red (helles Rot)
#define WOMO_COLOR_CRITICAL         lv_color_hex(0x8B0000)  // Dark Red (dunkles Rot)

// Sonnenauf-/-untergangszeiten (werden basierend auf Standort/Datum berechnet)
typedef struct {
    uint8_t sunrise_hour;
    uint8_t sunrise_minute;
    uint8_t sunset_hour;
    uint8_t sunset_minute;
} womo_sun_times_t;

/**
 * @brief Theme-Manager initialisieren
 * 
 * @param latitude Breitengrad des Standorts für Sonnenberechnungen
 * @param longitude Längengrad des Standorts für Sonnenberechnungen
 * @return ESP_OK bei Erfolg
 */
esp_err_t womo_theme_init(float latitude, float longitude);

/**
 * @brief Theme basierend auf aktueller Zeit und Systemstatus aktualisieren
 * 
 * Bestimmt automatisch den Theme-Modus basierend auf der Tageszeit
 * 
 * @param status Aktueller Systemstatus-Level
 * @return Aktueller Theme-Modus
 */
womo_theme_mode_t womo_theme_update(womo_status_level_t status);

/**
 * @brief Aktuelle Hintergrundfarbe abrufen
 * 
 * Gibt Farbe basierend auf Theme-Modus und Systemstatus zurück
 * 
 * @return LVGL-Farbe für Hintergrund
 */
lv_color_t womo_theme_get_background_color(void);

/**
 * @brief Sanfte Übergangsfarbe während der bürgerlichen Dämmerung abrufen
 * 
 * Interpoliert zwischen Tag (hellblau) und Nacht (dunkelblau) Farben
 * während der Phasen der bürgerlichen Dämmerung (24 Minuten vor/nach Sonnenauf-/-untergang)
 * 
 * @return LVGL-Farbe interpoliert basierend auf aktueller Zeit innerhalb der Dämmerungsperiode
 */
lv_color_t womo_theme_get_twilight_color(void);

/**
 * @brief Aktuellen Theme-Modus abrufen
 * 
 * @return Aktueller Theme-Modus (Tag/Nacht/Sonnenaufgang/Sonnenuntergang)
 */
womo_theme_mode_t womo_theme_get_mode(void);

/**
 * @brief Systemstatus-Level setzen
 * 
 * Ändert Hintergrundfarbe, um Warnungen/Fehler anzuzeigen
 * 
 * @param status Neuer Status-Level
 */
void womo_theme_set_status(womo_status_level_t status);

/**
 * @brief Aktuellen Systemstatus abrufen
 * 
 * @return Aktueller Status-Level
 */
womo_status_level_t womo_theme_get_status(void);

/**
 * @brief Sonnenauf-/-untergangszeiten manuell setzen
 * 
 * Verwenden, wenn GPS-Standort nicht verfügbar
 * 
 * @param sunrise_hour Stunde des Sonnenaufgangs (0-23)
 * @param sunrise_min Minute des Sonnenaufgangs (0-59)
 * @param sunset_hour Stunde des Sonnenuntergangs (0-23)
 * @param sunset_min Minute des Sonnenuntergangs (0-59)
 */
void womo_theme_set_sun_times(uint8_t sunrise_hour, uint8_t sunrise_min,
                               uint8_t sunset_hour, uint8_t sunset_min);

/**
 * @brief Aktuelle Sonnenauf-/-untergangszeiten abrufen
 * 
 * @return Zeiger auf Sonnenzeiten-Struktur
 */
const womo_sun_times_t* womo_theme_get_sun_times(void);

/**
 * @brief Prüfen, ob aktuell Tagsüber
 * 
 * @return true, wenn zwischen Sonnenaufgang und -untergang
 */
bool womo_theme_is_daytime(void);

/**
 * @brief Theme auf LVGL-Bildschirm anwenden
 * 
 * Setzt Hintergrundfarbe des Hauptbildschirms
 * 
 * @param screen LVGL-Bildschirmobjekt (oder NULL für Standardbildschirm)
 */
void womo_theme_apply_to_screen(lv_obj_t *screen);

/**
 * @brief Theme-Zustand auf Standard zurücksetzen (bei Boot/Einschalten aufrufen)
 * 
 * Setzt gecachte Farben, Theme-Modus zurück und erzwingt Neubewertung.
 * Notwendig, da statische Variablen Soft-Resets überleben.
 */
void womo_theme_reset(void);

/**
 * @brief Zum nächsten Theme-Modus wechseln (für Touch-Steuerung)
 * 
 * Wechselt durch: TAG -> SONNENUNTERGANG -> NACHT -> SONNENAUFgang -> TAG
 */
void womo_theme_cycle_mode(void);

/**
 * @brief Zum nächsten Status-Level wechseln (für Touch-Steuerung)
 * 
 * Wechselt durch: OK -> WARNUNG -> FEHLER -> KRITISCH -> OK
 */
void womo_theme_cycle_status(void);

/**
 * @brief Theme-Modus manuell setzen (deaktiviert automatische zeitbasierte Umschaltung)
 * 
 * @param mode Gewünschter Theme-Modus
 */
void womo_theme_set_mode(womo_theme_mode_t mode);

/**
 * @brief Automatische zeitbasierte Theme-Umschaltung aktivieren/deaktivieren
 * 
 * @param enable true = automatisch, false = manuelle Steuerung
 */
void womo_theme_set_auto_mode(bool enable);

/**
 * @brief Prüfen, ob automatischer Modus aktiviert ist
 * 
 * @return true bei automatisch, false bei manuell
 */
bool womo_theme_is_auto_mode(void);

#endif // WOMO_THEME_H
