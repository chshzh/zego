#
# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Zego LED module.
#
# Usage from an application's CMakeLists.txt:
#
#   set(ZEGO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zego)
#   include(${ZEGO_DIR}/led/CMakeLists.txt)
#

if(CONFIG_ZEGO_LED)
    zephyr_library()
    zephyr_library_sources(src/led.c)

    if(CONFIG_ZEGO_LED_BACKEND_DK)
        zephyr_library_sources(src/led_hw_dk.c)
    elseif(CONFIG_ZEGO_LED_BACKEND_ZEPHYR)
        zephyr_library_sources(src/led_hw_zephyr.c)
    endif()

    zephyr_include_directories(src)
endif()
