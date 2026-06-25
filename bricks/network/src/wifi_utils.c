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
#include <zephyr/sys/util.h>
#include <errno.h>
#include <string.h>

#include "wifi_utils.h"

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
 *   2. wps_pin     - set WPS PIN (12345678).  Clients join with:
 *                      DK:    wifi p2p connect <THIS_MAC> pin 12345678 --join
 *                      Phone: Wi-Fi Direct -> select DK -> enter PIN 12345678
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

#define P2P_GO_WPS_PIN              "12345678"
/* Re-arm WPS PIN after this many seconds if no client has connected. */
#define P2P_GO_WPS_REARM_INTERVAL_S 300

static void p2p_go_set_wps_pin_handler(struct k_work *work)
{
	struct net_if *iface = net_if_get_first_wifi();

	if (!iface) {
		LOG_ERR("P2P_GO: no Wi-Fi iface");
		return;
	}

	struct wifi_wps_config_params wps = {.oper = WIFI_WPS_PIN_SET};

	snprintf(wps.pin, sizeof(wps.pin), "%s", P2P_GO_WPS_PIN);

	int ret = net_mgmt(NET_REQUEST_WIFI_WPS_CONFIG, iface, &wps, sizeof(wps));

	if (ret) {
		LOG_ERR("P2P_GO: WPS PIN set failed (%d)", ret);
	} else {
		LOG_INF("P2P_GO: WPS PIN active: %s", P2P_GO_WPS_PIN);
		LOG_INF("P2P_GO: GO is beaconing - clients discover via Wi-Fi Direct scan");
	}

	/* Re-arm periodically so the GO remains connectable after the PIN window expires.
	 * DO NOT call P2P_FIND here - it always fails on a running GO because the
	 * radio is anchored to the group channel and cannot scan social channels.
	 * Clients find this GO by scanning ch1/6/11 and reading the Probe Response. */
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
	LOG_INF("P2P_GO: re-arming WPS PIN for client reconnect");
	k_work_reschedule_for_queue(&p2p_wps_workq, &p2p_go_wps_retry_work, K_NO_WAIT);
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

	LOG_INF("P2P_GO: group created - setting WPS PIN and starting discovery");

	if (!p2p_wps_workq_started) {
		k_work_queue_init(&p2p_wps_workq);
		k_work_queue_start(&p2p_wps_workq, p2p_wps_workq_stack,
				   K_THREAD_STACK_SIZEOF(p2p_wps_workq_stack), K_PRIO_COOP(7),
				   NULL);
		p2p_wps_workq_started = true;
	}
	k_work_init_delayable(&p2p_go_wps_retry_work, p2p_go_set_wps_pin_handler);
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
 * P2P_GC AUTO-CONNECT
 * ============================================================================
 *
 * When CONFIG_ZEGO_WIFI_P2P_GC_TARGET_GO_MAC is a non-empty string, P2P_GC
 * automatically runs 'wifi p2p connect <MAC> pin 12345678 --join' at boot and reconnects
 * automatically whenever the P2P group is lost.
 *
 * Connect / retry flow:
 *   - p2p_gc_do_connect() fires when the retry timer expires.
 *   - A "pending connect" flag (p2p_gc_pending) prevents sending a new connect
 *     command while one is already in-flight, avoiding "scan already in progress"
 *     errors from wpa_supplicant.
 *   - If net_mgmt returns 0 (command accepted): set pending, wait for CONNECT_RESULT.
 *       success  → wifi_p2p_gc_on_connect_result(true)  → cancel retries
 *       No success after 30 s → retry timer fires, clears pending, tries again.
 *   - If net_mgmt returns error (rejected): reschedule immediately.
 *
 * Disconnect / reconnect flow:
 *   - wifi_p2p_gc_on_disconnect() is called from the DISCONNECT_RESULT handler.
 *   - Resets p2p_gc_connected + p2p_gc_pending.
 *   - Schedules a reconnect attempt in 5 s.
 *
 * The connect runs on a dedicated 4 KB work queue (net_mgmt P2P connect can
 * overflow the 2 KB sysworkq stack, same as wps_pbc above).
 */

#if defined(CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P)

#define P2P_CLI_WORKQ_STACK_SIZE 4096
K_THREAD_STACK_DEFINE(p2p_cli_workq_stack, P2P_CLI_WORKQ_STACK_SIZE);
static struct k_work_q p2p_cli_workq;
static bool p2p_cli_workq_started;
static struct k_work_delayable p2p_gc_retry_work;
static bool p2p_gc_connected;
static bool p2p_gc_pending;   /* connect cmd accepted, waiting for result */
static bool p2p_find_running; /* WIFI_P2P_FIND is currently active */
static bool p2p_go_found;     /* target GO was found during pre-discovery */

/* wpa_supplicant --join makes exactly P2P_MAX_JOIN_SCAN_ATTEMPTS (10) scan
 * attempts, each ~4.6 s + 1 s retry gap, then emits GROUP_FORMATION_FAILURE
 * and goes idle - no CONNECT_RESULT is ever sent.
 * Empirical total (nRF7002): scan 1 starts at T≈1.3 s, scan 10 ends at
 * T≈63 s → GROUP_FORMATION_FAILURE at T≈64 s.
 * 90 s > 64 s, so wpa_supplicant has been idle ≥ 26 s before we restart,
 * guaranteeing no "scan already in progress" on the fresh P2P_CONNECT. */
#define P2P_GC_CONNECT_TIMEOUT_S 90

/* Prefix-mode find window: social-channel scan duration.  After this elapses
 * we stop the find and query the wpa_supplicant peer table directly.
 * We do NOT rely on NET_EVENT_WIFI_P2P_DEVICE_FOUND for prefix mode:
 * that event fires only for newly-discovered peers; if the GO is already
 * in wpa_supplicant's peer cache (from a previous session or scan) the
 * event is not re-fired even though the peer is visible. */
#define P2P_PREFIX_FIND_TIMEOUT_S 10

/* Size of the local peer-query buffer used after P2P_FIND completes. */
#define P2P_PREFIX_MAX_CANDIDATES 5

/* Parse "AA:BB:CC:DD:EE:FF" into 6 bytes. Returns true on success.
 * Note: newlib-nano's sscanf does not implement the "hh" length modifier,
 * so "%hhx" silently fails to convert. Parse into unsigned int with "%x"
 * (supported) and narrow to uint8_t. */
static bool parse_mac6(const char *s, uint8_t out[6])
{
	unsigned int v[6];

	if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
		return false;
	}
	for (int i = 0; i < 6; i++) {
		out[i] = (uint8_t)v[i];
	}
	return true;
}

/* Return true if the target GO MAC ends in :00:00:00 (prefix mode). */
static bool p2p_is_prefix_mode(void)
{
	const char *s = CONFIG_ZEGO_WIFI_P2P_GC_TARGET_GO_MAC;
	uint8_t b[6];

	if (!parse_mac6(s, b)) {
		return false;
	}
	return (b[3] == 0 && b[4] == 0 && b[5] == 0);
}

static void p2p_gc_do_connect(struct k_work *work)
{
	if (p2p_gc_connected) {
		return;
	}

	if (p2p_gc_pending) {
		/* 90 s with no CONNECT_RESULT: wpa_supplicant exhausted its 10
		 * join-scan attempts and is now idle.  Reset all state and retry. */
		LOG_INF("P2P_GC: %ds Timeout, and GO not found - restarting P2P connect",
			P2P_GC_CONNECT_TIMEOUT_S);
		p2p_gc_pending = false;
		p2p_go_found = false;
		p2p_find_running = false;
	}

	const char *mac_str = CONFIG_ZEGO_WIFI_P2P_GC_TARGET_GO_MAC;

	if (mac_str[0] == '\0') {
		return;
	}

	struct net_if *iface = net_if_get_first_wifi();

	if (!iface) {
		LOG_ERR("P2P_GC: no Wi-Fi iface, retry in 10 s");
		k_work_reschedule_for_queue(&p2p_cli_workq, k_work_delayable_from_work(work),
					    K_SECONDS(10));
		return;
	}

	/* ---- Prefix mode: run P2P_FIND, then query peer table for best RSSI ---- */
	if (p2p_is_prefix_mode()) {
		if (!p2p_find_running) {
			/* Start a fresh discovery scan.
			 * Note: do NOT reset any candidate state here - prefix mode now
			 * queries the peer table directly after find completes. */
			struct wifi_p2p_params find_params = {0};

			find_params.oper = WIFI_P2P_FIND;
			find_params.discovery_type = WIFI_P2P_FIND_ONLY_SOCIAL;
			find_params.timeout = P2P_PREFIX_FIND_TIMEOUT_S;

			int ret = net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface, &find_params,
					   sizeof(find_params));

			if (ret) {
				LOG_WRN("P2P_GC: prefix find failed (%d), retry in 10 s", ret);
				k_work_reschedule_for_queue(&p2p_cli_workq,
							    k_work_delayable_from_work(work),
							    K_SECONDS(10));
				return;
			}
			p2p_find_running = true;
			LOG_INF("P2P_GC: prefix mode %c%c:%c%c:%c%c:* - peer discovery (%d s)",
				mac_str[0], mac_str[1], mac_str[3], mac_str[4], mac_str[6],
				mac_str[7], P2P_PREFIX_FIND_TIMEOUT_S);
			/* Re-schedule to fire when the find window expires. */
			k_work_reschedule_for_queue(&p2p_cli_workq,
						    k_work_delayable_from_work(work),
						    K_SECONDS(P2P_PREFIX_FIND_TIMEOUT_S + 2));
			return;
		}

		/* Find window elapsed: query the wpa_supplicant peer table directly.
		 * DEVICE_FOUND events are unreliable for prefix mode - they only fire
		 * for newly-discovered peers; cached peers from prior sessions are
		 * silently skipped even though they appear in 'wifi p2p peer'. */
		p2p_find_running = false;

		static struct wifi_p2p_device_info peer_buf[P2P_PREFIX_MAX_CANDIDATES];
		struct wifi_p2p_params qparams = {0};

		memset(peer_buf, 0, sizeof(peer_buf));
		qparams.peers = peer_buf;
		qparams.oper = WIFI_P2P_PEER;
		qparams.peer_count = P2P_PREFIX_MAX_CANDIDATES;
		memset(qparams.peer_addr, 0xFF, 6); /* broadcast = all peers */

		int qret = net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface, &qparams, sizeof(qparams));

		if (qret) {
			LOG_WRN("P2P_GC: peer table query failed (%d), retry in 10 s", qret);
			k_work_reschedule_for_queue(
				&p2p_cli_workq, k_work_delayable_from_work(work), K_SECONDS(10));
			return;
		}

		/* Filter by OUI prefix and pick best RSSI. */
		uint8_t mac6[6];
		uint8_t *prefix = mac6;

		(void)parse_mac6(mac_str, mac6);

		LOG_INF("P2P_GC: peer table has %d entries, filtering for prefix "
			"%02X:%02X:%02X",
			qparams.peer_count, prefix[0], prefix[1], prefix[2]);

		int best = -1;

		for (int i = 0; i < qparams.peer_count; i++) {
			if (memcmp(peer_buf[i].mac, prefix, 3) != 0) {
				continue;
			}
			LOG_INF("P2P_GC:   [%d] %02X:%02X:%02X:%02X:%02X:%02X RSSI=%d", i,
				peer_buf[i].mac[0], peer_buf[i].mac[1], peer_buf[i].mac[2],
				peer_buf[i].mac[3], peer_buf[i].mac[4], peer_buf[i].mac[5],
				peer_buf[i].rssi);
			if (best < 0 || peer_buf[i].rssi > peer_buf[best].rssi) {
				best = i;
			}
		}

		if (best < 0) {
			LOG_WRN("P2P_GC: no GO with configured prefix found, retry in %d s",
				P2P_GC_CONNECT_TIMEOUT_S);
			k_work_reschedule_for_queue(&p2p_cli_workq,
						    k_work_delayable_from_work(work),
						    K_SECONDS(P2P_GC_CONNECT_TIMEOUT_S));
			return;
		}

		LOG_INF("P2P_GC: best GO %02X:%02X:%02X:%02X:%02X:%02X RSSI=%d dBm, connecting",
			peer_buf[best].mac[0], peer_buf[best].mac[1], peer_buf[best].mac[2],
			peer_buf[best].mac[3], peer_buf[best].mac[4], peer_buf[best].mac[5],
			peer_buf[best].rssi);

		struct wifi_p2p_params params = {0};

		memcpy(params.peer_addr, peer_buf[best].mac, 6);
		params.oper = WIFI_P2P_CONNECT;
		params.connect.method = WIFI_P2P_METHOD_KEYPAD;
		snprintf(params.connect.pin, sizeof(params.connect.pin), "12345678");
		params.connect.join = true;

		int ret = net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface, &params, sizeof(params));

		if (ret) {
			LOG_WRN("P2P_GC: prefix connect rejected (%d), retry in 10 s", ret);
			k_work_reschedule_for_queue(
				&p2p_cli_workq, k_work_delayable_from_work(work), K_SECONDS(10));
		} else {
			LOG_INF("P2P_GC: connect initiated -> pin 12345678 --join");
			p2p_gc_pending = true;
			k_work_reschedule_for_queue(&p2p_cli_workq,
						    k_work_delayable_from_work(work),
						    K_SECONDS(P2P_GC_CONNECT_TIMEOUT_S));
		}
		return;
	}

	/* ---- Exact-MAC mode ---- */
	struct wifi_p2p_params params = {0};

	if (!parse_mac6(mac_str, params.peer_addr)) {
		LOG_ERR("P2P_GC: invalid MAC '%s' in CONFIG_ZEGO_WIFI_P2P_GC_TARGET_GO_MAC",
			mac_str);
		return;
	}

	params.oper = WIFI_P2P_CONNECT;
	params.connect.method = WIFI_P2P_METHOD_KEYPAD;
	snprintf(params.connect.pin, sizeof(params.connect.pin), "12345678");
	params.connect.join = true;

	int ret = net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface, &params, sizeof(params));

	if (ret) {
		LOG_WRN("P2P_GC: connect to %s rejected (%d), retry in 10 s", mac_str, ret);
		/* Don't set pending - command was rejected, retry sooner. */
		k_work_reschedule_for_queue(&p2p_cli_workq, k_work_delayable_from_work(work),
					    K_SECONDS(10));
	} else {
		LOG_INF("P2P_GC: connect initiated -> wifi p2p connect %s pin 12345678 --join",
			mac_str);
		/* Command accepted.  Block further retries until CONNECT_RESULT
		 * arrives or P2P_GC_CONNECT_TIMEOUT_S passes. */
		p2p_gc_pending = true;
		k_work_reschedule_for_queue(&p2p_cli_workq, k_work_delayable_from_work(work),
					    K_SECONDS(P2P_GC_CONNECT_TIMEOUT_S));
	}
}

void wifi_p2p_gc_on_peer_found(const uint8_t *mac, int8_t rssi)
{
	if (!p2p_find_running || p2p_gc_connected || p2p_gc_pending) {
		return;
	}

	const char *mac_str = CONFIG_ZEGO_WIFI_P2P_GC_TARGET_GO_MAC;
	uint8_t target[6];

	if (!parse_mac6(mac_str, target)) {
		return;
	}

	if (p2p_is_prefix_mode()) {
		/* Prefix mode: DEVICE_FOUND events are unreliable (not fired for
		 * cached peers). Ignore events here; the peer table is queried
		 * directly after P2P_FIND completes via p2p_gc_do_connect(). */
		LOG_DBG("P2P_GC: prefix-mode peer seen %02X:%02X:%02X:%02X:%02X:%02X RSSI=%d "
			"(table query used instead)",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], rssi);
		return;
	}

	/* Exact-MAC mode: connect immediately on first match. */
	if (memcmp(mac, target, 6) != 0) {
		return;
	}

	LOG_INF("P2P_GC: target GO %s found - connecting immediately", mac_str);
	p2p_find_running = false;
	p2p_go_found = true;
	k_work_reschedule_for_queue(&p2p_cli_workq, &p2p_gc_retry_work, K_NO_WAIT);
}

void wifi_p2p_gc_on_connect_result(bool success)
{
	if (CONFIG_ZEGO_WIFI_P2P_GC_TARGET_GO_MAC[0] == '\0') {
		return;
	}

	p2p_gc_pending = false;

	if (success) {
		p2p_gc_connected = true;
		k_work_cancel_delayable(&p2p_gc_retry_work);
		LOG_INF("P2P_GC: connected to GO - auto-retry stopped");
	} else {
		/* CONNECT_RESULT failure: reset all state so next retry does a
		 * fresh P2P_FIND (prefix mode) or direct connect (exact-MAC mode). */
		p2p_go_found = false;
		p2p_find_running = false;
		k_work_reschedule_for_queue(&p2p_cli_workq, &p2p_gc_retry_work, K_SECONDS(10));
	}
}

void wifi_p2p_gc_on_disconnect(void)
{
	if (CONFIG_ZEGO_WIFI_P2P_GC_TARGET_GO_MAC[0] == '\0') {
		return;
	}

	if (!p2p_gc_connected) {
		return;
	}

	p2p_gc_connected = false;
	p2p_gc_pending = false;
	p2p_find_running = false;
	p2p_go_found = false;
	/* Wait 15 s before retrying: after a clean deauth wpa_supplicant starts
	 * a background cleanup scan (~5-17 s on nRF7002).  Issuing P2P_CONNECT
	 * too soon races with that scan and causes "Scan already in progress"
	 * errors.  15 s gives the cleanup scan time to drain. */
	LOG_INF("P2P_GC: disconnected from GO - reconnect in 15 s");
	k_work_reschedule_for_queue(&p2p_cli_workq, &p2p_gc_retry_work, K_SECONDS(15));
}

int wifi_run_p2p_gc_mode(void)
{
	const char *mac_str = CONFIG_ZEGO_WIFI_P2P_GC_TARGET_GO_MAC;

	if (mac_str[0] == '\0') {
		LOG_INF("P2P_GC: no target GO MAC set for auto-connect, follow guide to "
			"connect manually.");
		return 0;
	}

	LOG_INF("P2P_GC: auto-connect -> %s (retry every 90 s until success)", mac_str);

	p2p_gc_connected = false;
	p2p_gc_pending = false;
	p2p_find_running = false;
	p2p_go_found = false;

	if (!p2p_cli_workq_started) {
		k_work_queue_init(&p2p_cli_workq);
		k_work_queue_start(&p2p_cli_workq, p2p_cli_workq_stack,
				   K_THREAD_STACK_SIZEOF(p2p_cli_workq_stack), K_PRIO_COOP(7),
				   NULL);
		p2p_cli_workq_started = true;
	}

	k_work_init_delayable(&p2p_gc_retry_work, p2p_gc_do_connect);
	k_work_schedule_for_queue(&p2p_cli_workq, &p2p_gc_retry_work, K_NO_WAIT);

	return 0;
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

#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P */
