#
# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Zego shared modules aggregation point.
#
# Applications include this file from their own CMakeLists.txt:
#
#   set(ZEGO_MODULES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../modules)
#   include(${ZEGO_MODULES_DIR}/CMakeLists.txt)
#
# Each module guards itself with its CONFIG_ZEGO_* symbol so that only
# enabled modules contribute sources to the build.
#

if(CONFIG_ZEGO_BUTTON)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/button
                     ${CMAKE_CURRENT_BINARY_DIR}/zego_button)
endif()

if(CONFIG_ZEGO_NETWORK)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/network
                     ${CMAKE_CURRENT_BINARY_DIR}/zego_network)
endif()

if(CONFIG_ZEGO_LED)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/led
                     ${CMAKE_CURRENT_BINARY_DIR}/zego_led)
endif()
