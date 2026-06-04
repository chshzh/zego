# Network Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | `zego/network` |
| Version | 2026-06-04-17-10 |
| PRD Version | N/A (standalone library module) |
| NCS Version | v3.3.0 |
| Status | Stable |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-04-17-10 | Initial spec — reverse-designed from source |

---

## Overview

`zego/network` is the unified Wi-Fi / network event management layer for all zego-based
applications. It handles the full lifecycle of all four Wi-Fi modes (SoftAP, STA, P2P_GO,
P2P_CLIENT): waits for WPA supplicant ready, dispatches the selected mode's startup sequence,
monitors network events across all layers (L2–L4), and fires two `__weak` callback hooks
that applications override to publish app-specific zbus channels.

The module has no application-specific logic and contains no zbus channels of its own. It is
the bridge between the raw Zephyr net_mgmt event system and the application event model.

---

## Location

- **Path**: `zego/network/`
- **Files**: `src/net_event_mgmt.c`, `src/net_event_mgmt.h`, `src/wifi_utils.c`,
  `src/wifi_utils.h`, `Kconfig`, `Kconfig.defaults`, `CMakeLists.txt`, `zephyr/module.yml`

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

```c
/* net_event_mgmt.h — override both in src/modules/network/net_event_app.c */

/**
 * Called when Wi-Fi connectivity is established.
 *   STA / P2P_CLIENT  →  DHCP bound (IP assigned by AP or GO)
 *   SoftAP / P2P_GO   →  first client station associated
 */
void zego_network_on_wifi_connected(enum zego_wifi_mode mode,
                                    const char *ip_addr,   /* NUL-terminated dotted decimal */
                                    const char *mac_addr,  /* "XX:XX:XX:XX:XX:XX" */
                                    const char *ssid);     /* NUL-terminated, max 32 chars */

/**
 * Called when Wi-Fi connectivity is lost (DISCONNECT_RESULT received).
 */
void zego_network_on_wifi_disconnected(void);
```

Default implementations are `__weak` no-ops in `net_event_mgmt.c`. Override with strong
definitions in the application.

**Trigger events by mode:**

| Mode | Hook called when… | IP source |
|---|---|---|
| STA | `NET_EVENT_IPV4_DHCP_BOUND` | DHCP from AP |
| P2P_CLIENT | `NET_EVENT_IPV4_DHCP_BOUND` (SSID starts with `DIRECT-`) | DHCP from GO |
| SoftAP | `NET_EVENT_WIFI_AP_STA_CONNECTED` (first client) | Static (`CONFIG_NET_CONFIG_MY_IPV4_ADDR`) |
| P2P_GO | `NET_EVENT_WIFI_AP_STA_CONNECTED` (first client) | Static (`CONFIG_NET_CONFIG_MY_IPV4_ADDR`) |

> P2P_CLIENT detection: the module inspects the SSID at DHCP_BOUND time; if it starts with
> `DIRECT-`, the mode is reported as `ZEGO_WIFI_MODE_P2P_CLIENT`.

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
   P2P_CLIENT  → (no action; user runs 'wifi p2p find' + 'wifi p2p connect')
```

---

## Event Handler Map

| Event | Handler | Action |
|---|---|---|
| `NET_EVENT_IF_UP` / `IF_DOWN` | `l2_iface_event_handler` | Log only |
| `NET_EVENT_SUPPLICANT_READY` | `l3_wpa_supp_event_handler` | `k_sem_give(&wpa_supp_ready_sem)` |
| `NET_EVENT_SUPPLICANT_NOT_READY` | `l3_wpa_supp_event_handler` | Log error |
| `NET_EVENT_WIFI_CONNECT_RESULT` success | `l2_wifi_conn_event_handler` | Log; if P2P_GO, start DHCP server |
| `NET_EVENT_WIFI_CONNECT_RESULT` failure | `l2_wifi_conn_event_handler` | Log error with reason code |
| `NET_EVENT_WIFI_DISCONNECT_RESULT` | `l2_wifi_conn_event_handler` | Clear `network_connected`; call `zego_network_on_wifi_disconnected()` |
| `NET_EVENT_WIFI_AP_ENABLE_RESULT` success | `l2_softap_event_handler` (AP guard) | Re-assert static IP; log |
| `NET_EVENT_WIFI_AP_STA_CONNECTED` | `l2_softap_event_handler` (AP guard) | Track station; `k_sem_give(&station_connected_sem)`; call `zego_network_on_wifi_connected()` |
| `NET_EVENT_WIFI_AP_STA_DISCONNECTED` | `l2_softap_event_handler` (AP guard) | Remove station from table; log |
| `NET_EVENT_IPV4_DHCP_BOUND` | `l3_ipv4_event_handler` | Log IP; re-query SSID; call `zego_network_on_wifi_connected()` |
| `NET_EVENT_L4_CONNECTED` | `l4_event_handler` | Log (early SSID capture placeholder) |
| `NET_EVENT_L4_DISCONNECTED` | `l4_event_handler` | Log |

> `l2_softap_event_handler` and the station table are compiled only when
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

---

## SoftAP Station Table

A static array of `MAX_SOFTAP_STATIONS` (4) entries tracks connected clients. Protected by
`K_MUTEX_DEFINE(softap_mutex)`. Compiled in only when `CONFIG_WIFI_NM_WPA_SUPPLICANT_AP=y`.
The table is used to count connected stations and to track MAC addresses for proper
remove-on-disconnect bookkeeping.

---

## Error Handling

| Condition | Behaviour |
|---|---|
| WPA supplicant not ready after 30 s | `LOG_ERR`; `network_module_init` returns `-ETIMEDOUT` |
| `WIFI_MODE_CHAN` read fails at boot | Log warning; default to SoftAP |
| `NET_EVENT_WIFI_CONNECT_RESULT` failure | Log error + status code (0=generic, 2=auth timeout, 3=auth fail, 15=AP not found, 16=assoc timeout) |
| `NET_EVENT_WIFI_AP_ENABLE_RESULT` failure | Log error; app notification not sent |
| DHCP bound in unexpected mode | Skip `zego_network_on_wifi_connected()` call; log debug |

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
