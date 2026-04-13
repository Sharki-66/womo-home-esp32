idf_component_register(
    SRCS 
        "main.c"
        "time/womo_time.c"
        "time/womo_sun_calc.c"
        "gui/womo_theme.c"
        "gui/womo_locale.c"
        "gui/womo_gas_bottle.c"
        "gui/womo_tank.c"
        "gui/womo_weather.c"
        "gui/womo_battery.c"
        "gui/womo_connectivity_modal.c"
        "gui/womo_router_leds_modal.c"
        "gui/womo_settings_modal.c"
        "gui/womo_forecast_modal.c"
        "gui/womo_thresholds.c"
        "gui/womo_fonts_german.c"
        "gui/fonts/lv_font_material_16.c"
        "gui/fonts/lv_font_montserrat_12_german.c"
        "gui/fonts/lv_font_montserrat_14_german.c"
        "gui/fonts/lv_font_montserrat_16_german.c"
        "gui/fonts/lv_font_montserrat_20_german.c"
        "gui/fonts/lv_font_montserrat_24_german.c"
        "network/womo_wifi.c"
        "network/womo_weather_http.c"
        "network/womo_meteoalarm.c"
        "network/womo_geocode.c"
        "network/womo_router_uci.c"
        "network/womo_http_mutex.c"
        "network/womo_buzzer_http.c"
        "network/womo_display_http.c"
        "storage/womo_sd.c"
        "rs485/womo_rs485_display.c"
        "hardware/lvgl_port.c"
        "hardware/waveshare_rgb_lcd_port.c"
        "hardware/buzzer.c"
    INCLUDE_DIRS 
        "."
        "time"
        "gui"
        "network"
        "storage"
        "rs485"
        "hardware"
        "${CMAKE_CURRENT_LIST_DIR}/../../shared"
    REQUIRES
        fatfs
        esp_wifi
        esp_http_client
        esp_http_server
        esp-tls
        nvs_flash
        json
        driver
        esp_timer
    EMBED_TXTFILES
        "../spiffs/buzzer_studio.html"
        "../spiffs/womo_display.html"
)

# Warnungs-Unterdrückung für LVGL (Managed Component: lvgl__lvgl)
# idf_component_get_property schlägt still fehl wenn der Name nicht passt –
# deshalb mit cmake_language(CALL) absichern.
idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
if(lvgl_lib)
    target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
endif()
