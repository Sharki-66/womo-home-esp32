/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/queue.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "app_wifi.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Structure for recording the connected sta
 *
 */
struct modem_netif_sta_info {
    SLIST_ENTRY(modem_netif_sta_info) field;
    uint8_t mac[6];
    char name[32];
    esp_ip4_addr_t ip;
    int64_t start_time;
};

typedef SLIST_HEAD(modem_http_list_head, modem_netif_sta_info) modem_http_list_head_t;

typedef struct {
    bool valid;
    float voltage;
    float percent;
    int64_t timestamp_us;
} modem_power_status_t;

typedef esp_err_t (*modem_power_status_provider_t)(modem_power_status_t *out);
typedef void (*modem_imu_fast_request_cb_t)(uint32_t duration_ms, uint32_t interval_ms);

/**
 * @brief Deinit http server
 *
 * @return esp_err_t
 */
esp_err_t modem_http_deinit(httpd_handle_t server);

/**
 * @brief Initialize http server
 *
 * @param wifi_config wifi Configure Information
 * @return esp_err_t
 */
esp_err_t modem_http_init(modem_wifi_config_t *wifi_config);

/**
 * @brief Read the stored wifi configuration information from nvs and loaded into the structure wifi_config
 *
 * @param wifi_config wifi Configure Information
 * @return esp_err_t
 */
esp_err_t modem_http_get_nvs_wifi_config(modem_wifi_config_t *wifi_config);

/**
 * @brief Print information about the nodes in the chain table
 *
 * @param head Link table header
 * @return esp_err_t
 */
esp_err_t modem_http_print_nodes(modem_http_list_head_t *head);

/**
 * @brief Get managed list information
 *
 * @return struct modem_http_list_head_t
 */
modem_http_list_head_t *modem_http_get_stalist();

// Optionaler Callback, um Telemetriedaten (z. B. Akku) aus app_main anzubieten.
esp_err_t modem_http_set_power_status_provider(modem_power_status_provider_t cb);
esp_err_t modem_http_set_imu_fast_request_cb(modem_imu_fast_request_cb_t cb);

#ifdef __cplusplus
}
#endif
