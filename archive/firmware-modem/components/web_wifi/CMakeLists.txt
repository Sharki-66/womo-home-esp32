set(REQUIRES esp_wifi esp_eth esp_timer nvs_flash spiffs esp-tls vfs lwip esp_http_server iot_usbh_modem iot_usbh_cdc modem_at)

if(CONFIG_EXAMPLE_ENABLE_WEB_ROUTER)
    idf_component_register(SRC_DIRS "." "router_web"
                    INCLUDE_DIRS "." "include" "router_web"
                    REQUIRES ${REQUIRES})
    spiffs_create_partition_image(storage ./spiffs FLASH_IN_PROJECT)
else()
    idf_component_register(SRC_DIRS "."
                    INCLUDE_DIRS "." "include"
                    REQUIRES ${REQUIRES})
endif()
