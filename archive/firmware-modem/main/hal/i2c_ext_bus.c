#include "hal/i2c_ext_bus.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <i2c_bus.h>
#include "modem_config.h"

static const char *TAG = "i2c_ext_bus";

static i2c_bus_handle_t s_bus = NULL;
static i2c_master_bus_handle_t s_internal = NULL;
static SemaphoreHandle_t s_bus_mutex = NULL;

esp_err_t i2c_ext_bus_init(void)
{
    if (s_bus && s_internal) {
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = MODEM_I2C_EXT_SDA_GPIO,
        .scl_io_num = MODEM_I2C_EXT_SCL_GPIO,
        .sda_pullup_en = MODEM_I2C_EXT_PULLUP_ENABLE,
        .scl_pullup_en = MODEM_I2C_EXT_PULLUP_ENABLE,
        .master.clk_speed = MODEM_I2C_EXT_SPEED_HZ,
        .clk_flags = 0,
    };

    s_bus = i2c_bus_create(MODEM_I2C_EXT_PORT, &conf);
    ESP_RETURN_ON_FALSE(s_bus != NULL, ESP_FAIL, TAG, "i2c_bus_create failed");

    s_internal = i2c_bus_get_internal_bus_handle(s_bus);
    ESP_RETURN_ON_FALSE(s_internal != NULL, ESP_FAIL, TAG, "internal bus handle missing");

    if (s_bus_mutex == NULL) {
        s_bus_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_bus_mutex != NULL, ESP_ERR_NO_MEM, TAG, "mutex create failed");
    }

    ESP_LOGI(TAG, "I2C ext bereit (I2C%d, SDA=%d, SCL=%d)",
             MODEM_I2C_EXT_PORT, MODEM_I2C_EXT_SDA_GPIO, MODEM_I2C_EXT_SCL_GPIO);
    return ESP_OK;
}

i2c_bus_handle_t i2c_ext_bus_get(void)
{
    return s_bus;
}

i2c_master_bus_handle_t i2c_ext_bus_get_internal(void)
{
    return s_internal;
}

void i2c_ext_bus_lock(void)
{
    if (s_bus_mutex) {
        xSemaphoreTake(s_bus_mutex, portMAX_DELAY);
    }
}

void i2c_ext_bus_unlock(void)
{
    if (s_bus_mutex) {
        xSemaphoreGive(s_bus_mutex);
    }
}
