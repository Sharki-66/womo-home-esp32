file(GLOB_RECURSE UI_SOURCES "ui/*.c")

idf_component_register(
    SRCS 
        "main.c"
        ${UI_SOURCES}
    INCLUDE_DIRS 
        "." 
        "ui"
    REQUIRES 
        esp_lcd
        lvgl
)
