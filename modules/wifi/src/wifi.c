/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * wifi.c — application banner callbacks and banner callbacks.
 *
 * Overrides zego_banner_wifi_info() and zego_banner_app_extra() with
 * Kconfig-conditional content, then calls zego_banner_print() and sleeps.
 *
 * Feature gates:
 *   NRF70_AP_MODE          — SoftAP mode available
 *   NRF70_P2P_MODE         — P2P_GO / P2P_CLIENT modes available
 *   NET_L2_WIFI_SHELL      — 'wifi connect' shell command available
 *   WIFI_CREDENTIALS       — 'wifi cred' persistent credentials available
 *   ZEGO_WIFI_BLE_PROV — BLE provisioning available
 */

#include "wifi.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#if CONFIG_APP_MEMFAULT_MODULE
#include <memfault/version.h>
#endif

LOG_MODULE_REGISTER(zego_wifi, CONFIG_ZEGO_WIFI_LOG_LEVEL);

/* WIFI_MODE_CHAN is always defined here so wifi.c is the single owner
 * regardless of whether ZEGO_WIFI_MODE_SELECTOR is enabled. */
ZBUS_CHAN_DEFINE(WIFI_MODE_CHAN, struct wifi_mode_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

#if !CONFIG_ZEGO_WIFI_MODE_SELECTOR
/* STA-only build: no NVS access needed.  Publish STA on WIFI_MODE_CHAN at the
 * same APPLICATION priority 0 that wifi_mode_selector would use, so the
 * network module (priority 5) always sees a valid channel value. */
enum zego_wifi_mode zego_wifi_get_mode(void)
{
	return ZEGO_WIFI_MODE_STA;
}

static int wifi_sta_mode_init(void)
{
	struct wifi_mode_msg msg = {.mode = ZEGO_WIFI_MODE_STA};

	zbus_chan_pub(&WIFI_MODE_CHAN, &msg, K_NO_WAIT);
	return 0;
}

SYS_INIT(wifi_sta_mode_init, APPLICATION, 0);
#endif /* !CONFIG_ZEGO_WIFI_MODE_SELECTOR */

#ifndef ZEGO_BANNER_APP_NAME
#define ZEGO_BANNER_APP_NAME "unknown"
#endif

static const char *get_board_name(void)
{
	if (strstr(CONFIG_BOARD, "nrf7002dk") != NULL) {
		return "nRF7002DK";
	} else if (strstr(CONFIG_BOARD, "nrf54lm20dk") != NULL) {
		return "nRF54LM20DK+nRF7002EBII";
	}
	return CONFIG_BOARD;
}

/* ============================================================================
 * BANNER CALLBACKS
 * ============================================================================
 */

void zego_banner_wifi_info(void)
{
	enum zego_wifi_mode mode = zego_wifi_get_mode();
	const char *mode_str;

	switch (mode) {
	case ZEGO_WIFI_MODE_STA:
		mode_str = "STA";
		break;
	case ZEGO_WIFI_MODE_SOFTAP:
		mode_str = "SoftAP";
		break;
	case ZEGO_WIFI_MODE_P2P_GO:
		mode_str = "P2P_GO";
		break;
	case ZEGO_WIFI_MODE_P2P_CLIENT:
		mode_str = "P2P_CLIENT";
		break;
	default:
		mode_str = "Unknown";
		break;
	}

	LOG_INF("Current Wi-Fi Mode:  %s", mode_str);
	LOG_INF("==============================================");

#if CONFIG_NRF70_AP_MODE || CONFIG_NRF70_P2P_MODE
	LOG_INF("Type 'app_wifi_mode ["
#if CONFIG_NRF70_AP_MODE
		"softap|"
#endif
		"sta"
#if CONFIG_NRF70_P2P_MODE
		"|p2p_go|p2p_client"
#endif
		"]' to change mode.");
	LOG_INF("==============================================");
#endif

	LOG_INF("Connection instructions:");

	switch (mode) {
	case ZEGO_WIFI_MODE_STA:
		LOG_INF("STA mode - connect using any available option:");

#if CONFIG_NET_L2_WIFI_SHELL
		LOG_INF("----------------------------------------------");
		LOG_INF("[ Shell: one-time connect ]");
		LOG_INF("  wifi connect -s <SSID> -p <pass> -k 1");
		LOG_INF("  wifi connect --help     (more options)");
#if CONFIG_WIFI_CREDENTIALS
		LOG_INF("----------------------------------------------");
		LOG_INF("[ Shell: saved credentials (auto-connect on reboot) ]");
		LOG_INF("  wifi cred add <SSID> WPA2-PSK <pass> -k 1");
		LOG_INF("  wifi cred auto_connect (trigger auto-connect attempt without reboot)");
		LOG_INF("  wifi cred list          (show stored networks)");
		LOG_INF("  wifi cred delete <SSID> (remove a network)");
#endif
#endif

#if CONFIG_ZEGO_WIFI_BLE_PROV
		{
			struct net_if *_iface = net_if_get_first_wifi();
			struct net_linkaddr *_mac = _iface ? net_if_get_link_addr(_iface) : NULL;
			char ble_name[9];

			if (_mac && _mac->len >= 6) {
				snprintf(ble_name, sizeof(ble_name), "PV%02X%02X%02X",
					 _mac->addr[3], _mac->addr[4], _mac->addr[5]);
			} else {
				snprintf(ble_name, sizeof(ble_name), "PVxxxxxx");
			}
			LOG_INF("----------------------------------------------");
			LOG_INF("[ BLE provisioning (saves credentials) ]");
			LOG_INF("  Device name : %s", ble_name);
			LOG_INF("  1. Install 'nRF Wi-Fi Provisioner' on your phone");
			LOG_INF("  2. Open the app and connect to '%s'", ble_name);
			LOG_INF("  3. Select your AP and enter the password");
			LOG_INF("  4. Device connects and saves credentials for reboot");
		}
#endif
		break;

#if CONFIG_NRF70_AP_MODE
	case ZEGO_WIFI_MODE_SOFTAP:
#if defined(CONFIG_APP_WIFI_SSID)
		LOG_INF("Connect to AP SSID='%s' Password='%s'", CONFIG_APP_WIFI_SSID,
			CONFIG_APP_WIFI_PASSWORD);
#else
		LOG_INF("SoftAP: connect to the configured SSID.");
#endif
		break;
#endif /* CONFIG_NRF70_AP_MODE */

#if CONFIG_NRF70_P2P_MODE
	case ZEGO_WIFI_MODE_P2P_GO:
		LOG_INF("P2P_GO mode: P2P group + WPS PIN auto-started at boot.");
		LOG_INF("1. P2P Peer: Turn on Wi-Fi, disconnect from other APs");
		LOG_INF("2. P2P Peer: Wi-Fi Direct -> wait for DK, select it, enter PIN 12345678");
		break;

	case ZEGO_WIFI_MODE_P2P_CLIENT:
		LOG_INF("P2P_CLIENT mode: DK joins P2P Peer's P2P group:");
		LOG_INF("1. DK:    wifi p2p find              -- search for peers");
		LOG_INF("2. Phone: Enable Wi-Fi Direct, wait for DK MAC to appear");
		LOG_INF("3. DK:    wifi p2p peer              -- list peers, find P2P Peer MAC");
		LOG_INF("4. DK:    wifi p2p connect <P2P Peer MAC> pbc -g 0  -- connect");
		LOG_INF("5. Phone: Press ACCEPT on the Wi-Fi Direct invitation");
		break;
#endif /* CONFIG_NRF70_P2P_MODE */

	default:
		break;
	}

	LOG_INF("==============================================");
}

void zego_banner_app_extra(void)
{
	LOG_INF("Compiled modules:");
	LOG_INF("  wifi              (zego)");
#if CONFIG_ZEGO_BUTTON
	LOG_INF("  button            (zego)");
#endif
#if CONFIG_ZEGO_LED
	LOG_INF("  led               (zego)");
#endif
#if CONFIG_WIFI_MODULE
	LOG_INF("  network           (wifi)");
#endif
#if CONFIG_ZEGO_WIFI_BLE_PROV
	LOG_INF("  wifi_ble_prov     (BLE provisioning)");
#endif
#if CONFIG_NTP_MODULE
	LOG_INF("  ntp_sync          (server: %s)", CONFIG_NTP_SERVER);
#endif
#if CONFIG_APP_MEMFAULT_MODULE
	LOG_INF("  app_memfault      (sdk " MEMFAULT_SDK_VERSION_STR ")");
#endif

#if CONFIG_APP_HTTPS_CLIENT_MODULE
	LOG_INF("  app_https_client  (host: %s)", CONFIG_APP_HTTPS_HOSTNAME);
#endif
#if CONFIG_APP_MQTT_CLIENT_MODULE
	LOG_INF("  app_mqtt_client   (broker: %s)", CONFIG_APP_MQTT_CLIENT_BROKER_HOSTNAME);
#endif

#if CONFIG_WEBSERVER_MODULE
	LOG_INF("  webserver");
#endif
#if CONFIG_HEAPS_MONITOR
	LOG_INF("  heap_monitor");
#endif
	LOG_INF("==============================================");
}

/* ============================================================================
 * MAIN APPLICATION ENTRY POINT
 * ============================================================================
 */

static void print_banner(const char *version, const char *prd_version, const char *specs_version)
{
	struct net_if *iface = net_if_get_default();
	struct net_linkaddr *mac = NULL;

	/* Wi-Fi driver populates the link address after SYS_INIT completes.
	 * Poll briefly (up to 200 ms) so main() can still print the MAC. */
	for (int i = 0; i < 20; i++) {
		struct net_linkaddr *candidate = net_if_get_link_addr(iface);

		if (candidate && candidate->len == 6) {
			mac = candidate;
			break;
		}
		k_sleep(K_MSEC(10));
	}

	LOG_INF("==============================================");
	LOG_INF("%s", ZEGO_BANNER_APP_NAME);
	LOG_INF("==============================================");
	LOG_INF("Version: %s", version);
	LOG_INF("PRD:     %s", prd_version);
	LOG_INF("Specs:   %s", specs_version);
	LOG_INF("Build:   %s %s", __DATE__, __TIME__);
	LOG_INF("Board:   %s", get_board_name());

	if (mac) {
		LOG_INF("MAC:     %02X:%02X:%02X:%02X:%02X:%02X", mac->addr[0], mac->addr[1],
			mac->addr[2], mac->addr[3], mac->addr[4], mac->addr[5]);
	}

	LOG_INF("==============================================");
	zego_banner_wifi_info();
	zego_banner_app_extra();
}

void zego_wifi_print_banner(void)
{
	print_banner(APP_VERSION_STRING, PRD_VERSION, SPECS_VERSION);
}
