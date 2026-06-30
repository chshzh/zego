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
 * @brief Wait for the first SoftAP / P2P GO station to connect.
 *
 * @param timeout Maximum time to wait.
 * @return 0 on success, -EAGAIN on timeout.
 */
int network_wait_for_station_connected(k_timeout_t timeout);

/**
 * @brief Called when the Wi-Fi association succeeds (L2 connected, before DHCP).
 *
 * Weak hook — override in the application.  Fired at NET_EVENT_WIFI_CONNECT_RESULT
 * success for STA and P2P_GC modes.  At this point the device is associated
 * with the AP but does not yet have a routable IP address.
 *
 * The default implementation is a no-op.
 *
 * @param mode  Active Wi-Fi mode (ZEGO_WIFI_MODE_STA or ZEGO_WIFI_MODE_P2P_GC).
 */
void zego_on_net_event_wifi_connect(enum zego_wifi_mode mode);

/**
 * @brief Called when STA or P2P_GC obtains a DHCP-assigned IP address.
 *
 * Weak hook — override in the application.  Fired after DHCP negotiation
 * completes and the device has a routable IP on the AP's subnet.
 *
 * @param mode     Active Wi-Fi mode (ZEGO_WIFI_MODE_STA or ZEGO_WIFI_MODE_P2P_GC).
 * @param ip_addr  Device IP address string (NUL-terminated).
 * @param mac_addr Device MAC address string ("XX:XX:XX:XX:XX:XX").
 * @param ssid     Connected SSID (NUL-terminated).
 */
void zego_on_net_event_dhcp_bound(enum zego_wifi_mode mode, const char *ip_addr,
				  const char *mac_addr, const char *ssid);

/**
 * @brief Called when a station connects to the SoftAP or P2P_GO.
 *
 * Weak hook — override in the application.  Fired after the station table
 * is updated, so @p sta_count reflects the count after the new connection.
 *
 * @param sta_count  Total stations currently connected (including the new one).
 */
void zego_on_net_event_wifi_ap_sta_connected(int sta_count);

/**
 * @brief Called when Wi-Fi connectivity is lost.
 *
 * Weak hook — override in the application to publish app-specific zbus
 * channels.  The default implementation is a no-op.
 */
void zego_on_net_event_wifi_disconnect(void);

/**
 * @brief Called when the SoftAP or P2P_GO access point is enabled and ready
 *        to accept client connections.
 *
 * Weak hook — override in the application.  Fired immediately when
 * NET_EVENT_WIFI_AP_ENABLE_RESULT succeeds, before any client connects.
 * Use this to update LEDs or state machines that should reflect "AP up"
 * rather than "first client joined" (which is reported by
 * zego_on_net_event_wifi_ap_sta_connected).
 *
 * The default implementation is a no-op.
 *
 * @param mode     Active mode: ZEGO_WIFI_MODE_SOFTAP or ZEGO_WIFI_MODE_P2P_GO.
 * @param ip_addr  Gateway IP address string (static, NUL-terminated).
 * @param ssid     Hosted SSID (SoftAP); empty string for P2P_GO (SSID not
 *                 yet negotiated at AP_ENABLE time).
 */
void zego_on_net_event_wifi_ap_enabled(enum zego_wifi_mode mode, const char *ip_addr,
				       const char *ssid);

/**
 * @brief Called when a SoftAP or P2P_GO client disconnects.
 *
 * Weak hook — override in the application.  Fired after the station list
 * is updated, so @p remaining_clients reflects the count after removal.
 *
 * @param remaining_clients  Number of stations still connected (0 = no clients).
 */
void zego_on_net_event_wifi_ap_sta_disconnected(int remaining_clients);

/**
 * @brief Called when P2P pairing starts (@p active = true) or ends
 *        (@p active = false).
 *
 * Weak hook — override in the application to drive a pairing indication
 * (e.g. LED BREATHE).  Fired on both roles:
 *   - P2P_GO: true when the WPS PBC pairing window is (re)opened, false when
 *     the window expires or a client connects.
 *   - P2P_GC: true when discovery starts, false on connect success or give-up.
 *
 * The default implementation is a no-op.
 *
 * @param active  true = pairing in progress, false = pairing ended.
 */
void zego_on_net_event_p2p_pairing(bool active);

#endif /* NET_EVENT_MGMT_H */
