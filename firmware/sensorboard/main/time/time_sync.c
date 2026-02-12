#include "time/time_sync.h"

#include <string.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "time/rtc_pcf8523.h"
#include "driver/i2c_master.h"
#include "hal/sensor_i2c_bus.h"

static const char *TAG = "time_sync";

// Schwellenwert: System-Zeit aktualisieren wenn Abweichung > 2s
#define TIME_SYNC_UPDATE_THRESHOLD_SEC 2

// Schwellenwert: RTC aktualisieren wenn Abweichung > 10s
#define TIME_SYNC_RTC_UPDATE_THRESHOLD_SEC 10

// Minimum 1990 für gültige Zeit (1970 = ungültig)
#define TIME_SYNC_VALID_YEAR 1990

// PCF8523 Register für Batteriestatus
#define PCF8523_ADDR            0x68
#define PCF8523_REG_CONTROL_3   0x02
#define PCF8523_CONTROL3_BLF    (1 << 2)  // Battery Low Flag

static time_sync_status_t s_status = {
    .active_source = TIME_SOURCE_NONE,
    .last_sync_time = 0,
    .last_sync_value = 0,
    .rtc_battery_low = false,
    .system_time_valid = false,
    .gps_sync_count = 0,
    .lte_sync_count = 0,
    .rtc_sync_count = 0,
};

static SemaphoreHandle_t s_status_mutex = NULL;

const char* time_sync_source_to_string(time_source_t source)
{
    switch (source) {
        case TIME_SOURCE_GPS:  return "GPS";
        case TIME_SOURCE_LTE:  return "LTE";
        case TIME_SOURCE_RTC:  return "RTC";
        case TIME_SOURCE_NONE: return "NONE";
        default:               return "UNKNOWN";
    }
}

static bool is_time_valid(time_t t)
{
    struct tm tm_time = {0};
    gmtime_r(&t, &tm_time);
    return (tm_time.tm_year + 1900) >= TIME_SYNC_VALID_YEAR;
}

static esp_err_t update_system_time(time_t new_time, time_source_t source)
{
    if (!is_time_valid(new_time)) {
        ESP_LOGW(TAG, "Zeit ungültig (source=%s): %ld", time_sync_source_to_string(source), (long)new_time);
        return ESP_ERR_INVALID_ARG;
    }

    struct timeval tv_current = {0};
    gettimeofday(&tv_current, NULL);
    
    int64_t delta = (int64_t)new_time - (int64_t)tv_current.tv_sec;
    bool needs_update = !s_status.system_time_valid || (llabs(delta) > TIME_SYNC_UPDATE_THRESHOLD_SEC);

    if (needs_update) {
        struct timeval tv_new = {
            .tv_sec = new_time,
            .tv_usec = 0
        };
        settimeofday(&tv_new, NULL);
        
        ESP_LOGI(TAG, "System-Zeit aktualisiert via %s (Δ=%llds)", 
                 time_sync_source_to_string(source), (long long)delta);
    } else {
        ESP_LOGD(TAG, "System-Zeit bereits synchron (Δ=%llds, source=%s)", 
                 (long long)delta, time_sync_source_to_string(source));
    }

    return ESP_OK;
}

static esp_err_t update_rtc_time(time_t new_time)
{
    time_t rtc_time = 0;
    esp_err_t err = pcf8523_app_get_time(&rtc_time);
    
    if (err == ESP_OK) {
        int64_t delta = (int64_t)new_time - (int64_t)rtc_time;
        if (llabs(delta) < TIME_SYNC_RTC_UPDATE_THRESHOLD_SEC) {
            ESP_LOGD(TAG, "RTC bereits synchron (Δ=%llds)", (long long)delta);
            return ESP_OK;
        }
    }

    err = pcf8523_app_set_time(new_time);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RTC aktualisiert: %ld", (long)new_time);
    } else {
        ESP_LOGW(TAG, "RTC-Update fehlgeschlagen: %s", esp_err_to_name(err));
    }
    
    return err;
}

esp_err_t time_sync_init(void)
{
    if (s_status_mutex != NULL) {
        ESP_LOGW(TAG, "time_sync bereits initialisiert");
        return ESP_OK;
    }

    s_status_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_status_mutex != NULL, ESP_ERR_NO_MEM, TAG, "Mutex-Erstellung fehlgeschlagen");

    // RTC initialisieren und System-Zeit beim Boot wiederherstellen
    esp_err_t rtc_err = pcf8523_app_start();
    if (rtc_err != ESP_OK) {
        ESP_LOGW(TAG, "RTC-Start fehlgeschlagen: %s", esp_err_to_name(rtc_err));
    }

    time_t rtc_time = 0;
    rtc_err = pcf8523_app_get_time(&rtc_time);
    if (rtc_err == ESP_OK && is_time_valid(rtc_time)) {
        struct timeval tv = {
            .tv_sec = rtc_time,
            .tv_usec = 0
        };
        settimeofday(&tv, NULL);
        
        xSemaphoreTake(s_status_mutex, portMAX_DELAY);
        s_status.active_source = TIME_SOURCE_RTC;
        s_status.last_sync_time = rtc_time;
        s_status.last_sync_value = rtc_time;
        s_status.system_time_valid = true;
        s_status.rtc_sync_count = 1;
        xSemaphoreGive(s_status_mutex);

        struct tm tm_time = {0};
        gmtime_r(&rtc_time, &tm_time);
        ESP_LOGI(TAG, "System-Zeit aus RTC initialisiert: %04d-%02d-%02d %02d:%02d:%02d UTC",
                 tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
                 tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
    } else {
        ESP_LOGW(TAG, "RTC-Zeit ungültig oder Lesefehler beim Boot");
    }

    // Batteriestatus prüfen
    bool battery_low = false;
    esp_err_t bat_err = time_sync_check_rtc_battery(&battery_low);
    if (bat_err == ESP_OK && battery_low) {
        ESP_LOGW(TAG, "⚠️  RTC-Batterie schwach! Backup-Zeit nicht verlässlich.");
    }

    ESP_LOGI(TAG, "Zeit-Synchronisation initialisiert (Hierarchie: GPS → LTE → RTC)");
    return ESP_OK;
}

esp_err_t time_sync_update_from_gps(time_t gps_utc_time)
{
    ESP_RETURN_ON_FALSE(s_status_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "time_sync nicht initialisiert");
    ESP_RETURN_ON_FALSE(is_time_valid(gps_utc_time), ESP_ERR_INVALID_ARG, TAG, "GPS-Zeit ungültig");

    esp_err_t err = update_system_time(gps_utc_time, TIME_SOURCE_GPS);
    if (err != ESP_OK) {
        return err;
    }

    // RTC mit GPS-Zeit aktualisieren (beste Genauigkeit)
    update_rtc_time(gps_utc_time);

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    s_status.active_source = TIME_SOURCE_GPS;
    s_status.last_sync_time = gps_utc_time;
    s_status.last_sync_value = gps_utc_time;
    s_status.system_time_valid = true;
    s_status.gps_sync_count++;
    xSemaphoreGive(s_status_mutex);

    return ESP_OK;
}

esp_err_t time_sync_update_from_lte(time_t lte_utc_time)
{
    ESP_RETURN_ON_FALSE(s_status_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "time_sync nicht initialisiert");
    ESP_RETURN_ON_FALSE(is_time_valid(lte_utc_time), ESP_ERR_INVALID_ARG, TAG, "LTE-Zeit ungültig");

    // LTE nur nutzen wenn GPS in letzten 5 Minuten nicht verfügbar war
    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    time_t last_gps = (s_status.active_source == TIME_SOURCE_GPS) ? s_status.last_sync_time : 0;
    xSemaphoreGive(s_status_mutex);

    struct timeval tv_now = {0};
    gettimeofday(&tv_now, NULL);
    
    if (last_gps > 0 && (tv_now.tv_sec - last_gps) < 300) {
        ESP_LOGD(TAG, "GPS-Zeit noch aktuell (vor %ld s), LTE-Fallback nicht nötig", 
                 (long)(tv_now.tv_sec - last_gps));
        return ESP_OK;
    }

    esp_err_t err = update_system_time(lte_utc_time, TIME_SOURCE_LTE);
    if (err != ESP_OK) {
        return err;
    }

    // RTC mit LTE-Zeit aktualisieren
    update_rtc_time(lte_utc_time);

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    s_status.active_source = TIME_SOURCE_LTE;
    s_status.last_sync_time = lte_utc_time;
    s_status.last_sync_value = lte_utc_time;
    s_status.system_time_valid = true;
    s_status.lte_sync_count++;
    xSemaphoreGive(s_status_mutex);

    ESP_LOGI(TAG, "System-Zeit via LTE synchronisiert (Fallback)");
    return ESP_OK;
}

esp_err_t time_sync_check_rtc_battery(bool *battery_low)
{
    ESP_RETURN_ON_FALSE(battery_low != NULL, ESP_ERR_INVALID_ARG, TAG, "battery_low ist NULL");

    // I2C-Bus muss initialisiert sein
    esp_err_t bus_init = i2c_bus_init();
    if (bus_init != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus nicht verfügbar: %s", esp_err_to_name(bus_init));
        return bus_init;
    }

    i2c_master_bus_handle_t bus = i2c_bus_get_internal();
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_FAIL, TAG, "I2C bus handle fehlt");

    // PCF8523 Device Handle erstellen (temporär für diesen Zugriff)
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF8523_ADDR,
        .scl_speed_hz = 100000,
    };

    i2c_master_dev_handle_t dev = NULL;
    i2c_bus_lock();
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    i2c_bus_unlock();
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PCF8523 Device-Erstellung fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    // Control_3 Register lesen
    uint8_t reg = PCF8523_REG_CONTROL_3;
    uint8_t ctrl3 = 0;
    
    i2c_bus_lock();
    err = i2c_master_transmit_receive(dev, &reg, 1, &ctrl3, 1, 500);
    i2c_bus_unlock();

    // Device Handle freigeben
    i2c_bus_lock();
    i2c_master_bus_rm_device(dev);
    i2c_bus_unlock();

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Control_3 Register lesen fehlgeschlagen: %s", esp_err_to_name(err));
        return err;
    }

    *battery_low = (ctrl3 & PCF8523_CONTROL3_BLF) != 0;

    // Status aktualisieren
    if (s_status_mutex != NULL) {
        xSemaphoreTake(s_status_mutex, portMAX_DELAY);
        s_status.rtc_battery_low = *battery_low;
        xSemaphoreGive(s_status_mutex);
    }

    ESP_LOGD(TAG, "RTC Batteriestatus: %s (Control_3=0x%02X)", 
             *battery_low ? "SCHWACH" : "OK", ctrl3);

    return ESP_OK;
}

esp_err_t time_sync_get_status(time_sync_status_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status ist NULL");
    ESP_RETURN_ON_FALSE(s_status_mutex != NULL, ESP_ERR_INVALID_STATE, TAG, "time_sync nicht initialisiert");

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    memcpy(status, &s_status, sizeof(time_sync_status_t));
    xSemaphoreGive(s_status_mutex);

    return ESP_OK;
}
