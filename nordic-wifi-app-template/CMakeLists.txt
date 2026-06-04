#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

# Register zego modules: button, led, wifi, network, wifi_ble_prov.
# Paths are relative to this CMakeLists.txt which lives inside zego/, so
# sibling modules are under ../modules/<name>/.
get_filename_component(ZEGO_BUTTON_DIR       ${CMAKE_CURRENT_SOURCE_DIR}/../modules/button       REALPATH)
get_filename_component(ZEGO_LED_DIR          ${CMAKE_CURRENT_SOURCE_DIR}/../modules/led          REALPATH)
get_filename_component(ZEGO_WIFI_DIR         ${CMAKE_CURRENT_SOURCE_DIR}/../modules/wifi         REALPATH)
get_filename_component(ZEGO_NETWORK_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/../modules/network      REALPATH)
get_filename_component(ZEGO_WIFI_BLE_PROV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../modules/wifi_ble_prov REALPATH)
list(APPEND EXTRA_ZEPHYR_MODULES
  ${ZEGO_BUTTON_DIR}
  ${ZEGO_LED_DIR}
  ${ZEGO_WIFI_DIR}
  ${ZEGO_NETWORK_DIR}
  ${ZEGO_WIFI_BLE_PROV_DIR}
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nordic_wifi_app_template)

target_include_directories(app PUBLIC ${ZEPHYR_BASE}/subsys/net/ip)

# All modules are provided via EXTRA_ZEPHYR_MODULES above (zego/wifi,
# zego/network, zego/button, zego/led, zego/wifi_ble_prov).
# Add application modules below as the project grows.

# APP_VERSION_STRING, PRD_VERSION, SPECS_VERSION are injected by
# zego/wifi/CMakeLists.txt via target_compile_definitions.
target_sources(app PRIVATE
  src/main.c
  src/modules/network/net_event_app.c
)

add_subdirectory(src/modules/ux)
