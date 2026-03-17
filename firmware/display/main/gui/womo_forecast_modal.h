/*
 * womo_forecast_modal.h – 5-Tage Wetter-Vorhersage Modal
 *
 * Öffnet sich wenn der Nutzer auf das Wetter-Icon tippt.
 * Zeigt 5 Tages-Spalten mit Icon, Max/Min-Temperatur, Niederschlag und Wind.
 */

#ifndef WOMO_FORECAST_MODAL_H
#define WOMO_FORECAST_MODAL_H

#include "lvgl.h"
#include "womo_weather_http.h"
#include "network/womo_meteoalarm.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Öffnet das 5-Tage-Vorhersage-Modal.
 *
 * Wenn das Modal bereits offen ist, wird es in den Vordergrund gebracht
 * und mit den neuen Daten aktualisiert.
 *
 * @param parent  Parent-Objekt (normalerweise lv_screen_active())
 * @param forecast Zeiger auf Forecast-Daten (darf NULL sein → "Keine Daten")
 */
void womo_forecast_modal_show(lv_obj_t *parent, const womo_weather_forecast_t *forecast);

/**
 * @brief Schließt das Modal und gibt alle Ressourcen frei.
 */
void womo_forecast_modal_close(void);

/**
 * @brief Gibt true zurück, wenn das Modal gerade offen ist.
 */
bool womo_forecast_modal_is_open(void);

/**
 * @brief Aktualisiert die angezeigten Forecast-Daten (ohne Modal neu zu öffnen).
 *
 * Wird von main.c aufgerufen, wenn ein neuer Forecast-Callback eintrifft,
 * während das Modal bereits geöffnet ist.
 */
void womo_forecast_modal_update(const womo_weather_forecast_t *forecast);

/**
 * @brief Ort und Sonnenzeiten im Modal-Header setzen/aktualisieren.
 *
 * Kann vor oder nach womo_forecast_modal_show() aufgerufen werden.
 * location darf NULL sein (kein Ortsname anzeigen).
 */
void womo_forecast_modal_set_location(const char *location,
                                      uint8_t sr_h, uint8_t sr_m,
                                      uint8_t ss_h, uint8_t ss_m);

/**
 * @brief Meteoalarm-Warnungen im Modal aktualisieren.
 *
 * Kann vor oder nach womo_forecast_modal_show() aufgerufen werden.
 * Das Modal zeigt die Warnungen unten im Warnbereich an.
 * Wird mit NULL oder result->count==0 aufgerufen → "Keine Warnungen".
 */
void womo_forecast_modal_set_warnings(const womo_meteoalarm_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* WOMO_FORECAST_MODAL_H */
