idf_component_register(
    SRCS "womo_analog.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_adc esp_timer womo_common
)
