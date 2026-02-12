/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "sensor_config.h"
#include "sensors/analog_sensor.h"
#include "sensors/bno055_sensor.h"
#include "sensors/bme680_sensor.h"
#include "sensors/hx711_sensor.h"
#include "time/rtc_pcf8523.h"
#include "time/time_sync.h"
#include "network/rs485_modem.h"

static const char *TAG = "sensor_main";

void app_main(void)
{
#ifdef CONFIG_ESP32_S3_USB_OTG
    const gpio_config_t host_sel_cfg = {
        .pin_bit_mask = BIT64(GPIO_NUM_18),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&host_sel_cfg));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_18, 1));

    // GPIO13 schaltet USB-Device-Power (GPIO12/17 bleiben frei für RS485)
    const gpio_config_t usb_power_cfg = {
        .pin_bit_mask = BIT64(GPIO_NUM_13),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&usb_power_cfg));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_13, 0));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_13, 1));
#endif

    // RS485 Debug-Logging aktivieren
    esp_log_level_set("rs485_modem", ESP_LOG_DEBUG);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Zeit-Synchronisation initialisieren (liest RTC beim Boot)
    esp_err_t time_err = time_sync_init();
    if (time_err != ESP_OK) {
        ESP_LOGW(TAG, "Zeit-Synchronisation nicht verfügbar: %s", esp_err_to_name(time_err));
    }

    // ADC für Spannungen/Tanks vorbereiten
    esp_err_t analog_err = analog_init();
    if (analog_err != ESP_OK) {
        ESP_LOGW(TAG, "ADC init fehlgeschlagen: %s", esp_err_to_name(analog_err));
    }

    // BME680 auf externem I2C (0x76/0x77) starten
    esp_err_t bme_err = bme680_app_start();
    if (bme_err != ESP_OK) {
        ESP_LOGW(TAG, "BME680 nicht aktiv (err=%s)", esp_err_to_name(bme_err));
    }

    // HX711 (Gaswaage) starten
    esp_err_t hx_err = hx711_app_start();
    if (hx_err != ESP_OK) {
        ESP_LOGW(TAG, "HX711 nicht aktiv (err=%s)", esp_err_to_name(hx_err));
    }

    // BNO055 als letztes initialisieren, kleine Pause davor
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_err_t bno_err = bno055_app_start();
    if (bno_err != ESP_OK) {
        ESP_LOGW(TAG, "BNO055 nicht aktiv (err=%s)", esp_err_to_name(bno_err));
    }

    // RS485-Kommunikation zum Display initialisieren
    esp_err_t rs485_err = rs485_modem_init();
    if (rs485_err != ESP_OK) {
        ESP_LOGW(TAG, "RS485 nicht gestartet (err=%s)", esp_err_to_name(rs485_err));
    } else {
        ESP_LOGI(TAG, "✓ RS485-Display-Kommunikation aktiv");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
