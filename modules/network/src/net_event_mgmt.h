/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef NET_EVENT_MGMT_H
#define NET_EVENT_MGMT_H

#include <zephyr/kernel.h>
#include <wifi.h> /* enum zego_wifi_mode */

/**
 * @brief Wait for the Wi-Fi interface to be up and WPA supplicant ready.
 *
 * Blocks until NET_EVENT_SUPPLICANT_READY has fired.
 *
 * @param timeout Maximum time to wait (K_FOREVER to wait indefinitely).
 * @return 0 on success, negative error code on timeout.
 */
int network_wait_for_wpa_supp_ready(k_timeout_t timeout);

/**
 * @brief Wait for the first SoftAP / P2P GO station to connect.
 *
 * @param timeout Maximum time to wait.
 * @return 0 on success, -EAGAIN on timeout.
 */
int network_wait_for_station_connected(k_timeout_t timeout);

/**
 * @brief Called when Wi-Fi connectivity is established.
 *
 * Weak hook — override in the application to publish app-specific zbus
 * channels.  Called by the network module at:
 *   - STA / P2P_CLIENT: DHCP bound (IP assigned)
 *   - SoftAP / P2P_GO:  first station connected
 *
 * The default implementation is a no-op.
 *
 * @param mode     Active Wi-Fi mode at the time of connection.
 * @param ip_addr  Device IP address string (NUL-terminated).
 * @param mac_addr Device MAC address string ("XX:XX:XX:XX:XX:XX").
 * @param ssid     Connected or hosted SSID (NUL-terminated).
 */
void zego_network_on_wifi_connected(enum zego_wifi_mode mode, const char *ip_addr,
				    const char *mac_addr, const char *ssid);

/**
 * @brief Called when Wi-Fi connectivity is lost.
 *
 * Weak hook — override in the application to publish app-specific zbus
 * channels.  The default implementation is a no-op.
 */
void zego_network_on_wifi_disconnected(void);

/**
 * @brief Called when the SoftAP or P2P_GO access point is enabled and ready
 *        to accept client connections.
 *
 * Weak hook — override in the application.  Fired immediately when
 * NET_EVENT_WIFI_AP_ENABLE_RESULT succeeds, before any client connects.
 * Use this to update LEDs or state machines that should reflect "AP up"
 * rather than "first client joined" (which is reported by
 * zego_network_on_wifi_connected).
 *
 * The default implementation is a no-op.
 *
 * @param mode     Active mode: ZEGO_WIFI_MODE_SOFTAP or ZEGO_WIFI_MODE_P2P_GO.
 * @param ip_addr  Gateway IP address string (static, NUL-terminated).
 * @param ssid     Hosted SSID (SoftAP); empty string for P2P_GO (SSID not
 *                 yet negotiated at AP_ENABLE time).
 */
void zego_network_on_softap_ready(enum zego_wifi_mode mode, const char *ip_addr,
				  const char *ssid);

/**
 * @brief Called when a SoftAP or P2P_GO client disconnects.
 *
 * Weak hook — override in the application.  Fired after the station list
 * is updated, so @p remaining_clients reflects the count after removal.
 *
 * @param remaining_clients  Number of stations still connected (0 = no clients).
 */
void zego_network_on_softap_sta_disconnected(int remaining_clients);

#endif /* NET_EVENT_MGMT_H */
