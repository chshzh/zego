#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

# Register zego bricks: button, led, wifi, network, wifi_ble_prov.
# Paths are relative to this CMakeLists.txt which lives inside zego/, so
# sibling bricks are under ../bricks/<name>/.
get_filename_component(ZEGO_BUTTON_DIR       ${CMAKE_CURRENT_SOURCE_DIR}/../bricks/button       REALPATH)
get_filename_component(ZEGO_LED_DIR          ${CMAKE_CURRENT_SOURCE_DIR}/../bricks/led          REALPATH)
get_filename_component(ZEGO_WIFI_DIR         ${CMAKE_CURRENT_SOURCE_DIR}/../bricks/wifi         REALPATH)
get_filename_component(ZEGO_NETWORK_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/../bricks/network      REALPATH)
get_filename_component(ZEGO_WIFI_BLE_PROV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../bricks/wifi_ble_prov REALPATH)
get_filename_component(ZEGO_MEMONITOR_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/../bricks/memonitor    REALPATH)
get_filename_component(ZEGO_UX_DIR            ${CMAKE_CURRENT_SOURCE_DIR}/../bricks/ux           REALPATH)
list(APPEND EXTRA_ZEPHYR_MODULES
  ${ZEGO_BUTTON_DIR}
  ${ZEGO_LED_DIR}
  ${ZEGO_WIFI_DIR}
  ${ZEGO_NETWORK_DIR}
  ${ZEGO_WIFI_BLE_PROV_DIR}
  ${ZEGO_MEMONITOR_DIR}
  ${ZEGO_UX_DIR}
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nordic_wifi_app_template)

target_include_directories(app PUBLIC ${ZEPHYR_BASE}/subsys/net/ip)

# All bricks are provided via EXTRA_ZEPHYR_MODULES above (zego/wifi,
# zego/network, zego/button, zego/led, zego/wifi_ble_prov, zego/ux).
# Add application modules below as the project grows.

# APP_VERSION_STRING, PRD_VERSION, SPECS_VERSION are injected by
# zego/wifi/CMakeLists.txt via target_compile_definitions.
target_sources(app PRIVATE
  src/main.c
  src/modules/network/net_event_mgmt_app.c
)
