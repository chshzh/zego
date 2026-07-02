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

/*
 * net_event_mgmt.c - unified Wi-Fi / network event management (zego/network)
 *
 *   SYS_INIT priority ordering
 *   ───────────────────────────
 *   0  wifi_mode_selector_init  - reads NVS, publishes WIFI_MODE_CHAN
 *   5  network_module_init      - registers all event callbacks (this file)
 *
 *   Boot sequence
 *   ─────────────
 *   network_module_init() registers event handlers and returns immediately.
 *   When NET_EVENT_SUPPLICANT_READY fires, start_mode_work is submitted to
 *   the system workqueue, which then dispatches to the mode startup:
 *     SoftAP      → wifi_run_softap_mode()
 *     STA         → NET_REQUEST_WIFI_CONNECT_STORED
 *     P2P_GO      → wifi_run_p2p_go_mode()
 *     P2P_GC  → user runs 'wifi p2p find' + 'wifi p2p connect'
 *
 *   App notification
 *   ─────────────────
 *   For STA/P2P_GC: zego_on_net_event_dhcp_bound() is called when IP is assigned.
 *   For SoftAP/P2P_GO:  zego_on_net_event_wifi_ap_sta_connected() is called when a station joins.
 *   On link loss:       zego_on_net_event_wifi_disconnect(will_retry) is called;
 *                       will_retry is false only for STA with zero stored
 *                       credentials (P2P_GC always retries).
 *   All are __weak and defined as no-ops here; each app overrides them in its
 *   own net_event_app.c to publish app-specific zbus channels.
 *
 *   DHCP client lifecycle (STA / P2P_GC)
 *   ─────────────────────────────────────────
 *   net_config_init (boot) calls net_dhcpv4_start() immediately, before the
 *   WiFi interface is connected. By the time STA or P2P_GC actually connects
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
#define L3_IPV4_MASK     ((uint64_t)(NET_EVENT_IPV4_DHCP_BOUND | NET_EVENT_IPV4_ADDR_DEL))
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
/* IP from the last DHCP_BOUND - used to detect lease renewal vs. new address */
static struct in_addr last_bound_ip;

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
 * DEFERRED DHCP STOP (P2P_GC)
 * ============================================================================
 * net_dhcpv4_stop() must NOT be called from inside the NET_EVENT_IPV4_DHCP_BOUND
 * callback - doing so re-enters the DHCP state machine while it is still
 * processing the bound event and causes an MPU fault.  This work item defers
 * the stop to the system workqueue, which runs after the callback returns.
 */
static struct k_work_delayable dhcp_diag_work;

static void dhcp_diag_handler(struct k_work *work)
{
	struct net_if *iface = net_if_get_first_wifi();

	if (!iface) {
		return;
	}

	net_dhcpv4_stop(iface);
	LOG_DBG("P2P_GC: deferred DHCP stop done");
}

/* ============================================================================
 * STA RECONNECT: direct-disconnect retry + L3 DHCP-timeout watchdog
 * ============================================================================
 * STA must keep retrying a stored network after any disconnect (see
 * network-spec.md "STA Reconnect Sequence").  Ownership is split so neither
 * board configuration runs two competing reconnect loops:
 *
 *   CONFIG_ZEGO_WIFI_BLE_PROV=y  -> wifi_ble_prov owns reconnect timing (its
 *                                   own DISCONNECT_RESULT handler
 *                                   re-associates, rotating stored SSIDs).
 *   CONFIG_ZEGO_WIFI_BLE_PROV=n  -> zego/network owns it via l3_reconnect_work
 *                                   below: scheduled directly from
 *                                   NET_EVENT_WIFI_DISCONNECT_RESULT, and as
 *                                   the escape path if the L3 watchdog fires.
 *
 * The L3 watchdog is a second, independent concern: a successful
 * NET_EVENT_WIFI_CONNECT_RESULT is an L2 (802.11 association) event only - it
 * does not guarantee an IP.  Without it, a STA that associates but never gets
 * a DHCP lease (or whose lease later expires while the link stays up) sits
 * "associated, no IP" forever, because reconnect loops treat association as
 * "connected" and stop retrying.  The watchdog is armed on association and on
 * lease loss, cancelled on DHCP_BOUND and on DISCONNECT_RESULT.  If it fires
 * it issues NET_REQUEST_WIFI_DISCONNECT, producing a DISCONNECT_RESULT that
 * re-arms whichever reconnect path owns STA recovery - escaping the
 * half-connected state without a separate reschedule here.
 *
 * l3_reconnect_work is intentionally NOT nested inside the DHCP-timeout
 * guard below: direct-disconnect retry must work even if a project disables
 * CONFIG_ZEGO_NETWORK_STA_DHCP_TIMEOUT_SEC (an unrelated feature).
 *
 * Both mechanisms are scoped to STA only: P2P_GC has its own reconnect logic
 * (wifi_utils.c) and SoftAP/P2P_GO have no DHCP client.
 */
#if !IS_ENABLED(CONFIG_ZEGO_WIFI_BLE_PROV)
/* Retry delays for zego/network-owned STA reconnect, logged at every
 * reschedule so retry timing is visible on the console (mirrors the detail
 * wifi_ble_prov's own reconnect loop already logs via log_retry_plan()). */
#define L3_RECONNECT_DISCONNECT_DELAY_SEC 2
#define L3_RECONNECT_RETRY_DELAY_SEC      5

static struct k_work_delayable l3_reconnect_work;

static void l3_reconnect_handler(struct k_work *work)
{
	struct net_if *iface = net_if_get_wifi_sta();

	if (active_mode != ZEGO_WIFI_MODE_STA || iface == NULL || network_connected) {
		return;
	}

	LOG_INF("STA reconnect: requesting CONNECT_STORED now");
	/* Runs on the system workqueue - sized for the deep WPA ctrl-socket
	 * chain that NET_REQUEST_WIFI_CONNECT_STORED needs. */
	int rc = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);

	if (rc) {
		LOG_WRN("STA reconnect: CONNECT_STORED request failed (%d), retrying in %d s", rc,
			L3_RECONNECT_RETRY_DELAY_SEC);
		k_work_reschedule(&l3_reconnect_work, K_SECONDS(L3_RECONNECT_RETRY_DELAY_SEC));
	}
}
#endif /* !CONFIG_ZEGO_WIFI_BLE_PROV */

#if CONFIG_ZEGO_NETWORK_STA_DHCP_TIMEOUT_SEC > 0
static struct k_work_delayable l3_watchdog_work;

static void l3_watchdog_arm(void)
{
	if (active_mode != ZEGO_WIFI_MODE_STA) {
		return;
	}
	k_work_reschedule(&l3_watchdog_work, K_SECONDS(CONFIG_ZEGO_NETWORK_STA_DHCP_TIMEOUT_SEC));
}

static void l3_watchdog_cancel(void)
{
	k_work_cancel_delayable(&l3_watchdog_work);
#if !IS_ENABLED(CONFIG_ZEGO_WIFI_BLE_PROV)
	k_work_cancel_delayable(&l3_reconnect_work);
#endif
}

static void l3_watchdog_handler(struct k_work *work)
{
	struct net_if *iface = net_if_get_wifi_sta();

	if (active_mode != ZEGO_WIFI_MODE_STA || network_connected || iface == NULL) {
		return;
	}

	LOG_WRN("L3 watchdog: associated but no IP after %d s - forcing reconnect",
		CONFIG_ZEGO_NETWORK_STA_DHCP_TIMEOUT_SEC);

	/* Clean L2 teardown.  This produces a DISCONNECT_RESULT, which drives
	 * whichever reconnect path owns STA recovery (wifi_ble_prov, or
	 * l3_reconnect_work above). */
	int rc = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);

	if (rc) {
		LOG_ERR("L3 watchdog: NET_REQUEST_WIFI_DISCONNECT failed (%d)", rc);
	}
}
#else
static inline void l3_watchdog_arm(void)
{
}
static inline void l3_watchdog_cancel(void)
{
#if !IS_ENABLED(CONFIG_ZEGO_WIFI_BLE_PROV)
	k_work_cancel_delayable(&l3_reconnect_work);
#endif
}
#endif /* CONFIG_ZEGO_NETWORK_STA_DHCP_TIMEOUT_SEC > 0 */

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
 * a SYS_INIT function - the main-thread stack is too small for that path.
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
	case ZEGO_WIFI_MODE_P2P_GC:
		return "P2P_GC";
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

/* Shared by the STA/P2P_GC DHCP-bound path and the P2P_GO ready-with-IP log:
 * both need "who am I / what network am I on" text for a status line.
 * ssid_out is set to an empty string if the iface status query fails or the
 * SSID has not propagated yet (e.g. right after P2P_GO group creation). */
static void iface_get_mac_ssid(struct net_if *iface, char mac_out[18], char *ssid_out,
			       size_t ssid_out_len)
{
	struct wifi_iface_status wstatus = {0};

	iface_mac_to_str(iface, mac_out);

	ssid_out[0] = '\0';
	if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &wstatus, sizeof(wstatus)) == 0 &&
	    wstatus.ssid_len > 0) {
		snprintf(ssid_out, ssid_out_len, "%.*s", wstatus.ssid_len, (char *)wstatus.ssid);
	}
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
 * will_retry tells the app whether it should show a "trying to reconnect"
 * indication or an "action needed" one.  Computed by the caller:
 *   - STA: true if >=1 Wi-Fi credential is stored (l3_reconnect_work / the
 *     wifi_ble_prov reconnect loop will keep retrying); false only when
 *     zero credentials are stored - nothing to retry with.
 *   - P2P_GC: always true - it either reconnects to its saved GO or
 *     auto-pairs indefinitely, so it never has a "not possible" case.
 */
void __weak zego_on_net_event_wifi_disconnect(bool will_retry)
{
	ARG_UNUSED(will_retry);
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

/*
 * NOTE: zego_on_net_event_wifi_ap_sta_disconnected() may fire up to 5 minutes
 * after a P2P_GC (or SoftAP client) loses power or crashes.
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
void __weak zego_on_net_event_wifi_ap_sta_disconnected(int remaining_clients)
{
	ARG_UNUSED(remaining_clients);
}

void __weak zego_on_net_event_p2p_pairing(bool active)
{
	ARG_UNUSED(active);
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
 * L2: WIFI CONNECT RESULT HANDLER  (STA / P2P GO and CLIENT - connection attempt results)
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
				} else {
					wifi_print_status();

					/* SSID is negotiated by WPS and may not have
					 * propagated to iface status yet at group-creation
					 * time; fall back to "<pending>" rather than block. */
					char mac[18];
					char ssid[WIFI_SSID_MAX_LEN + 1];

					iface_get_mac_ssid(iface, mac, ssid, sizeof(ssid));

					LOG_INF("P2P_GO: ready with IP %s (mac=%s ssid=%s)",
						CONFIG_NET_CONFIG_MY_IPV4_ADDR, mac,
						ssid[0] ? ssid : "<pending>");
				}
			} else if (active_mode == ZEGO_WIFI_MODE_P2P_GC) {
				wifi_p2p_gc_on_connect_result(true);

				/* net_config_init pre-assigns CONFIG_NET_CONFIG_MY_IPV4_ADDR
				 * ("192.168.7.1") as NET_ADDR_OVERRIDABLE at boot, in every
				 * mode. A DK-based GO uses that exact same address as its
				 * own gateway/DHCP-server IP (wifi_setup_dhcp_server()), so
				 * pairing GC-to-DK-GO leaves the GC holding the SAME address
				 * as the peer it's about to DHCP from - DHCP never binds
				 * because the interface already "owns" the address it needs
				 * to reach. Remove it here so the lease (DK GO: 192.168.7.x;
				 * phone GO: its own subnet) can bind cleanly. No-op if it
				 * was already replaced by a prior lease. */
				struct in_addr cfg_ip;

				zsock_inet_pton(AF_INET, CONFIG_NET_CONFIG_MY_IPV4_ADDR, &cfg_ip);
				net_if_ipv4_addr_rm(iface, &cfg_ip);

				/* Restart DHCP like STA mode - wait for DHCP_BOUND to get
				 * the GO-assigned IP (phone: e.g. 192.168.49.x; DK GO:
				 * 192.168.7.x). */
				LOG_INF("P2P_GC: restarting DHCP on %s",
					net_if_get_device(iface) ? net_if_get_device(iface)->name
								 : "?");
				net_dhcpv4_restart(iface);
			} else if (active_mode == ZEGO_WIFI_MODE_STA) {
				LOG_INF("STA: restarting DHCP on %s",
					net_if_get_device(iface) ? net_if_get_device(iface)->name
								 : "?");
				net_dhcpv4_restart(iface);
				/* Associated at L2 - arm the watchdog; it is
				 * cancelled when DHCP binds. */
				l3_watchdog_arm();
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
				LOG_ERR("  Reason: Timed out - check credentials / AP "
					"availability");
				break;
			default:
				LOG_ERR("  Reason: Unknown %d", status->status);
				break;
			}

			if (active_mode == ZEGO_WIFI_MODE_STA) {
				/* WPA supplicant does NOT fire DISCONNECT_RESULT after a
				 * failed connect attempt (only after a successful one
				 * later drops) - schedule the retry here too, or a STA
				 * that never associates in the first place would sit
				 * forever with no further attempt. */
				bool has_creds = wifi_utils_has_stored_credentials();

				zego_on_net_event_wifi_disconnect(has_creds);
#if !IS_ENABLED(CONFIG_ZEGO_WIFI_BLE_PROV)
				if (has_creds) {
					LOG_WRN("STA reconnect: retrying in %d s",
						L3_RECONNECT_RETRY_DELAY_SEC);
					k_work_reschedule(&l3_reconnect_work,
							  K_SECONDS(L3_RECONNECT_RETRY_DELAY_SEC));
				}
#endif
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
		memset(&last_bound_ip, 0, sizeof(last_bound_ip));
		/* Link is down - stand the L3 watchdog down so it does not
		 * double-trigger against whichever reconnect path takes over
		 * below. */
		l3_watchdog_cancel();
		if (active_mode == ZEGO_WIFI_MODE_P2P_GC) {
			wifi_p2p_gc_on_disconnect();
			/* P2P_GC always retries (saved-MAC reconnect or pairing
			 * discovery) - it never has a "not possible" case. */
			zego_on_net_event_wifi_disconnect(true);
		} else if (active_mode == ZEGO_WIFI_MODE_STA) {
			bool has_creds = wifi_utils_has_stored_credentials();

			/* Re-checked here rather than assumed from the prior
			 * connection: a one-time shell 'wifi connect' that was
			 * never saved via 'wifi cred add' also disconnects with
			 * zero stored credentials. */
			zego_on_net_event_wifi_disconnect(has_creds);
#if !IS_ENABLED(CONFIG_ZEGO_WIFI_BLE_PROV)
			/* With BLE prov present, its own DISCONNECT_RESULT
			 * handler owns reconnect timing (see network-spec.md
			 * STA Reconnect Sequence for why the two are not run
			 * together). */
			if (has_creds) {
				LOG_INF("STA reconnect: scheduling retry in %d s",
					L3_RECONNECT_DISCONNECT_DELAY_SEC);
				k_work_reschedule(&l3_reconnect_work,
						  K_SECONDS(L3_RECONNECT_DISCONNECT_DELAY_SEC));
			}
#endif
		}
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
 * L2: P2P EVENT HANDLER  (P2P_GC peer discovery)
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
	if (active_mode != ZEGO_WIFI_MODE_P2P_GC) {
		return;
	}

	const struct wifi_p2p_device_info *info = (const struct wifi_p2p_device_info *)cb->info;

	if (!info) {
		return;
	}

	wifi_p2p_gc_on_peer_found(info->mac, info->rssi);
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
 * branching - common bookkeeping (station table, semaphore, MAC logging)
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
			char dk_ip[INET_ADDRSTRLEN];

			snprintf(dk_ip, sizeof(dk_ip), "%s", CONFIG_NET_CONFIG_MY_IPV4_ADDR);

			if (is_p2p_go) {
				wifi_p2p_go_cancel_wps_timer();

				char ssid[WIFI_SSID_MAX_LEN + 1] = {0};
				struct net_if *wifi_iface = net_if_get_first_wifi();
				struct wifi_iface_status wstatus = {0};

				if (wifi_iface &&
				    net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, wifi_iface, &wstatus,
					     sizeof(wstatus)) == 0 &&
				    wstatus.ssid_len > 0) {
					snprintf(ssid, sizeof(ssid), "%.*s", wstatus.ssid_len,
						 (char *)wstatus.ssid);
				}

				LOG_INF("L2-NET_EVENT_WIFI_AP_STA_CONNECTED: mac=%s mode=P2P_GO "
					"ip=%s ssid=%s",
					mac_str, dk_ip, ssid[0] ? ssid : "<pending>");

				/* AP_ENABLE_RESULT never fires for P2P_GO (only CONNECT_RESULT
				 * does), so the cached mode/ip/ssid used by
				 * zego_on_net_event_wifi_ap_sta_connected() are still zero.
				 * Update them now with the real values before the callback. */
				zego_on_net_event_wifi_ap_enabled(ZEGO_WIFI_MODE_P2P_GO, dk_ip,
								  ssid);
			} else {
				wifi_softap_cancel_remind_timer();
				LOG_INF("L2-NET_EVENT_WIFI_AP_STA_CONNECTED: mac=%s mode=SoftAP "
					"ip=%s clients=%d ssid=%s",
					mac_str, dk_ip, sta_count, CONFIG_ZEGO_WIIF_SOFTAP_SSID);
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
			wifi_p2p_go_rearm_wps_pin();
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
 * L3: DHCP BOUND HANDLER  (STA / P2P_GC - invokes wifi_connected callback)
 * ============================================================================
 */

static void l3_ipv4_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
				  struct net_if *iface)
{
	if (mgmt_event == NET_EVENT_IPV4_ADDR_DEL) {
		/* DHCP lease lost (expired / renewal failed) or address otherwise
		 * removed.  If the Wi-Fi link is STILL associated, this is a lease
		 * loss while connected - a "have link, no IP" zombie that no L2
		 * event will recover.  Re-arm the watchdog to force a reconnect.
		 * If the link is already down, the DISCONNECT_RESULT path owns
		 * recovery, so ignore it here. */
		struct wifi_iface_status wstatus = {0};

		if (active_mode == ZEGO_WIFI_MODE_STA && network_connected &&
		    net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &wstatus, sizeof(wstatus)) ==
			    0 &&
		    wstatus.state >= WIFI_STATE_ASSOCIATED) {
			LOG_WRN("L3-NET_EVENT_IPV4_ADDR_DEL: lease lost while associated - "
				"arming watchdog");
			network_connected = false;
			memset(&last_bound_ip, 0, sizeof(last_bound_ip));
			l3_watchdog_arm();
		}
		return;
	}

	if (mgmt_event != NET_EVENT_IPV4_DHCP_BOUND) {
		return;
	}

	/* Got a lease (new or renewed) - stand the L3 watchdog down. */
	l3_watchdog_cancel();

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

	/* Re-query at DHCP time - the iface status is fully settled here,
	 * resolving the race where ssid_len == 0 at L4_CONNECTED time. */
	char mac[18];

	iface_get_mac_ssid(iface, mac, sta_ssid, sizeof(sta_ssid));

	/* Detect lease renewal: same IP, already connected -> skip full reconnect fan-out */
#if defined(CONFIG_NET_DHCPV4)
	bool is_renewal =
		network_connected && net_ipv4_addr_cmp(&dhcpv4->requested_ip, &last_bound_ip);

	if (is_renewal) {
		LOG_INF("L3-NET_EVENT_IPV4_DHCP_BOUND: lease renewed (same ip=%s) - skipping "
			"reconnect",
			ip);
		return;
	}
	last_bound_ip = dhcpv4->requested_ip;
#endif

	LOG_INF("L3-NET_EVENT_IPV4_DHCP_BOUND: mode=%s ip=%s mask=%s gw=%s",
		zego_mode_to_str(active_mode), ip, mask, gw);

	if (active_mode != ZEGO_WIFI_MODE_STA && active_mode != ZEGO_WIFI_MODE_P2P_GO &&
	    active_mode != ZEGO_WIFI_MODE_P2P_GC) {
		LOG_DBG("DHCP bound in mode %s - ignoring", zego_mode_to_str(active_mode));
		return;
	}

	network_connected = true;
	if (active_mode == ZEGO_WIFI_MODE_P2P_GC) {
		/* Do NOT stop DHCP here.  net_dhcpv4_stop() removes the bound IPv4
		 * address (net_if_ipv4_addr_rm in the BOUND state), which leaves the
		 * P2P client with no IP - it can still send the initial AUDIO_START
		 * command (sent before the stop) but then silently drops all inbound
		 * audio from the GO.  Keep the DHCP lease active (renews like STA) so
		 * 192.168.7.2 stays assigned and the client keeps receiving. */
		k_work_cancel_delayable(&dhcp_diag_work);
	}
	wifi_print_status();

	bool is_p2p_gc =
		(active_mode == ZEGO_WIFI_MODE_P2P_GC) || (strncmp(sta_ssid, "DIRECT-", 7) == 0);

	zego_on_net_event_dhcp_bound(is_p2p_gc ? ZEGO_WIFI_MODE_P2P_GC : ZEGO_WIFI_MODE_STA, ip,
				     mac, sta_ssid);
}

/* ============================================================================
 * L4: CONNECT / DISCONNECT HANDLER  (commented out - see L4_CONN_MASK note)
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

		if (!wifi_utils_has_stored_credentials()) {
			LOG_INF("No stored credentials - use 'wifi cred add' or BLE "
				"provisioning to connect");
			zego_on_net_event_wifi_disconnect(false);
			break;
		}
		if (sta_iface) {
			int rc = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, sta_iface, NULL, 0);

			if (rc) {
#if !IS_ENABLED(CONFIG_ZEGO_WIFI_BLE_PROV)
				LOG_WRN("Auto-connect (CONNECT_STORED) failed (%d), retrying in "
					"%d s",
					rc, L3_RECONNECT_RETRY_DELAY_SEC);
				k_work_reschedule(&l3_reconnect_work,
						  K_SECONDS(L3_RECONNECT_RETRY_DELAY_SEC));
#else
				LOG_WRN("Auto-connect (CONNECT_STORED) failed (%d), retrying", rc);
#endif
			} else {
				LOG_INF("Auto-connecting with stored credentials...");
			}
		}
		break;
	}
	case ZEGO_WIFI_MODE_P2P_GO:
		wifi_run_p2p_go_mode();
		break;
	case ZEGO_WIFI_MODE_P2P_GC:
		wifi_run_p2p_gc_mode();
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

	/* L2: P2P peer discovery (CLIENT only - fires NET_EVENT_WIFI_P2P_DEVICE_FOUND) */
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
#if !IS_ENABLED(CONFIG_ZEGO_WIFI_BLE_PROV)
	k_work_init_delayable(&l3_reconnect_work, l3_reconnect_handler);
#endif
#if CONFIG_ZEGO_NETWORK_STA_DHCP_TIMEOUT_SEC > 0
	k_work_init_delayable(&l3_watchdog_work, l3_watchdog_handler);
#endif

	LOG_INF("All network event handlers initialized");
	/* Mode startup is deferred to start_mode_work, submitted by NET_EVENT_SUPPLICANT_READY. */
	return 0;
}

SYS_INIT(network_module_init, APPLICATION, 5);
