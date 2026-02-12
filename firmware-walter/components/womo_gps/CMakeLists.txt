idf_component_register(
    SRCS "womo_gps.cpp"
    INCLUDE_DIRS "include"
    REQUIRES dptechnics__walter-modem freertos womo_common
)
