/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * net_event_app.c — application-side Wi-Fi event hooks.
 *
 * zego/network fires weak callbacks when connectivity changes:
 *
 *   zego_network_on_softap_ready()     — SoftAP/P2P_GO AP enabled (before any client)
 *   zego_network_on_wifi_connected()   — IP assigned (STA/P2P_CLIENT) or
 *                                        first station joined (SoftAP/P2P_GO)
 *   zego_network_on_wifi_disconnected() — link lost
 *
 * Override them here (strong definitions beat the weak no-ops in
 * zego/network) to react to network events — e.g. publish a zbus channel,
 * start an MQTT client, or kick off an HTTP request.
 *
 * APP_WIFI_STATE_CHAN is published here so app_ux can drive LED 0.
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

/* Define APP_WIFI_STATE_CHAN here — declared in messages.h. */
ZBUS_CHAN_DEFINE(APP_WIFI_STATE_CHAN, struct app_wifi_state_msg, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.state = APP_WIFI_STATE_CONNECTING,
			       .mode = ZEGO_WIFI_MODE_STA));

void zego_network_on_softap_ready(enum zego_wifi_mode mode, const char *ip_addr,
				  const char *ssid)
{
	LOG_INF("SoftAP ready: mode=%s ip=%s ssid=%s",
		mode == ZEGO_WIFI_MODE_P2P_GO ? "p2p_go" : "softap", ip_addr, ssid);

	struct app_wifi_state_msg msg = {
		.mode = mode,
		.state = APP_WIFI_STATE_SOFTAP,
	};

	zbus_chan_pub(&APP_WIFI_STATE_CHAN, &msg, K_NO_WAIT);
}

void zego_network_on_wifi_connected(enum zego_wifi_mode mode, const char *ip_addr,
				    const char *mac_addr, const char *ssid)
{
	LOG_INF("Wi-Fi connected: mode=%s ip=%s mac=%s ssid=%s",
		(mode == ZEGO_WIFI_MODE_STA)        ? "sta" :
		(mode == ZEGO_WIFI_MODE_SOFTAP)     ? "softap" :
		(mode == ZEGO_WIFI_MODE_P2P_GO)     ? "p2p_go" : "p2p_client",
		ip_addr, mac_addr, ssid);

	struct app_wifi_state_msg msg = {
		.mode = mode,
		.state = (mode == ZEGO_WIFI_MODE_SOFTAP || mode == ZEGO_WIFI_MODE_P2P_GO)
				 ? APP_WIFI_STATE_SOFTAP
				 : APP_WIFI_STATE_CONNECTED,
	};

	zbus_chan_pub(&APP_WIFI_STATE_CHAN, &msg, K_NO_WAIT);
}

void zego_network_on_wifi_disconnected(void)
{
	LOG_INF("Wi-Fi disconnected");

	struct app_wifi_state_msg msg = {
		.state = APP_WIFI_STATE_ERROR,
		.mode = ZEGO_WIFI_MODE_STA,
	};

	zbus_chan_pub(&APP_WIFI_STATE_CHAN, &msg, K_NO_WAIT);
}
