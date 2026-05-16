/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#include "sensors/ina226_sensor.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ina226.h"
#include "hal/sensor_i2c_bus.h"
#include "sensor_config.h"

static const char *TAG = "ina226";

static ina226_handle_t  s_dev  = NULL;
static TaskHandle_t     s_task = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static ina226_snapshot_t s_snap  = {0};

static void ina226_task(void *arg)
{
    while (1) {
        float v_bus = 0.0f, v_shunt = 0.0f, i_a = 0.0f, p_w = 0.0f;

        bool ok = (ina226_get_bus_voltage(s_dev,   &v_bus)   == ESP_OK)
               && (ina226_get_shunt_voltage(s_dev, &v_shunt) == ESP_OK)
               && (ina226_get_current(s_dev,        &i_a)    == ESP_OK)
               && (ina226_get_power(s_dev,           &p_w)   == ESP_OK);

        ina226_snapshot_t snap = {
            .valid        = ok,
            .v_bus_v      = v_bus,
            .i_a          = i_a,
            .p_w          = p_w,
            .v_shunt_mv   = v_shunt * 1000.0f,   // Library liefert Volt
            .timestamp_us = esp_timer_get_time(),
        };

        if (ok) {
            ESP_LOGI(TAG, "V=%.3f V  I=%.3f A  P=%.1f W  Vsh=%.3f mV",
                     (double)v_bus, (double)i_a, (double)p_w, (double)(v_shunt * 1000.0f));
        } else {
            ESP_LOGW(TAG, "Lesefehler – Snapshot ungültig");
        }

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_snap = snap;
        xSemaphoreGive(s_mutex);

        vTaskDelay(pdMS_TO_TICKS(SENSOR_INA226_POLL_INTERVAL_MS));
    }
}

esp_err_t ina226_app_start(void)
{
    if (s_task) return ESP_OK;

    esp_err_t err = i2c_bus_init();
    if (err != ESP_OK) return err;

    i2c_master_bus_handle_t bus = i2c_bus_get_internal();
    if (!bus) return ESP_FAIL;

    ina226_config_t cfg = I2C_INA226_CONFIG_DEFAULT;
    cfg.i2c_address             = SENSOR_INA226_I2C_ADDR;
    cfg.i2c_clock_speed         = SENSOR_INA226_I2C_CLK_HZ;
    cfg.averaging_mode          = INA226_AVG_MODE_16;
    cfg.shunt_voltage_conv_time = INA226_VOLT_CONV_TIME_1_1MS;
    cfg.bus_voltage_conv_time   = INA226_VOLT_CONV_TIME_1_1MS;
    cfg.operating_mode          = INA226_OP_MODE_CONT_SHUNT_BUS;
    cfg.shunt_resistance        = SENSOR_INA226_SHUNT_MOHM / 1000.0f;
    cfg.max_current             = (float)SENSOR_INA226_MAX_CURRENT_A;

    err = ina226_init(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "INA226 init fehlgeschlagen: %s", esp_err_to_name(err));
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "INA226 0x%02X bereit (Shunt=%.1f mΩ, I_max=%d A, lib %s)",
             SENSOR_INA226_I2C_ADDR,
             (double)(SENSOR_INA226_SHUNT_MOHM),
             SENSOR_INA226_MAX_CURRENT_A,
             ina226_get_fw_version());

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ina226_delete(s_dev);
        return ESP_ERR_NO_MEM;
    }

    BaseType_t r = xTaskCreatePinnedToCore(ina226_task, "ina226_task", 3072, NULL, 3, &s_task, 0);
    if (r != pdPASS) {
        s_task = NULL;
        ina226_delete(s_dev);
        vSemaphoreDelete(s_mutex);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t ina226_app_get_snapshot(ina226_snapshot_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_mutex) {
        memset(out, 0, sizeof(*out));
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_snap;
    xSemaphoreGive(s_mutex);
    return out->valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}
