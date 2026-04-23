/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

#include "hal/sensor_i2c_bus.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include <i2c_bus.h>
#include "sensor_config.h"

static const char *TAG = "i2c_bus";

static i2c_bus_handle_t s_bus = NULL;
static i2c_master_bus_handle_t s_internal = NULL;
static SemaphoreHandle_t s_bus_mutex = NULL;

/**
 * @brief I2C-Bus-Recovery: 9 SCL-Pulse als GPIO senden, um einen hängenden
 *        Slave (SDA klemmt LOW nach unterbrochenem Transfer) zu befreien.
 *        Danach STOP-Condition erzeugen.
 */
static void i2c_bus_recover(void)
{
    ESP_LOGW(TAG, "I2C Bus-Recovery: 9 SCL-Pulse auf GPIO%d", SENSOR_I2C_EXT_SCL_GPIO);

    gpio_config_t scl_cfg = {
        .pin_bit_mask = BIT64(SENSOR_I2C_EXT_SCL_GPIO),
        .mode         = GPIO_MODE_OUTPUT_OD,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config_t sda_cfg = {
        .pin_bit_mask = BIT64(SENSOR_I2C_EXT_SDA_GPIO),
        .mode         = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&scl_cfg);
    gpio_config(&sda_cfg);

    gpio_set_level(SENSOR_I2C_EXT_SDA_GPIO, 1);
    gpio_set_level(SENSOR_I2C_EXT_SCL_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(1));

    for (int i = 0; i < 9; i++) {
        gpio_set_level(SENSOR_I2C_EXT_SCL_GPIO, 0);
        esp_rom_delay_us(5);
        gpio_set_level(SENSOR_I2C_EXT_SCL_GPIO, 1);
        esp_rom_delay_us(5);
        /* Sobald SDA wieder HIGH → Slave hat freigegeben */
        if (gpio_get_level(SENSOR_I2C_EXT_SDA_GPIO)) {
            ESP_LOGI(TAG, "SDA freigegeben nach %d Pulsen", i + 1);
            break;
        }
    }

    /* STOP-Condition: SDA LOW → SCL HIGH → SDA HIGH */
    gpio_set_level(SENSOR_I2C_EXT_SDA_GPIO, 0);
    esp_rom_delay_us(5);
    gpio_set_level(SENSOR_I2C_EXT_SCL_GPIO, 1);
    esp_rom_delay_us(5);
    gpio_set_level(SENSOR_I2C_EXT_SDA_GPIO, 1);
    esp_rom_delay_us(5);

    /* Pins wieder auf Input setzen – i2c_new_master_bus konfiguriert sie neu */
    gpio_reset_pin(SENSOR_I2C_EXT_SCL_GPIO);
    gpio_reset_pin(SENSOR_I2C_EXT_SDA_GPIO);
    vTaskDelay(pdMS_TO_TICKS(10));
}

esp_err_t i2c_bus_init(void)
{
    if (s_bus && s_internal) {
        return ESP_OK;
    }

    /* Bus-Recovery vor Init: befreit hängende Slaves (SDA klemmt LOW) */
    i2c_bus_recover();

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SENSOR_I2C_EXT_SDA_GPIO,
        .scl_io_num = SENSOR_I2C_EXT_SCL_GPIO,
        .sda_pullup_en = SENSOR_I2C_EXT_PULLUP_ENABLE,
        .scl_pullup_en = SENSOR_I2C_EXT_PULLUP_ENABLE,
        .master.clk_speed = SENSOR_I2C_EXT_SPEED_HZ,
        .clk_flags = 0,
    };

    s_bus = i2c_bus_create(SENSOR_I2C_EXT_PORT, &conf);
    ESP_RETURN_ON_FALSE(s_bus != NULL, ESP_FAIL, TAG, "i2c_bus_create failed");

    s_internal = i2c_bus_get_internal_bus_handle(s_bus);
    ESP_RETURN_ON_FALSE(s_internal != NULL, ESP_FAIL, TAG, "internal bus handle missing");

    if (s_bus_mutex == NULL) {
        s_bus_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_bus_mutex != NULL, ESP_ERR_NO_MEM, TAG, "mutex create failed");
    }

    ESP_LOGI(TAG, "I2C bereit (I2C%d, SDA=%d, SCL=%d)",
             SENSOR_I2C_EXT_PORT, SENSOR_I2C_EXT_SDA_GPIO, SENSOR_I2C_EXT_SCL_GPIO);
    return ESP_OK;
}

i2c_bus_handle_t i2c_bus_get(void)
{
    return s_bus;
}

i2c_master_bus_handle_t i2c_bus_get_internal(void)
{
    return s_internal;
}

void i2c_bus_lock(void)
{
    if (s_bus_mutex) {
        xSemaphoreTake(s_bus_mutex, portMAX_DELAY);
    }
}

void i2c_bus_unlock(void)
{
    if (s_bus_mutex) {
        xSemaphoreGive(s_bus_mutex);
    }
}
