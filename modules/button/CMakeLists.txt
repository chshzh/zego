#
# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Zego button module.  Provides debounced button event publishing over zbus.
# Source files are added only when CONFIG_ZEGO_BUTTON=y.
#

if(CONFIG_ZEGO_BUTTON)
    target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/button.c)
    target_include_directories(app PUBLIC ${CMAKE_CURRENT_LIST_DIR})
endif()
