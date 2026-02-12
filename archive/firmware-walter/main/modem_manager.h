#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "womo_gps.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Serialisiert LTE- und GNSS-Nutzung des Walter-Modems.
 * - Schaltet LTE gezielt ein/aus (über vorhandene LTE-Runtime-Queue)
 * - Wartet auf LTE-Bereitschaft
 * - Führt einen vollständigen GNSS-Zyklus aus (Clock/Assist über LTE, LTE pausieren, Fix holen, LTE wieder aktivieren)
 */
esp_err_t modem_manager_set_lte_enabled(bool enable, TickType_t timeout_ticks);

/**
 * Führt einen GNSS-Fix durch und liefert bei Erfolg den Fix zurück.
 * Ablauf:
 *  1) LTE aktivieren und auf Link warten
 *  2) GNSS-Fix anfordern (LTE wird für den Fix pausiert)
 *  3) Auf neuen Fix warten bis timeout_ticks
 *  4) LTE wieder aktivieren
 */
esp_err_t modem_manager_run_gnss_cycle(TickType_t timeout_ticks, womo_gps_data_t *out_fix);

#ifdef __cplusplus
}
#endif
