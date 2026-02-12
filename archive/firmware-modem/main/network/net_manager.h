#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "app_wifi.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Event-Bit für erfolgreiche PPP-IP
#define NET_EVENT_GOT_IP_BIT (BIT0)

esp_err_t net_manager_init(void);
esp_err_t net_manager_start(modem_wifi_config_t *wifi_cfg);
EventBits_t net_manager_wait_event(EventBits_t bits, bool clear_on_exit, bool wait_for_all, TickType_t ticks_to_wait);
EventGroupHandle_t net_manager_events(void);

#ifdef __cplusplus
}
#endif
