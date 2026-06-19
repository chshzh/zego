/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file wifi_ble_prov.h
 * @brief Zego Wi-Fi BLE provisioning module public API.
 *
 * Defines the WIFI_CHAN zbus channel and its message types.  Both the
 * provisioning subscriber (wifi_ble_prov.c) and the application publisher
 * (net_event_app.c) include this header.
 *
 * Enable with CONFIG_ZEGO_WIFI_BLE_PROV=y.
 */

#ifndef WIFI_BLE_PROV_H_
#define WIFI_BLE_PROV_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/zbus/zbus.h>

/**
 * @brief Wi-Fi connection status message type.
 */
enum wifi_msg_type {
	WIFI_STA_CONNECTED,
	WIFI_STA_DISCONNECTED,
	WIFI_DNS_READY,
	WIFI_ERROR,
};

/**
 * @brief Wi-Fi connection status message published on WIFI_CHAN.
 */
struct wifi_msg {
	enum wifi_msg_type type;
	int32_t rssi;
	int error_code;
};

/**
 * @brief Zbus channel: Wi-Fi STA connect/disconnect events.
 *
 * Defined in wifi_ble_prov.c (owned by this module).
 * Publisher: app's net_event_app.c (include this header, use
 *            ZBUS_CHAN_DECLARE(WIFI_CHAN) to get the extern reference).
 * Subscriber: zego_wifi_ble_prov (updates BLE advertisement).
 */
ZBUS_CHAN_DECLARE(WIFI_CHAN);

/**
 * @brief Start or stop BLE provisioning advertisement at runtime.
 *
 * Allows the application to enable or disable the BLE provisioning
 * advertisement.  Uses the same advertising parameters as the initial
 * start in wifi_ble_prov_init() - FAST if unprovisioned, SLOW if already
 * provisioned.
 *
 * @param enable  true to start advertising, false to stop.
 * @return 0 on success, -EALREADY if already in the requested state,
 *         negative errno on other failures.
 */
int zego_wifi_ble_prov_advertise(bool enable);

#endif /* WIFI_BLE_PROV_H_ */
