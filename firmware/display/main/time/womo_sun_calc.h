/*
 * WoMo Sun Calculation
 * 
 * Calculates sunrise and sunset times based on GPS coordinates and date
 * Uses simplified astronomical formula for civil twilight
 */

#ifndef WOMO_SUN_CALC_H
#define WOMO_SUN_CALC_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/**
 * @brief Calculate sunrise and sunset times for given location and date
 * 
 * Uses simplified astronomical formula. Accuracy: ±2 minutes for latitudes 50°N-55°N
 * 
 * @param latitude Location latitude in degrees (-90 to +90)
 * @param longitude Location longitude in degrees (-180 to +180)
 * @param date Date for calculation (uses tm_year, tm_mon, tm_mday)
 * @param sunrise_hour Output: sunrise hour (0-23)
 * @param sunrise_min Output: sunrise minute (0-59)
 * @param sunset_hour Output: sunset hour (0-23)
 * @param sunset_min Output: sunset minute (0-59)
 * @return true if calculation successful, false if invalid input
 */
bool womo_sun_calc_times(double latitude, double longitude, const struct tm *date,
                         uint8_t *sunrise_hour, uint8_t *sunrise_min,
                         uint8_t *sunset_hour, uint8_t *sunset_min);

#endif // WOMO_SUN_CALC_H
