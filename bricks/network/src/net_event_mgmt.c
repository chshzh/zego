/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * net_event_mgmt.c — unified Wi-Fi / network event management (zego/network)
 *
 *   SYS_INIT priority ordering
 *   ───────────────────────────
 *   0  wifi_mode_selector_init  — reads NVS, publishes WIFI_MODE_CHAN
 *   5  network_module_init      — registers all event callbacks (this file)
 *
 *   Boot sequence
 *   ─────────────
 *   network_module_init() registers event handlers and returns immediately.
 *   When NET_EVENT_SUPPLICANT_READY fires, start_mode_work is submitted to
 *   the system workqueue, which then dispatches to the mode startup:
 *     SoftAP      → wifi_run_softap_mode()
 *     STA         → NET_REQUEST_WIFI_CONNECT_STORED
 *     P2P_GO      → wifi_run_p2p_go_mode()
 *     P2P_CLIENT  → user runs 'wifi p2p find' + 'wifi p2p connect'
 *
 *   App notification
 *   ─────────────────
 *   For STA/P2P_CLIENT: zego_on_net_event_dhcp_bound() is called when IP is assigned.
 *   For SoftAP/P2P_GO:  zego_on_net_event_wifi_ap_sta_connected() is called when a station joins.
 *   On link loss:       zego_on_net_event_wifi_disconnect() is called.
 *   All are __weak and defined as no-ops here; each app overrides them in its
 *   own net_event_app.c to publish app-specific zbus channels.
 *
 *   DHCP client lifecycle (STA / P2P_CLIENT)
 *   ─────────────────────────────────────────
 *   net_config_init (boot) calls net_dhcpv4_start() immediately, before the
 *   WiFi interface is connected. By the time STA or P2P_CLIENT actually connects
 *   (potentially tens of seconds later), DHCP is deep into exponential-backoff
 *   retries.  net_dhcpv4_start() is a no-op in any state other than DISABLED,
 *   so a plain re-call does nothing.
 *   Fix: on CONNECT_RESULT success, stop DHCP (→ DISABLED) then start it again
 *   so the state machine runs a fresh cycle on the now-live WiFi link.
 */

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi_nm.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/socket.h>
#include <zephyr/zbus/zbus.h>
#include <supp_events.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include <wifi.h>
#include "net_event_mgmt.h"
#include "wifi_utils.h"

LOG_MODULE_REGISTER(zego_net_event_mgmt, CONFIG_ZEGO_NETWORK_LOG_LEVEL);

/* ============================================================================
 * EVENT MASKS
 * ============================================================================
 */

#define L2_IF_EVENT_MASK ((uint64_t)(NET_EVENT_IF_DOWN | NET_EVENT_IF_UP))
#define L2_WIFI_CONN_MASK                                                                          \
	((uint64_t)(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |             \
		    NET_EVENT_WIFI_SCAN_DONE))
#define L2_AP_MASK                                                                                 \
	((uint64_t)(NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_STA_CONNECTED |            \
		    NET_EVENT_WIFI_AP_STA_DISCONNECTED))
#define L3_WPA_SUPP_MASK ((uint64_t)(NET_EVENT_SUPPLICANT_READY | NET_EVENT_SUPPLICANT_NOT_READY))
#define L3_IPV4_MASK     ((uint64_t)NET_EVENT_IPV4_DHCP_BOUND)
/* NET_EVENT_L4_CONNECTED fires when *any* interface becomes routable (has an IP
 * and is up). Useful for multi-interface boards (WiFi + Ethernet) where you want
 * a single "network ready" signal without caring which interface delivered it.
 * For single-interface WiFi-only apps, L2-WIFI_CONNECT_RESULT + L3-DHCP_BOUND
 * are more precise and preferred. Uncomment if you add a second interface. */
/* #define L4_CONN_MASK ((uint64_t)(NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)) */

/* ============================================================================
 * MODULE STATE
 * ============================================================================
 */

static enum zego_wifi_mode active_mode = ZEGO_WIFI_MODE_SOFTAP;
static bool network_connected;
static bool initial_scan_done; /* set on first NET_EVENT_WIFI_SCAN_DONE */
/* SSID captured at L4_CONNECTED or re-confirmed at DHCP_BOUND */
static char sta_ssid[WIFI_SSID_MAX_LEN + 1];

#if IS_ENABLED(CONFIG_WIFI_NM_WPA_SUPPLICANT_AP)
/* SoftAP / P2P station table */
#define MAX_SOFTAP_STATIONS 4

struct softap_station {
	bool valid;
	uint8_t mac[6];
};

static struct softap_station connected_stations[MAX_SOFTAP_STATIONS];
static K_MUTEX_DEFINE(softap_mutex);

static int softap_station_count(void)
{
	int count = 0;

	for (int i = 0; i < MAX_SOFTAP_STATIONS; i++) {
		if (connected_stations[i].valid) {
			count++;
		}
	}
	return count;
}
#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT_AP */

/* ============================================================================
 * SEMAPHORES
 * ============================================================================
 */

static K_SEM_DEFINE(station_connected_sem, 0, 1); /* SoftAP: first STA joined  */

/* ============================================================================
 * DEFERRED DHCP STOP (P2P_CLIENT)
 * ============================================================================
 * driver_zephyr.c calls net_dhcpv4_restart() from the hostap_handler thread
 * a few ms AFTER the CONNECT_RESULT callback fires.  Stopping DHCP directly
 * in the callback is therefore undone almost immediately.  This work item is
 * scheduled 100 ms after the static IP is assigned, ensuring it fires AFTER
 * the supplicant's restart and keeps DHCP permanently stopped for P2P_CLIENT.
 */
static struct k_work_delayable dhcp_diag_work;

static void dhcp_diag_handler(struct k_work *work)
{
	struct net_if *iface = net_if_get_first_wifi();

	if (!iface) {
		return;
	}

	net_dhcpv4_stop(iface);
	LOG_DBG("P2P_CLIENT: deferred DHCP stop done (state=%d)", (int)iface->config.dhcpv4.state);
}

/* ============================================================================
 * EVENT CALLBACK STRUCTURES
 * ============================================================================
 */

static struct net_mgmt_event_callback iface_event_cb;
static struct net_mgmt_event_callback wpa_event_cb;
static struct net_mgmt_event_callback wifi_event_cb;
#if IS_ENABLED(CONFIG_WIFI_NM_WPA_SUPPLICANT_AP)
static struct net_mgmt_event_callback ap_event_cb;
#endif
#if IS_ENABLED(CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P)
static struct net_mgmt_event_callback p2p_event_cb;
#endif
static struct net_mgmt_event_callback ipv4_event_cb;
/* static struct net_mgmt_event_callback l4_event_cb; */

/* Work item: dispatches mode startup after WPA supplicant is ready.
 * Runs on the system workqueue (CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE), which is
 * sized for the deep WPA ctrl-socket chain (wpa_cli_cmd → z_wpa_ctrl_request
 * → zvfs_select). Never call NET_REQUEST_WIFI_CONNECT_STORED directly from
 * a SYS_INIT function — the main-thread stack is too small for that path.
 */
static void start_mode_work_handler(struct k_work *work);
static K_WORK_DEFINE(start_mode_work, start_mode_work_handler);

/* ============================================================================
 * HELPER UTILITIES
 * ============================================================================
 */

static const char *wifi_disconn_reason_str(enum wifi_disconn_reason reason)
{
	switch (reason) {
	case WIFI_REASON_DISCONN_SUCCESS:
		return "success";
	case WIFI_REASON_DISCONN_UNSPECIFIED:
		return "unspecified";
	case WIFI_REASON_DISCONN_USER_REQUEST:
		return "user-request";
	case WIFI_REASON_DISCONN_AP_LEAVING:
		return "ap-leaving";
	case WIFI_REASON_DISCONN_INACTIVITY:
		return "inactivity";
	default:
		return "unknown";
	}
}

static const char *zego_mode_to_str(enum zego_wifi_mode mode)
{
	switch (mode) {
	case ZEGO_WIFI_MODE_SOFTAP:
		return "SoftAP";
	case ZEGO_WIFI_MODE_STA:
		return "STA";
	case ZEGO_WIFI_MODE_P2P_GO:
		return "P2P_GO";
	case ZEGO_WIFI_MODE_P2P_CLIENT:
		return "P2P_CLIENT";
	default:
		return "Unknown";
	}
}

static void mac_to_str(const uint8_t mac[6], char out[18])
{
	snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
		 mac[5]);
}

static void iface_mac_to_str(struct net_if *iface, char out[18])
{
	if (iface == NULL) {
		snprintf(out, 18, "%s", "00:00:00:00:00:00");
		return;
	}

	struct net_linkaddr *link_addr = net_if_get_link_addr(iface);

	if (link_addr == NULL || link_addr->len < 6) {
		snprintf(out, 18, "%s", "00:00:00:00:00:00");
		return;
	}

	mac_to_str((const uint8_t *)link_addr->addr, out);
}

/* ============================================================================
 * WEAK CALLBACK DEFAULT IMPLEMENTATIONS (no-op; overridden per app)
 * ============================================================================
 */

void __weak zego_on_net_event_wifi_connect(enum zego_wifi_mode mode)
{
	ARG_UNUSED(mode);
}

/*
 * NOTE: zego_on_net_event_wifi_ap_sta_disconnected() may fire up to 5 minutes
 * after a P2P_CLIENT (or SoftAP client) loses power or crashes.
 *
 * Reason: when a client disappears without sending a deauth/disassoc frame
 * (power cut, battery pull, crash), the GO/AP has no immediate indication.
 * wpa_supplicant (hostapd) detects the loss via the AP inactivity timer:
 * it sends keepalive null-data frames; once all retries fail it evicts the
 * station.  The default timeout is ap_max_inactivity = 300 s (5 minutes).
 *
 * A clean client shutdown (e.g. normal reboot) sends a deauth frame and the
 * disconnect event fires immediately.
 *
 * To reduce the detection window, lower ap_max_inactivity at runtime:
 *
 *   uart:~$ wpa_cli -i wlan0 set ap_max_inactivity 30
 *
 * There is no Zephyr Kconfig for this value; it can also be set via a custom
 * wpa_supplicant config or by calling wpa_cli from application code.
 */
void __weak zego_on_net_event_wifi_disconnect(void)
{
}

void __weak zego_on_net_event_dhcp_bound(enum zego_wifi_mode mode, const char *ip_addr,
					 const char *mac_addr, const char *ssid)
{
	ARG_UNUSED(mode);
	ARG_UNUSED(ip_addr);
	ARG_UNUSED(mac_addr);
	ARG_UNUSED(ssid);
}

void __weak zego_on_net_event_wifi_ap_enabled(enum zego_wifi_mode mode, const char *ip_addr,
					      const char *ssid)
{
	ARG_UNUSED(mode);
	ARG_UNUSED(ip_addr);
	ARG_UNUSED(ssid);
}

void __weak zego_on_net_event_wifi_ap_sta_connected(int sta_count)
{
	ARG_UNUSED(sta_count);
}

void __weak zego_on_net_event_wifi_ap_sta_disconnected(int remaining_clients)
{
	ARG_UNUSED(remaining_clients);
}

/* ============================================================================
 * L2: INTERFACE EVENT HANDLER  (NET_EVENT_IF_UP / NET_EVENT_IF_DOWN)
 * ============================================================================
 */

static void l2_iface_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
				   struct net_if *iface)
{
	char ifname[IFNAMSIZ + 1] = {0};
	int ret = net_if_get_name(iface, ifname, sizeof(ifname) - 1);

	if (ret < 0) {
		LOG_ERR("Cannot get interface %d (%p) name", net_if_get_by_iface(iface), iface);
		ifname[0] = '?';
		ifname[1] = '\0';
	}

	switch (mgmt_event) {
	case NET_EVENT_IF_UP:
		LOG_INF("L2-NET_EVENT_IF_UP: iface=%s", ifname);
		break;
	case NET_EVENT_IF_DOWN:
		LOG_INF("L2-NET_EVENT_IF_DOWN: iface=%s", ifname);
		break;
	default:
		LOG_DBG("Unhandled if event: 0x%08" PRIx64, mgmt_event);
		break;
	}
}

/* ============================================================================
 * L2: WIFI CONNECT RESULT HANDLER  (STA / P2P GO and CLIENT — connection attempt results)
 * ============================================================================
 */

static void l2_wifi_conn_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
				       struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		const struct wifi_status *status = (const struct wifi_status *)cb->info;

		if (status->status == 0) {
			LOG_INF("L2-NET_EVENT_WIFI_CONNECT_RESULT: success");
			zego_on_net_event_wifi_connect(active_mode);
			if (active_mode == ZEGO_WIFI_MODE_P2P_GO) {
				int ret = wifi_setup_dhcp_server();

				if (ret < 0) {
					LOG_WRN("P2P_GO: DHCP server start failed (%d)", ret);
				}
			} else if (active_mode == ZEGO_WIFI_MODE_P2P_CLIENT) {
				wifi_p2p_client_on_connect_result(true);
				/* Assign a static IP instead of relying on DHCPv4.
				 * DHCPv4 packet creation fails on this platform in P2P mode
				 * (net_ipv4_create returns -ENOBUFS).  The P2P_GO runs a
				 * DHCP server on 192.168.7.1/24 with pool start at .2, so
				 * using a fixed 192.168.7.2/24 address is safe and reliable. */
				if (wifi_p2p_client_setup_static_ip(iface) == 0) {
					char mac[18];

					iface_mac_to_str(iface, mac);
					network_connected = true;
					// wifi_print_status();
					/* Skip NET_REQUEST_WIFI_IFACE_STATUS here: it triggers
					 * SIGNAL_POLL internally which times out (~15 s) on the
					 * first P2P connection while wpa_supplicant is still
					 * initialising.  The SSID is always "DIRECT-xx" in P2P
					 * mode; notify immediately with the known static IP. */
					zego_on_net_event_dhcp_bound(ZEGO_WIFI_MODE_P2P_CLIENT,
								     "192.168.7.2", mac, "P2P");
					/* Schedule a deferred DHCP stop to counteract the
					 * net_dhcpv4_restart() that driver_zephyr.c fires
					 * a few ms later from the hostap_handler thread. */
					k_work_reschedule(&dhcp_diag_work, K_MSEC(100));
				}
			} else if (active_mode == ZEGO_WIFI_MODE_STA) {
				/* Same immediate restart pattern for STA. */
				LOG_INF("STA: restarting DHCP on %s",
					net_if_get_device(iface) ? net_if_get_device(iface)->name
								 : "?");
				net_dhcpv4_restart(iface);
			}
		} else if (!initial_scan_done && status->status == 1) {
			/* Status=1 before the first scan completes is the normal
			 * "connect before scan" race in the supplicant state machine.
			 * The supplicant will retry automatically after scanning. */
			LOG_WRN("L2-NET_EVENT_WIFI_CONNECT_RESULT: pre-scan retry (status=1)");
		} else {
			LOG_ERR("L2-NET_EVENT_WIFI_CONNECT_RESULT: failed: status=%d",
				status->status);
			switch (status->status) {
			case 1:
				LOG_ERR("  Reason: Generic failure");
				break;
			case 2:
				LOG_ERR("  Reason: Authentication timeout");
				break;
			case 3:
				LOG_ERR("  Reason: Authentication failed");
				break;
			case 15:
				LOG_ERR("  Reason: AP not found");
				break;
			case 16:
				LOG_ERR("  Reason: Association timeout");
				break;
			case -ETIMEDOUT:
				LOG_ERR("  Reason: Timed out — check credentials / AP "
					"availability");
				break;
			default:
				LOG_ERR("  Reason: Unknown %d", status->status);
				break;
			}
		}
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT: {
		const struct wifi_status *status = (const struct wifi_status *)cb->info;
		enum wifi_disconn_reason reason =
			status ? status->disconn_reason : WIFI_REASON_DISCONN_UNSPECIFIED;

		LOG_WRN("NET_EVENT_WIFI_DISCONNECT_RESULT: status=%d reason=%d (%s) last_ssid=%s",
			status ? status->status : -1, reason, wifi_disconn_reason_str(reason),
			wifi_utils_get_last_ssid() ? wifi_utils_get_last_ssid() : "<unknown>");
		network_connected = false;
		if (active_mode == ZEGO_WIFI_MODE_P2P_CLIENT) {
			wifi_p2p_client_on_disconnect();
		}
		zego_on_net_event_wifi_disconnect();
		break;
	}
	case NET_EVENT_WIFI_SCAN_DONE:
		initial_scan_done = true;
		break;
	default:
		LOG_DBG("Unhandled Wi-Fi event: 0x%08" PRIx64, mgmt_event);
		break;
	}
}

/* ============================================================================
 * L2: P2P EVENT HANDLER  (P2P_CLIENT peer discovery)
 * ============================================================================
 */

#if IS_ENABLED(CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P)
static void l2_p2p_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
				 struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (mgmt_event != NET_EVENT_WIFI_P2P_DEVICE_FOUND) {
		return;
	}
	if (active_mode != ZEGO_WIFI_MODE_P2P_CLIENT) {
		return;
	}

	const struct wifi_p2p_device_info *info = (const struct wifi_p2p_device_info *)cb->info;

	if (!info) {
		return;
	}

	wifi_p2p_client_on_peer_found(info->mac, info->rssi);
}
#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P */

/* ============================================================================
 * L2: AP EVENT HANDLER  (SOFTAP/P2P_GO enable result, STA join/leave)
 * ============================================================================
 *
 * P2P Group Owner (GO) is an AP at the 802.11 / hostapd level.  WPA
 * supplicant runs the same hostapd code path for both SoftAP and P2P_GO,
 * so the kernel fires NET_EVENT_WIFI_AP_* events for BOTH modes.  This one
 * handler therefore serves ZEGO_WIFI_MODE_SOFTAP and ZEGO_WIFI_MODE_P2P_GO.
 *
 * The two modes diverge in the following ways:
 *
 *   SoftAP:
 *     - SSID is fixed at build time (CONFIG_ZEGO_WIIF_SOFTAP_SSID)
 *     - Static IP only (no DHCP server)
 *     - Cancels the SoftAP periodic-remind timer on first client
 *     - Supports up to MAX_SOFTAP_STATIONS clients simultaneously
 *
 *   P2P_GO:
 *     - SSID is negotiated by WPS and always starts with "DIRECT-"
 *     - DHCP server is started when CONNECT_RESULT fires (see
 *       l2_wifi_conn_event_handler), not here
 *     - Cancels the WPS re-arm timer on first client
 *     - Typically only one P2P client connects at a time
 *
 * The is_p2p_go flag inside NET_EVENT_WIFI_AP_STA_CONNECTED performs the
 * branching — common bookkeeping (station table, semaphore, MAC logging)
 * runs unconditionally for both modes.
 */

#if IS_ENABLED(CONFIG_WIFI_NM_WPA_SUPPLICANT_AP)
static void l2_ap_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
				struct net_if *iface)
{
	ARG_UNUSED(iface);

	switch (mgmt_event) {

	case NET_EVENT_WIFI_AP_ENABLE_RESULT: {
		const struct wifi_status *status = (const struct wifi_status *)cb->info;

		if (status->status != 0) {
			LOG_ERR("L2-NET_EVENT_WIFI_AP_ENABLE_RESULT: failed: status=%d",
				status->status);
			break;
		}

		struct net_if *ap_iface = net_if_get_first_wifi();
		struct in_addr addr, netmask;

		/* Re-assert the static IP. The WPA-level disconnect before AP_ENABLE
		 * can remove the manually-assigned address. */
		zsock_inet_pton(AF_INET, CONFIG_NET_CONFIG_MY_IPV4_ADDR, &addr);
		zsock_inet_pton(AF_INET, "255.255.255.0", &netmask);
		net_if_ipv4_addr_rm(ap_iface, &addr);
		net_if_ipv4_addr_add(ap_iface, &addr, NET_ADDR_MANUAL, 0);
		net_if_ipv4_set_netmask_by_addr(ap_iface, &addr, &netmask);

		LOG_INF("L2-NET_EVENT_WIFI_AP_ENABLE_RESULT: success SSID='%s' IP='%s' waiting for "
			"client",
			CONFIG_ZEGO_WIIF_SOFTAP_SSID, CONFIG_NET_CONFIG_MY_IPV4_ADDR);
		zego_on_net_event_wifi_ap_enabled(
			active_mode, CONFIG_NET_CONFIG_MY_IPV4_ADDR,
			active_mode == ZEGO_WIFI_MODE_SOFTAP ? CONFIG_ZEGO_WIIF_SOFTAP_SSID : "");
		break;
	}

	case NET_EVENT_WIFI_AP_STA_CONNECTED: {
		const struct wifi_ap_sta_info *sta = (const struct wifi_ap_sta_info *)cb->info;
		char mac_str[18];

		snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x", sta->mac[0],
			 sta->mac[1], sta->mac[2], sta->mac[3], sta->mac[4], sta->mac[5]);

		k_mutex_lock(&softap_mutex, K_FOREVER);
		for (int i = 0; i < MAX_SOFTAP_STATIONS; i++) {
			if (!connected_stations[i].valid) {
				connected_stations[i].valid = true;
				memcpy(connected_stations[i].mac, sta->mac, 6);
				break;
			}
		}
		int sta_count = softap_station_count();

		k_mutex_unlock(&softap_mutex);

		k_sem_give(&station_connected_sem);

		{
			bool is_p2p_go = (active_mode == ZEGO_WIFI_MODE_P2P_GO);
			struct net_if *wifi_iface = net_if_get_first_wifi();
			char dk_ip[INET_ADDRSTRLEN];
			char dk_mac[18];
			char ssid[WIFI_SSID_MAX_LEN + 1] = {0};

			snprintf(dk_ip, sizeof(dk_ip), "%s", CONFIG_NET_CONFIG_MY_IPV4_ADDR);
			iface_mac_to_str(wifi_iface, dk_mac);

			if (is_p2p_go) {
				wifi_p2p_go_cancel_wps_timer();

				struct wifi_iface_status wstatus = {0};

				if (wifi_iface &&
				    net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, wifi_iface, &wstatus,
					     sizeof(wstatus)) == 0 &&
				    wstatus.ssid_len > 0) {
					snprintf(ssid, sizeof(ssid), "%.*s", wstatus.ssid_len,
						 (char *)wstatus.ssid);
				}

				LOG_INF("L2-NET_EVENT_WIFI_AP_STA_CONNECTED: mac=%s mode=P2P_GO "
					"ip=%s",
					mac_str, dk_ip);
			} else {
				wifi_softap_cancel_remind_timer();
				snprintf(ssid, sizeof(ssid), "%s", CONFIG_ZEGO_WIIF_SOFTAP_SSID);
				LOG_INF("L2-NET_EVENT_WIFI_AP_STA_CONNECTED: mac=%s mode=SoftAP "
					"ip=%s clients=%d",
					mac_str, dk_ip, sta_count);
			}

			zego_on_net_event_wifi_ap_sta_connected(sta_count);
		}
		break;
	}

	case NET_EVENT_WIFI_AP_STA_DISCONNECTED: {
		const struct wifi_ap_sta_info *sta = (const struct wifi_ap_sta_info *)cb->info;
		char mac_str[18];

		snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x", sta->mac[0],
			 sta->mac[1], sta->mac[2], sta->mac[3], sta->mac[4], sta->mac[5]);

		k_mutex_lock(&softap_mutex, K_FOREVER);
		for (int i = 0; i < MAX_SOFTAP_STATIONS; i++) {
			if (connected_stations[i].valid &&
			    memcmp(connected_stations[i].mac, sta->mac, 6) == 0) {
				memset(&connected_stations[i], 0, sizeof(connected_stations[i]));
				break;
			}
		}
		int rem_count = softap_station_count();

		k_mutex_unlock(&softap_mutex);

		LOG_INF("L2-NET_EVENT_WIFI_AP_STA_DISCONNECTED: mac=%s clients=%d", mac_str,
			rem_count);
		if (active_mode == ZEGO_WIFI_MODE_P2P_GO) {
			wifi_p2p_go_rearm_pbc();
		}
		zego_on_net_event_wifi_ap_sta_disconnected(rem_count);
		break;
	}

	default:
		break;
	}
}
#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT_AP */

/* ============================================================================
 * L3: WPA SUPPLICANT EVENT HANDLER  (SUPPLICANT_READY / NOT_READY)
 * ============================================================================
 */

static void l3_wpa_supp_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
				      struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_SUPPLICANT_READY:
		LOG_INF("L3-NET_EVENT_SUPPLICANT_READY");
		k_work_submit(&start_mode_work);
		break;
	case NET_EVENT_SUPPLICANT_NOT_READY:
		LOG_ERR("L3-NET_EVENT_SUPPLICANT_NOT_READY");
		break;
	default:
		LOG_DBG("Unhandled WPA event: 0x%08" PRIx64, mgmt_event);
		break;
	}
}

/* ============================================================================
 * L3: DHCP BOUND HANDLER  (STA / P2P_CLIENT — invokes wifi_connected callback)
 * ============================================================================
 */

static void l3_ipv4_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
				  struct net_if *iface)
{
	if (mgmt_event != NET_EVENT_IPV4_DHCP_BOUND) {
		return;
	}

#if defined(CONFIG_NET_DHCPV4)
	const struct net_if_dhcpv4 *dhcpv4 = cb->info;
	char ip[INET_ADDRSTRLEN] = "0.0.0.0";
	char gw[INET_ADDRSTRLEN] = "0.0.0.0";
	char mask[INET_ADDRSTRLEN] = "0.0.0.0";
	struct in_addr gw_addr = net_if_ipv4_get_gw(iface);
	struct in_addr mask_addr = net_if_ipv4_get_netmask_by_addr(iface, &dhcpv4->requested_ip);

	net_addr_ntop(AF_INET, &dhcpv4->requested_ip, ip, sizeof(ip));
	net_addr_ntop(AF_INET, &gw_addr, gw, sizeof(gw));
	net_addr_ntop(AF_INET, &mask_addr, mask, sizeof(mask));
#else
	char ip[] = "0.0.0.0";
	char gw[] = "0.0.0.0";
	char mask[] = "0.0.0.0";
#endif

	/* Re-query SSID at DHCP time — the iface status is fully settled here,
	 * resolving the race where ssid_len == 0 at L4_CONNECTED time. */
	struct wifi_iface_status wstatus = {0};

	if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &wstatus, sizeof(wstatus)) == 0 &&
	    wstatus.ssid_len > 0) {
		snprintf(sta_ssid, sizeof(sta_ssid), "%.*s", wstatus.ssid_len,
			 (char *)wstatus.ssid);
	}

	LOG_INF("L3-NET_EVENT_IPV4_DHCP_BOUND: mode=%s ip=%s mask=%s gw=%s",
		zego_mode_to_str(active_mode), ip, mask, gw);

	if (active_mode != ZEGO_WIFI_MODE_STA && active_mode != ZEGO_WIFI_MODE_P2P_GO &&
	    active_mode != ZEGO_WIFI_MODE_P2P_CLIENT) {
		LOG_DBG("DHCP bound in mode %s — ignoring", zego_mode_to_str(active_mode));
		return;
	}

	network_connected = true;
	wifi_print_status();

	bool is_p2p_client = (active_mode == ZEGO_WIFI_MODE_P2P_CLIENT) ||
			     (strncmp(sta_ssid, "DIRECT-", 7) == 0);
	char mac[18];

	iface_mac_to_str(iface, mac);

	zego_on_net_event_dhcp_bound(is_p2p_client ? ZEGO_WIFI_MODE_P2P_CLIENT : ZEGO_WIFI_MODE_STA,
				     ip, mac, sta_ssid);
}

/* ============================================================================
 * L4: CONNECT / DISCONNECT HANDLER  (commented out — see L4_CONN_MASK note)
 * ============================================================================
 */
/*
 * static void l4_event_handler(struct net_mgmt_event_callback *cb,
 *                               uint64_t mgmt_event, struct net_if *iface)
 * {
 *     char ifname[IFNAMSIZ + 1] = {0};
 *     net_if_get_name(iface, ifname, sizeof(ifname) - 1);
 *     switch (mgmt_event) {
 *     case NET_EVENT_L4_CONNECTED:
 *         LOG_INF("L4-NET_EVENT_L4_CONNECTED: iface=%s", ifname);
 *         break;
 *     case NET_EVENT_L4_DISCONNECTED:
 *         LOG_INF("L4-NET_EVENT_L4_DISCONNECTED: iface=%s", ifname);
 *         break;
 *     default:
 *         break;
 *     }
 * }
 */

/* ============================================================================
 * MODE STARTUP WORK ITEM  (runs on system workqueue after WPA supp ready)
 * ============================================================================
 */

static void start_mode_work_handler(struct k_work *work)
{
	switch (active_mode) {
	case ZEGO_WIFI_MODE_SOFTAP:
		wifi_run_softap_mode();
		break;
	case ZEGO_WIFI_MODE_STA: {
		struct net_if *sta_iface = net_if_get_wifi_sta();

		if (sta_iface) {
			int rc = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, sta_iface, NULL, 0);

			if (rc) {
				LOG_INF("No stored credentials or connect failed (%d) — "
					"use 'wifi cred add' to provision",
					rc);
			} else {
				LOG_INF("Auto-connecting with stored credentials (if exists)...");
			}
		}
		break;
	}
	case ZEGO_WIFI_MODE_P2P_GO:
		wifi_run_p2p_go_mode();
		break;
	case ZEGO_WIFI_MODE_P2P_CLIENT:
		wifi_run_p2p_client_mode();
		break;
	default:
		LOG_WRN("Unsupported mode, falling back to SoftAP");
		wifi_run_softap_mode();
		break;
	}
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================
 */

int network_wait_for_station_connected(k_timeout_t timeout)
{
	return k_sem_take(&station_connected_sem, timeout);
}

/* ============================================================================
 * MODULE INITIALIZATION  (SYS_INIT APPLICATION priority 5)
 * ============================================================================
 */

int network_module_init(void)
{
	LOG_INF("Initializing network event handlers");

	/* Read active mode from WIFI_MODE_CHAN (published by wifi_mode_selector at priority 0) */
	struct wifi_mode_msg mode_msg = {.mode = ZEGO_WIFI_MODE_SOFTAP};
	int ret = zbus_chan_read(&WIFI_MODE_CHAN, &mode_msg, K_NO_WAIT);

	if (ret) {
		LOG_WRN("Failed to read WIFI_MODE_CHAN (%d), defaulting to SoftAP", ret);
		active_mode = ZEGO_WIFI_MODE_SOFTAP;
	} else {
		active_mode = (enum zego_wifi_mode)mode_msg.mode;
	}
	LOG_INF("Active Wi-Fi mode: %s", zego_mode_to_str(active_mode));

	/* L2: interface UP/DOWN */
	net_mgmt_init_event_callback(&iface_event_cb, l2_iface_event_handler, L2_IF_EVENT_MASK);
	net_mgmt_add_event_callback(&iface_event_cb);

	/* L2: Wi-Fi connect/disconnect results */
	net_mgmt_init_event_callback(&wifi_event_cb, l2_wifi_conn_event_handler, L2_WIFI_CONN_MASK);
	net_mgmt_add_event_callback(&wifi_event_cb);

#if IS_ENABLED(CONFIG_WIFI_NM_WPA_SUPPLICANT_AP)
	/* L2: AP events (SoftAP + P2P_GO) */
	net_mgmt_init_event_callback(&ap_event_cb, l2_ap_event_handler, L2_AP_MASK);
	net_mgmt_add_event_callback(&ap_event_cb);
#endif

	/* L3: WPA supplicant */
	net_mgmt_init_event_callback(&wpa_event_cb, l3_wpa_supp_event_handler, L3_WPA_SUPP_MASK);
	net_mgmt_add_event_callback(&wpa_event_cb);

	/* L2: P2P peer discovery (CLIENT only — fires NET_EVENT_WIFI_P2P_DEVICE_FOUND) */
#if IS_ENABLED(CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P)
	net_mgmt_init_event_callback(&p2p_event_cb, l2_p2p_event_handler,
				     NET_EVENT_WIFI_P2P_DEVICE_FOUND);
	net_mgmt_add_event_callback(&p2p_event_cb);
#endif

	/* L3: DHCP bound → notify app */
	net_mgmt_init_event_callback(&ipv4_event_cb, l3_ipv4_event_handler, L3_IPV4_MASK);
	net_mgmt_add_event_callback(&ipv4_event_cb);

	/* L4: uncomment if you need a unified "any interface routable" signal.
	 * net_mgmt_init_event_callback(&l4_event_cb, l4_event_handler, L4_CONN_MASK);
	 * net_mgmt_add_event_callback(&l4_event_cb); */

	k_work_init_delayable(&dhcp_diag_work, dhcp_diag_handler);

	LOG_INF("All network event handlers initialized");
	/* Mode startup is deferred to start_mode_work, submitted by NET_EVENT_SUPPLICANT_READY. */
	return 0;
}

SYS_INIT(network_module_init, APPLICATION, 5);
