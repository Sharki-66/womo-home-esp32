#include "network/net_manager.h"

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "iot_eth.h"
#include "iot_eth_netif_glue.h"
#include "iot_usbh_modem.h"
#include "modem_http_config.h"
#include "network_test.h"
#include "ping/ping_sock.h"
#include "esp_check.h"

static const char *TAG = "net_manager";
static EventGroupHandle_t s_event_group = NULL;

static const usb_modem_id_t usb_modem_id_list[] = {
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x1782, 0x4d11}, 2, -1, "China Mobile, ML302/Fibocom, MC610-EU"},
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x1E0E, 0x9011}, 5, 4, "SIMCOM, A7600C1/SIMCOM, A7670E"},
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x1E0E, 0x9205}, 2, -1, "SIMCOM, SIM7080G"},
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x2CB7, 0x0D01}, 2, 6, "Fibocom, LE270-CN"},
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x2C7C, 0x6001}, 4, -1, "Quectel, EC600N-CN"},
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x2C7C, 0x0125}, 2, -1, "Quectel, EC20"},
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x19D1, 0x1003}, 2, -1, "YUGE, YM310 X09"},
    {.match_id = {USB_DEVICE_ID_MATCH_VID_PID, 0x19D1, 0x0001}, 2, -1, "Luat, Air780E"},
    {.match_id = {0}},
};

static void iot_event_handle(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == IOT_ETH_EVENT) {
        switch (event_id) {
        case IOT_ETH_EVENT_CONNECTED:
            ESP_LOGI(TAG, "IOT_ETH_EVENT_CONNECTED");
            break;
        case IOT_ETH_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "IOT_ETH_EVENT_DISCONNECTED");
            xEventGroupClearBits(s_event_group, NET_EVENT_GOT_IP_BIT);
            stop_ping_timer();
            break;
        case IOT_ETH_EVENT_START:
            ESP_LOGI(TAG, "IOT_ETH_EVENT_START");
            break;
        case IOT_ETH_EVENT_STOP:
            ESP_LOGI(TAG, "IOT_ETH_EVENT_STOP");
            break;
        default:
            ESP_LOGI(TAG, "IOT_ETH_EVENT_UNKNOWN");
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_PPP_GOT_IP) {
            ESP_LOGI(TAG, "GOT_IP");
            xEventGroupSetBits(s_event_group, NET_EVENT_GOT_IP_BIT);
            start_ping_timer();
        } else if (event_id == IP_EVENT_PPP_LOST_IP) {
            ESP_LOGW(TAG, "LOST_IP");
            xEventGroupClearBits(s_event_group, NET_EVENT_GOT_IP_BIT);
            stop_ping_timer();
        }
    }
}

esp_err_t net_manager_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_log_level_set("httpd", ESP_LOG_WARN);
    esp_log_level_set("httpd_uri", ESP_LOG_WARN);

    s_event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_event_group != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create event group");

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_GOT_IP, iot_event_handle, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_LOST_IP, iot_event_handle, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IOT_ETH_EVENT, ESP_EVENT_ANY_ID, iot_event_handle, NULL));
    return ESP_OK;
}

esp_err_t net_manager_start(modem_wifi_config_t *wifi_cfg)
{
    ESP_RETURN_ON_FALSE(wifi_cfg != NULL, ESP_ERR_INVALID_ARG, TAG, "wifi_cfg is NULL");

    usbh_cdc_driver_config_t cdc_cfg = {
        .task_stack_size = 1024 * 4,
        .task_priority = configMAX_PRIORITIES - 1,
        .task_coreid = 0,
        .skip_init_usb_host_driver = false,
    };
    ESP_ERROR_CHECK(usbh_cdc_driver_install(&cdc_cfg));

    usbh_modem_config_t modem_cfg = {
        .modem_id_list = usb_modem_id_list,
        .at_tx_buffer_size = 256,
        .at_rx_buffer_size = 256,
    };
    ESP_ERROR_CHECK(usbh_modem_install(&modem_cfg));
    ESP_LOGI(TAG, "modem board installed");

#ifdef CONFIG_EXAMPLE_ENABLE_WEB_ROUTER
    modem_http_get_nvs_wifi_config(wifi_cfg);
    modem_http_init(wifi_cfg);
#endif
    // Hotspot aus: kein SoftAP/STA hochfahren
    wifi_cfg->mode = WIFI_MODE_NULL;
    wifi_cfg->sta_ssid[0] = '\0';
    wifi_cfg->sta_password[0] = '\0';
    // Nur starten, falls WLAN gewünscht
    if (wifi_cfg->mode != WIFI_MODE_NULL) {
        app_wifi_main(wifi_cfg);
    }
    esp_netif_set_default_netif(usbh_modem_get_netif());
    return ESP_OK;
}

EventBits_t net_manager_wait_event(EventBits_t bits, bool clear_on_exit, bool wait_for_all, TickType_t ticks_to_wait)
{
    return xEventGroupWaitBits(s_event_group, bits, clear_on_exit, wait_for_all, ticks_to_wait);
}

EventGroupHandle_t net_manager_events(void)
{
    return s_event_group;
}
