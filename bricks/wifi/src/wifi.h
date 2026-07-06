/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file wifi.h
 * @brief Zego wifi module public API.
 *
 * Provides the Wi-Fi mode enum, the WIFI_MODE_CHAN zbus channel, and the
 * mode accessor used by network modules.  The startup banner previously
 * declared here has moved to zego/bricks/ux — see zego_ux_print_banner()
 * in ux.h.
 *
 * Enable with CONFIG_ZEGO_WIFI=y.
 *
 * Wi-Fi mode enum values are persisted as uint8_t in NVS.
 * Do NOT reorder or renumber them without erasing NVS on all devices.
 */

#ifndef ZEGO_WIFI_H_
#define ZEGO_WIFI_H_

#include <zephyr/zbus/zbus.h>

/**
 * @brief Wi-Fi operating mode.
 *
 * Stored as uint8_t in NVS under settings key "app/zego_wifi_mode".
 * Values are intentionally fixed — changing them breaks NVS compat.
 */
enum zego_wifi_mode {
	ZEGO_WIFI_MODE_STA        = 0, /**< Station: join an existing network */
	ZEGO_WIFI_MODE_SOFTAP     = 1, /**< SoftAP:  create an access point   */
	ZEGO_WIFI_MODE_P2P_GO     = 2, /**< P2P Group Owner (Wi-Fi Direct)    */
	ZEGO_WIFI_MODE_P2P_GC     = 3, /**< P2P GC (Group Client): join peer's group */
};

/**
 * @brief Wi-Fi mode message published once at boot on WIFI_MODE_CHAN.
 */
struct wifi_mode_msg {
	enum zego_wifi_mode mode;
};

/**
 * @brief Zbus channel: Wi-Fi mode published by wifi_mode_selector at
 *        SYS_INIT APPLICATION priority 0.
 *
 * Subscribers read this channel to determine the active Wi-Fi mode before
 * starting the network stack.
 */
ZBUS_CHAN_DECLARE(WIFI_MODE_CHAN);

/**
 * @brief Return the Wi-Fi mode loaded from NVS at boot.
 *
 * Safe to call from any context after SYS_INIT APPLICATION priority 0.
 */
enum zego_wifi_mode zego_wifi_get_mode(void);

#endif /* ZEGO_WIFI_H_ */
