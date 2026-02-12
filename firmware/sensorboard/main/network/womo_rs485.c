#include "network/womo_rs485.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "sensor_config.h"

static const char *TAG = "womo_rs485";
static bool s_initialized = false;
static const uart_port_t s_uart_port = SENSOR_RS485_UART_PORT;

esp_err_t womo_rs485_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    uart_config_t uart_cfg = {
        .baud_rate = SENSOR_RS485_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    esp_err_t err = uart_driver_install(s_uart_port,
                                        SENSOR_RS485_BUFFER_SIZE,
                                        SENSOR_RS485_BUFFER_SIZE,
                                        0,
                                        NULL,
                                        0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(s_uart_port, &uart_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART params: %s", esp_err_to_name(err));
        uart_driver_delete(s_uart_port);
        return err;
    }

    gpio_set_pull_mode(SENSOR_RS485_DE_GPIO, GPIO_PULLDOWN_ONLY);
    gpio_set_direction(SENSOR_RS485_DE_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(SENSOR_RS485_DE_GPIO, 0);

    err = uart_set_pin(s_uart_port,
                       SENSOR_RS485_TX_GPIO,
                       SENSOR_RS485_RX_GPIO,
                       SENSOR_RS485_RTS_GPIO,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        uart_driver_delete(s_uart_port);
        return err;
    }

    // RS485 Half-Duplex mit automatischem RTS/DE
    err = uart_set_mode(s_uart_port, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set RS485 mode: %s", esp_err_to_name(err));
        uart_driver_delete(s_uart_port);
        return err;
    }

    uint8_t rx_timeout = (uint8_t)(SENSOR_RS485_READ_TIMEOUT_MS / 10);
    if (rx_timeout == 0) {
        rx_timeout = 1;
    }
    err = uart_set_rx_timeout(s_uart_port, rx_timeout);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set RX timeout: %s", esp_err_to_name(err));
    }

    s_initialized = true;
    ESP_LOGI(TAG, "RS485 UART initialized (UART%d, %d baud)", (int)s_uart_port, SENSOR_RS485_BAUDRATE);
    return ESP_OK;
}

void womo_rs485_deinit(void)
{
    if (!s_initialized) {
        return;
    }
    uart_driver_delete(s_uart_port);
    s_initialized = false;
}

esp_err_t womo_rs485_write(const uint8_t *data, size_t length, TickType_t ticks_to_wait)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!data || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int to_write = (int)length;
    int written = uart_write_bytes(s_uart_port, (const char *)data, to_write);
    if (written != to_write) {
        return ESP_FAIL;
    }

    return uart_wait_tx_done(s_uart_port, ticks_to_wait);
}

int womo_rs485_read(uint8_t *buffer, size_t max_length, TickType_t ticks_to_wait)
{
    if (!s_initialized || !buffer || max_length == 0) {
        return -1;
    }

    int bytes_read = uart_read_bytes(s_uart_port, buffer, max_length, ticks_to_wait);
    return bytes_read;
}

void womo_rs485_flush_input(void)
{
    if (!s_initialized) {
        return;
    }
    uart_flush_input(s_uart_port);
}
