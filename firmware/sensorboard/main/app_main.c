/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms (modifiziert)
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
#include "esp_wifi.h"
#include "sensors/analog_sensor.h"
#include "sensors/bno055_sensor.h"
#include "sensors/bme680_sensor.h"
#include "sensors/hx711_sensor.h"
#include "sensors/ina226_sensor.h"
#include "time/rtc_pcf8523.h"
#include "time/time_sync.h"
#include "network/espnow_modem.h"
#include "network/wifi/sensor_wifi.h"
#include "network/wifi/sensor_http.h"
#include "network/gasbee_ble_client.h"
#include "hal/deep_sleep.h"
#include "led_strip.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

static const char *TAG = "sensor_main";

/** Log-Level je Modul gemäß sensor_config.h setzen. */
static void sensor_log_init(void)
{
    esp_log_level_set("sensor_main",  LOG_LEVEL_SENSOR_MAIN);
    esp_log_level_set("deep_sleep",   LOG_LEVEL_DEEP_SLEEP);
    esp_log_level_set("womo_rs485_legacy",   LOG_LEVEL_SENSORBOARD);
    esp_log_level_set("espnow_modem",  LOG_LEVEL_SENSORBOARD);
    esp_log_level_set("analog",       LOG_LEVEL_ANALOG);
    esp_log_level_set("bno055_app",   LOG_LEVEL_BNO055);
    esp_log_level_set("BNO055",       LOG_LEVEL_BNO055);
    esp_log_level_set("bme680_app",   LOG_LEVEL_BME680);
    esp_log_level_set("bme680",       LOG_LEVEL_BME680);
    esp_log_level_set("hx711_app",    LOG_LEVEL_HX711);
    esp_log_level_set("ina226",       LOG_LEVEL_INA226);
    esp_log_level_set("gasbee_ble",   LOG_LEVEL_GASBEE_BLE);
    esp_log_level_set("i2c_bus",      LOG_LEVEL_I2C_BUS);
    esp_log_level_set("pcf8523_app",  LOG_LEVEL_PCF8523);
    esp_log_level_set("time_sync",    LOG_LEVEL_TIME_SYNC);
    esp_log_level_set("wifi",         LOG_LEVEL_WIFI);
    esp_log_level_set("esp_netif_lwip", LOG_LEVEL_WIFI);
    esp_log_level_set("esp_netif_handlers", LOG_LEVEL_WIFI);
}

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

/**
 * Display 5V einschalten + 12V Bordnetz EIN-Puls.
 * Wird direkt beim Boot/Wakeup aufgerufen – VOR allem anderen.
 */
static void pwr_12v_on_early(void)
{
    /* Display 5V einschalten: GPIO7 HIGH → Q5 (2N7002) leitet → Q4 (AO3401A) leitet */
    gpio_config_t cfg = {
        .pin_bit_mask   = BIT64(SENSOR_DISPLAY_PWR_GPIO),
        .mode           = GPIO_MODE_OUTPUT,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(SENSOR_DISPLAY_PWR_GPIO, 1);  /* HIGH → Q5 leitet → Display EIN */
    ESP_LOGI(TAG, "Display 5V EIN (GPIO%d HIGH, Q5→Q4 leitet)", SENSOR_DISPLAY_PWR_GPIO);

    /* LBE 12V-Relais EIN: bistabiles Relais benötigt Puls 1-3 s auf GPIO11.
     * GPIO11 (EIN) und GPIO12 (AUS) müssen im Ruhezustand LOW sein! */
    gpio_config_t cfg2 = {
        .pin_bit_mask   = BIT64(SENSOR_PWR_12V_ON_GPIO) | BIT64(SENSOR_PWR_12V_OFF_GPIO),
        .mode           = GPIO_MODE_OUTPUT,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg2);
    gpio_set_level(SENSOR_PWR_12V_ON_GPIO,  0);   /* sicher LOW vor Puls */
    gpio_set_level(SENSOR_PWR_12V_OFF_GPIO, 0);
    gpio_set_level(SENSOR_PWR_12V_ON_GPIO,  1);   /* EIN-Puls starten */
    vTaskDelay(pdMS_TO_TICKS(100));              /* 100 ms Puls (bistabiles Relais) */
    gpio_set_level(SENSOR_PWR_12V_ON_GPIO,  0);   /* Puls beenden → LOW */
    ESP_LOGI(TAG, "LBE 12V EIN (GPIO%d 100ms-Puls abgeschlossen)", SENSOR_PWR_12V_ON_GPIO);
}

void app_main(void)
{
    sensor_log_init();

    /* Display 5V sperren – GPIO7 OUTPUT LOW: Q5 (2N7002) sperrt → Q4 (AO3401A) sperrt.
     * Nach Deep Sleep: RTC-Hold aufheben, dann normalen GPIO-Modus zurücksetzen. */
    rtc_gpio_hold_dis(SENSOR_DISPLAY_PWR_GPIO);
    rtc_gpio_deinit(SENSOR_DISPLAY_PWR_GPIO);
    gpio_config_t disp_cfg = {
        .pin_bit_mask = BIT64(SENSOR_DISPLAY_PWR_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&disp_cfg);
    gpio_set_level(SENSOR_DISPLAY_PWR_GPIO, 0);  /* LOW → Q5 aus → Display AUS */
    /* GPIO13 (N-Kanal MOSFET Radio): OUTPUT LOW = sicher aus (kein passiver Pulldown in Hardware) */
    gpio_config_t mm_cfg = {
        .pin_bit_mask = BIT64(SENSOR_MULTIMEDIA_PWR_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&mm_cfg);
    gpio_set_level(SENSOR_MULTIMEDIA_PWR_GPIO, 0); /* N-MOSFET: LOW = AUS (Radio aus beim Boot) */

    // ── Touch initialisieren (immer zuerst) ──────────────────────────
    deep_sleep_init();

    // ── Wakeup-Behandlung ────────────────────────────────────────────
    bool is_touch_wakeup = deep_sleep_wakeup_by_touch();
    if (is_touch_wakeup) {
        ESP_LOGI(TAG, "Touch-Wakeup erkannt → Hold aufheben");
        gpio_hold_dis(SENSOR_ESPNOW_DE_LEGACY_GPIO);
    } else {
        deep_sleep_enter_cold();
        /* Kehrt nicht zurück */
    }
    /* Ab hier: nur Touch-Wakeup-Pfad */
    pwr_12v_on_early();

    rgb_led_off();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Zeit-Synchronisation initialisieren (RTC + I2C-Bus – muss VOR WiFi/HTTP passieren)
    esp_err_t time_err = time_sync_init();
    if (time_err != ESP_OK) {
        ESP_LOGW(TAG, "Zeit-Synchronisation nicht verfügbar: %s", esp_err_to_name(time_err));
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

    // INA226 (Strom-/Leistungsmessung Hauptleitung) starten
    esp_err_t ina_err = ina226_app_start();
    if (ina_err != ESP_OK) {
        ESP_LOGW(TAG, "INA226 nicht aktiv (err=%s)", esp_err_to_name(ina_err));
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
    esp_err_t rs485_err = espnow_modem_init();
    if (rs485_err != ESP_OK) {
        ESP_LOGW(TAG, "ESP-NOW nicht gestartet (err=%s)", esp_err_to_name(rs485_err));
    } else {
        ESP_LOGI(TAG, "✓ ESP-NOW-Kommunikation aktiv");
    }

    // Touch-Debug-Monitor (nur Log-Ausgabe, keine Aktion)
    deep_sleep_start_monitor();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
