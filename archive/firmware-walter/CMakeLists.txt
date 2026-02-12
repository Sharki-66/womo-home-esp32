cmake_minimum_required(VERSION 3.16)

# Skip unused ESP-IDF display components to avoid unnecessary toolchain issues
set(IDF_EXCLUDE_COMPONENTS
	esp_lcd
	esp_lcd_touch
	esp_lcd_touch_gt911)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(walter)
