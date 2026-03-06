/*
 * WoMo Sun Calculation - Implementation
 * 
 * Simplified astronomical formula for sunrise/sunset calculation
 * Reference: https://en.wikipedia.org/wiki/Sunrise_equation
 */

#include "womo_sun_calc.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "womo_sun_calc";

// Constants
#define PI 3.14159265358979323846
#define DEG_TO_RAD (PI / 180.0)
#define RAD_TO_DEG (180.0 / PI)

// Solar zenith angle for civil twilight (96° for sunrise/sunset)
#define CIVIL_TWILIGHT_ZENITH 96.0

/**
 * @brief Calculate day of year (1-366)
 */
static int day_of_year(const struct tm *date)
{
    struct tm temp = *date;
    temp.tm_hour = 12;
    temp.tm_min = 0;
    temp.tm_sec = 0;
    
    // Calculate day of year
    struct tm jan1 = {0};
    jan1.tm_year = date->tm_year;
    jan1.tm_mon = 0;  // January
    jan1.tm_mday = 1;
    jan1.tm_hour = 12;
    
    time_t t_date = mktime(&temp);
    time_t t_jan1 = mktime(&jan1);
    
    int days = (int)((t_date - t_jan1) / 86400) + 1;
    return days;
}

/**
 * @brief Calculate approximate solar noon and time difference from longitude
 */
static double calculate_solar_noon(int day, double longitude)
{
    // Approximate solar noon in UTC hours
    return 12.0 - (longitude / 15.0);
}

/**
 * @brief Calculate hour angle for given zenith
 */
static double calculate_hour_angle(double latitude_rad, double declination_rad, double zenith_rad)
{
    double cos_hour_angle = (cos(zenith_rad) - sin(latitude_rad) * sin(declination_rad)) /
                           (cos(latitude_rad) * cos(declination_rad));
    
    // Check for polar day/night
    if (cos_hour_angle > 1.0) {
        return 0.0;  // Polar night
    }
    if (cos_hour_angle < -1.0) {
        return PI;   // Polar day
    }
    
    return acos(cos_hour_angle);
}

bool womo_sun_calc_times(double latitude, double longitude, const struct tm *date,
                         uint8_t *sunrise_hour, uint8_t *sunrise_min,
                         uint8_t *sunset_hour, uint8_t *sunset_min)
{
    // Validate inputs
    if (!date || !sunrise_hour || !sunrise_min || !sunset_hour || !sunset_min) {
        return false;
    }
    
    if (latitude < -90.0 || latitude > 90.0 ||
        longitude < -180.0 || longitude > 180.0) {
        ESP_LOGW(TAG, "Invalid coordinates: lat=%.2f lon=%.2f", latitude, longitude);
        return false;
    }
    
    // Get day of year
    int day = day_of_year(date);
    if (day < 1 || day > 366) {
        ESP_LOGW(TAG, "Invalid day of year: %d", day);
        return false;
    }
    
    // Convert to radians
    double lat_rad = latitude * DEG_TO_RAD;
    double zenith_rad = CIVIL_TWILIGHT_ZENITH * DEG_TO_RAD;
    
    // Calculate solar declination (simplified formula)
    // More accurate would use equation of time, but this is good enough (±2 min error)
    double n = day;
    double declination = 23.45 * sin((360.0 / 365.25) * (n + 284.0) * DEG_TO_RAD);
    double decl_rad = declination * DEG_TO_RAD;
    
    // Calculate hour angle
    double hour_angle_rad = calculate_hour_angle(lat_rad, decl_rad, zenith_rad);
    double hour_angle_deg = hour_angle_rad * RAD_TO_DEG;
    
    // Calculate solar noon
    double solar_noon = calculate_solar_noon(day, longitude);
    
    // Calculate sunrise and sunset in UTC hours
    double sunrise_utc = solar_noon - (hour_angle_deg / 15.0);
    double sunset_utc = solar_noon + (hour_angle_deg / 15.0);
    
    // Get timezone offset from current time
    time_t now = time(NULL);
    struct tm tm_local;
    struct tm tm_utc;
    localtime_r(&now, &tm_local);
    gmtime_r(&now, &tm_utc);
    
    // Calculate timezone offset in hours (simplified - assumes no DST complications during calculation)
    int tz_offset_seconds = (int)difftime(mktime(&tm_local), mktime(&tm_utc));
    double tz_offset_hours = tz_offset_seconds / 3600.0;
    
    // Convert to local time
    double sunrise_local = sunrise_utc + tz_offset_hours;
    double sunset_local = sunset_utc + tz_offset_hours;
    
    // Handle day overflow/underflow
    while (sunrise_local < 0.0) sunrise_local += 24.0;
    while (sunrise_local >= 24.0) sunrise_local -= 24.0;
    while (sunset_local < 0.0) sunset_local += 24.0;
    while (sunset_local >= 24.0) sunset_local -= 24.0;
    
    // Convert to hours and minutes
    *sunrise_hour = (uint8_t)sunrise_local;
    *sunrise_min = (uint8_t)((sunrise_local - *sunrise_hour) * 60.0);
    *sunset_hour = (uint8_t)sunset_local;
    *sunset_min = (uint8_t)((sunset_local - *sunset_hour) * 60.0);
    
    ESP_LOGI(TAG, "Sun times for lat=%.4f lon=%.4f day=%d: sunrise=%02d:%02d sunset=%02d:%02d",
             latitude, longitude, day, *sunrise_hour, *sunrise_min, *sunset_hour, *sunset_min);
    
    return true;
}
