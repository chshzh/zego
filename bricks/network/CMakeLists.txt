#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

if(CONFIG_ZEGO_NETWORK)
	zephyr_library()
	zephyr_library_sources(
		src/net_event_mgmt.c
		src/wifi_utils.c
	)
	if(CONFIG_ZEGO_NETWORK_MDNS)
		zephyr_library_sources(src/mdns.c)
	endif()
	zephyr_include_directories(src)
	zephyr_include_directories(${ZEPHYR_BASE}/subsys/net/ip)
endif()
