# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# Nutze lokale Komponenten und managed_components im Suchpfad
set(EXTRA_COMPONENT_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/components"
        "${CMAKE_CURRENT_LIST_DIR}/managed_components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-fdiagnostics-color=always)

project(womo_sensorboard)

# SPIFFS-Image für Parkhilfe-Webseite (horizon.html, favicon.ico)
spiffs_create_partition_image(storage spiffs FLASH_IN_PROJECT)
