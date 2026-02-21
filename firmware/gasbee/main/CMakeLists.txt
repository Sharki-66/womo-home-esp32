idf_component_register(
    SRCS
        "app_main.c"
        "ble_gas.c"
    INCLUDE_DIRS "."
    REQUIRES
        hx711
        nvs_flash
        esp_timer
        bt
        esp_event
)
