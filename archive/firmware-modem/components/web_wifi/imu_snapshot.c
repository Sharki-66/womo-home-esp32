#include "web_wifi_imu.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_imu_mutex = NULL;
static web_wifi_imu_snapshot_t s_imu_snapshot = {0};

static void web_wifi_imu_ensure_mutex(void)
{
    if (s_imu_mutex == NULL) {
        s_imu_mutex = xSemaphoreCreateMutex();
    }
}

void web_wifi_imu_update(const web_wifi_imu_snapshot_t *sample)
{
    if (!sample) {
        return;
    }
    web_wifi_imu_ensure_mutex();
    if (s_imu_mutex == NULL) {
        s_imu_snapshot = *sample;
        return;
    }
    if (xSemaphoreTake(s_imu_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_imu_snapshot = *sample;
        xSemaphoreGive(s_imu_mutex);
    }
}

bool web_wifi_imu_get_snapshot(web_wifi_imu_snapshot_t *out)
{
    if (!out) {
        return false;
    }
    web_wifi_imu_ensure_mutex();
    if (s_imu_mutex == NULL) {
        *out = s_imu_snapshot;
        return out->valid;
    }
    if (xSemaphoreTake(s_imu_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }
    *out = s_imu_snapshot;
    xSemaphoreGive(s_imu_mutex);
    return out->valid;
}
