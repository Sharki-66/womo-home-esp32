/*
 * WoMo Theme Manager - Implementation
 */

#include "womo_theme.h"
#include "time/womo_time.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "womo_theme";

// Current theme state
static womo_theme_mode_t current_mode = WOMO_THEME_DAY;
static womo_status_level_t current_status = WOMO_STATUS_OK;
static bool auto_mode_enabled = true;   // Auto mode for smooth twilight transitions

// Performance optimization: cache last computed color
static lv_color_t cached_twilight_color;
static uint8_t last_computed_hour = 255;  // Invalid hour to force first computation
static uint8_t last_computed_minute = 255;
static bool is_twilight_cached = false;  // Flag to indicate if current cache is from twilight calculation
static womo_sun_times_t sun_times = {
    .sunrise_hour = 7,
    .sunrise_minute = 0,
    .sunset_hour = 18,
    .sunset_minute = 0
};

// Location for sun calculations (optional)
static float location_lat = 0.0;
static float location_lon = 0.0;

// Pre-calculated twilight color lookup table (24 steps, 0=night, 23=day)
// Initialized at runtime to avoid compile-time constant issues
static lv_color_t womo_twilight_colors[WOMO_CIVIL_TWILIGHT_MINUTES];
static bool twilight_colors_initialized = false;

esp_err_t womo_theme_init(float latitude, float longitude)
{
    ESP_LOGI(TAG, "Initializing theme manager");
    ESP_LOGI(TAG, "Location: %.4f, %.4f", latitude, longitude);
    
    location_lat = latitude;
    location_lon = longitude;
    
    // TODO: Calculate actual sunrise/sunset based on location and date
    // For now using default times
    ESP_LOGI(TAG, "Sunrise: %02d:%02d, Sunset: %02d:%02d",
             sun_times.sunrise_hour, sun_times.sunrise_minute,
             sun_times.sunset_hour, sun_times.sunset_minute);
    
    return ESP_OK;
}

static bool is_between_times(uint8_t current_hour, uint8_t current_min,
                             uint8_t start_hour, uint8_t start_min,
                             uint8_t end_hour, uint8_t end_min)
{
    uint16_t current_minutes = current_hour * 60 + current_min;
    uint16_t start_minutes = start_hour * 60 + start_min;
    uint16_t end_minutes = end_hour * 60 + end_min;
    
    return (current_minutes >= start_minutes && current_minutes < end_minutes);
}

    // Prüft, ob die aktuelle Zeit in der bürgerlichen Dämmerung liegt
    static bool is_in_civil_twilight_now(void)
    {
        struct tm timeinfo;

        if (womo_time_get(&timeinfo) != ESP_OK || timeinfo.tm_year < (2024 - 1900)) {
            return false; // Ohne valide Zeit kein Twilight-Gradient
        }

        uint8_t hour = timeinfo.tm_hour;
        uint8_t minute = timeinfo.tm_min;

        // Start der Morgendämmerung = sunrise - WOMO_CIVIL_TWILIGHT_MINUTES
        uint8_t morning_start_hour = sun_times.sunrise_hour;
        uint8_t morning_start_min = sun_times.sunrise_minute;
        if (morning_start_min >= WOMO_CIVIL_TWILIGHT_MINUTES) {
            morning_start_min -= WOMO_CIVIL_TWILIGHT_MINUTES;
        } else {
            morning_start_min = morning_start_min + 60 - WOMO_CIVIL_TWILIGHT_MINUTES;
            morning_start_hour = (morning_start_hour > 0) ? morning_start_hour - 1 : 23;
        }

        // Start der Abenddämmerung = sunset - WOMO_CIVIL_TWILIGHT_MINUTES
        uint8_t evening_start_hour = sun_times.sunset_hour;
        uint8_t evening_start_min = sun_times.sunset_minute;
        if (evening_start_min >= WOMO_CIVIL_TWILIGHT_MINUTES) {
            evening_start_min -= WOMO_CIVIL_TWILIGHT_MINUTES;
        } else {
            evening_start_min = evening_start_min + 60 - WOMO_CIVIL_TWILIGHT_MINUTES;
            evening_start_hour = (evening_start_hour > 0) ? evening_start_hour - 1 : 23;
        }

        bool in_morning = is_between_times(hour, minute,
                                           morning_start_hour, morning_start_min,
                                           sun_times.sunrise_hour, sun_times.sunrise_minute);

        bool in_evening = is_between_times(hour, minute,
                                           evening_start_hour, evening_start_min,
                                           sun_times.sunset_hour, sun_times.sunset_minute);

        return in_morning || in_evening;
    }

// Initialize twilight colors lookup table (called once at startup)
static void init_twilight_colors(void)
{
    if (twilight_colors_initialized) {
        return;
    }
    
    // 24 pre-calculated color steps from night blue to day blue
    const uint32_t color_values[WOMO_CIVIL_TWILIGHT_MINUTES] = {
        // Step 0-5: Deep night blue to darker blue
        0x191970, 0x1E1E7A, 0x232384, 0x28288E, 0x2D2D98, 0x3232A2,
        // Step 6-11: Darker blue to medium blue
        0x3737AC, 0x3C3CB6, 0x4141C0, 0x4646CA, 0x4B4BD4, 0x5050DE,
        // Step 12-17: Medium blue to lighter blue
        0x5555E8, 0x5A5AED, 0x5F5FEF, 0x6464F1, 0x6969F3, 0x6E6EF5,
        // Step 18-23: Lighter blue to day sky blue
        0x7373F7, 0x7878F9, 0x7D7DFB, 0x8282E9, 0x8585EA, 0x87CEEB
    };
    
    // Convert hex values to lv_color_t at runtime
    for (int i = 0; i < WOMO_CIVIL_TWILIGHT_MINUTES; i++) {
        womo_twilight_colors[i] = lv_color_hex(color_values[i]);
    }
    
    twilight_colors_initialized = true;
    ESP_LOGI(TAG, "Twilight color lookup table initialized (%d colors)", WOMO_CIVIL_TWILIGHT_MINUTES);
}

// Fast lookup function - no floating point math!
static lv_color_t get_twilight_color_by_minute(uint8_t minutes_since_start, bool is_evening)
{
    // Ensure colors are initialized
    init_twilight_colors();
    
    // Clamp to valid range
    if (minutes_since_start >= WOMO_CIVIL_TWILIGHT_MINUTES) {
        minutes_since_start = WOMO_CIVIL_TWILIGHT_MINUTES - 1;
    }
    
    if (is_evening) {
        // Evening: reverse lookup (day -> night)
        uint8_t reverse_index = (WOMO_CIVIL_TWILIGHT_MINUTES - 1) - minutes_since_start;
        return womo_twilight_colors[reverse_index];
    } else {
        // Morning: forward lookup (night -> day)
        return womo_twilight_colors[minutes_since_start];
    }
}

womo_theme_mode_t womo_theme_update(womo_status_level_t status)
{
    struct tm timeinfo;
    
    current_status = status;
    
    // Skip automatic mode if disabled (manual control via touch)
    if (!auto_mode_enabled) {
        return current_mode;
    }
    
    // Get current time; if clock is uninitialized (e.g. 1970), keep last known mode.
    // WICHTIG: current_mode NICHT auf DAY resetten – das würde via full_theme_refresh()
    // den Ducato bei temporärem Zeitfehler fälschlich auf Weiß (Tag) zurücksetzen.
    if (womo_time_get(&timeinfo) != ESP_OK || timeinfo.tm_year < (2024 - 1900)) {
        ESP_LOGW(TAG, "Time invalid, keeping current mode: %d", current_mode);
        return current_mode;
    }
    
    uint8_t hour = timeinfo.tm_hour;
    uint8_t minute = timeinfo.tm_min;
    
    // Civil twilight: 24 minutes before sunrise/sunset (bürgerliche Dämmerung)
    uint8_t sunrise_start_hour = sun_times.sunrise_hour;
    uint8_t sunrise_start_min = sun_times.sunrise_minute;
    if (sunrise_start_min >= WOMO_CIVIL_TWILIGHT_MINUTES) {
        sunrise_start_min -= WOMO_CIVIL_TWILIGHT_MINUTES;
    } else {
        sunrise_start_min = sunrise_start_min + 60 - WOMO_CIVIL_TWILIGHT_MINUTES;
        sunrise_start_hour = (sunrise_start_hour > 0) ? sunrise_start_hour - 1 : 23;
    }
    
    uint8_t sunset_start_hour = sun_times.sunset_hour;
    uint8_t sunset_start_min = sun_times.sunset_minute;
    if (sunset_start_min >= WOMO_CIVIL_TWILIGHT_MINUTES) {
        sunset_start_min -= WOMO_CIVIL_TWILIGHT_MINUTES;
    } else {
        sunset_start_min = sunset_start_min + 60 - WOMO_CIVIL_TWILIGHT_MINUTES;
        sunset_start_hour = (sunset_start_hour > 0) ? sunset_start_hour - 1 : 23;
    }
    
    // Determine theme mode
    womo_theme_mode_t prev_mode = current_mode;
    if (is_between_times(hour, minute, 
                        sunrise_start_hour, sunrise_start_min,
                        sun_times.sunrise_hour, sun_times.sunrise_minute)) {
        current_mode = WOMO_THEME_SUNRISE;
    }
    else if (is_between_times(hour, minute,
                             sun_times.sunrise_hour, sun_times.sunrise_minute,
                             sunset_start_hour, sunset_start_min)) {
        current_mode = WOMO_THEME_DAY;
    }
    else if (is_between_times(hour, minute,
                             sunset_start_hour, sunset_start_min,
                             sun_times.sunset_hour, sun_times.sunset_minute)) {
        current_mode = WOMO_THEME_SUNSET;
    }
    else {
        current_mode = WOMO_THEME_NIGHT;
    }

    if (current_mode != prev_mode) {
        static const char * const mode_names[] = {"DAY", "NIGHT", "SUNRISE", "SUNSET"};
        ESP_LOGI(TAG, "Theme: %s", mode_names[current_mode]);
    }

    return current_mode;
}

lv_color_t womo_theme_get_twilight_color(void)
{
    struct tm timeinfo;
    
    // Get current time
    if (womo_time_get(&timeinfo) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get time for twilight color, using day color");
        return WOMO_COLOR_DAY_NORMAL;
    }
    
    uint8_t hour = timeinfo.tm_hour;
    uint8_t minute = timeinfo.tm_min;
    
    // Performance optimization: return cached color if minute hasn't changed
    // During twilight, we only need to recalculate once per minute (not every second!)
    if (hour == last_computed_hour && minute == last_computed_minute) {
        return cached_twilight_color;
    }
    
    // FAST PATH: Check if we're in daytime or nighttime first (no calculations needed)
    // This avoids expensive twilight calculations 99% of the time!
    if (womo_theme_is_daytime()) {
        // Check if we're close to sunset (within 30 minutes)
        uint16_t current_minutes = hour * 60 + minute;
        uint16_t sunset_minutes = sun_times.sunset_hour * 60 + sun_times.sunset_minute;
        
        if (current_minutes < (sunset_minutes - WOMO_CIVIL_TWILIGHT_MINUTES) || 
            current_minutes > sunset_minutes) {
            // Far from sunset - return fixed day color (no calculations!)
            // Cache for the entire minute since it's a fixed color
            last_computed_hour = hour;
            last_computed_minute = minute;
            cached_twilight_color = WOMO_COLOR_DAY_NORMAL;
            is_twilight_cached = false;  // This is a fixed color, not twilight
            return WOMO_COLOR_DAY_NORMAL;
        }
    } else {
        // Check if we're far from sunrise (within 30 minutes)
        uint16_t current_minutes = hour * 60 + minute;
        uint16_t sunrise_minutes = sun_times.sunrise_hour * 60 + sun_times.sunrise_minute;
        
        if (current_minutes < (sunrise_minutes - WOMO_CIVIL_TWILIGHT_MINUTES) || 
            current_minutes > sunrise_minutes) {
            // Far from sunrise - return fixed night color (no calculations!)
            // Cache for the entire minute since it's a fixed color
            last_computed_hour = hour;
            last_computed_minute = minute;
            cached_twilight_color = WOMO_COLOR_NIGHT_NORMAL;
            is_twilight_cached = false;  // This is a fixed color, not twilight
            return WOMO_COLOR_NIGHT_NORMAL;
        }
    }
    
    // SLOW PATH: Only calculate twilight times when we're actually near sunrise/sunset
    ESP_LOGD(TAG, "Near twilight - performing interpolation calculations");
    
    // Calculate civil twilight start times (24 minutes before sunrise/sunset)
    uint8_t morning_twilight_hour = sun_times.sunrise_hour;
    uint8_t morning_twilight_min = sun_times.sunrise_minute;
    if (morning_twilight_min >= WOMO_CIVIL_TWILIGHT_MINUTES) {
        morning_twilight_min -= WOMO_CIVIL_TWILIGHT_MINUTES;
    } else {
        morning_twilight_min = morning_twilight_min + 60 - WOMO_CIVIL_TWILIGHT_MINUTES;
        morning_twilight_hour = (morning_twilight_hour > 0) ? morning_twilight_hour - 1 : 23;
    }
    
    uint8_t evening_twilight_hour = sun_times.sunset_hour;
    uint8_t evening_twilight_min = sun_times.sunset_minute;
    if (evening_twilight_min >= WOMO_CIVIL_TWILIGHT_MINUTES) {
        evening_twilight_min -= WOMO_CIVIL_TWILIGHT_MINUTES;
    } else {
        evening_twilight_min = evening_twilight_min + 60 - WOMO_CIVIL_TWILIGHT_MINUTES;
        evening_twilight_hour = (evening_twilight_hour > 0) ? evening_twilight_hour - 1 : 23;
    }
    
    // Compute the color based on current time (complex calculations only during twilight)
    lv_color_t computed_color;
    
    // Check if we're in morning civil twilight (dark blue -> light blue)
    if (is_between_times(hour, minute,
                        morning_twilight_hour, morning_twilight_min,
                        sun_times.sunrise_hour, sun_times.sunrise_minute)) {
        
        // Simple minute-based lookup - no floating point calculations!
        uint16_t current_minutes = hour * 60 + minute;
        uint16_t start_minutes = morning_twilight_hour * 60 + morning_twilight_min;
        uint8_t minutes_elapsed = (uint8_t)(current_minutes - start_minutes);
        
        ESP_LOGD(TAG, "Morning twilight: minute %d/%d", minutes_elapsed + 1, WOMO_CIVIL_TWILIGHT_MINUTES);
        computed_color = get_twilight_color_by_minute(minutes_elapsed, false);  // Morning = forward
    }
    
    // Check if we're in evening civil twilight (light blue -> dark blue)
    else if (is_between_times(hour, minute,
                             evening_twilight_hour, evening_twilight_min,
                             sun_times.sunset_hour, sun_times.sunset_minute)) {
        
        // Simple minute-based lookup - no floating point calculations!
        uint16_t current_minutes = hour * 60 + minute;
        uint16_t start_minutes = evening_twilight_hour * 60 + evening_twilight_min;
        uint8_t minutes_elapsed = (uint8_t)(current_minutes - start_minutes);
        
        ESP_LOGD(TAG, "Evening twilight: minute %d/%d", minutes_elapsed + 1, WOMO_CIVIL_TWILIGHT_MINUTES);
        computed_color = get_twilight_color_by_minute(minutes_elapsed, true);   // Evening = reverse
    }
    
    // Not in twilight - return standard day/night color based on time
    else if (womo_theme_is_daytime()) {
        computed_color = WOMO_COLOR_DAY_NORMAL;
    } else {
        computed_color = WOMO_COLOR_NIGHT_NORMAL;
    }
    
    // Cache the twilight result for the entire minute
    // During twilight, this expensive calculation only runs once per minute!
    last_computed_hour = hour;
    last_computed_minute = minute;
    cached_twilight_color = computed_color;
    is_twilight_cached = true;  // Mark as twilight calculation
    
    ESP_LOGD(TAG, "Twilight color calculated and cached for minute %02d:%02d", hour, minute);
    
    return computed_color;
}

lv_color_t womo_theme_get_background_color(void)
{
    // Error/Critical status overrides time-based themes
    if (current_status == WOMO_STATUS_ERROR) {
        return WOMO_COLOR_ERROR;
    }
    else if (current_status == WOMO_STATUS_CRITICAL) {
        return WOMO_COLOR_CRITICAL;
    }
    // WARNING ändert den Hintergrund NICHT – die betroffenen Widgets
    // (Tank, Batterie) zeigen ihren Warnstatus selbst per Farbe an.
    
    // Normal status - use smooth twilight transitions for all modes
    // SUNRISE/SUNSET verwenden Gradient, keine feste Farbe!
    switch (current_mode) {
        case WOMO_THEME_SUNRISE:
        case WOMO_THEME_SUNSET:
        case WOMO_THEME_DAY:
        case WOMO_THEME_NIGHT:
            // Alle Modi verwenden den glatten Twilight-Color-Übergang
            return womo_theme_get_twilight_color();
        default:
            return womo_theme_get_twilight_color();
    }
}

womo_theme_mode_t womo_theme_get_mode(void)
{
    return current_mode;
}

void womo_theme_set_status(womo_status_level_t status)
{
    if (current_status != status) {
        ESP_LOGI(TAG, "Status changed: %d -> %d", current_status, status);
        current_status = status;
    }
}

womo_status_level_t womo_theme_get_status(void)
{
    return current_status;
}

void womo_theme_set_sun_times(uint8_t sunrise_hour, uint8_t sunrise_min,
                               uint8_t sunset_hour, uint8_t sunset_min)
{
    bool changed = (sun_times.sunrise_hour   != sunrise_hour ||
                    sun_times.sunrise_minute != sunrise_min  ||
                    sun_times.sunset_hour    != sunset_hour  ||
                    sun_times.sunset_minute  != sunset_min);
    sun_times.sunrise_hour   = sunrise_hour;
    sun_times.sunrise_minute = sunrise_min;
    sun_times.sunset_hour    = sunset_hour;
    sun_times.sunset_minute  = sunset_min;

    if (changed) {
        ESP_LOGI(TAG, "Sun times updated - Sunrise: %02d:%02d, Sunset: %02d:%02d",
                 sunrise_hour, sunrise_min, sunset_hour, sunset_min);
    }
}

const womo_sun_times_t* womo_theme_get_sun_times(void)
{
    return &sun_times;
}

bool womo_theme_is_daytime(void)
{
    struct tm timeinfo;
    
    if (womo_time_get(&timeinfo) != ESP_OK || timeinfo.tm_year < (2024 - 1900)) {
        return true; // Default to day if time not available or clearly invalid
    }
    
    uint8_t hour = timeinfo.tm_hour;
    uint8_t minute = timeinfo.tm_min;
    
    return is_between_times(hour, minute,
                           sun_times.sunrise_hour, sun_times.sunrise_minute,
                           sun_times.sunset_hour, sun_times.sunset_minute);
}

void womo_theme_apply_to_screen(lv_obj_t *screen)
{
    if (screen == NULL) {
        screen = lv_scr_act();
    }
    
    lv_color_t bg_color = womo_theme_get_background_color();

    // Gradient bei SUNRISE/SUNSET oder während bürgerlicher Dämmerung (nur wenn Status OK)
    bool twilight_now = is_in_civil_twilight_now();
    
    // Gradient IMMER bei SUNRISE/SUNSET-Modi (unabhängig vom Status)
    // Bei anderen Modi nur wenn Status OK und tatsächlich Twilight
    bool gradient_allowed = (current_mode == WOMO_THEME_SUNRISE || current_mode == WOMO_THEME_SUNSET) ||
                            (current_status == WOMO_STATUS_OK && twilight_now);

    // Verwende vertikalen Verlauf: oben Tagesblau, unten Nachtblau
    if (gradient_allowed) {
        lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_color(screen, WOMO_COLOR_DAY_NORMAL, 0);      // oben
        lv_obj_set_style_bg_grad_color(screen, WOMO_COLOR_NIGHT_NORMAL, 0); // unten
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_color(screen, bg_color, 0);
        lv_obj_set_style_bg_grad_color(screen, bg_color, 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    }
    
    ESP_LOGI(TAG, "Applied theme to screen - Mode: %d, Status: %d, Gradient: %s", 
             current_mode, current_status, gradient_allowed ? "YES" : "NO");
}

void womo_theme_reset(void)
{
    // Reset all cached state to force fresh evaluation
    current_mode = WOMO_THEME_DAY;  // Safe default fallback
    current_status = WOMO_STATUS_OK;
    auto_mode_enabled = true;
    
    // Invalidate cached twilight color
    cached_twilight_color = WOMO_COLOR_DAY_NORMAL;
    last_computed_hour = 255;
    last_computed_minute = 255;
    is_twilight_cached = false;
    
    ESP_LOGI(TAG, "Theme state reset to defaults (boot/power-on)");
}

void womo_theme_cycle_mode(void)
{
    // Cycle through theme modes
    switch (current_mode) {
        case WOMO_THEME_DAY:
            current_mode = WOMO_THEME_SUNSET;
            ESP_LOGI(TAG, "Cycled to SUNSET mode");
            break;
        case WOMO_THEME_SUNSET:
            current_mode = WOMO_THEME_NIGHT;
            ESP_LOGI(TAG, "Cycled to NIGHT mode");
            break;
        case WOMO_THEME_NIGHT:
            current_mode = WOMO_THEME_SUNRISE;
            ESP_LOGI(TAG, "Cycled to SUNRISE mode");
            break;
        case WOMO_THEME_SUNRISE:
            current_mode = WOMO_THEME_DAY;
            ESP_LOGI(TAG, "Cycled to DAY mode");
            break;
        default:
            current_mode = WOMO_THEME_DAY;
            break;
    }
}

void womo_theme_cycle_status(void)
{
    // Cycle through status levels
    switch (current_status) {
        case WOMO_STATUS_OK:
            current_status = WOMO_STATUS_WARNING;
            ESP_LOGI(TAG, "Cycled to WARNING status");
            break;
        case WOMO_STATUS_WARNING:
            current_status = WOMO_STATUS_ERROR;
            ESP_LOGI(TAG, "Cycled to ERROR status");
            break;
        case WOMO_STATUS_ERROR:
            current_status = WOMO_STATUS_CRITICAL;
            ESP_LOGI(TAG, "Cycled to CRITICAL status");
            break;
        case WOMO_STATUS_CRITICAL:
            current_status = WOMO_STATUS_OK;
            ESP_LOGI(TAG, "Cycled to OK status");
            break;
        default:
            current_status = WOMO_STATUS_OK;
            break;
    }
}

void womo_theme_set_mode(womo_theme_mode_t mode)
{
    current_mode = mode;
    ESP_LOGI(TAG, "Manual mode set to: %d", mode);
}

void womo_theme_set_auto_mode(bool enable)
{
    auto_mode_enabled = enable;
    ESP_LOGI(TAG, "Auto mode %s", enable ? "ENABLED" : "DISABLED");
}

bool womo_theme_is_auto_mode(void)
{
    return auto_mode_enabled;
}
