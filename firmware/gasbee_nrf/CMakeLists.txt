# SPDX-FileCopyrightText: 2026 Hajo Harms
# SPDX-License-Identifier: MIT
#
# GasBee auf Seeed XIAO nRF52840 (nRF Connect SDK v3.4.1, Board xiao_ble)

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gasbee_nrf)

target_sources(app PRIVATE
    src/main.c
    src/hx711.c
    src/ble_gas.c
    src/battery.c
)
target_sources_ifdef(CONFIG_GASBEE_STATUS_LED app PRIVATE src/status_led.c)
