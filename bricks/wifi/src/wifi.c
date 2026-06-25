/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * wifi.c - application banner callbacks and banner callbacks.
 *
 * Overrides zego_banner_wifi_info() and zego_banner_app_extra() with
 * Kconfig-conditional content, then calls zego_banner_print() and sleeps.
 *
 * Feature gates:
 *   NRF70_AP_MODE          - SoftAP mode available
 *   NRF70_P2P_MODE         - P2P_GO / P2P_GC modes available
 *   NET_L2_WIFI_SHELL      - 'wifi connect' shell command available
 *   WIFI_CREDENTIALS       - 'wifi cred' persistent credentials available
 *   ZEGO_WIFI_BLE_PROV - BLE provisioning available
 */

#include "wifi.h"
#include "console.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
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
	case ZEGO_WIFI_MODE_P2P_GC:
		mode_str = "P2P_GC";
		break;
	default:
		mode_str = "Unknown";
		break;
	}

	LOG_INF("Current Wi-Fi Mode:  " CLR_CYNB "%s" CLR_RST, mode_str);

	/* Only advertise the mode-switch command when more than one mode is
	 * compiled in — a single-mode build has nothing to switch to. */
#define _ZEGO_WIFI_MODE_COUNT                                                                      \
	(IS_ENABLED(CONFIG_ZEGO_WIFI_MODE_STA_ENABLED) +                                           \
	 IS_ENABLED(CONFIG_ZEGO_WIFI_MODE_SOFTAP_ENABLED) +                                        \
	 IS_ENABLED(CONFIG_ZEGO_WIFI_MODE_P2P_GO_ENABLED) +                                        \
	 IS_ENABLED(CONFIG_ZEGO_WIFI_MODE_P2P_GC_ENABLED))

#if _ZEGO_WIFI_MODE_COUNT > 1
	LOG_INF(CLR_PRP "Type 'zego_wifi_mode ["
#if CONFIG_ZEGO_WIFI_MODE_STA_ENABLED
			"sta"
#endif
#if CONFIG_ZEGO_WIFI_MODE_SOFTAP_ENABLED
			"|softap"
#endif
#if CONFIG_ZEGO_WIFI_MODE_P2P_GO_ENABLED
			"|p2p_go"
#endif
#if CONFIG_ZEGO_WIFI_MODE_P2P_GC_ENABLED
			"|p2p_gc"
#endif
			"]' to change mode." CLR_RST);
#endif

	LOG_INF("----------------------------------------------");

	switch (mode) {
	case ZEGO_WIFI_MODE_STA:
		LOG_INF("Connect using any available option for the current Wi-Fi Mode:");

#if CONFIG_NET_L2_WIFI_SHELL
		LOG_INF("----------------------------------------------");
		LOG_INF(CLR_PRP "[ Shell: one-time connect ]" CLR_RST);
		LOG_INF(CLR_PRP "  wifi connect -s <SSID> -p <pass> -k 1" CLR_RST);
		LOG_INF(CLR_PRP "  wifi connect --help     (more options)" CLR_RST);
#if CONFIG_WIFI_CREDENTIALS
		LOG_INF("----------------------------------------------");
		LOG_INF(CLR_PRP "[ Shell: saved credentials (auto-connect on reboot) ]" CLR_RST);
		LOG_INF(CLR_PRP "  wifi cred add <SSID> WPA2-PSK <pass> -k 1" CLR_RST);
		LOG_INF(CLR_PRP "  wifi cred auto_connect (trigger auto-connect attempt without "
				"reboot)" CLR_RST);
		LOG_INF(CLR_PRP "  wifi cred list          (show stored networks)" CLR_RST);
		LOG_INF(CLR_PRP "  wifi cred delete <SSID> (remove a network)" CLR_RST);
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
			LOG_INF(CLR_PRP "[ BLE provisioning (saves credentials) ]" CLR_RST);
			LOG_INF(CLR_PRP "  Device name : %s" CLR_RST, ble_name);
			LOG_INF(CLR_PRP
				"  1. Install 'nRF Wi-Fi Provisioner' on your phone" CLR_RST);
			LOG_INF(CLR_PRP "  2. Open the app and connect to '%s'" CLR_RST, ble_name);
			LOG_INF(CLR_PRP "  3. Select your AP and enter the password" CLR_RST);
			LOG_INF(CLR_PRP
				"  4. Device connects and saves credentials for reboot" CLR_RST);
		}
#endif
		break;

#if CONFIG_NRF70_AP_MODE
	case ZEGO_WIFI_MODE_SOFTAP:
#if defined(CONFIG_APP_WIFI_SSID)
		LOG_INF(CLR_PRP "Connect to AP SSID='%s' Password='%s'" CLR_RST,
			CONFIG_APP_WIFI_SSID, CONFIG_APP_WIFI_PASSWORD);
#else
		LOG_INF("SoftAP: connect to the configured SSID.");
#endif
		break;
#endif /* CONFIG_NRF70_AP_MODE */

#if CONFIG_ZEGO_WIFI_MODE_P2P_GO_ENABLED
	case ZEGO_WIFI_MODE_P2P_GO: {
		struct net_if *_iface = net_if_get_first_wifi();
		struct net_linkaddr *_mac = _iface ? net_if_get_link_addr(_iface) : NULL;
		char mac_str[18] = "XX:XX:XX:XX:XX:XX";

		if (_mac && _mac->len >= 6) {
			snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
				 _mac->addr[0], _mac->addr[1], _mac->addr[2], _mac->addr[3],
				 _mac->addr[4], _mac->addr[5]);
		}
		if (CONFIG_ZEGO_WIFI_P2P_GC_TARGET_GO_MAC[0] != '\0') {
			LOG_INF(CLR_PRP
				"P2P_GC DK is configured to auto-connect to this GO." CLR_RST);
		} else {
			LOG_INF(CLR_PRP "P2P_GC can connect via WPS PIN using one of the options "
					"below:" CLR_RST);
			LOG_INF(CLR_PRP "[ DK as P2P_GC ]" CLR_RST);
			LOG_INF(CLR_PRP
				"  P2P_GC DK:  wifi p2p connect %s pin 12345678 --join" CLR_RST,
				mac_str);
			LOG_INF(CLR_PRP "[ Phone as P2P_GC ]" CLR_RST);
			LOG_INF(CLR_PRP
				"  1. Phone: Turn on Wi-Fi, disconnect from other APs" CLR_RST);
			LOG_INF(CLR_PRP
				"  2. Phone: Wi-Fi Direct -> wait for DK, select it, enter PIN "
				"12345678" CLR_RST);
		}
		break;
	}
#endif /* CONFIG_ZEGO_WIFI_MODE_P2P_GO_ENABLED */

#if CONFIG_ZEGO_WIFI_MODE_P2P_GC_ENABLED
	case ZEGO_WIFI_MODE_P2P_GC:
		if (CONFIG_ZEGO_WIFI_P2P_GC_TARGET_GO_MAC[0] != '\0') {
			LOG_INF(CLR_PRP
				"P2P_GC mode: auto-connecting to GO %s (pin 12345678 --join, "
				"retry every 90 s)" CLR_RST,
				CONFIG_ZEGO_WIFI_P2P_GC_TARGET_GO_MAC);
		} else {
			LOG_INF(CLR_PRP "P2P_GC mode: connect to a P2P_GO DK via WPS PIN:" CLR_RST);
			LOG_INF(CLR_PRP "[ DK GO ]" CLR_RST);
			LOG_INF(CLR_PRP
				"  If you have the GO's MAC(see GO logs), run directly:" CLR_RST);
			LOG_INF(CLR_PRP
				"    wifi p2p connect <GO MAC> pin 12345678 --join" CLR_RST);
			LOG_INF(CLR_PRP "  Otherwise, discover first:" CLR_RST);
			LOG_INF(CLR_PRP "  1. wifi p2p find" CLR_RST);
			LOG_INF(CLR_PRP "  2. wifi p2p peer       (note GO MAC)" CLR_RST);
			LOG_INF(CLR_PRP
				"  3. wifi p2p connect <GO MAC> pin 12345678 --join" CLR_RST);
			LOG_INF(CLR_PRP "[ Phone GO ] (NOT recommended -- Android routing + mDNS "
					"blocked)" CLR_RST);
			LOG_INF(CLR_PRP
				"  1. Phone: Turn on Wi-Fi, disconnect from other APs" CLR_RST);
			LOG_INF(CLR_PRP "  2. wifi p2p find" CLR_RST);
			LOG_INF(CLR_PRP "  3. wifi p2p peer       (find phone MAC)" CLR_RST);
			LOG_INF(CLR_PRP "  4. wifi p2p connect <phone MAC> pbc -g 0" CLR_RST);
			LOG_INF(CLR_PRP "  5. Phone: accept the invitation" CLR_RST);
		}
		break;
#endif /* CONFIG_ZEGO_WIFI_MODE_P2P_GC_ENABLED */

	default:
		break;
	}

	LOG_INF("==============================================");
}

void zego_banner_app_extra(void)
{
	LOG_INF("Compiled modules:");
#if CONFIG_ZEGO_BUTTON
	LOG_INF("  " CLR_BLU "button" CLR_RST "            (zego)");
#endif
#if CONFIG_ZEGO_LED
	LOG_INF("  " CLR_BLU "led" CLR_RST "               (zego)");
#endif
	LOG_INF("  " CLR_BLU "wifi" CLR_RST "              (zego)");

#if CONFIG_WIFI_MODULE
	LOG_INF("  " CLR_BLU "network" CLR_RST "           (zego)");
#endif
#if CONFIG_ZEGO_WIFI_BLE_PROV
	LOG_INF("  " CLR_BLU "wifi_ble_prov" CLR_RST "     (zego)");
#endif
#if CONFIG_ZEGO_MEMONITOR
	LOG_INF("  " CLR_BLU "memonitor" CLR_RST "         (zego)");
#endif
#if CONFIG_NTP_MODULE
	LOG_INF("  " CLR_BLU "ntp_sync" CLR_RST "          (server: %s)", CONFIG_NTP_SERVER);
#endif
#if CONFIG_APP_MEMFAULT_MODULE
	LOG_INF("  " CLR_BLU "app_memfault" CLR_RST "      (sdk " MEMFAULT_SDK_VERSION_STR ")");
#endif

#if CONFIG_APP_HTTPS_CLIENT_MODULE
	LOG_INF("  " CLR_BLU "app_https_client" CLR_RST "  (host: %s)", CONFIG_APP_HTTPS_HOSTNAME);
#endif
#if CONFIG_APP_MQTT_CLIENT_MODULE
	LOG_INF("  " CLR_BLU "app_mqtt_client" CLR_RST "   (broker: %s)",
		CONFIG_APP_MQTT_CLIENT_BROKER_HOSTNAME);
#endif

#if CONFIG_WEBSERVER_MODULE
	LOG_INF("  " CLR_BLU "webserver" CLR_RST "         (port: %d)", CONFIG_WEBSERVER_PORT);
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
	LOG_INF(" " CLR_BLU "%s" CLR_RST, ZEGO_BANNER_APP_NAME);
	LOG_INF("==============================================");
	LOG_INF("Version: " CLR_GRN "%s" CLR_RST, version);
	LOG_INF("PRD:     " CLR_GRN "%s" CLR_RST, prd_version);
	LOG_INF("Specs:   " CLR_GRN "%s" CLR_RST, specs_version);
	LOG_INF("Build:   " CLR_GRN "%s %s" CLR_RST, __DATE__, __TIME__);
	LOG_INF("Board:   " CLR_GRN "%s" CLR_RST, get_board_name());

	if (mac) {
		LOG_INF("MAC:     " CLR_GRN "%02X:%02X:%02X:%02X:%02X:%02X" CLR_RST, mac->addr[0],
			mac->addr[1], mac->addr[2], mac->addr[3], mac->addr[4], mac->addr[5]);
	}

	LOG_INF("==============================================");
	zego_banner_app_extra();
	zego_banner_wifi_info();
}

void zego_wifi_print_banner(void)
{
	/* Flush all pending log messages generated by SYS_INIT modules before
	 * printing the banner.  Without this, the burst of audio/codec/USB init
	 * logs that arrive just after the MAC poll can overflow-evict individual
	 * banner lines (CONFIG_LOG_MODE_OVERFLOW=y drops the oldest pending
	 * message when the ring buffer fills). */
	log_process();
	k_sleep(K_MSEC(50));
	log_process();

	print_banner(APP_VERSION_STRING, CONFIG_ZEGO_APP_PRD_VERSION,
		     CONFIG_ZEGO_APP_SPECS_VERSION);
}
