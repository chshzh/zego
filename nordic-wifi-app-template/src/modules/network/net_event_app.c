/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * net_event_app.c — application-side Wi-Fi event hooks.
 *
 * zego/network fires weak callbacks when connectivity changes:
 *
 *   zego_on_net_event_wifi_ap_enabled()       — SoftAP/P2P_GO AP enabled (before any client)
 *   zego_on_net_event_dhcp_bound()        — IP assigned via DHCP (STA/P2P_CLIENT)
 *   zego_on_net_event_wifi_ap_sta_connected()  — station joined SoftAP/P2P_GO
 *   zego_on_net_event_wifi_disconnect() — link lost
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
#include "led.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(net_event_app, LOG_LEVEL_INF);

/* Define APP_WIFI_STATE_CHAN here — declared in messages.h. */
ZBUS_CHAN_DEFINE(APP_WIFI_STATE_CHAN, struct app_wifi_state_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.state = APP_WIFI_STATE_CONNECTING, .mode = ZEGO_WIFI_MODE_STA));

void zego_on_net_event_wifi_ap_enabled(enum zego_wifi_mode mode, const char *ip_addr,
				       const char *ssid)
{
	struct app_wifi_state_msg msg = {
		.mode = mode,
		.state = APP_WIFI_STATE_SOFTAP,
	};

	zbus_chan_pub(&APP_WIFI_STATE_CHAN, &msg, K_NO_WAIT);

	/* TODO: AP is up, no client connected yet.
	 * Add application logic here — e.g. start mDNS, advertise a service. */
	LOG_INF("TODO: AP is up, no client connected yet — add your application logic in src/modules/network/net_event_app.c/zego_on_net_event_wifi_ap_enabled()");
}

void zego_on_net_event_dhcp_bound(enum zego_wifi_mode mode, const char *ip_addr,
				  const char *mac_addr, const char *ssid)
{


	struct led_msg led = {.type = LED_COMMAND_ON, .led_number = 0};

	zbus_chan_pub(&LED_CMD_CHAN, &led, K_NO_WAIT);

	struct app_wifi_state_msg msg = {
		.mode = mode,
		.state = APP_WIFI_STATE_CONNECTED,
	};

	zbus_chan_pub(&APP_WIFI_STATE_CHAN, &msg, K_NO_WAIT);

	/* TODO: Device has an IP address — start your application here.
	 * ip_addr, mac_addr, ssid are available as function arguments.
	 * Example: connect to MQTT broker, start HTTP client, send telemetry. */
	LOG_INF("TODO: Device has IP %s (mac=%s ssid=%s) — start your application in src/modules/network/net_event_app.c/zego_on_net_event_dhcp_bound()",
		ip_addr, mac_addr, ssid);
}

void zego_on_net_event_wifi_ap_sta_connected(int sta_count)
{
	LOG_INF("SoftAP client connected: total=%d", sta_count);

	if (sta_count >= 1) {
		struct led_msg led = {.type = LED_COMMAND_ON, .led_number = 0};

		zbus_chan_pub(&LED_CMD_CHAN, &led, K_NO_WAIT);
		struct app_wifi_state_msg msg = {
			.mode = ZEGO_WIFI_MODE_SOFTAP,
			.state = APP_WIFI_STATE_CONNECTED,
		};

		zbus_chan_pub(&APP_WIFI_STATE_CHAN, &msg, K_NO_WAIT);

		/* TODO: First (or additional) client connected to SoftAP/P2P_GO.
		 * sta_count is the total number of connected stations.
		 * Example: start a provisioning server, send a welcome packet. */
		LOG_INF("TODO: SoftAP/P2P_GO client connected (total=%d) — add your application logic in src/modules/network/net_event_app.c/zego_on_net_event_wifi_ap_sta_connected()",
			sta_count);
	}
}

void zego_on_net_event_wifi_disconnect(void)
{

	struct led_msg led = {.type = LED_COMMAND_ROTATE, .led_number = 0};

	zbus_chan_pub(&LED_CMD_CHAN, &led, K_NO_WAIT);

	struct app_wifi_state_msg msg = {
		.state = APP_WIFI_STATE_ERROR,
		.mode = ZEGO_WIFI_MODE_STA,
	};

	zbus_chan_pub(&APP_WIFI_STATE_CHAN, &msg, K_NO_WAIT);

	/* TODO: Wi-Fi link lost — clean up application state here.
	 * Example: disconnect MQTT, cancel pending requests, flush buffers. */
	LOG_INF("TODO: Wi-Fi link lost — clean up your application state in src/modules/network/net_event_app.c/zego_on_net_event_wifi_disconnect()");
}

void zego_on_net_event_wifi_ap_sta_disconnected(int remaining_clients)
{
	if (remaining_clients == 0) {
		struct led_msg led = {.type = LED_COMMAND_ROTATE, .led_number = 0};

		zbus_chan_pub(&LED_CMD_CHAN, &led, K_NO_WAIT);

		struct app_wifi_state_msg msg = {
			.state = APP_WIFI_STATE_SOFTAP,
			.mode = ZEGO_WIFI_MODE_SOFTAP,
		};

		zbus_chan_pub(&APP_WIFI_STATE_CHAN, &msg, K_NO_WAIT);

		/* TODO: Last client left the SoftAP/P2P_GO — no stations connected.
		 * Example: stop provisioning server, re-arm WPS, update cloud status. */
		LOG_INF("TODO: Last SoftAP/P2P_GO client left — add your application logic in src/modules/network/net_event_app.c/zego_on_net_event_wifi_ap_sta_disconnected()");
	}
}
