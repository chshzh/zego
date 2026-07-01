/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * net_event_app.c - application-side Wi-Fi event hooks.
 *
 * zego/network fires weak callbacks when connectivity changes:
 *
 *   zego_on_net_event_wifi_ap_enabled()       - SoftAP/P2P_GO AP enabled (before any client)
 *   zego_on_net_event_dhcp_bound()        - IP assigned via DHCP (STA/P2P_CLIENT)
 *   zego_on_net_event_wifi_ap_sta_connected()  - station joined SoftAP/P2P_GO
 *   zego_on_net_event_wifi_disconnect(will_retry) - link lost (or STA has no
 *       stored credentials); will_retry says whether it will reconnect on its own
 *
 * Override them here (strong definitions beat the weak no-ops in
 * zego/network) to react to network events - e.g. publish a zbus channel,
 * start an MQTT client, or kick off an HTTP request.
 *
 * APP_WIFI_STATE_CHAN is published here so app_ux can drive the LEDs.
 * Add your own zbus channels following the same ZBUS_CHAN_DEFINE pattern.
 *
 * ── How to extend ────────────────────────────────────────────────────────────
 *
 * 1. Add your app message type and channel declaration to messages.h.
 * 2. Define the channel in this file with ZBUS_CHAN_DEFINE.
 * 3. Publish inside the callbacks below.
 * 4. Subscribe in any module with ZBUS_SUBSCRIBER_DEFINE / ZBUS_LISTENER_DEFINE.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <net_event_mgmt.h>
#include "../messages.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(net_event_app, LOG_LEVEL_INF);

/* Define APP_WIFI_STATE_CHAN here - declared in messages.h. */
ZBUS_CHAN_DEFINE(APP_WIFI_STATE_CHAN, struct app_wifi_state_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.state = APP_WIFI_STATE_CONNECTING, .mode = ZEGO_WIFI_MODE_STA));

/* Tracks whether a link is currently up, so zego_on_net_event_p2p_pairing(false)
 * can resolve the state back to CONNECTED vs CONNECTING when pairing ends. */
static bool s_connected;

/*
 * P2P pairing started/ended. Publishes PAIRING state so app_ux drives the
 * pairing LED (CONFIG_APP_UX_PAIRING_LED_IDX) while pairing is active,
 * reverting to CONNECTED or CONNECTING when it ends. Called by the zego/network
 * P2P engine (GO: window open/close; GC: discovery start / connect success or give-up).
 */
void zego_on_net_event_p2p_pairing(bool active)
{
	struct app_wifi_state_msg msg = {
		.mode = zego_wifi_get_mode(),
		.state = active ? APP_WIFI_STATE_PAIRING
				: (s_connected ? APP_WIFI_STATE_CONNECTED
					       : APP_WIFI_STATE_CONNECTING),
	};

	zbus_chan_pub(&APP_WIFI_STATE_CHAN, &msg, K_NO_WAIT);
	LOG_INF("P2P pairing %s", active ? "started" : "ended");
}

void zego_on_net_event_wifi_ap_enabled(enum zego_wifi_mode mode, const char *ip_addr,
				       const char *ssid)
{
	struct app_wifi_state_msg msg = {
		.mode = mode,
		.state = APP_WIFI_STATE_SOFTAP,
	};

	zbus_chan_pub(&APP_WIFI_STATE_CHAN, &msg, K_NO_WAIT);

	/* TODO: AP is up, no client connected yet.
	 * Add application logic here - e.g. start mDNS, advertise a service. */
	LOG_INF("TODO: AP is up, no client connected yet - add your application logic in "
		"src/modules/network/net_event_app.c/zego_on_net_event_wifi_ap_enabled()");
}

void zego_on_net_event_dhcp_bound(enum zego_wifi_mode mode, const char *ip_addr,
				  const char *mac_addr, const char *ssid)
{
	s_connected = true;

	struct app_wifi_state_msg msg = {
		.mode = mode,
		.state = APP_WIFI_STATE_CONNECTED,
	};

	zbus_chan_pub(&APP_WIFI_STATE_CHAN, &msg, K_NO_WAIT);

	/* TODO: Device has an IP address - start your application here.
	 * ip_addr, mac_addr, ssid are available as function arguments.
	 * Example: connect to MQTT broker, start HTTP client, send telemetry. */
	LOG_INF("TODO: Device has IP %s (mac=%s ssid=%s) - start your application in "
		"src/modules/network/net_event_app.c/zego_on_net_event_dhcp_bound()",
		ip_addr, mac_addr, ssid);
}

void zego_on_net_event_wifi_ap_sta_connected(int sta_count)
{
	LOG_INF("AP client connected: total=%d", sta_count);

	if (sta_count >= 1) {
		struct app_wifi_state_msg msg = {
			.mode = ZEGO_WIFI_MODE_SOFTAP,
			.state = APP_WIFI_STATE_CONNECTED,
		};

		zbus_chan_pub(&APP_WIFI_STATE_CHAN, &msg, K_NO_WAIT);

		/* TODO: First (or additional) client connected to SoftAP/P2P_GO.
		 * sta_count is the total number of connected stations (max 3).
		 * Example: start a provisioning server, send a welcome packet. */
		LOG_INF("TODO: AP/P2P_GO client connected (now %d/3 devices connected) - add your "
			"application "
			"logic in "
			"src/modules/network/net_event_app.c/"
			"zego_on_net_event_wifi_ap_sta_connected()",
			sta_count);
	}
}

/*
 * will_retry distinguishes "link lost but a retry is already scheduled"
 * from "reconnection is not possible" (STA with zero stored credentials -
 * the only false case today; P2P_GC always passes true).  LED 0 rotates in
 * the first case and fast-blinks only in the second - see FR-105.
 */
void zego_on_net_event_wifi_disconnect(bool will_retry)
{
	s_connected = false;

	struct app_wifi_state_msg msg = {
		.state = will_retry ? APP_WIFI_STATE_CONNECTING : APP_WIFI_STATE_ERROR,
		.mode = ZEGO_WIFI_MODE_STA,
	};

	zbus_chan_pub(&APP_WIFI_STATE_CHAN, &msg, K_NO_WAIT);

	/* TODO: Wi-Fi link lost - clean up application state here.
	 * Example: disconnect MQTT, cancel pending requests, flush buffers. */
	LOG_INF("TODO: Wi-Fi link lost (retrying=%d) - clean up your application state in "
		"src/modules/network/net_event_app.c/zego_on_net_event_wifi_disconnect()",
		will_retry);
}

/*
 * NOTE: this may fire up to ~5 minutes after a P2P_GC (or SoftAP client)
 * loses power or crashes, since the AP only detects the loss via its
 * inactivity timer (default 300 s) when no deauth frame was sent. A clean
 * client shutdown fires this immediately. See network-spec.md for how to
 * lower the timeout at runtime (wpa_cli set ap_max_inactivity).
 */
void zego_on_net_event_wifi_ap_sta_disconnected(int remaining_clients)
{
	LOG_INF("TODO: AP/P2P_GO client disconnected (now %d/3 devices connected) - add your "
		"application "
		"logic in "
		"src/modules/network/net_event_app.c/"
		"zego_on_net_event_wifi_ap_sta_disconnected()",
		remaining_clients);

	if (remaining_clients == 0) {
		struct app_wifi_state_msg msg = {
			.state = APP_WIFI_STATE_SOFTAP,
			.mode = ZEGO_WIFI_MODE_SOFTAP,
		};

		zbus_chan_pub(&APP_WIFI_STATE_CHAN, &msg, K_NO_WAIT);

		/* TODO: Last client left the SoftAP/P2P_GO - no stations connected.
		 * Example: stop provisioning server, re-arm WPS, update cloud status. */
	}
}
