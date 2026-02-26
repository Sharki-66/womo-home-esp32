/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sensor_config.h"
#include "sensors/analog_sensor.h"
#include "sensors/bno055_sensor.h"
#include "sensors/bme680_sensor.h"
#include "sensors/hx711_sensor.h"
#include "time/rtc_pcf8523.h"
#include "time/time_sync.h"
#include "network/rs485_modem.h"
#include "network/wifi/sensor_wifi.h"
#include "network/wifi/sensor_http.h"
#include "network/gasbee_ble_client.h"
#include "led_strip.h"

static const char *TAG = "sensor_main";

/** Onboard WS2812 RGB-LED (GPIO48) ausschalten. */
static void rgb_led_off(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = SENSOR_RGB_LED_GPIO,
        .max_leds         = 1,
        .led_model        = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src        = RMT_CLK_SRC_DEFAULT,
        .resolution_hz  = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    led_strip_handle_t led = NULL;
    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &led);
    if (err == ESP_OK) {
        led_strip_clear(led);
        led_strip_refresh(led);
        vTaskDelay(pdMS_TO_TICKS(50));
        led_strip_del(led);
        ESP_LOGI(TAG, "RGB-LED (GPIO%d) ausgeschaltet", SENSOR_RGB_LED_GPIO);
    } else {
        ESP_LOGW(TAG, "RGB-LED init fehlgeschlagen: %s", esp_err_to_name(err));
    }
}

void app_main(void)
{
    rgb_led_off();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // WiFi-Verbindung zum RUTX11 herstellen (bleibt dauerhaft aktiv)
    esp_err_t wifi_err = sensor_wifi_init();
    if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi nicht gestartet: %s", esp_err_to_name(wifi_err));
    }

    // HTTP-Server für Parkhilfe (womo-sensor.local)
    esp_err_t http_err = sensor_http_start();
    if (http_err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP-Server nicht gestartet: %s", esp_err_to_name(http_err));
    }

    // Zeit-Synchronisation initialisieren (liest RTC beim Boot)
    esp_err_t time_err = time_sync_init();
    if (time_err != ESP_OK) {
        ESP_LOGW(TAG, "Zeit-Synchronisation nicht verfügbar: %s", esp_err_to_name(time_err));
    }

    // NTP-Client starten: synchronisiert System-Zeit und setzt RTC nach WiFi-Connect
    esp_err_t ntp_err = time_sync_start_ntp(SENSOR_NTP_SERVER);
    if (ntp_err != ESP_OK) {
        ESP_LOGW(TAG, "NTP-Client nicht gestartet: %s", esp_err_to_name(ntp_err));
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

    // GasBee BLE Client (ESP32-C3 Mini Gaswaage via BLE)
    esp_err_t gasbee_err = gasbee_ble_client_start();
    if (gasbee_err != ESP_OK) {
        ESP_LOGW(TAG, "GasBee BLE Client nicht gestartet (err=%s)", esp_err_to_name(gasbee_err));
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
