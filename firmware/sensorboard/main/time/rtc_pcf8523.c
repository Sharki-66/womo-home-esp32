#include "time/rtc_pcf8523.h"

#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "hal/sensor_i2c_bus.h"
#include "sensor_config.h"

// PCF8523 Control_3 Bits
#define PCF8523_CTRL3_PM2       (1 << 7)  // Power Management Bit 2
#define PCF8523_CTRL3_PM1       (1 << 6)  // Power Management Bit 1
#define PCF8523_CTRL3_PM0       (1 << 5)  // Power Management Bit 0
#define PCF8523_CTRL3_BSF       (1 << 4)  // Battery Switch-over Flag (VDD-Ausfall erkannt)
#define PCF8523_CTRL3_BLF       (1 << 3)  // Battery Low Flag (Spannung < ~1.2V)
#define PCF8523_CTRL3_BSIE      (1 << 1)  // Battery Switch-over Interrupt Enable
#define PCF8523_CTRL3_BLIE      (1 << 0)  // Battery Low Interrupt Enable

#define PCF8523_ADDR            0x68
#define PCF8523_REG_CONTROL_1   0x00
#define PCF8523_REG_CONTROL_3   0x02
#define PCF8523_REG_SECONDS     0x03

#define PCF8523_LOG_INTERVAL_MS 5000

static const char *TAG = "pcf8523_app";

static i2c_master_dev_handle_t s_dev = NULL;
static TaskHandle_t s_task = NULL;

static uint8_t bcd2dec(uint8_t val)
{
    return (val >> 4) * 10 + (val & 0x0F);
}

static uint8_t dec2bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

static bool is_leap_year(int year)
{
    if (year % 400 == 0) {
        return true;
    }
    if (year % 100 == 0) {
        return false;
    }
    return (year % 4 == 0);
}

static time_t tm_to_epoch_utc(const struct tm *tm_utc)
{
    static const int days_before_month[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    int year = tm_utc->tm_year + 1900;
    int month = tm_utc->tm_mon + 1;
    int day = tm_utc->tm_mday;

    int64_t days = 0;
    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }

    days += days_before_month[month - 1];
    if (month > 2 && is_leap_year(year)) {
        days += 1;
    }
    days += (day - 1);

    int64_t seconds = (days * 86400LL)
                      + (tm_utc->tm_hour * 3600LL)
                      + (tm_utc->tm_min * 60LL)
                      + tm_utc->tm_sec;
    return (time_t)seconds;
}

static esp_err_t pcf8523_read(uint8_t reg, uint8_t *buf, size_t len)
{
    i2c_bus_lock();
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 500);
    i2c_bus_unlock();
    return err;
}

static esp_err_t pcf8523_write_byte(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    i2c_bus_lock();
    esp_err_t err = i2c_master_transmit(s_dev, data, sizeof(data), 500);
    i2c_bus_unlock();
    return err;
}

static esp_err_t pcf8523_write_regs(uint8_t start_reg, const uint8_t *data, size_t len)
{
    uint8_t buf[1 + 8] = {0};
    if (len > 8) {
        return ESP_ERR_INVALID_ARG;
    }
    buf[0] = start_reg;
    for (size_t i = 0; i < len; i++) {
        buf[1 + i] = data[i];
    }
    i2c_bus_lock();
    esp_err_t err = i2c_master_transmit(s_dev, buf, 1 + len, 500);
    i2c_bus_unlock();
    return err;
}

static esp_err_t pcf8523_set_24h_mode(void)
{
    uint8_t ctrl = 0;
    esp_err_t err = pcf8523_read(PCF8523_REG_CONTROL_1, &ctrl, 1);
    if (err != ESP_OK) {
        return err;
    }
    ctrl &= ~(1 << 3); // 12/24h = 0 -> 24h
    return pcf8523_write_byte(PCF8523_REG_CONTROL_1, ctrl);
}

static void pcf8523_task(void *arg)
{
    const TickType_t delay_ticks = pdMS_TO_TICKS(PCF8523_LOG_INTERVAL_MS);
    while (1) {
        uint8_t buf[7] = {0};
        esp_err_t err = pcf8523_read(PCF8523_REG_SECONDS, buf, sizeof(buf));
        if (err == ESP_OK) {
            bool os_flag = (buf[0] & 0x80) != 0; // Oscillator Stop Flag
            int sec = bcd2dec(buf[0] & 0x7F);
            int min = bcd2dec(buf[1] & 0x7F);
            int hour = bcd2dec(buf[2] & 0x3F);
            int day = bcd2dec(buf[3] & 0x3F);
            int month = bcd2dec(buf[5] & 0x1F);
            int year = bcd2dec(buf[6]) + 2000;
            if (os_flag) {
                ESP_LOGW(TAG, "RTC: OS-Flag gesetzt – Zeit ungültig (noch nie gesetzt)");
            } else {
                ESP_LOGI(TAG, "RTC: %04d-%02d-%02d %02d:%02d:%02d",
                         year, month, day, hour, min, sec);
            }
        } else {
            ESP_LOGW(TAG, "RTC lesen fehlgeschlagen (%s)", esp_err_to_name(err));
        }
        vTaskDelay(delay_ticks);
    }
}

esp_err_t pcf8523_app_start(void)
{
    if (s_task) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "I2C init fehlgeschlagen");

    i2c_bus_handle_t bus = i2c_bus_get();
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_FAIL, TAG, "I2C bus fehlt");

    uint8_t found[8] = {0};
    i2c_bus_lock();
    uint8_t count = i2c_bus_scan(bus, found, sizeof(found));
    i2c_bus_unlock();
    bool seen = false;
    for (int i = 0; i < count; i++) {
        if (found[i] == PCF8523_ADDR) {
            seen = true;
            break;
        }
    }
    if (!seen) {
        ESP_LOGW(TAG, "PCF8523 (0x%02X) nicht gesehen", PCF8523_ADDR);
        return ESP_ERR_NOT_FOUND;
    }

    i2c_master_bus_handle_t bus_internal = i2c_bus_get_internal();
    ESP_RETURN_ON_FALSE(bus_internal != NULL, ESP_FAIL, TAG, "internal bus handle missing");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF8523_ADDR,
        .scl_speed_hz = SENSOR_I2C_EXT_SPEED_HZ,
        .scl_wait_us = 0xffff,
    };

    i2c_bus_lock();
    esp_err_t add_err = i2c_master_bus_add_device(bus_internal, &dev_cfg, &s_dev);
    i2c_bus_unlock();
    ESP_RETURN_ON_ERROR(add_err, TAG, "add device failed");

    esp_err_t mode_err = pcf8523_set_24h_mode();
    if (mode_err != ESP_OK) {
        ESP_LOGW(TAG, "24h Modus setzen fehlgeschlagen (%s)", esp_err_to_name(mode_err));
    }

    // Control_3 konfigurieren: Battery Switch-over aktivieren, Flags auslesen
    uint8_t ctrl3 = 0;
    esp_err_t bat_err = pcf8523_read(PCF8523_REG_CONTROL_3, &ctrl3, 1);
    if (bat_err == ESP_OK) {
        bool blf = (ctrl3 & PCF8523_CTRL3_BLF) != 0;
        bool bsf = (ctrl3 & PCF8523_CTRL3_BSF) != 0;
        ESP_LOGI(TAG, "Control_3 = 0x%02X | BLF=%d (bat low) BSF=%d (power-cut)", ctrl3, blf, bsf);

        // PM-Bits 7-5 auf 000 = Standard Battery Switch-over aktivieren
        // Interrupt-Bits deaktivieren, BSF/BLF Flags stehen lassen
        uint8_t new_ctrl3 = ctrl3 & (PCF8523_CTRL3_BSF | PCF8523_CTRL3_BLF);
        esp_err_t write_err = pcf8523_write_byte(PCF8523_REG_CONTROL_3, new_ctrl3);
        if (write_err == ESP_OK) {
            ESP_LOGI(TAG, "Battery Switch-over aktiviert (Standard-Mode)");
        } else {
            ESP_LOGW(TAG, "Control_3 schreiben fehlgeschlagen");
        }

        if (blf) ESP_LOGW(TAG, "⚠️  RTC Batterie schwach (BLF gesetzt)");
        if (bsf) ESP_LOGW(TAG, "⚡ RTC war auf Batterie (BSF – VDD-Ausfall erkannt)");
        if (!blf && !bsf) ESP_LOGI(TAG, "✓ RTC Batterie OK, kein Stromausfall erkannt");
    } else {
        ESP_LOGW(TAG, "Control_3 konnte nicht gelesen werden");
    }

    BaseType_t r = xTaskCreatePinnedToCore(pcf8523_task, "pcf8523_task", 4096, NULL, 4, &s_task, 0);
    if (r != pdPASS) {
        s_task = NULL;
        ESP_RETURN_ON_ERROR(ESP_FAIL, TAG, "task start failed");
    }

    ESP_LOGI(TAG, "PCF8523 gestartet (I2C%d, SDA=%d, SCL=%d)",
             SENSOR_I2C_EXT_PORT, SENSOR_I2C_EXT_SDA_GPIO, SENSOR_I2C_EXT_SCL_GPIO);
    return ESP_OK;
}

esp_err_t pcf8523_app_get_battery_status(bool *bat_low, bool *bat_switched)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t ctrl3 = 0;
    esp_err_t err = pcf8523_read(PCF8523_REG_CONTROL_3, &ctrl3, 1);
    if (err != ESP_OK) {
        return err;
    }

    if (bat_low)     *bat_low     = (ctrl3 & PCF8523_CTRL3_BLF) != 0;
    if (bat_switched) *bat_switched = (ctrl3 & PCF8523_CTRL3_BSF) != 0;

    return ESP_OK;
}

esp_err_t pcf8523_app_set_time(time_t utc_time)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (utc_time <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    struct tm tm_utc = {0};
    gmtime_r(&utc_time, &tm_utc);

    if (tm_utc.tm_year < 100) {
        return ESP_ERR_INVALID_ARG; // vor Jahr 2000 nicht unterstützen
    }

    uint8_t payload[7] = {0};
    payload[0] = dec2bcd((uint8_t)tm_utc.tm_sec) & 0x7F;
    payload[1] = dec2bcd((uint8_t)tm_utc.tm_min) & 0x7F;
    payload[2] = dec2bcd((uint8_t)tm_utc.tm_hour) & 0x3F;
    payload[3] = dec2bcd((uint8_t)tm_utc.tm_mday) & 0x3F;
    payload[4] = dec2bcd((uint8_t)tm_utc.tm_wday) & 0x07;
    payload[5] = dec2bcd((uint8_t)(tm_utc.tm_mon + 1)) & 0x1F;
    payload[6] = dec2bcd((uint8_t)(tm_utc.tm_year - 100));

    esp_err_t err = pcf8523_write_regs(PCF8523_REG_SECONDS, payload, sizeof(payload));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RTC gesetzt: %04d-%02d-%02d %02d:%02d:%02d UTC",
                 tm_utc.tm_year + 1900,
                 tm_utc.tm_mon + 1,
                 tm_utc.tm_mday,
                 tm_utc.tm_hour,
                 tm_utc.tm_min,
                 tm_utc.tm_sec);
    }
    return err;
}

esp_err_t pcf8523_app_get_time(time_t *utc_time)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!utc_time) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buf[7] = {0};
    esp_err_t err = pcf8523_read(PCF8523_REG_SECONDS, buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }

    // OS-Flag (Bit 7 im Seconds-Register): gesetzt wenn Oszillator gestoppt war
    // → RTC wurde nie gesetzt oder Backup-Batterie war leer → Zeit ungültig
    if (buf[0] & 0x80) {
        ESP_LOGW(TAG, "RTC OS-Flag gesetzt: Zeit nie gesetzt oder Batterie leer gewesen");
        return ESP_ERR_INVALID_STATE;
    }

    int sec = bcd2dec(buf[0] & 0x7F);
    int min = bcd2dec(buf[1] & 0x7F);
    int hour = bcd2dec(buf[2] & 0x3F);
    int day = bcd2dec(buf[3] & 0x3F);
    int month = bcd2dec(buf[5] & 0x1F);
    int year = bcd2dec(buf[6]) + 2000;

    if (sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 || month < 1 || month > 12 || year < 2000) {
        return ESP_ERR_INVALID_STATE;
    }

    struct tm tm_utc = {0};
    tm_utc.tm_year = year - 1900;
    tm_utc.tm_mon = month - 1;
    tm_utc.tm_mday = day;
    tm_utc.tm_hour = hour;
    tm_utc.tm_min = min;
    tm_utc.tm_sec = sec;

    *utc_time = tm_to_epoch_utc(&tm_utc);
    return ESP_OK;
}
