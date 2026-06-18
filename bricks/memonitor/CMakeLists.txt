#
# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

if(CONFIG_ZEGO_MEMONITOR)
    zephyr_library()
    zephyr_library_sources(src/memonitor.c)
    zephyr_include_directories(src)
endif()
