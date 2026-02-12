#include "sensors/max17048_sensor.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "i2c_bus.h"
#include "max17048.h"
#include "modem_config.h"

static const char *TAG = "max17048_app";

static i2c_bus_handle_t s_bus = NULL;
static max17048_handle_t s_fg = NULL;
static TaskHandle_t s_log_task = NULL;
static SemaphoreHandle_t s_fg_mutex = NULL;
static max17048_snapshot_t s_last = {0};

static esp_err_t max17048_read(float *voltage, float *percent)
{
    ESP_RETURN_ON_FALSE(s_fg != NULL, ESP_ERR_INVALID_STATE, TAG, "MAX17048 nicht initialisiert");
    ESP_RETURN_ON_FALSE(voltage != NULL && percent != NULL, ESP_ERR_INVALID_ARG, TAG, "Null-Argument");

    if (xSemaphoreTake(s_fg_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ev = max17048_get_cell_voltage(s_fg, voltage);
    esp_err_t ep = max17048_get_cell_percent(s_fg, percent);

    xSemaphoreGive(s_fg_mutex);

    if (ev != ESP_OK) {
        return ev;
    }
    return ep;
}

static void max17048_log_task(void *arg)
{
    const TickType_t delay_ticks = pdMS_TO_TICKS(5000); // Debug: 5s
    while (1) {
        float voltage = 0.0f;
        float percent = 0.0f;
        esp_err_t err = max17048_read(&voltage, &percent);
        if (err == ESP_OK) {
            s_last.voltage = voltage;
            s_last.percent = percent;
            s_last.timestamp_us = esp_timer_get_time();
            s_last.valid = true;
            ESP_LOGI(TAG, "VBAT=%.3f V SOC=%.1f%%", voltage, percent);
        } else {
            s_last.valid = false;
            ESP_LOGW(TAG, "Messung fehlgeschlagen (%s)", esp_err_to_name(err));
        }
        vTaskDelay(delay_ticks);
    }
}

static esp_err_t setup_fg(int sda, int scl, int speed_hz)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = MODEM_I2C_PULLUP_ENABLE,
        .scl_pullup_en = MODEM_I2C_PULLUP_ENABLE,
        .master.clk_speed = speed_hz,
        .clk_flags = 0,
    };

    s_bus = i2c_bus_create(MODEM_I2C_PORT, &conf);
    ESP_RETURN_ON_FALSE(s_bus != NULL, ESP_FAIL, TAG, "I2C-Bus konnte nicht erstellt werden");

    s_fg = max17048_create(s_bus, MAX17048_I2C_ADDR_DEFAULT);
    ESP_RETURN_ON_FALSE(s_fg != NULL, ESP_FAIL, TAG, "MAX17048 Handle konnte nicht erstellt werden");

    // Optionaler Scan zur Diagnose
    uint8_t found[8] = {0};
    uint8_t count = i2c_bus_scan(s_bus, found, sizeof(found));
    bool seen = false;
    for (int i = 0; i < count; i++) {
        if (found[i] == MAX17048_I2C_ADDR_DEFAULT) {
            seen = true;
        }
    }
    if (!seen) {
        ESP_LOGW(TAG, "Scan: %u Device(s), 0x36 nicht gesehen", count);
    } else {
        ESP_LOGI(TAG, "Scan: MAX17048 (0x36) gefunden, total %u Device(s)", count);
    }

    return seen ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t max17048_app_start(void)
{
    if (s_log_task) {
        return ESP_OK;
    }

    if (s_fg_mutex == NULL) {
        s_fg_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_fg_mutex != NULL, ESP_ERR_NO_MEM, TAG, "Mutex konnte nicht erstellt werden");
    }

    // Nur Pins lt. modem_config verwenden
    esp_err_t err = setup_fg(MODEM_I2C_SDA_GPIO, MODEM_I2C_SCL_GPIO, MODEM_I2C_SPEED_HZ);

    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "MAX17048 nicht gesehen auf SDA=%d/SCL=%d", MODEM_I2C_SDA_GPIO, MODEM_I2C_SCL_GPIO);
        if (s_fg) {
            max17048_delete(&s_fg);
        }
        if (s_bus) {
            i2c_bus_delete(&s_bus);
        }
        return ESP_FAIL;
    } else if (err != ESP_OK) {
        return err;
    }

    BaseType_t r = xTaskCreatePinnedToCore(max17048_log_task, "max17048_log", 4096, NULL, 4, &s_log_task, 0);
    if (r != pdPASS) {
        ESP_LOGW(TAG, "MAX17048-Log-Task konnte nicht gestartet werden");
        s_log_task = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t max17048_app_get_snapshot(max17048_snapshot_t *out)
{
    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG, "out ist NULL");
    ESP_RETURN_ON_FALSE(s_fg != NULL, ESP_ERR_INVALID_STATE, TAG, "MAX17048 nicht initialisiert");

    float voltage = 0.0f;
    float percent = 0.0f;
    esp_err_t err = max17048_read(&voltage, &percent);
    if (err == ESP_OK) {
        s_last.voltage = voltage;
        s_last.percent = percent;
        s_last.timestamp_us = esp_timer_get_time();
        s_last.valid = true;
    }

    *out = s_last;
    return err;
}
