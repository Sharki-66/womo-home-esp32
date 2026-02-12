idf_component_register(
    SRCS "womo_rs485.c"
    INCLUDE_DIRS "include"
    REQUIRES driver womo_common
)
