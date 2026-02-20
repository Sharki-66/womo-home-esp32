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
        [STR_RS485_TIMEOUT_LABEL] = "RS485: Timeout (5 min keine Daten)",
        [STR_RS485_TIMEOUT_SOURCE] = "RS485 Timeout",
        [STR_STATUS_OK] = "OK",
        [STR_STATUS_WARNING] = "Warnung",
        [STR_STATUS_CRITICAL] = "Kritisch",
        [STR_STATUS_ERROR] = "Fehler",
        [STR_MODE_DAY] = "Tag",
        [STR_MODE_NIGHT] = "Nacht",
        [STR_MODE_SUNRISE] = "Sonnenaufgang",
        [STR_MODE_SUNSET] = "Sonnenuntergang",
        [STR_RS485_WAITING_HELLO] = "RS485: Warte auf Sensorboard",
        [STR_RS485_INVALID_DATA] = "RS485: Ungültige Daten",
        [STR_WEEKDAY_SUN] = "So",
        [STR_WEEKDAY_MON] = "Mo",
        [STR_WEEKDAY_TUE] = "Di",
        [STR_WEEKDAY_WED] = "Mi",
        [STR_WEEKDAY_THU] = "Do",
        [STR_WEEKDAY_FRI] = "Fr",
        [STR_WEEKDAY_SAT] = "Sa",
        [STR_CONNECTIVITY_TITLE] = "Konnektivität",
        [STR_AP_SECTION_TITLE] = "AP / HotSpot",
        [STR_AP_STATUS_ACTIVE] = "Aktiv",
        [STR_AP_STATUS_INACTIVE] = "Inaktiv",
        [STR_WIFI_SECTION_TITLE] = "WLAN",
        [STR_WIFI_ENABLE_SWITCH] = "WLAN aktiv",
        [STR_WIFI_SCAN_BUTTON] = "Netzwerksuche",
        [STR_WIFI_SCANNING_STATUS] = "Suche läuft...",
        [STR_WIFI_SELECT_PLACEHOLDER] = "Netzwerk auswählen",
        [STR_WIFI_PASSWORD_LABEL] = "Passwort",
        [STR_WIFI_PASSWORD_PLACEHOLDER] = "Kennwort eingeben",
        [STR_WIFI_NO_RESULTS] = "Keine Netzwerke gefunden",
        [STR_WIFI_STATUS_CONNECTING] = "Verbinde...",
        [STR_WIFI_STATUS_DISCONNECTED] = "Nicht verbunden",
        [STR_WIFI_STATUS_ERROR] = "Verbindung fehlgeschlagen",
        [STR_WIFI_ACTION_NOT_AVAILABLE] = "Aktion nicht verfügbar",
        [STR_WIFI_CONNECT_BUTTON] = "Verbinden",
        [STR_WIFI_CANCEL_BUTTON] = "Abbrechen",
        [STR_LTE_SECTION_TITLE] = "LTE",
        [STR_LTE_ENABLE_SWITCH] = "LTE aktiv",
        [STR_LTE_STATUS_WAITING] = "Warte auf Daten",
        [STR_LTE_STATUS_OFFLINE] = "Offline",
        [STR_LTE_STATUS_DISABLING] = "Deaktiviere...",
        [STR_LTE_STATUS_ENABLING] = "Aktiviere...",
        [STR_MODAL_CLOSE_BUTTON] = "Schliessen",
        [STR_GAS_MODAL_TITLE_FRONT] = "Gasflasche vorne",
        [STR_GAS_MODAL_TITLE_REAR] = "Gasflasche hinten",
        [STR_GAS_MODAL_QUESTION] = "Flasche gewechselt?",
        [STR_GAS_MODAL_BTN_YES] = "Ja",
        [STR_GAS_MODAL_BTN_NO] = "Nein",
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
        [STR_RS485_TIMEOUT_LABEL] = "RS485: Timeout (no data for 5 min)",
        [STR_RS485_TIMEOUT_SOURCE] = "RS485 timeout",
        [STR_STATUS_OK] = "OK",
        [STR_STATUS_WARNING] = "Warning",
        [STR_STATUS_CRITICAL] = "Critical",
        [STR_STATUS_ERROR] = "Error",
        [STR_MODE_DAY] = "Day",
        [STR_MODE_NIGHT] = "Night",
        [STR_MODE_SUNRISE] = "Sunrise",
        [STR_MODE_SUNSET] = "Sunset",
        [STR_RS485_WAITING_HELLO] = "RS485: Waiting for Sensorboard",
        [STR_RS485_INVALID_DATA] = "RS485: Invalid data",
        [STR_WEEKDAY_SUN] = "Sun",
        [STR_WEEKDAY_MON] = "Mon",
        [STR_WEEKDAY_TUE] = "Tue",
        [STR_WEEKDAY_WED] = "Wed",
        [STR_WEEKDAY_THU] = "Thu",
        [STR_WEEKDAY_FRI] = "Fri",
        [STR_WEEKDAY_SAT] = "Sat",
        [STR_CONNECTIVITY_TITLE] = "Connectivity",
        [STR_AP_SECTION_TITLE] = "AP / HotSpot",
        [STR_AP_STATUS_ACTIVE] = "Active",
        [STR_AP_STATUS_INACTIVE] = "Off",
        [STR_WIFI_SECTION_TITLE] = "WiFi",
        [STR_WIFI_ENABLE_SWITCH] = "WiFi enabled",
        [STR_WIFI_SCAN_BUTTON] = "Scan networks",
        [STR_WIFI_SCANNING_STATUS] = "Scanning...",
        [STR_WIFI_SELECT_PLACEHOLDER] = "Select network",
        [STR_WIFI_PASSWORD_LABEL] = "Password",
        [STR_WIFI_PASSWORD_PLACEHOLDER] = "Enter password",
        [STR_WIFI_NO_RESULTS] = "No networks found",
        [STR_WIFI_STATUS_CONNECTING] = "Connecting...",
        [STR_WIFI_STATUS_DISCONNECTED] = "Disconnected",
        [STR_WIFI_STATUS_ERROR] = "Connection failed",
        [STR_WIFI_ACTION_NOT_AVAILABLE] = "Action unavailable",
        [STR_WIFI_CONNECT_BUTTON] = "Connect",
        [STR_WIFI_CANCEL_BUTTON] = "Cancel",
        [STR_LTE_SECTION_TITLE] = "LTE",
        [STR_LTE_ENABLE_SWITCH] = "LTE enabled",
        [STR_LTE_STATUS_WAITING] = "Waiting for data",
        [STR_LTE_STATUS_OFFLINE] = "Offline",
        [STR_LTE_STATUS_DISABLING] = "Disabling...",
        [STR_LTE_STATUS_ENABLING] = "Enabling...",
        [STR_MODAL_CLOSE_BUTTON] = "Close",
        [STR_GAS_MODAL_TITLE_FRONT] = "Front gas bottle",
        [STR_GAS_MODAL_TITLE_REAR] = "Rear gas bottle",
        [STR_GAS_MODAL_QUESTION] = "Bottle replaced?",
        [STR_GAS_MODAL_BTN_YES] = "Yes",
        [STR_GAS_MODAL_BTN_NO] = "No",
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
