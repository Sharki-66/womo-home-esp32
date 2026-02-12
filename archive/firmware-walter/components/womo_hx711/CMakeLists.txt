idf_component_register(
    SRCS "womo_hx711.c"
    INCLUDE_DIRS "include"
    REQUIRES driver womo_common esp-idf-lib__hx711 freertos
)
