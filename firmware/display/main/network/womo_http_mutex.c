/*
 * SPDX-FileCopyrightText: 2025-2026 Hajo Harms
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file womo_http_mutex.c
 * @brief Globaler Mutex für HTTPS-Requests (TLS-Session-Serialisierung).
 */
#include "womo_http_mutex.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "http_mutex";
static SemaphoreHandle_t s_http_mutex = NULL;

esp_err_t womo_http_mutex_init(void)
{
    if (s_http_mutex) return ESP_OK;           /* bereits initialisiert */
    s_http_mutex = xSemaphoreCreateMutex();
    if (!s_http_mutex) {
        ESP_LOGE(TAG, "Mutex-Erstellung fehlgeschlagen");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "HTTPS-Mutex initialisiert");
    return ESP_OK;
}

esp_err_t womo_http_mutex_acquire(void)
{
    if (!s_http_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_http_mutex, pdMS_TO_TICKS(30000)) != pdTRUE) {
        ESP_LOGW(TAG, "HTTPS-Mutex Timeout (30 s)");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void womo_http_mutex_release(void)
{
    if (s_http_mutex) {
        xSemaphoreGive(s_http_mutex);
    }
}
