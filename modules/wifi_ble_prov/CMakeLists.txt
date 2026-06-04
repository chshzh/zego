#
# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

if(CONFIG_ZEGO_WIFI_BLE_PROV)
	zephyr_library()
	zephyr_library_sources(src/wifi_ble_prov.c)
	zephyr_include_directories(src)
	# wifi_prov_core generates request.pb.h in its binary dir (nanopb).
	# Expose it to this module and anything that includes wifi_ble_prov.h.
	zephyr_include_directories($<TARGET_PROPERTY:wifi_prov_core,BINARY_DIR>)
endif()
