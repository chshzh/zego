#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

if(CONFIG_ZEGO_UX)
	zephyr_library()
	zephyr_library_sources(src/ux.c)
	zephyr_include_directories(src)
endif()
