#include "womo_locale.h"
#include <string.h>

// Current locale
static womo_locale_t s_current_locale = WOMO_LOCALE_DE;

// String table [locale][string_id]
static const char* s_strings[WOMO_LOCALE_MAX][STR_MAX] = {
    // German strings
    {
        [STR_TITLE] = "WoMo Home Control",
        [STR_MODE] = "Modus:",
        [STR_STATUS] = "Status:",
        [STR_WIFI] = "WiFi:",
        [STR_WIFI_DISCONNECTED] = "WiFi: Getrennt",
        [STR_TEMP] = "Temp:",
        [STR_HUMID] = "Feuchte:",
        [STR_PRESS] = "Druck:",
        [STR_GAS] = "Gas:",
        [STR_IMU] = "IMU:",
        [STR_WEIGHT] = "Gewicht:",
        [STR_BATTERY] = "Batterie:",
        [STR_TANKS] = "Tanks:",
        [STR_RS485_WAITING] = "RS485: Warte...",
        [STR_RS485_PACKETS] = "RS485: %lu Pakete",
        [STR_STATUS_OK] = "OK",
        [STR_STATUS_WARNING] = "Warnung",
        [STR_STATUS_CRITICAL] = "Kritisch",
        [STR_STATUS_ERROR] = "Fehler",
        [STR_MODE_DAY] = "Tag",
        [STR_MODE_NIGHT] = "Nacht",
        [STR_MODE_SUNRISE] = "Sonnenaufgang",
        [STR_MODE_SUNSET] = "Sonnenuntergang",
        [STR_WEEKDAY_SUN] = "So",
        [STR_WEEKDAY_MON] = "Mo",
        [STR_WEEKDAY_TUE] = "Di",
        [STR_WEEKDAY_WED] = "Mi",
        [STR_WEEKDAY_THU] = "Do",
        [STR_WEEKDAY_FRI] = "Fr",
        [STR_WEEKDAY_SAT] = "Sa",
    },
    // English strings
    {
        [STR_TITLE] = "WoMo Home Control",
        [STR_MODE] = "Mode:",
        [STR_STATUS] = "Status:",
        [STR_WIFI] = "WiFi:",
        [STR_WIFI_DISCONNECTED] = "WiFi: Disconnected",
        [STR_TEMP] = "Temp:",
        [STR_HUMID] = "Humid:",
        [STR_PRESS] = "Press:",
        [STR_GAS] = "Gas:",
        [STR_IMU] = "IMU:",
        [STR_WEIGHT] = "Weight:",
        [STR_BATTERY] = "Battery:",
        [STR_TANKS] = "Tanks:",
        [STR_RS485_WAITING] = "RS485: Waiting...",
        [STR_RS485_PACKETS] = "RS485: %lu packets",
        [STR_STATUS_OK] = "OK",
        [STR_STATUS_WARNING] = "Warning",
        [STR_STATUS_CRITICAL] = "Critical",
        [STR_STATUS_ERROR] = "Error",
        [STR_MODE_DAY] = "Day",
        [STR_MODE_NIGHT] = "Night",
        [STR_MODE_SUNRISE] = "Sunrise",
        [STR_MODE_SUNSET] = "Sunset",
        [STR_WEEKDAY_SUN] = "Sun",
        [STR_WEEKDAY_MON] = "Mon",
        [STR_WEEKDAY_TUE] = "Tue",
        [STR_WEEKDAY_WED] = "Wed",
        [STR_WEEKDAY_THU] = "Thu",
        [STR_WEEKDAY_FRI] = "Fri",
        [STR_WEEKDAY_SAT] = "Sat",
    }
};

void womo_locale_init(void)
{
    s_current_locale = WOMO_LOCALE_DE;  // Default to German
}

void womo_locale_set(womo_locale_t locale)
{
    if (locale < WOMO_LOCALE_MAX) {
        s_current_locale = locale;
    }
}

womo_locale_t womo_locale_get(void)
{
    return s_current_locale;
}

const char* womo_locale_get_string(womo_string_id_t str_id)
{
    if (str_id >= STR_MAX) {
        return "???";
    }
    
    const char* str = s_strings[s_current_locale][str_id];
    return str ? str : "???";
}

const char* womo_locale_get_weekday(uint8_t day)
{
    if (day > 6) {
        return "??";
    }
    
    womo_string_id_t str_id = STR_WEEKDAY_SUN + day;
    return womo_locale_get_string(str_id);
}
