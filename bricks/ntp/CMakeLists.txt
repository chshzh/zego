#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

if(CONFIG_ZEGO_NTP)
	zephyr_library()
	zephyr_library_sources(src/ntp.c)
	zephyr_include_directories(src)
endif()
