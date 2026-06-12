/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

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

	return 0;
}

static bool dhcp_server_started;

int wifi_setup_dhcp_server(void)
{
	struct net_if *iface;
	struct in_addr pool_start;
	struct in_addr gw_addr, netmask;
	int ret;

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
		CONFIG_ZEGO_WIIF_SOFTAP_PASSWORD, CONFIG_NET_CONFIG_MY_IPV4_ADDR);
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

#else /* !CONFIG_WIFI_NM_WPA_SUPPLICANT_AP — stubs */

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
 * P2P_GO AUTO-START: group_add + WPS PIN + 5-minute wait timer
 * ============================================================================
 */

#if defined(CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P) && defined(CONFIG_WIFI_NM_WPA_SUPPLICANT_WPS)

#define P2P_GO_WPS_PIN          "12345678"
#define P2P_GO_CLIENT_TIMEOUT_S 300 /* 5 minutes */

/* Dedicated work queue for WPS operations.
 * net_mgmt(NET_REQUEST_WIFI_WPS_CONFIG) calls deep into wpa_supplicant and
 * overflows the 2 KB sysworkq stack.  A private 4 KB stack is sufficient. */
#define P2P_WPS_WORKQ_STACK_SIZE 4096
K_THREAD_STACK_DEFINE(p2p_wps_workq_stack, P2P_WPS_WORKQ_STACK_SIZE);
static struct k_work_q p2p_wps_workq;
static bool p2p_wps_workq_started;
static struct k_work_delayable p2p_go_wps_retry_work;

static void p2p_go_set_wps_pin(void)
{
	struct net_if *iface = net_if_get_first_wifi();

	if (!iface) {
		LOG_ERR("P2P_GO: no Wi-Fi iface for WPS PIN");
		return;
	}

	struct wifi_wps_config_params wps = {
		.oper = WIFI_WPS_PIN_SET,
	};

	snprintf(wps.pin, sizeof(wps.pin), "%s", P2P_GO_WPS_PIN);

	int ret = net_mgmt(NET_REQUEST_WIFI_WPS_CONFIG, iface, &wps, sizeof(wps));

	if (ret) {
		LOG_ERR("P2P_GO: WPS PIN set failed (%d)", ret);
	} else {
		LOG_INF("P2P_GO: WPS PIN active: %s", P2P_GO_WPS_PIN);
		LOG_INF("P2P_GO: On your phone: Wi-Fi Direct -> connect -> PIN %s", P2P_GO_WPS_PIN);
	}
}

static void p2p_go_wps_retry_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_WRN("P2P_GO: no client connected in %d s, re-opening WPS window",
		P2P_GO_CLIENT_TIMEOUT_S);
	p2p_go_set_wps_pin();
	k_work_schedule_for_queue(&p2p_wps_workq, &p2p_go_wps_retry_work,
				  K_SECONDS(P2P_GO_CLIENT_TIMEOUT_S));
}

void wifi_p2p_go_cancel_wps_timer(void)
{
	k_work_cancel_delayable(&p2p_go_wps_retry_work);
}

int wifi_run_p2p_go_mode(void)
{
	struct net_if *iface = net_if_get_first_wifi();

	if (!iface) {
		LOG_ERR("P2P_GO: no Wi-Fi interface");
		return -ENODEV;
	}

	LOG_INF("P2P_GO mode: creating group...");

	/* group_add: persistent = -1 → autonomous (non-persistent) group */
	struct wifi_p2p_params p2p = {
		.oper = WIFI_P2P_GROUP_ADD,
		.group_add = {.persistent = -1},
	};

	int ret = net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface, &p2p, sizeof(p2p));

	if (ret) {
		LOG_ERR("P2P_GO: group_add failed (%d)", ret);
		return ret;
	}

	LOG_INF("P2P_GO: group created. GO IP: %s", CONFIG_NET_CONFIG_MY_IPV4_ADDR);

	p2p_go_set_wps_pin();

	if (!p2p_wps_workq_started) {
		k_work_queue_init(&p2p_wps_workq);
		k_work_queue_start(&p2p_wps_workq, p2p_wps_workq_stack,
				   K_THREAD_STACK_SIZEOF(p2p_wps_workq_stack), K_PRIO_COOP(7),
				   NULL);
		p2p_wps_workq_started = true;
	}
	k_work_init_delayable(&p2p_go_wps_retry_work, p2p_go_wps_retry_handler);
	k_work_schedule_for_queue(&p2p_wps_workq, &p2p_go_wps_retry_work,
				  K_SECONDS(P2P_GO_CLIENT_TIMEOUT_S));
	LOG_INF("P2P_GO: waiting for client (timeout: %d s)...", P2P_GO_CLIENT_TIMEOUT_S);

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

#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P && CONFIG_WIFI_NM_WPA_SUPPLICANT_WPS */
