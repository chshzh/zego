# Network Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | `zego/network` |
| Version | 2026-06-14-00-21 |
| PRD Version | N/A (standalone library module) |
| NCS Version | v3.3.0 |
| Status | Stable |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-04-17-10 | Initial spec — reverse-designed from source |
| 2026-06-05-09-31 | Added Supported Hardware section; documented nRF5340 Audio DK + nRF7002EK |
| 2026-06-09-17-25 | P2P_CLIENT auto-connect: `wifi_run_p2p_client_mode()` starts peer discovery at boot; PBC first then PIN 12345678 fallback; 30 s retry, 5 s reconnect delay; added Kconfig, API table entry, test points |
| 2026-06-10-00-00 | Clarified P2P_GO/SoftAP shared AP handler; renamed `l2_softap_event_handler`→`l2_ap_event_handler`, `L2_SOFTAP_MASK`→`L2_AP_MASK`, `softap_event_cb`→`ap_event_cb`, `zego_on_net_event_softap_ready`→`zego_on_net_event_wifi_ap_enabled`, `zego_on_net_event_softap_sta_disconnected`→`zego_on_net_event_wifi_ap_sta_disconnected`; added P2P_GO vs SoftAP comparison table |
| 2026-06-11-13-52 | Expanded Weak-hook API section to all 6 hooks; added `zego_on_net_event_wifi_connect`, `zego_on_net_event_wifi_ap_sta_connected`, `zego_on_net_event_wifi_ap_enabled`, `zego_on_net_event_wifi_ap_sta_disconnected` with full signatures and trigger context |
| 2026-06-14-00-21 | P2P_CLIENT: replaced discovery+PBC+PIN flow with direct --join using CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC; static IP 192.168.7.2/24 assigned at CONNECT_RESULT; connect retry 90 s; reconnect delay 15 s. P2P_GO: added PBC auto-rearm on client disconnect and every 110 s. Hook trigger map: zego_on_net_event_dhcp_bound for P2P_CLIENT now triggered from CONNECT_RESULT success (not DHCP_BOUND). Kconfig updated |

---

## Overview

`zego/network` is the unified Wi-Fi / network event management layer for all zego-based
applications. It handles the full lifecycle of all four Wi-Fi modes (SoftAP, STA, P2P_GO,
P2P_CLIENT): waits for WPA supplicant ready, dispatches the selected mode's startup sequence,
monitors network events across all layers (L2–L4), and fires six `__weak` callback hooks
that applications override to publish app-specific zbus channels.

The module has no application-specific logic and contains no zbus channels of its own. It is
the bridge between the raw Zephyr net_mgmt event system and the application event model.

---

## Location

- **Path**: `zego/network/`
- **Files**: `src/net_event_mgmt.c`, `src/net_event_mgmt.h`, `src/wifi_utils.c`,
  `src/wifi_utils.h`, `Kconfig`, `Kconfig.defaults`, `CMakeLists.txt`, `zephyr/module.yml`

---

## Supported Hardware

The `zego/network` module is board-agnostic. Any board with a working nRF70-series Wi-Fi driver
and WPA supplicant is supported. Tested combinations:

| Board | Build target | Notes |
|-------|-------------|-------|
| nRF54LM20DK + nRF7002EB2 | `nrf54lm20dk/nrf54lm20a/cpuapp` + `-DSHIELD=nrf7002eb2` | Same capabilities; larger flash/RAM |
| nRF7002DK | `nrf7002dk/nrf5340/cpuapp` | STA + SoftAP + P2P; WPA supplicant on nRF5340 app core |
| nRF5340 Audio DK + nRF7002EK | `nrf5340_audio_dk/nrf5340/cpuapp` + `-DSHIELD=nrf7002ek` | STA + SoftAP + P2P; application must supply a DTS overlay mapping the nRF7002EK SPI bus to the Audio DK GPIO pins |

---

## Module Type

- [x] **Library module** — callback-driven via `net_mgmt_event_callback`. No dedicated thread.
  All event handlers run in the caller's context (WPA supplicant thread or system work queue).

---

## Dependency

`CONFIG_ZEGO_NETWORK` depends on `CONFIG_ZEGO_WIFI` — the mode selector must run first
(SYS_INIT priority 0) to publish `WIFI_MODE_CHAN` before the network module reads it.

---

## Zbus Integration

**Subscribes to**: `WIFI_MODE_CHAN` — read once at `SYS_INIT` (priority 5) via
`zbus_chan_read()` with `K_NO_WAIT`.

**Publishes to**: nothing directly. Instead uses the weak-hook pattern (see below).

### Weak-hook API

All hooks are `__weak` no-ops in `net_event_mgmt.c`. Override with strong definitions in
`src/modules/network/net_event_app.c`.

```c
/* net_event_mgmt.h — override in src/modules/network/net_event_app.c */

/**
 * Called when STA / P2P_CLIENT association succeeds (L2 connected, before DHCP).
 * Device is associated but has no routable IP yet.
 */
void zego_on_net_event_wifi_connect(enum zego_wifi_mode mode);

/**
 * Called when STA / P2P_CLIENT obtains a DHCP-assigned IP, or when the first
 * SoftAP / P2P_GO client associates (static IP).
 */
void zego_on_net_event_dhcp_bound(enum zego_wifi_mode mode,
                                  const char *ip_addr,   /* NUL-terminated dotted decimal */
                                  const char *mac_addr,  /* "XX:XX:XX:XX:XX:XX" */
                                  const char *ssid);     /* NUL-terminated, max 32 chars */

/**
 * Called when Wi-Fi connectivity is lost (DISCONNECT_RESULT received).
 */
void zego_on_net_event_wifi_disconnect(void);

/**
 * Called when the SoftAP or P2P_GO access point is enabled and ready to accept
 * clients (NET_EVENT_WIFI_AP_ENABLE_RESULT success). Fired before any client connects.
 * ssid is empty for P2P_GO (SSID not yet negotiated at AP_ENABLE time).
 */
void zego_on_net_event_wifi_ap_enabled(enum zego_wifi_mode mode,
                                       const char *ip_addr,  /* gateway IP, static */
                                       const char *ssid);

/**
 * Called after a SoftAP / P2P_GO client joins. sta_count reflects the count after
 * the new connection (≥ 1).
 */
void zego_on_net_event_wifi_ap_sta_connected(int sta_count);

/**
 * Called after a SoftAP / P2P_GO client disconnects. remaining_clients reflects
 * the count after removal (0 = no clients remain).
 */
void zego_on_net_event_wifi_ap_sta_disconnected(int remaining_clients);
```

**Hook trigger map:**

| Hook | Trigger event | Mode(s) |
|---|---|---|
| `zego_on_net_event_wifi_connect` | `NET_EVENT_WIFI_CONNECT_RESULT` success | STA, P2P_CLIENT |
| `zego_on_net_event_dhcp_bound` | `NET_EVENT_IPV4_DHCP_BOUND` | STA, P2P_CLIENT |
| `zego_on_net_event_dhcp_bound` | `NET_EVENT_WIFI_AP_STA_CONNECTED` (first client) | SoftAP, P2P_GO |
| `zego_on_net_event_wifi_disconnect` | `NET_EVENT_WIFI_DISCONNECT_RESULT` | STA, P2P_CLIENT |
| `zego_on_net_event_wifi_ap_enabled` | `NET_EVENT_WIFI_AP_ENABLE_RESULT` success | SoftAP, P2P_GO |
| `zego_on_net_event_wifi_ap_sta_connected` | `NET_EVENT_WIFI_AP_STA_CONNECTED` | SoftAP, P2P_GO |
| `zego_on_net_event_wifi_ap_sta_disconnected` | `NET_EVENT_WIFI_AP_STA_DISCONNECTED` | SoftAP, P2P_GO |

> P2P_CLIENT detection: the mode is known at boot from `WIFI_MODE_CHAN`; `zego_on_net_event_dhcp_bound()`
> is called with `mode=ZEGO_WIFI_MODE_P2P_CLIENT` directly from the `CONNECT_RESULT` handler.

---

## Boot Sequence

`SYS_INIT(network_module_init, APPLICATION, 5)` runs after the mode selector (priority 0).

```
1. Read WIFI_MODE_CHAN  (K_NO_WAIT; fallback to SoftAP on error)
2. Register all net_mgmt event callbacks (L2 IF, L2 WiFi, L2 SoftAP, L3 WPA, L3 DHCP, L4)
3. k_sem_take(&wpa_supp_ready_sem, K_SECONDS(30))  ← bounded wait
4. On timeout → log error, return -ETIMEDOUT
5. On ready → dispatch to mode startup:
   SoftAP      → wifi_run_softap_mode()
   STA         → NET_REQUEST_WIFI_CONNECT_STORED
   P2P_GO      → wifi_run_p2p_go_mode()
   P2P_CLIENT  → wifi_run_p2p_client_mode()
```

---

## P2P_CLIENT Auto-Connect Sequence

`wifi_run_p2p_client_mode()` drives connection to a known P2P_GO without any shell interaction.
The target GO MAC is specified at compile time via `CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC`.

```
1. Issue WIFI_P2P_CONNECT to target MAC using method=PBC and flag=--join
   ("wifi p2p connect <MAC> pbc --join")
   → set p2p_client_pending = true; schedule p2p_client_timeout_work in 90 s

2a. NET_EVENT_WIFI_CONNECT_RESULT success →
    → wifi_p2p_client_setup_static_ip(): assign 192.168.7.2/24 to wlan0
    → zego_on_net_event_dhcp_bound(P2P_CLIENT, "192.168.7.2", mac, "P2P") called
    → cancel timeout work; p2p_client_pending = false
    → schedule dhcp_diag_work(100 ms) to stop any lingering DHCP client

2b. NET_EVENT_WIFI_CONNECT_RESULT failure (GO not found) →
    → log warning; timeout work still armed

3.  p2p_client_timeout_work fires after P2P_CLIENT_CONNECT_TIMEOUT_S (90 s) →
    → wpa_supplicant has exhausted its 10 internal join-scan attempts and is idle
    → p2p_client_pending = false; go back to step 1 (fresh P2P_CONNECT)

4.  NET_EVENT_WIFI_DISCONNECT_RESULT in P2P_CLIENT mode →
    → cancel any pending connect timeout
    → zego_on_net_event_wifi_disconnect() called
    → schedule reconnect in P2P_CLIENT_RECONNECT_DELAY_S (15 s) to allow wpa_supplicant
      background cleanup scan to drain before next connect attempt
    → go back to step 1
```

> **Why 90 s timeout?** `wpa_supplicant` attempts `P2P_MAX_JOIN_SCAN_ATTEMPTS` (10) scans
> of ~8–9 s each before silently giving up. 90 s gives the 10-scan cycle a safe margin so
> we never race with an in-progress scan when re-issuing `P2P_CONNECT`.

> **Why 15 s reconnect delay?** After a clean GO deauth, `wpa_supplicant` starts a background
> cleanup scan (5–17 s on nRF7002). Issuing `P2P_CONNECT` inside this window causes
> `nrf_wifi_wpa_supp_scan2: Scan already in progress` errors and a stuck state.

> **Static IP instead of DHCP**: P2P_CLIENT always uses 192.168.7.2/24 (GO is 192.168.7.1).
> `NET_EVENT_IPV4_DHCP_BOUND` is **not** used for P2P_CLIENT; `zego_on_net_event_dhcp_bound()`
> is called directly from the `CONNECT_RESULT` success handler.

LED feedback flows through `APP_WIFI_STATE_CHAN` in `net_event_app.c` — the UX module sees
`APP_WIFI_STATE_CONNECTING` during discovery and `APP_WIFI_STATE_CONNECTED` after CONNECT_RESULT,
driving the same ROTATE → solid-ON LED transitions as STA mode.

---

## Event Handler Map

| Event | Handler | Action |
|---|---|---|
| `NET_EVENT_IF_UP` / `IF_DOWN` | `l2_iface_event_handler` | Log only |
| `NET_EVENT_SUPPLICANT_READY` | `l3_wpa_supp_event_handler` | `k_sem_give(&wpa_supp_ready_sem)` |
| `NET_EVENT_SUPPLICANT_NOT_READY` | `l3_wpa_supp_event_handler` | Log error |
| `NET_EVENT_WIFI_CONNECT_RESULT` success | `l2_wifi_conn_event_handler` | Log; if P2P_GO, start DHCP server |
| `NET_EVENT_WIFI_CONNECT_RESULT` failure | `l2_wifi_conn_event_handler` | Log error with reason code |
| `NET_EVENT_WIFI_DISCONNECT_RESULT` | `l2_wifi_conn_event_handler` | Clear `network_connected`; call `zego_on_net_event_wifi_disconnect()` |
| `NET_EVENT_WIFI_AP_ENABLE_RESULT` success | `l2_ap_event_handler` (AP guard) | Re-assert static IP; call `zego_on_net_event_wifi_ap_enabled()` |
| `NET_EVENT_WIFI_AP_STA_CONNECTED` | `l2_ap_event_handler` (AP guard) | Track station; `k_sem_give(&station_connected_sem)`; call `zego_on_net_event_dhcp_bound()` |
| `NET_EVENT_WIFI_AP_STA_DISCONNECTED` | `l2_ap_event_handler` (AP guard) | Remove station from table; call `zego_on_net_event_wifi_ap_sta_disconnected()` |
| `NET_EVENT_WIFI_CONNECT_RESULT` success (P2P_CLIENT) | `l2_wifi_conn_event_handler` | Assign static IP 192.168.7.2/24; call `zego_on_net_event_dhcp_bound()`; cancel timeout work |
| `NET_EVENT_WIFI_DISCONNECT_RESULT` (P2P_CLIENT) | `l2_wifi_conn_event_handler` | Cancel timeout work; call `zego_on_net_event_wifi_disconnect()`; reschedule connect in 15 s |
| `NET_EVENT_IPV4_DHCP_BOUND` | `l3_ipv4_event_handler` | Log IP; re-query SSID; call `zego_on_net_event_dhcp_bound()` |
| `NET_EVENT_L4_CONNECTED` | `l4_event_handler` | Log (early SSID capture placeholder) |
| `NET_EVENT_L4_DISCONNECTED` | `l4_event_handler` | Log |

> **P2P_GO shares the SoftAP AP handler.** At the 802.11 level a P2P Group Owner is an AP —
> WPA supplicant implements P2P_GO using the same hostapd code path as SoftAP. The kernel
> therefore fires `NET_EVENT_WIFI_AP_*` events for both `ZEGO_WIFI_MODE_SOFTAP` and
> `ZEGO_WIFI_MODE_P2P_GO`. `l2_ap_event_handler` handles both, branching internally on
> `is_p2p_go`. See [P2P_GO and SoftAP — Shared AP Code Path](#p2p_go-and-softap--shared-ap-code-path) below.

> `l2_ap_event_handler` and the station table are compiled only when
> `CONFIG_WIFI_NM_WPA_SUPPLICANT_AP=y`.

---

## Public API

### `net_event_mgmt.h`

```c
/**
 * Block until NET_EVENT_SUPPLICANT_READY.
 * @param timeout  e.g. K_SECONDS(30)
 * @return 0 on success, -EAGAIN / -ETIMEDOUT on timeout
 */
int network_wait_for_wpa_supp_ready(k_timeout_t timeout);

/**
 * Block until the first SoftAP / P2P_GO client connects.
 * @param timeout  e.g. K_FOREVER
 * @return 0 on success, -EAGAIN on timeout
 */
int network_wait_for_station_connected(k_timeout_t timeout);
```

### `wifi_utils.h` (internal helpers, available to zego/network consumers)

| Function | Purpose |
|---|---|
| `wifi_run_softap_mode()` | Set regulatory domain, start DHCP server, enable AP |
| `wifi_run_p2p_go_mode()` | Create P2P group, activate WPS PIN, start 5-min timer |
| `wifi_setup_dhcp_server()` | Assign static IP + start DHCPv4 server (idempotent) |
| `wifi_utils_auto_connect_stored()` | Trigger `NET_REQUEST_WIFI_CONNECT_STORED` |
| `wifi_utils_ensure_gateway_softap_credentials()` | Write default SoftAP creds to settings if absent |
| `wifi_utils_get_last_ssid()` | Return last connected SSID string |
| `wifi_softap_cancel_remind_timer()` | Cancel SoftAP periodic reminder work |
| `wifi_p2p_go_cancel_wps_timer()` | Cancel P2P_GO WPS re-arm timer |
| `wifi_run_p2p_client_mode()` | Start P2P peer discovery; handle PBC→PIN connection and 5 s reconnect loop |
| `wifi_print_status()` | Print Wi-Fi interface status to log |
| `wifi_print_dhcp_ip()` | Print DHCP IP / netmask / GW to log |

---

## Kconfig Symbols

| Symbol | Type | Default | Description |
|---|---|---|---|
| `CONFIG_ZEGO_NETWORK` | bool | n | Enable the network module |
| `CONFIG_ZEGO_WIIF_SOFTAP_REG_DOMAIN` | string | `"US"` | Regulatory domain for SoftAP / P2P_GO |
| `CONFIG_ZEGO_WIIF_SOFTAP_SSID` | string | `"device_AP"` | SoftAP SSID |
| `CONFIG_ZEGO_WIIF_SOFTAP_PASSWORD` | string | `"password@123"` | SoftAP WPA2 password (≥ 8 chars) |
| `CONFIG_ZEGO_WIIF_SOFTAP_BAND_2_4_GHZ` | bool (choice) | y | Use 2.4 GHz band |
| `CONFIG_ZEGO_WIIF_SOFTAP_BAND_5_GHZ` | bool (choice) | n | Use 5 GHz band |
| `CONFIG_ZEGO_WIIF_SOFTAP_CHANNEL` | int (1–196) | 1 | SoftAP / P2P_GO channel |
| `CONFIG_ZEGO_NETWORK_LOG_LEVEL` | choice | INF | Module log level |
| `CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC` | string | `""` | Target P2P_GO MAC address ("XX:XX:XX:XX:XX:XX"); required for auto-connect --join mode |
| `CONFIG_ZEGO_NETWORK_P2P_CLIENT_CONNECT_TIMEOUT_S` | int | 90 | Seconds to wait for CONNECT_RESULT before re-issuing P2P_CONNECT; must be > wpa_supplicant's internal join-scan cycle (~80 s for 10 attempts) |
| `CONFIG_ZEGO_NETWORK_P2P_CLIENT_RECONNECT_DELAY_S` | int | 15 | Seconds to wait after disconnect before reconnecting; allows wpa_supplicant cleanup scan to drain |

---

## P2P_GO and SoftAP — Shared AP Code Path

A P2P Group Owner is an Access Point at the 802.11 / hostapd level. WPA supplicant runs the
same AP bringup code for both SoftAP and P2P_GO, which means the Zephyr kernel fires
`NET_EVENT_WIFI_AP_*` events for both modes identically.

`l2_ap_event_handler` handles both. The `is_p2p_go` flag gates the
small differences:

| Aspect | SoftAP | P2P_GO |
|--------|--------|--------|
| SSID | Fixed: `CONFIG_ZEGO_WIIF_SOFTAP_SSID` | Negotiated by WPS; always starts `DIRECT-` |
| IP assignment | Static only | Static IP; DHCP server started at `CONNECT_RESULT` |
| On first client | Cancel SoftAP remind timer | Cancel WPS re-arm timer |
| Typical client count | 1–4 | 1 |
| `NET_EVENT_WIFI_AP_*` events | Yes | Yes (same events, same handler) |

Common logic (station table bookkeeping, `station_connected_sem`, MAC logging) runs
unconditionally for both modes.

---

## SoftAP Station Table

A static array of `MAX_SOFTAP_STATIONS` (4) entries tracks connected clients. Protected by
`K_MUTEX_DEFINE(softap_mutex)`. Compiled in only when `CONFIG_WIFI_NM_WPA_SUPPLICANT_AP=y`.
The table is used to count connected stations and to track MAC addresses for proper
remove-on-disconnect bookkeeping. It is shared between SoftAP and P2P_GO modes.

---

## Error Handling

| Condition | Behaviour |
|---|---|
| WPA supplicant not ready after 30 s | `LOG_ERR`; `network_module_init` returns `-ETIMEDOUT` |
| `WIFI_MODE_CHAN` read fails at boot | Log warning; default to SoftAP |
| `NET_EVENT_WIFI_CONNECT_RESULT` failure | Log error + status code (0=generic, 2=auth timeout, 3=auth fail, 15=AP not found, 16=assoc timeout) |
| `NET_EVENT_WIFI_AP_ENABLE_RESULT` failure | Log error; app notification not sent |
| DHCP bound in unexpected mode | Skip `zego_on_net_event_dhcp_bound()` call; log debug |

---

## Memory Estimate

| Item | Flash | RAM |
|---|---|---|
| `net_event_mgmt.c` | ~8 KB | ~1 KB (static state, semaphores, callbacks) |
| `wifi_utils.c` | ~4 KB | ~0.5 KB (SSID buffer, timer state) |
| **Total** | **~12 KB** | **~1.5 KB** |

---

## Test Points (UART log)

| Condition | Expected log |
|---|---|
| Module init | `[zego_net_event_mgmt] Initializing network event handlers` |
| Mode resolved | `[zego_net_event_mgmt] Active Wi-Fi mode: STA` (or other mode) |
| WPA supplicant ready | `[zego_net_event_mgmt] NET_EVENT_SUPPLICANT_READY` |
| SoftAP enabled | `[zego_net_event_mgmt] SoftAP enabled: SSID='...' IP='192.168.7.1' waiting for client` |
| STA connected (DHCP) | `[zego_net_event_mgmt] NET_EVENT_IPV4_DHCP_BOUND: ip=... ssid=...` |
| Wi-Fi disconnected | `[zego_net_event_mgmt] NET_EVENT_WIFI_DISCONNECT_RESULT: status=... reason=...` |
| P2P_CLIENT discovery start | `[zego_wifi_utils] P2P_CLIENT mode: scanning for peers (timeout: 30 s)...` |
| P2P peer found → PBC | `[zego_wifi_utils] P2P_CLIENT: peer found, trying WPS PBC...` |
| PBC fallback to PIN | `[zego_wifi_utils] P2P_CLIENT: PBC not accepted, trying PIN 12345678` |
| P2P_CLIENT connected | `[zego_net_event_mgmt] Wi-Fi connected: mode=P2P_CLIENT ip=... ssid=DIRECT-...` |
| P2P_CLIENT reconnect | `[zego_wifi_utils] P2P_CLIENT: disconnected, retrying in 5 s...` |
