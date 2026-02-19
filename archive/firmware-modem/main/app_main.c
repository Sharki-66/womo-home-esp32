/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "app_wifi.h"
#include "network/net_manager.h"
#include "modem_http_config.h"
#include "time/gnss_time.h"
#include "network_test.h"
#include "modem_config.h"
#include "iot_usbh_modem.h"
#include "at_3gpp_ts_27_007.h"
#include "sensors/analog_sensor.h"
#include "sensors/max17048_sensor.h"
#include "sensors/bno055_sensor.h"
#include "sensors/bme680_sensor.h"
#include "time/rtc_pcf8523.h"
#include "sensors/hx711_sensor.h"
#include "time/time_sync.h"
#include "time/lte_time.h"
#include "network/rs485_modem.h"

static const char *TAG = "PPP_4G_main";

static esp_err_t power_status_provider(modem_power_status_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    max17048_snapshot_t snap = {0};
    esp_err_t err = max17048_app_get_snapshot(&snap);
    out->valid = snap.valid;
    out->voltage = snap.voltage;
    out->percent = snap.percent;
    out->timestamp_us = snap.timestamp_us;
    return err;
}

#ifdef CONFIG_DUMP_SYSTEM_STATUS
#define TASK_MAX_COUNT 32
typedef struct {
    uint32_t ulRunTimeCounter;
    uint32_t xTaskNumber;
} taskData_t;

static taskData_t previousSnapshot[TASK_MAX_COUNT];
static int taskTopIndex = 0;
static uint32_t previousTotalRunTime = 0;

static taskData_t *getPreviousTaskData(uint32_t xTaskNumber)
{
    // Try to find the task in the list of tasks
    for (int i = 0; i < taskTopIndex; i++) {
        if (previousSnapshot[i].xTaskNumber == xTaskNumber) {
            return &previousSnapshot[i];
        }
    }
    // Allocate a new entry
    ESP_ERROR_CHECK(!(taskTopIndex < TASK_MAX_COUNT));
    taskData_t *result = &previousSnapshot[taskTopIndex];
    result->xTaskNumber = xTaskNumber;
    taskTopIndex++;
    return result;
}

static void _system_dump()
{
    uint32_t totalRunTime;

    TaskStatus_t taskStats[TASK_MAX_COUNT];
    uint32_t taskCount = uxTaskGetSystemState(taskStats, TASK_MAX_COUNT, &totalRunTime);
    ESP_ERROR_CHECK(!(taskTopIndex < TASK_MAX_COUNT));
    uint32_t totalDelta = totalRunTime - previousTotalRunTime;
    float f = 100.0 / totalDelta;
    // Dumps the the CPU load and stack usage for all tasks
    // CPU usage is since last dump in % compared to total time spent in tasks. Note that time spent in interrupts will be included in measured time.
    // Stack usage is displayed as nr of unused bytes at peak stack usage.

    ESP_LOGI(TAG, "Task dump\n");
    ESP_LOGI(TAG, "Load\tStack left\tName\tPRI\n");

    for (uint32_t i = 0; i < taskCount; i++) {
        TaskStatus_t *stats = &taskStats[i];
        taskData_t *previousTaskData = getPreviousTaskData(stats->xTaskNumber);

        uint32_t taskRunTime = stats->ulRunTimeCounter;
        float load = f * (taskRunTime - previousTaskData->ulRunTimeCounter);
        ESP_LOGI(TAG, "%.2f \t%" PRIu32 "\t%s %" PRIu32 "\t\n", load, stats->usStackHighWaterMark, stats->pcTaskName, (uint32_t)stats->uxBasePriority);

        previousTaskData->ulRunTimeCounter = taskRunTime;
    }
    ESP_LOGI(TAG, "Free heap=%d Free mini=%d bigst=%d, internal=%d bigst=%d",
             heap_caps_get_free_size(MALLOC_CAP_DEFAULT), heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT),
             heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT),
             heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    previousTotalRunTime = totalRunTime;
}
#endif

void app_main(void)
{
#ifdef CONFIG_ESP32_S3_USB_OTG
    // USB mode select host
    const gpio_config_t io_config = {
        .pin_bit_mask = BIT64(GPIO_NUM_18),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_config));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_18, 1));

    // Set host usb dev power mode
    // NOTE: GPIO12 und GPIO17 werden für RS485 benötigt, nur GPIO13 für USB Power
    const gpio_config_t power_io_config = {
        .pin_bit_mask = BIT64(GPIO_NUM_13),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&power_io_config));

    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_13, 0)); // Turn power off
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_13, 1)); // Turn on usb dev power mode
#endif
    /* Initialize NVS for Wi-Fi storage */

    // Mehr WLAN/Web-Server Logs während der Stabilitätsanalyse
    esp_log_level_set("wifi", ESP_LOG_INFO);
    esp_log_level_set("httpd", ESP_LOG_INFO);
    esp_log_level_set("httpd_txrx", ESP_LOG_INFO);
    esp_log_level_set("esp_netif", ESP_LOG_INFO);
    
    // RS485 Debug-Logging aktivieren
    esp_log_level_set("rs485_modem", ESP_LOG_DEBUG);
    esp_log_level_set("lte_time", ESP_LOG_DEBUG);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition was truncated and needs to be erased
         * Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Zeit-Synchronisation initialisieren (liest RTC beim Boot)
    esp_err_t time_err = time_sync_init();
    if (time_err != ESP_OK) {
        ESP_LOGW(TAG, "Zeit-Synchronisation nicht verfügbar: %s", esp_err_to_name(time_err));
    }

    ESP_ERROR_CHECK(net_manager_init());

    modem_wifi_config_t wifi_cfg = MODEM_WIFI_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(net_manager_start(&wifi_cfg));

    ESP_ERROR_CHECK(modem_http_set_imu_fast_request_cb(bno055_app_request_fast));

    // Battery-Fuel-Gauge loggen (R86 -> ADC unbrauchbar); nicht hart fehlschlagen
    esp_err_t fg_err = max17048_app_start();
    if (fg_err == ESP_OK) {
        ESP_ERROR_CHECK(modem_http_set_power_status_provider(power_status_provider));
    } else {
        ESP_LOGW(TAG, "MAX17048 nicht aktiv (err=%s)", esp_err_to_name(fg_err));
    }

    // BME680 auf externem I2C (0x76/0x77) starten
    esp_err_t bme_err = bme680_app_start();
    if (bme_err != ESP_OK) {
        ESP_LOGW(TAG, "BME680 nicht aktiv (err=%s)", esp_err_to_name(bme_err));
    }

    // PCF8523 RTC wird durch time_sync verwaltet
    // (time_sync_init hat bereits RTC gestartet und System-Zeit initialisiert)

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

    // GPIO1 bleibt frei (R86 NC); kein ADC-Logging notwendig

    // RS485-Kommunikation zum Display initialisieren
    esp_err_t rs485_err = rs485_modem_init();
    if (rs485_err != ESP_OK) {
        ESP_LOGW(TAG, "RS485 nicht gestartet (err=%s)", esp_err_to_name(rs485_err));
    } else {
        ESP_LOGI(TAG, "✓ RS485-Display-Kommunikation aktiv");
    }

    EventBits_t got_ip = net_manager_wait_event(NET_EVENT_GOT_IP_BIT, false, false, pdMS_TO_TICKS(5000));
    if ((got_ip & NET_EVENT_GOT_IP_BIT) == 0) {
        ESP_LOGW(TAG, "Kein GOT_IP (AP/Offline), starte trotzdem LTE/GNSS");
    }

    // LTE-Zeit-Synchronisation starten (Fallback wenn GPS nicht verfügbar)
    esp_err_t lte_time_err = lte_time_task_start();
    if (lte_time_err != ESP_OK) {
        ESP_LOGW(TAG, "LTE-Zeit-Task nicht gestartet: %s", esp_err_to_name(lte_time_err));
    } else {
        ESP_LOGI(TAG, "LTE-Zeit-Task gestartet");
    }

    bool gnss_started = false;
    while (1) {
        EventBits_t bits = net_manager_wait_event(NET_EVENT_GOT_IP_BIT, true, false, pdMS_TO_TICKS(10000));
        if (bits & NET_EVENT_GOT_IP_BIT) {
            if (!gnss_started) {
                gnss_task_start();
                gnss_started = true;
            }

            vTaskDelay(pdMS_TO_TICKS(1000)); // Wait a bit for DNS to be ready
            test_query_public_ip(); // Query public IP via HTTP
#if CONFIG_EXAMPLE_USB_PPP_DOWNLOAD_SPEED_TEST
            // Regionaler Speedtest-Endpoint (kleine Datei, gut erreichbar in EU)
            test_download_speed("http://speedtest.tele2.net/10MB.zip");
#endif
            esp_modem_at_csq_t csq = {0};
            esp_err_t csq_err = at_cmd_get_signal_quality(usbh_modem_get_atparser(), &csq);
            if (csq_err != ESP_OK) {
                // kurze Retry nach 2s, falls AT-Parser noch nicht bereit ist
                vTaskDelay(pdMS_TO_TICKS(2000));
                csq_err = at_cmd_get_signal_quality(usbh_modem_get_atparser(), &csq);
            }
            if (csq_err == ESP_OK) {
                if (csq.rssi != 99) {
                    int csq_dbm = -113 + 2 * csq.rssi;
                    ESP_LOGI(TAG, "Signal: CSQ=%d (%d dBm), BER=%d", csq.rssi, csq_dbm, csq.ber);
                } else {
                    ESP_LOGW(TAG, "Signal: CSQ=99 (unbekannt), BER=%d", csq.ber);
                }
            } else {
                ESP_LOGW(TAG, "Signalabfrage (AT+CSQ) fehlgeschlagen");
            }
            char str[64] = {0};
            at_cmd_get_manufacturer_id(usbh_modem_get_atparser(), str, sizeof(str));
            ESP_LOGI(TAG, "Manufacturer ID: %s", str);
        }

#ifdef CONFIG_DUMP_SYSTEM_STATUS
        _system_dump();
#endif
    }
    usbh_modem_uninstall();
    usbh_cdc_driver_uninstall();
}
