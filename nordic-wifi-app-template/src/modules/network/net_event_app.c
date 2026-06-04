/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * net_event_app.c — application-side Wi-Fi event hooks.
 *
 * zego/network fires two weak callbacks when connectivity changes:
 *
 *   zego_network_on_wifi_connected()   — IP assigned (STA/P2P_CLIENT) or
 *                                        first station joined (SoftAP/P2P_GO)
 *   zego_network_on_wifi_disconnected() — link lost
 *
 * Override them here (strong definitions beat the weak no-ops in
 * zego/network) to react to network events — e.g. publish a zbus channel,
 * start an MQTT client, or kick off an HTTP request.
 *
 * ── How to extend ────────────────────────────────────────────────────────────
 *
 * 1. Define your app message type and zbus channel in src/modules/messages.h:
 *
 *      struct my_wifi_msg {
 *          enum zego_wifi_mode mode;
 *          char ip[16];
 *          char mac[18];
 *          char ssid[33];
 *          bool connected;
 *      };
 *      ZBUS_CHAN_DECLARE(MY_WIFI_CHAN);
 *
 * 2. Own the channel in one .c file (exactly one translation unit):
 *
 *      ZBUS_CHAN_DEFINE(MY_WIFI_CHAN, struct my_wifi_msg, NULL, NULL,
 *                       ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(.connected = false));
 *
 * 3. Replace the LOG_INF calls below with a zbus_chan_pub():
 *
 *      struct my_wifi_msg msg = {
 *          .mode = mode, .connected = true,
 *      };
 *      strncpy(msg.ip,   ip_addr,  sizeof(msg.ip)   - 1);
 *      strncpy(msg.mac,  mac_addr, sizeof(msg.mac)  - 1);
 *      strncpy(msg.ssid, ssid,     sizeof(msg.ssid) - 1);
 *      zbus_chan_pub(&MY_WIFI_CHAN, &msg, K_NO_WAIT);
 *
 * 4. Subscribe in any module with ZBUS_SUBSCRIBER_DEFINE / ZBUS_LISTENER_DEFINE.
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <net_event_mgmt.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(net_event_app, LOG_LEVEL_INF);

void zego_network_on_wifi_connected(enum zego_wifi_mode mode, const char *ip_addr,
				    const char *mac_addr, const char *ssid)
{
	LOG_INF("Wi-Fi connected: mode=%d ip=%s mac=%s ssid=%s", mode, ip_addr, mac_addr, ssid);

	/*
	 * TODO: publish your app zbus channel here, e.g.:
	 *
	 *   struct my_wifi_msg msg = { .connected = true, .mode = mode };
	 *   strncpy(msg.ip, ip_addr, sizeof(msg.ip) - 1);
	 *   zbus_chan_pub(&MY_WIFI_CHAN, &msg, K_NO_WAIT);
	 */
}

void zego_network_on_wifi_disconnected(void)
{
	LOG_INF("Wi-Fi disconnected");

	/*
	 * TODO: publish your app zbus channel here, e.g.:
	 *
	 *   struct my_wifi_msg msg = { .connected = false };
	 *   zbus_chan_pub(&MY_WIFI_CHAN, &msg, K_NO_WAIT);
	 */
}
