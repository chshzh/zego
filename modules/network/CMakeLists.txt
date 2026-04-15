#
# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Zego network module.
#

if(CONFIG_ZEGO_NETWORK)
    target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/network.c)
    target_include_directories(app PUBLIC ${CMAKE_CURRENT_LIST_DIR})
endif()
