idf_component_register(
    SRCS 
        "main.c"
        "time/womo_time.c"
        "gui/womo_theme.c"
        "network/womo_wifi.c"
        "storage/womo_sd.c"
        "sensors/womo_bme680.c"
        "hardware/lvgl_port.c"
        "hardware/waveshare_rgb_lcd_port.c"
    INCLUDE_DIRS 
        "."
        "time"
        "gui"
        "network"
        "storage"
        "sensors"
        "hardware"
    REQUIRES
        fatfs
        esp_wifi
        nvs_flash
)

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
