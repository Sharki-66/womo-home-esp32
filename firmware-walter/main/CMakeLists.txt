idf_component_register(
    SRCS "main.cpp" "web/womo_web.c"
    INCLUDE_DIRS "." "web"
    REQUIRES esp_event esp_netif esp_wifi esp_http_server nvs_flash driver lwip i2cdev adithya-187326__bno055 k0i05__esp_bme680 womo_hx711 womo_analog womo_rs485 womo_gps womo_common json
)
