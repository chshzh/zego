#
# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Zego LED module.
#

if(CONFIG_ZEGO_LED)
    target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/led.c)
    target_include_directories(app PUBLIC ${CMAKE_CURRENT_LIST_DIR})
endif()
