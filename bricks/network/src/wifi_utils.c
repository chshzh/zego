/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* CONFIG_NET_CONFIG_MY_IPV4_ADDR depends on CONFIG_NET_CONFIG_SETTINGS.
 * Provide a fallback so the P2P_GO / SoftAP code paths compile on apps
 * that do not enable net_config_init (e.g. memfault project). */
#ifndef CONFIG_NET_CONFIG_MY_IPV4_ADDR
#define CONFIG_NET_CONFIG_MY_IPV4_ADDR "192.168.7.1"
#endif

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/wifi_utils.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>

#include <wifi.h>
#include "wifi_utils.h"
#include "net_event_mgmt.h" /* zego_on_net_event_p2p_pairing() */

LOG_MODULE_REGISTER(zego_wifi_utils, CONFIG_ZEGO_NETWORK_LOG_LEVEL);

static char last_connected_ssid[WIFI_SSID_MAX_LEN + 1];

const char *wifi_utils_get_last_ssid(void)
{
	if (last_connected_ssid[0] == '\0') {
		return NULL;
	}
	return last_connected_ssid;
}

int wifi_utils_ensure_gateway_softap_credentials(void)
{
#if !defined(CONFIG_WIFI_CREDENTIALS)
	return -ENOTSUP;
#else
	struct wifi_credentials_personal creds = {0};
	size_t ssid_len = strlen(CONFIG_ZEGO_WIIF_SOFTAP_SSID);
	int ret = wifi_credentials_get_by_ssid_personal_struct(CONFIG_ZEGO_WIIF_SOFTAP_SSID,
							       ssid_len, &creds);

	if (ret == 0) {
		return 0;
	}

	if (ret != -ENOENT) {
		LOG_ERR("Failed to read stored credentials for %s: %d",
			CONFIG_ZEGO_WIIF_SOFTAP_SSID, ret);
		return ret;
	}

	creds.header.type = WIFI_SECURITY_TYPE_PSK;
	memcpy(creds.header.ssid, CONFIG_ZEGO_WIIF_SOFTAP_SSID, ssid_len);
	creds.header.ssid_len = ssid_len;
	creds.password_len = strlen(CONFIG_ZEGO_WIIF_SOFTAP_PASSWORD);
	memcpy(creds.password, CONFIG_ZEGO_WIIF_SOFTAP_PASSWORD, creds.password_len);
	creds.password[creds.password_len] = '\0';

	ret = wifi_credentials_set_personal_struct(&creds);
	if (ret == 0) {
		LOG_INF("Stored default Wi-Fi credentials for %s", CONFIG_ZEGO_WIIF_SOFTAP_SSID);
	} else {
		LOG_ERR("Failed to store default credentials for %s: %d",
			CONFIG_ZEGO_WIIF_SOFTAP_SSID, ret);
	}

	return ret;
#endif
}

int wifi_utils_auto_connect_stored(void)
{
#if !defined(CONFIG_WIFI_CREDENTIALS_CONNECT_STORED)
	return -ENOTSUP;
#else
	struct net_if *iface = net_if_get_first_wifi();
	int ret;

	if (iface == NULL) {
		return -ENODEV;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);
	if (ret == 0) {
		LOG_INF("Auto-connect request issued for stored Wi-Fi credentials");
	} else if (ret != -EALREADY) {
		LOG_WRN("Auto-connect request failed: %d", ret);
	}

	return ret;
#endif
}

int wifi_set_mode(int mode)
{
	struct net_if *iface;
	struct wifi_mode_info mode_info = {0};
	int ret;

	iface = net_if_get_first_wifi();
	if (!iface) {
		LOG_ERR("Failed to get Wi-Fi iface");
		return -ENODEV;
	}

	mode_info.oper = WIFI_MGMT_SET;
	mode_info.if_index = net_if_get_by_iface(iface);
	mode_info.mode = mode;

	ret = net_mgmt(NET_REQUEST_WIFI_MODE, iface, &mode_info, sizeof(mode_info));
	if (ret) {
		LOG_ERR("Mode setting failed: %d", ret);
		return ret;
	}

	LOG_INF("Wi-Fi mode set to %d", mode);
	return 0;
}

int wifi_set_channel(int channel)
{
	struct net_if *iface;
	struct wifi_channel_info channel_info = {0};
	int ret;

	iface = net_if_get_first_wifi();
	if (!iface) {
		LOG_ERR("Failed to get Wi-Fi iface");
		return -ENODEV;
	}

	channel_info.oper = WIFI_MGMT_SET;
	channel_info.if_index = net_if_get_by_iface(iface);
	channel_info.channel = channel;

	if ((channel_info.channel < WIFI_CHANNEL_MIN) ||
	    (channel_info.channel > WIFI_CHANNEL_MAX)) {
		LOG_ERR("Invalid channel number: %d. Range is (%d-%d)", channel, WIFI_CHANNEL_MIN,
			WIFI_CHANNEL_MAX);
		return -EINVAL;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_CHANNEL, iface, &channel_info, sizeof(channel_info));
	if (ret) {
		LOG_ERR("Channel setting failed: %d", ret);
		return ret;
	}

	LOG_INF("Wi-Fi channel set to %d", channel_info.channel);
	return 0;
}

int wifi_set_tx_injection_mode(void)
{
	struct net_if *iface;

	iface = net_if_get_first_wifi();
	if (!iface) {
		LOG_ERR("Failed to get Wi-Fi iface");
		return -ENODEV;
	}

	if (net_eth_txinjection_mode(iface, true)) {
		LOG_ERR("TX Injection mode enable failed");
		return -1;
	}

	LOG_INF("TX Injection mode enabled");
	return 0;
}

#if IS_ENABLED(CONFIG_WIFI_NM_WPA_SUPPLICANT_AP)
int wifi_set_reg_domain(void)
{
	struct net_if *iface;
	struct wifi_reg_domain regd = {0};
	int ret = -1;

	iface = net_if_get_first_wifi();
	if (!iface) {
		LOG_ERR("Failed to get Wi-Fi iface");
		return ret;
	}

	regd.oper = WIFI_MGMT_SET;
	strncpy(regd.country_code, CONFIG_ZEGO_WIIF_SOFTAP_REG_DOMAIN, (WIFI_COUNTRY_CODE_LEN + 1));

	ret = net_mgmt(NET_REQUEST_WIFI_REG_DOMAIN, iface, &regd, sizeof(regd));
	if (ret) {
		LOG_ERR("Cannot set regulatory domain: %d", ret);
	} else {
		LOG_INF("Regulatory domain set to %s", CONFIG_ZEGO_WIIF_SOFTAP_REG_DOMAIN);
	}

	return ret;
}

static int wifi_set_softap(const char *ssid, const char *psk)
{
	struct net_if *iface;
	struct wifi_connect_req_params params = {0};
	int ret;

	iface = net_if_get_first_wifi();
	if (!iface) {
		LOG_ERR("Failed to get Wi-Fi interface");
		return -1;
	}

	params.ssid = (uint8_t *)ssid;
	params.ssid_length = strlen(ssid);
	if (params.ssid_length > WIFI_SSID_MAX_LEN) {
		LOG_ERR("SSID length is too long (max %d characters)", WIFI_SSID_MAX_LEN);
		return -1;
	}
	params.psk = (uint8_t *)psk;
	params.psk_length = strlen(psk);
#if defined(CONFIG_ZEGO_WIIF_SOFTAP_BAND_5_GHZ)
	params.band = WIFI_FREQ_BAND_5_GHZ;
#else
	params.band = WIFI_FREQ_BAND_2_4_GHZ;
#endif
	params.channel = CONFIG_ZEGO_WIIF_SOFTAP_CHANNEL;

	if (!wifi_utils_validate_chan(params.band, params.channel)) {
		LOG_ERR("Invalid SoftAP channel %d for Wi-Fi band %d", params.channel, params.band);
		return -EINVAL;
	}
	params.security = WIFI_SECURITY_TYPE_PSK;

	ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &params,
		       sizeof(struct wifi_connect_req_params));
	if (ret) {
		LOG_ERR("SoftAP mode enable failed: %s", strerror(-ret));
		return ret;
	}

	LOG_INF("SoftAP AP_ENABLE request accepted (band=%d channel=%d)", params.band,
		params.channel);
	return 0;
}

static bool dhcp_server_started;

int wifi_setup_dhcp_server(void)
{
	struct net_if *iface;
	struct in_addr gw_addr, netmask;
#if IS_ENABLED(CONFIG_NET_DHCPV4_SERVER)
	struct in_addr pool_start;
	int ret;
#endif

	LOG_INF("DHCP server setup starting...");

	if (dhcp_server_started) {
		LOG_WRN("DHCP server already started");
		return 0;
	}

	iface = net_if_get_first_wifi();
	if (!iface) {
		LOG_ERR("Failed to get Wi-Fi interface");
		return -1;
	}

	/* Ensure the gateway static IP is assigned to the Wi-Fi interface.
	 * net_config_init (prio 95) may assign it to a different iface if the
	 * Wi-Fi interface is not yet up.  net_dhcpv4_server_start requires the
	 * server address to already be present on the iface. */
	zsock_inet_pton(AF_INET, CONFIG_NET_CONFIG_MY_IPV4_ADDR, &gw_addr);
	zsock_inet_pton(AF_INET, "255.255.255.0", &netmask);
	net_if_ipv4_addr_rm(iface, &gw_addr);
	struct net_if_addr *ifaddr = net_if_ipv4_addr_add(iface, &gw_addr, NET_ADDR_MANUAL, 0);

	if (!ifaddr) {
		LOG_ERR("Failed to assign %s to Wi-Fi interface", CONFIG_NET_CONFIG_MY_IPV4_ADDR);
		return -EINVAL;
	}
	net_if_ipv4_set_netmask_by_addr(iface, &gw_addr, &netmask);
	LOG_INF("Static IP %s/24 assigned to Wi-Fi interface", CONFIG_NET_CONFIG_MY_IPV4_ADDR);

#if IS_ENABLED(CONFIG_NET_DHCPV4_SERVER)
	ret = zsock_inet_pton(AF_INET, "192.168.7.2", &pool_start);
	if (ret != 1) {
		LOG_ERR("Invalid DHCP pool start address");
		return -1;
	}

	ret = net_dhcpv4_server_start(iface, &pool_start);
	if (ret == -EALREADY) {
		LOG_INF("DHCP server already running");
		dhcp_server_started = true;
		return 0;
	} else if (ret < 0) {
		LOG_ERR("Failed to start DHCP server: %d", ret);
		return ret;
	}

	dhcp_server_started = true;
	LOG_INF("DHCP server started with pool starting at 192.168.7.2");
#else
	LOG_WRN("DHCP server not compiled in — skipping server start");
#endif
	return 0;
}

/* ============================================================================
 * P2P_GC STATIC IP
 * ============================================================================
 */

int wifi_p2p_gc_setup_static_ip(struct net_if *iface)
{
	struct in_addr client_ip, netmask, cfg_ip;

	zsock_inet_pton(AF_INET, "192.168.7.2", &client_ip);
	zsock_inet_pton(AF_INET, "255.255.255.0", &netmask);

	/* Stop DHCP so it doesn't race against our static assignment.
	 * net_config_init calls net_dhcpv4_start() at boot; stopping it here
	 * prevents DHCP from sending DISCOVER packets and competing for the
	 * single IPv4 address slot (CONFIG_NET_IF_MAX_IPV4_COUNT=1). */
	net_dhcpv4_stop(iface);

	/* net_config_init pre-assigns CONFIG_NET_CONFIG_MY_IPV4_ADDR ("192.168.7.1")
	 * as NET_ADDR_OVERRIDABLE at boot, filling the only IPv4 slot.
	 * Remove it so the add below can claim the slot.
	 * Also remove any leftover 192.168.7.2 from a prior connect attempt. */
	zsock_inet_pton(AF_INET, "192.168.7.1", &cfg_ip);
	net_if_ipv4_addr_rm(iface, &cfg_ip);
	net_if_ipv4_addr_rm(iface, &client_ip);

	struct net_if_addr *ifaddr = net_if_ipv4_addr_add(iface, &client_ip, NET_ADDR_MANUAL, 0);

	if (!ifaddr) {
		LOG_ERR("P2P_GC: failed to assign 192.168.7.2/24 to %s",
			net_if_get_device(iface) ? net_if_get_device(iface)->name : "?");
		return -EINVAL;
	}

	net_if_ipv4_set_netmask_by_addr(iface, &client_ip, &netmask);
	LOG_INF("P2P_GC: static IP 192.168.7.2/24 assigned to %s",
		net_if_get_device(iface) ? net_if_get_device(iface)->name : "?");
	return 0;
}

/* ============================================================================
 * SOFTAP PERIODIC REMINDER
 * Logs SSID/password every 300 s until the first client connects.
 * ============================================================================
 */
#define SOFTAP_REMIND_TIMEOUT_S 300

static struct k_work_delayable softap_remind_work;

static void softap_remind_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_WRN("SoftAP: no client in %d s  SSID='%s'  Password='%s'  IP=%s",
		SOFTAP_REMIND_TIMEOUT_S, CONFIG_ZEGO_WIIF_SOFTAP_SSID,
		CONFIG_ZEGO_WIIF_SOFTAP_PASSWORD,
#if defined(CONFIG_NET_CONFIG_MY_IPV4_ADDR)
		CONFIG_NET_CONFIG_MY_IPV4_ADDR
#else
		"(unconfigured)"
#endif
	);
	k_work_schedule(&softap_remind_work, K_SECONDS(SOFTAP_REMIND_TIMEOUT_S));
}

void wifi_softap_cancel_remind_timer(void)
{
	k_work_cancel_delayable(&softap_remind_work);
}

int wifi_run_softap_mode(void)
{
	int ret;

	LOG_INF("Setting up SoftAP mode: SSID='%s' band=%s channel=%d",
		CONFIG_ZEGO_WIIF_SOFTAP_SSID,
		IS_ENABLED(CONFIG_ZEGO_WIIF_SOFTAP_BAND_5_GHZ) ? "5 GHz" : "2.4 GHz",
		CONFIG_ZEGO_WIIF_SOFTAP_CHANNEL);

	/* Stop the DHCP client that net_config starts at boot.
	 * In SoftAP mode the AP device is the DHCP server, not a client.
	 * Without this, the client loops sending failed DISCOVERs which
	 * exhausts TX packet buffers and causes the DHCP server to fail
	 * to send OFFER messages with -ENOMEM. */
	struct net_if *iface = net_if_get_first_wifi();

	if (iface) {
		net_dhcpv4_stop(iface);
	}

	ret = wifi_set_reg_domain();
	if (ret) {
		LOG_ERR("Failed to set regulatory domain: %d", ret);
		return ret;
	}

	ret = wifi_setup_dhcp_server();
	if (ret) {
		LOG_WRN("DHCP server setup failed (%d), continuing to enable AP", ret);
	}

	ret = wifi_set_softap(CONFIG_ZEGO_WIIF_SOFTAP_SSID, CONFIG_ZEGO_WIIF_SOFTAP_PASSWORD);
	if (ret) {
		LOG_ERR("Failed to setup SoftAP: %d", ret);
		return ret;
	}

	/* Arm periodic reminder until first client connects */
	k_work_init_delayable(&softap_remind_work, softap_remind_handler);
	k_work_schedule(&softap_remind_work, K_SECONDS(SOFTAP_REMIND_TIMEOUT_S));

	return 0;
}

#else /* !CONFIG_WIFI_NM_WPA_SUPPLICANT_AP - stubs */

int wifi_set_reg_domain(void)
{
	return -ENOTSUP;
}

int wifi_setup_dhcp_server(void)
{
	return -ENOTSUP;
}

void wifi_softap_cancel_remind_timer(void)
{
}

int wifi_run_softap_mode(void)
{
	LOG_WRN("SoftAP requires CONFIG_WIFI_NM_WPA_SUPPLICANT_AP");
	return -ENOTSUP;
}

#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT_AP */

int wifi_print_status(void)
{
	struct net_if *iface;
	struct wifi_iface_status status = {0};
	int ret;

	iface = net_if_get_first_wifi();
	if (!iface) {
		LOG_ERR("Failed to get Wi-Fi interface");
		return -ENODEV;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status,
		       sizeof(struct wifi_iface_status));
	if (ret) {
		LOG_ERR("Status request failed: %d", ret);
		return ret;
	}

	LOG_INF("Wi-Fi Status:");
	LOG_INF("  State: %s", wifi_state_txt(status.state));

	if (status.state >= WIFI_STATE_ASSOCIATED) {
		strncpy(last_connected_ssid, status.ssid, WIFI_SSID_MAX_LEN);
		last_connected_ssid[WIFI_SSID_MAX_LEN] = '\0';
		LOG_INF("  Mode: %s", wifi_mode_txt(status.iface_mode));
		LOG_INF("  SSID: %.32s", status.ssid);
		LOG_INF("  BSSID: %02x:%02x:%02x:%02x:%02x:%02x", status.bssid[0], status.bssid[1],
			status.bssid[2], status.bssid[3], status.bssid[4], status.bssid[5]);
		LOG_INF("  Band: %s", wifi_band_txt(status.band));
		LOG_INF("  Channel: %d", status.channel);
		LOG_INF("  Security: %s", wifi_security_txt(status.security));
		LOG_INF("  RSSI: %d dBm", status.rssi);
	} else {
		last_connected_ssid[0] = '\0';
	}

	return 0;
}

/* ============================================================================
 * P2P_GO AUTO-START: group_add + WPS PBC
 * ============================================================================
 *
 * Boot sequence:
 *   1. group_add   - create an autonomous (non-persistent) P2P group.
 *                    CONNECT_RESULT fires once the group is up; net_event_mgmt.c
 *                    starts the DHCP server there.
 *   2. wps_pbc     - arm WPS PBC (WIFI_WPS_PBC), continuously re-armed.  A P2P_GC
 *                    DK joins with:
 *                      wifi p2p connect <THIS_MAC> pbc --join
 *                    PBC needs no out-of-band secret, so button-only pairing
 *                    works.  A FIXED PIN (WIFI_WPS_PIN_SET) is NOT used: it fails
 *                    the WPS Registrar init on the nRF GO and tears down the AP
 *                    interface.  The nRF GO supports PBC or a GO-generated random
 *                    PIN (WIFI_WPS_PIN_GET) only - see nrf/samples/wifi/p2p.
 *
 * NOTE: P2P_FIND (discovery scan) must NOT be called on a running GO.
 * A GO is already beaconing on its operating channel; clients find it by
 * scanning social channels (1/6/11) and reading the P2P IE in the Probe
 * Response.  Calling P2P_FIND while a GO is active always fails with
 * "Failed to start p2p_scan" because the radio is anchored to the GO channel.
 *
 * wps_pin calls deep into wpa_supplicant and overflows the 2 KB sysworkq
 * stack.  A dedicated 4 KB work queue is used for that call.
 */

#if defined(CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P) && defined(CONFIG_WIFI_NM_WPA_SUPPLICANT_WPS)

#define P2P_WPS_WORKQ_STACK_SIZE 4096
K_THREAD_STACK_DEFINE(p2p_wps_workq_stack, P2P_WPS_WORKQ_STACK_SIZE);
static struct k_work_q p2p_wps_workq;
static bool p2p_wps_workq_started;
static struct k_work_delayable p2p_go_wps_retry_work;
static struct k_work_delayable p2p_go_pair_window_work;

/* Re-arm WPS PBC after this many seconds so the GO stays connectable. */
#define P2P_GO_WPS_REARM_INTERVAL_S 300

/* How long the LED breathes after a GO pairing double-click (UX cue only — the
 * GO actually stays connectable continuously). */
#define P2P_GO_PAIR_WINDOW_S 120

/* Pairing window elapsed: stop the BREATHE indication. zego_on_net_event_p2p_pairing
 * resolves the LED back to CONNECTED (if a client joined) or ROTATE otherwise. */
static void p2p_go_pair_window_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	zego_on_net_event_p2p_pairing(false);
}

static void p2p_go_set_wps_pin_handler(struct k_work *work)
{
	struct net_if *iface = net_if_get_first_wifi();

	if (!iface) {
		LOG_ERR("P2P_GO: no Wi-Fi iface");
		return;
	}

	/* Arm WPS PBC (Push Button Config).  The nRF GO supports PBC and
	 * GO-generated PIN (WIFI_WPS_PIN_GET); it does NOT support a fixed PIN via
	 * WIFI_WPS_PIN_SET - that fails the WPS Registrar init and tears down the
	 * AP interface.  PBC is the headless-friendly method: the GC joins with
	 * 'pbc --join' and no PIN needs to be conveyed out of band. */
	struct wifi_wps_config_params wps = {.oper = WIFI_WPS_PBC};

	int ret = net_mgmt(NET_REQUEST_WIFI_WPS_CONFIG, iface, &wps, sizeof(wps));

	if (ret) {
		LOG_ERR("P2P_GO: WPS PBC arm failed (%d)", ret);
	} else {
		LOG_INF("P2P_GO: WPS PBC armed - GC can join with 'pbc --join'");
		LOG_INF("P2P_GO: GO is beaconing - clients discover via Wi-Fi Direct scan");
	}

	/* Re-arm periodically so the GO remains connectable after the PBC walk
	 * timer expires.  DO NOT call P2P_FIND here - it always fails on a running
	 * GO because the radio is anchored to the group channel. */
	k_work_reschedule_for_queue(&p2p_wps_workq, k_work_delayable_from_work(work),
				    K_SECONDS(P2P_GO_WPS_REARM_INTERVAL_S));
}

void wifi_p2p_go_cancel_wps_timer(void)
{
	if (p2p_wps_workq_started) {
		k_work_cancel_delayable(&p2p_go_wps_retry_work);
	}
}

void wifi_p2p_go_rearm_wps_pin(void)
{
	if (!p2p_wps_workq_started) {
		return;
	}
	LOG_INF("P2P_GO: re-arming WPS PBC for client (re)connect");
	k_work_reschedule_for_queue(&p2p_wps_workq, &p2p_go_wps_retry_work, K_NO_WAIT);
}

/* P2P_GO double-click: refresh the PBC window and start the LED-breathe cue for
 * P2P_GO_PAIR_WINDOW_S (the GO stays connectable continuously; the window is a
 * UX cue). The window-end work clears the breathe. */
static void wifi_p2p_go_open_pairing_window(void)
{
	LOG_INF("P2P_GO: pairing window open (~%d s) - double-click a P2P_GC to pair",
		P2P_GO_PAIR_WINDOW_S);
	wifi_p2p_go_rearm_wps_pin();
	zego_on_net_event_p2p_pairing(true);
	if (p2p_wps_workq_started) {
		k_work_reschedule_for_queue(&p2p_wps_workq, &p2p_go_pair_window_work,
					    K_SECONDS(P2P_GO_PAIR_WINDOW_S));
	}
}

int wifi_run_p2p_go_mode(void)
{
	struct net_if *iface = net_if_get_first_wifi();

	if (!iface) {
		LOG_ERR("P2P_GO: no Wi-Fi interface");
		return -ENODEV;
	}

	LOG_INF("P2P_GO: creating group...");

	struct wifi_p2p_params p2p = {
		.oper = WIFI_P2P_GROUP_ADD,
		.group_add = {.persistent = -1},
	};

	int ret = net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface, &p2p, sizeof(p2p));

	if (ret) {
		LOG_ERR("P2P_GO: group_add failed (%d)", ret);
		return ret;
	}

	LOG_INF("P2P_GO: group created - arming WPS PBC");

	if (!p2p_wps_workq_started) {
		k_work_queue_init(&p2p_wps_workq);
		k_work_queue_start(&p2p_wps_workq, p2p_wps_workq_stack,
				   K_THREAD_STACK_SIZEOF(p2p_wps_workq_stack), K_PRIO_COOP(7),
				   NULL);
		p2p_wps_workq_started = true;
	}
	k_work_init_delayable(&p2p_go_wps_retry_work, p2p_go_set_wps_pin_handler);
	k_work_init_delayable(&p2p_go_pair_window_work, p2p_go_pair_window_handler);
	k_work_schedule_for_queue(&p2p_wps_workq, &p2p_go_wps_retry_work, K_NO_WAIT);

	return 0;
}

#else /* fallback stubs when P2P or WPS not enabled */

int wifi_run_p2p_go_mode(void)
{
	LOG_WRN("P2P_GO requires CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P and "
		"CONFIG_WIFI_NM_WPA_SUPPLICANT_WPS");
	return -ENOTSUP;
}

void wifi_p2p_go_cancel_wps_timer(void)
{
}

void wifi_p2p_go_rearm_wps_pin(void)
{
}

#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P && CONFIG_WIFI_NM_WPA_SUPPLICANT_WPS */

/* ============================================================================
 * P2P_GC PAIRING + AUTO-RECONNECT
 * ============================================================================
 *
 * There is no compile-time GO MAC.  The GC learns its GO at runtime:
 *
 *   Pairing (button):  wifi_p2p_start_pairing() runs a P2P_FIND, picks the
 *                      best-RSSI peer that is a P2P GO (group_capab GO bit),
 *                      joins it with 'wifi p2p connect <MAC> pbc --join', and on
 *                      CONNECT_RESULT success persists the GO MAC to NVS
 *                      (settings key "net/p2p_gc_go_mac").
 *
 *   Reconnect:         at boot, and after any disconnect, the GC connects
 *                      directly to the saved GO MAC, retrying until success.
 *                      The GO keeps its WPS PBC armed continuously, so this
 *                      succeeds with no button press after a power cycle.
 *
 * WPS PBC is used, not a fixed PIN: WIFI_WPS_PIN_SET fails the WPS Registrar
 * init on the nRF GO; PBC is the supported headless method (see nrf/samples/wifi/p2p).
 *
 * State:
 *   - p2p_pairing_active routes p2p_gc_do_connect() into discovery+learn;
 *     otherwise it does a direct connect to saved_go_mac (reconnect).
 *   - p2p_gc_pending blocks new connect commands while one is in flight,
 *     avoiding "scan already in progress" errors from wpa_supplicant.
 *
 * All net_mgmt P2P calls run on a dedicated 4 KB work queue (they can overflow
 * the 2 KB sysworkq stack, same as the GO WPS work above).
 */

#if defined(CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P)

#define P2P_CLI_WORKQ_STACK_SIZE 4096
K_THREAD_STACK_DEFINE(p2p_cli_workq_stack, P2P_CLI_WORKQ_STACK_SIZE);
static struct k_work_q p2p_cli_workq;
static bool p2p_cli_workq_started;
static struct k_work_delayable p2p_gc_retry_work;

static bool p2p_gc_connected;
static bool p2p_gc_pending;     /* connect cmd accepted, waiting for result */
static bool p2p_find_running;   /* WIFI_P2P_FIND is currently active */
static bool p2p_pairing_active; /* button-triggered pairing in progress */

static uint8_t saved_go_mac[6];     /* learned GO MAC, persisted in NVS */
static bool have_saved_go;          /* saved_go_mac is valid */
static uint8_t pending_go_mac[6];   /* GO selected during the current pairing */
static bool have_pending_mac;       /* pending_go_mac is valid (pairing connect) */
static uint8_t pairing_find_cycles; /* empty discovery cycles in this pairing */

/* wpa_supplicant --join makes exactly P2P_MAX_JOIN_SCAN_ATTEMPTS (10) scan
 * attempts, each ~4.6 s + 1 s retry gap, then emits GROUP_FORMATION_FAILURE
 * and goes idle - no CONNECT_RESULT is ever sent.
 * Empirical total (nRF7002): scan 1 starts at T≈1.3 s, scan 10 ends at
 * T≈63 s → GROUP_FORMATION_FAILURE at T≈64 s.
 * 90 s > 64 s, so wpa_supplicant has been idle ≥ 26 s before we restart,
 * guaranteeing no "scan already in progress" on the fresh P2P_CONNECT. */
#define P2P_GC_CONNECT_TIMEOUT_S 90

/* After a clean deauth wpa_supplicant runs a background cleanup scan
 * (~5-17 s on nRF7002).  Wait this long before re-issuing P2P_CONNECT. */
#define P2P_GC_RECONNECT_DELAY_S 15

/* Pairing find window: social-channel scan duration.  After this elapses we
 * stop the find and query the wpa_supplicant peer table directly.  We do NOT
 * rely on NET_EVENT_WIFI_P2P_DEVICE_FOUND: that event fires only for newly-
 * discovered peers; a GO already in wpa_supplicant's peer cache is not
 * re-reported even though it is visible in 'wifi p2p peer'. */
#define P2P_PAIR_FIND_TIMEOUT_S 10

/* Size of the local peer-query buffer used after P2P_FIND completes. */
#define P2P_PAIR_MAX_CANDIDATES 5

/* Give up a pairing attempt after this many empty discovery cycles. */
#define P2P_PAIR_MAX_FIND_CYCLES 2

/* ---- NVS persistence of the learned GO MAC (settings subtree "net") ----
 * A separate subtree from the wifi mode selector's "app" handler: two static
 * handlers cannot share one subtree name. */
#define P2P_GC_GO_MAC_KEY "net/p2p_gc_go_mac"

static int net_settings_set_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (strcmp(key, "p2p_gc_go_mac") == 0 && len == sizeof(saved_go_mac)) {
		uint8_t buf[6];
		ssize_t rc = read_cb(cb_arg, buf, sizeof(buf));

		if (rc == (ssize_t)sizeof(buf)) {
			memcpy(saved_go_mac, buf, sizeof(saved_go_mac));
			have_saved_go = true;
		}
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(zego_net_settings, "net", NULL, net_settings_set_cb, NULL, NULL);

static void p2p_gc_load_saved_go(void)
{
	/* settings_subsys_init() is idempotent; the wifi mode selector (SYS_INIT
	 * priority 0) has already run it, but call it for self-containment. */
	(void)settings_subsys_init();
	(void)settings_load_subtree("net"); /* triggers net_settings_set_cb */
}

static void p2p_gc_save_go(const uint8_t mac[6])
{
	int ret = settings_save_one(P2P_GC_GO_MAC_KEY, mac, 6);

	if (ret) {
		LOG_ERR("P2P_GC: failed to persist GO MAC: %d", ret);
	} else {
		LOG_INF("P2P_GC: saved GO %02X:%02X:%02X:%02X:%02X:%02X to NVS", mac[0], mac[1],
			mac[2], mac[3], mac[4], mac[5]);
	}
}

/* Issue 'wifi p2p connect <mac> pbc --join'.  Returns net_mgmt rc.
 * PBC (not PIN): the nRF GO accepts WPS PBC; a fixed PIN is unsupported. */
static int p2p_gc_issue_connect(struct net_if *iface, const uint8_t mac[6])
{
	struct wifi_p2p_params params = {0};

	memcpy(params.peer_addr, mac, 6);
	params.oper = WIFI_P2P_CONNECT;
	params.connect.method = WIFI_P2P_METHOD_PBC;
	params.connect.join = true;

	return net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface, &params, sizeof(params));
}

/* Mark a connect as in-flight and arm the timeout retry. */
static void p2p_gc_arm_timeout(struct k_work *work)
{
	p2p_gc_pending = true;
	k_work_reschedule_for_queue(&p2p_cli_workq, k_work_delayable_from_work(work),
				    K_SECONDS(P2P_GC_CONNECT_TIMEOUT_S));
}

static void p2p_gc_do_connect(struct k_work *work)
{
	if (p2p_gc_connected) {
		/* Re-pair requested while connected: drop the current group first.
		 * The DISCONNECT_RESULT handler reschedules into pairing discovery
		 * because p2p_pairing_active is set.  This net_mgmt call runs on the
		 * 4 KB P2P work queue (not the button listener), as required. */
		if (p2p_pairing_active) {
			struct net_if *iface = net_if_get_first_wifi();

			LOG_INF("P2P_GC: re-pairing - disconnecting current GO first");
			if (iface) {
				(void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
			}
		}
		return;
	}

	if (p2p_gc_pending) {
		/* 90 s with no CONNECT_RESULT: wpa_supplicant exhausted its 10
		 * join-scan attempts and is now idle.  Reset and retry. */
		LOG_INF("P2P_GC: %ds timeout, GO not reachable - restarting",
			P2P_GC_CONNECT_TIMEOUT_S);
		p2p_gc_pending = false;
		p2p_find_running = false;
	}

	struct net_if *iface = net_if_get_first_wifi();

	if (!iface) {
		LOG_ERR("P2P_GC: no Wi-Fi iface, retry in 10 s");
		k_work_reschedule_for_queue(&p2p_cli_workq, k_work_delayable_from_work(work),
					    K_SECONDS(10));
		return;
	}

	/* ---- Pairing: discover GOs, pick best RSSI, connect, learn MAC ---- */
	if (p2p_pairing_active) {
		if (!p2p_find_running) {
			struct wifi_p2p_params find_params = {0};

			find_params.oper = WIFI_P2P_FIND;
			find_params.discovery_type = WIFI_P2P_FIND_ONLY_SOCIAL;
			find_params.timeout = P2P_PAIR_FIND_TIMEOUT_S;

			int ret = net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface, &find_params,
					   sizeof(find_params));

			if (ret) {
				LOG_WRN("P2P_GC: pairing find failed (%d), retry in 10 s", ret);
				k_work_reschedule_for_queue(&p2p_cli_workq,
							    k_work_delayable_from_work(work),
							    K_SECONDS(10));
				return;
			}
			p2p_find_running = true;
			LOG_INF("P2P_GC: pairing - peer discovery (%d s)", P2P_PAIR_FIND_TIMEOUT_S);
			k_work_reschedule_for_queue(&p2p_cli_workq,
						    k_work_delayable_from_work(work),
						    K_SECONDS(P2P_PAIR_FIND_TIMEOUT_S + 2));
			return;
		}

		/* Find window elapsed: query the wpa_supplicant peer table directly.
		 * DEVICE_FOUND events are unreliable - they only fire for newly-
		 * discovered peers; cached peers are silently skipped even though
		 * they appear in 'wifi p2p peer'. */
		p2p_find_running = false;

		static struct wifi_p2p_device_info peer_buf[P2P_PAIR_MAX_CANDIDATES];
		struct wifi_p2p_params qparams = {0};

		memset(peer_buf, 0, sizeof(peer_buf));
		qparams.peers = peer_buf;
		qparams.oper = WIFI_P2P_PEER;
		qparams.peer_count = P2P_PAIR_MAX_CANDIDATES;
		memset(qparams.peer_addr, 0xFF, 6); /* broadcast = all peers */

		int qret = net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface, &qparams, sizeof(qparams));

		if (qret) {
			LOG_WRN("P2P_GC: peer table query failed (%d), retry in 10 s", qret);
			k_work_reschedule_for_queue(
				&p2p_cli_workq, k_work_delayable_from_work(work), K_SECONDS(10));
			return;
		}

		/* Select the strongest-RSSI peer that is actually a P2P Group Owner.
		 * P2P Group Capability bit 0 (P2P_GROUP_CAPAB_GROUP_OWNER) is set on a
		 * device currently acting as a GO.  Filtering on it avoids locking onto
		 * nearby non-GO P2P devices (phones mid-discovery, etc.). */
#define P2P_GROUP_CAPAB_GROUP_OWNER BIT(0)
		LOG_INF("P2P_GC: peer table has %d entries, selecting GO", qparams.peer_count);

		int best = -1;

		for (int i = 0; i < qparams.peer_count; i++) {
			bool is_go = (peer_buf[i].group_capab & P2P_GROUP_CAPAB_GROUP_OWNER) != 0;

			LOG_INF("P2P_GC:   [%d] %02X:%02X:%02X:%02X:%02X:%02X RSSI=%d GO=%d", i,
				peer_buf[i].mac[0], peer_buf[i].mac[1], peer_buf[i].mac[2],
				peer_buf[i].mac[3], peer_buf[i].mac[4], peer_buf[i].mac[5],
				peer_buf[i].rssi, is_go);
			if (!is_go) {
				continue; /* skip non-GO P2P devices */
			}
			if (best < 0 || peer_buf[i].rssi > peer_buf[best].rssi) {
				best = i;
			}
		}

		if (best < 0) {
			pairing_find_cycles++;
			if (pairing_find_cycles >= P2P_PAIR_MAX_FIND_CYCLES) {
				LOG_WRN("P2P_GC: no GO found while pairing - giving up");
				p2p_pairing_active = false;
				zego_on_net_event_p2p_pairing(false); /* stop LED breathe */
				if (have_saved_go) {
					/* fall back to reconnecting the previous GO */
					k_work_reschedule_for_queue(
						&p2p_cli_workq, k_work_delayable_from_work(work),
						K_SECONDS(P2P_GC_RECONNECT_DELAY_S));
				}
				return;
			}
			LOG_WRN("P2P_GC: no GO found yet, retrying discovery (%u/%u)",
				pairing_find_cycles, P2P_PAIR_MAX_FIND_CYCLES);
			k_work_reschedule_for_queue(&p2p_cli_workq,
						    k_work_delayable_from_work(work), K_SECONDS(2));
			return;
		}

		memcpy(pending_go_mac, peer_buf[best].mac, 6);
		have_pending_mac = true;

		LOG_INF("P2P_GC: pairing with GO %02X:%02X:%02X:%02X:%02X:%02X RSSI=%d dBm",
			pending_go_mac[0], pending_go_mac[1], pending_go_mac[2], pending_go_mac[3],
			pending_go_mac[4], pending_go_mac[5], peer_buf[best].rssi);

		int ret = p2p_gc_issue_connect(iface, pending_go_mac);

		if (ret) {
			LOG_WRN("P2P_GC: pairing connect rejected (%d), retry in 10 s", ret);
			have_pending_mac = false;
			k_work_reschedule_for_queue(
				&p2p_cli_workq, k_work_delayable_from_work(work), K_SECONDS(10));
		} else {
			LOG_INF("P2P_GC: connect initiated -> pbc --join");
			p2p_gc_arm_timeout(work);
		}
		return;
	}

	/* ---- Reconnect: direct connect to the saved GO MAC ---- */
	if (have_saved_go) {
		have_pending_mac = false; /* reconnect, not a learning connect */

		int ret = p2p_gc_issue_connect(iface, saved_go_mac);

		if (ret) {
			LOG_WRN("P2P_GC: reconnect to %02X:%02X:%02X:%02X:%02X:%02X rejected (%d), "
				"retry in 10 s",
				saved_go_mac[0], saved_go_mac[1], saved_go_mac[2], saved_go_mac[3],
				saved_go_mac[4], saved_go_mac[5], ret);
			k_work_reschedule_for_queue(
				&p2p_cli_workq, k_work_delayable_from_work(work), K_SECONDS(10));
		} else {
			LOG_INF("P2P_GC: reconnecting to saved GO "
				"%02X:%02X:%02X:%02X:%02X:%02X (pbc --join)",
				saved_go_mac[0], saved_go_mac[1], saved_go_mac[2], saved_go_mac[3],
				saved_go_mac[4], saved_go_mac[5]);
			p2p_gc_arm_timeout(work);
		}
		return;
	}

	/* No saved GO and not pairing: stay idle until a pairing double-click. */
	LOG_DBG("P2P_GC: idle (no saved GO; double-click Button 0 to pair)");
}

/* Begin a button-triggered pairing.  Only sets state and schedules work - this
 * may run in the button ZBUS_LISTENER context, so it must not call net_mgmt
 * directly (those P2P calls need the 4 KB work queue).  The actual discovery,
 * and the disconnect-first step when re-pairing while connected, run inside
 * p2p_gc_do_connect() on p2p_cli_workq. */
static void p2p_gc_start_pairing_internal(void)
{
	/* Ignore repeat double-clicks while a pairing is already in flight - a
	 * fresh P2P_FIND/CONNECT issued mid-operation fails with "Scan already in
	 * progress".  A re-pair while *connected* is still allowed (pairing_active
	 * is false then) and overwrites the saved GO. */
	if (p2p_pairing_active || p2p_find_running || p2p_gc_pending) {
		LOG_INF("P2P_GC: pairing already in progress - ignoring double-click");
		return;
	}

	LOG_INF("P2P_GC: pairing requested - searching for a P2P_GO");
	p2p_pairing_active = true;
	p2p_gc_pending = false;
	p2p_find_running = false;
	have_pending_mac = false;
	pairing_find_cycles = 0;

	/* Start the LED-breathe cue immediately on the double-click. */
	zego_on_net_event_p2p_pairing(true);

	k_work_reschedule_for_queue(&p2p_cli_workq, &p2p_gc_retry_work, K_NO_WAIT);
}

void wifi_p2p_gc_on_peer_found(const uint8_t *mac, int8_t rssi)
{
	/* Pairing uses a direct peer-table query after P2P_FIND, not these events
	 * (cached peers do not re-fire DEVICE_FOUND).  Log for diagnostics only. */
	LOG_DBG("P2P_GC: peer seen %02X:%02X:%02X:%02X:%02X:%02X RSSI=%d", mac[0], mac[1], mac[2],
		mac[3], mac[4], mac[5], rssi);
}

void wifi_p2p_gc_on_connect_result(bool success)
{
	p2p_gc_pending = false;

	if (success) {
		p2p_gc_connected = true;
		k_work_cancel_delayable(&p2p_gc_retry_work);

		/* If this connect was a pairing attempt, persist the learned GO MAC.
		 * This overwrites any previously-saved GO - re-pairing forgets the
		 * old one (PRD FR-107 (6)). */
		if (p2p_pairing_active && have_pending_mac) {
			memcpy(saved_go_mac, pending_go_mac, sizeof(saved_go_mac));
			have_saved_go = true;
			p2p_gc_save_go(saved_go_mac);
		}
		p2p_pairing_active = false;
		have_pending_mac = false;
		LOG_INF("P2P_GC: connected to GO - auto-retry stopped");
	} else {
		/* Failure: keep p2p_pairing_active as-is so the retry repeats the
		 * same phase (pairing discovery, or reconnect to saved MAC). */
		p2p_find_running = false;
		have_pending_mac = false;
		k_work_reschedule_for_queue(&p2p_cli_workq, &p2p_gc_retry_work, K_SECONDS(10));
	}
}

void wifi_p2p_gc_on_disconnect(void)
{
	/* Ignore spurious disconnects when we were neither connected nor pairing
	 * (e.g. the late AP-inactivity deauth that can fire minutes later). */
	if (!p2p_gc_connected && !p2p_pairing_active) {
		return;
	}

	p2p_gc_connected = false;
	p2p_gc_pending = false;
	p2p_find_running = false;

	/* Wait before retrying: after a clean deauth wpa_supplicant runs a
	 * background cleanup scan (~5-17 s on nRF7002); issuing P2P_CONNECT or
	 * P2P_FIND too soon races with it ("Scan already in progress"). */
	if (p2p_pairing_active) {
		LOG_INF("P2P_GC: disconnected - starting pairing discovery in %d s",
			P2P_GC_RECONNECT_DELAY_S);
	} else {
		LOG_INF("P2P_GC: disconnected from GO - reconnect in %d s",
			P2P_GC_RECONNECT_DELAY_S);
	}
	k_work_reschedule_for_queue(&p2p_cli_workq, &p2p_gc_retry_work,
				    K_SECONDS(P2P_GC_RECONNECT_DELAY_S));
}

int wifi_run_p2p_gc_mode(void)
{
	p2p_gc_connected = false;
	p2p_gc_pending = false;
	p2p_find_running = false;
	p2p_pairing_active = false;
	have_pending_mac = false;
	pairing_find_cycles = 0;

	if (!p2p_cli_workq_started) {
		k_work_queue_init(&p2p_cli_workq);
		k_work_queue_start(&p2p_cli_workq, p2p_cli_workq_stack,
				   K_THREAD_STACK_SIZEOF(p2p_cli_workq_stack), K_PRIO_COOP(7),
				   NULL);
		p2p_cli_workq_started = true;
	}
	k_work_init_delayable(&p2p_gc_retry_work, p2p_gc_do_connect);

	p2p_gc_load_saved_go();

	if (have_saved_go) {
		LOG_INF("P2P_GC: saved GO %02X:%02X:%02X:%02X:%02X:%02X - reconnecting "
			"(retry every %d s until success)",
			saved_go_mac[0], saved_go_mac[1], saved_go_mac[2], saved_go_mac[3],
			saved_go_mac[4], saved_go_mac[5], P2P_GC_CONNECT_TIMEOUT_S);
		k_work_schedule_for_queue(&p2p_cli_workq, &p2p_gc_retry_work, K_NO_WAIT);
	} else {
		LOG_INF("P2P_GC: no saved GO - double-click Button 0 to pair with a P2P_GO");
	}

	return 0;
}

void wifi_p2p_start_pairing(void)
{
	enum zego_wifi_mode mode = zego_wifi_get_mode();

	switch (mode) {
	case ZEGO_WIFI_MODE_P2P_GO:
#if defined(CONFIG_WIFI_NM_WPA_SUPPLICANT_WPS)
		wifi_p2p_go_open_pairing_window();
#else
		LOG_WRN("P2P_GO pairing requires CONFIG_WIFI_NM_WPA_SUPPLICANT_WPS");
#endif
		break;
	case ZEGO_WIFI_MODE_P2P_GC:
		p2p_gc_start_pairing_internal();
		break;
	default:
		LOG_WRN("P2P pairing is only available in P2P_GO / P2P_GC mode");
		break;
	}
}

#else /* stubs when P2P not enabled */

int wifi_run_p2p_gc_mode(void)
{
	return 0;
}

void wifi_p2p_gc_on_connect_result(bool success)
{
	ARG_UNUSED(success);
}

void wifi_p2p_gc_on_disconnect(void)
{
}

void wifi_p2p_gc_on_peer_found(const uint8_t *mac, int8_t rssi)
{
	ARG_UNUSED(mac);
	ARG_UNUSED(rssi);
}

void wifi_p2p_start_pairing(void)
{
}

#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P */
