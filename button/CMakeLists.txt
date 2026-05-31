#
# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Zego button module.
#
# Usage from an application's CMakeLists.txt:
#
#   set(ZEGO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zego)
#   include(${ZEGO_DIR}/button/CMakeLists.txt)
#
# Or indirectly through the aggregation file:
#
#   set(ZEGO_MODULES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zego/modules)
#   include(${ZEGO_MODULES_DIR}/CMakeLists.txt)
#

if(CONFIG_ZEGO_BUTTON)
    zephyr_library()
    zephyr_library_sources(src/button.c)
    zephyr_include_directories(src)
endif()
