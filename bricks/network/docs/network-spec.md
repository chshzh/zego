# Network Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | `zego/network` |
| Version | 2026-07-01-14-15 |
| PRD Version | N/A (standalone library module) |
| NCS Version | v3.3.0 |
| Status | Stable |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-07-01-14-15 | Fixed stale P2P_GC IP-assignment description that had drifted from the code since the 2026-06-30 PBC/NVS-persistence rework: the spec still said P2P_GC gets a hardcoded static IP (192.168.7.2/24) and calls `zego_on_net_event_dhcp_bound()` directly from `CONNECT_RESULT`. The actual code (unchanged by this edit) has P2P_GC restart the DHCP **client** on `CONNECT_RESULT` success and wait for real `NET_EVENT_IPV4_DHCP_BOUND`, exactly like STA — needed because a phone GO hands out a different subnet than a DK GO. Updated the P2P_GC Reconnect Sequence, the `zego_on_net_event_dhcp_bound()` doc-comment, the P2P_GC detection note, and the Event Handler Map row accordingly. Noted `wifi_p2p_gc_setup_static_ip()` is now dead code (kept, not called). No P2P_GO changes: its DHCP server setup was already, and remains, the same `wifi_setup_dhcp_server()` call shared with SoftAP. |
| 2026-07-01-10-54 | Updated to PRD v2026-07-01-10-50 (nordic-wifi-app-template): disconnection-handling overhaul. (1) `zego_on_net_event_wifi_disconnect()` gains a `bool will_retry` parameter so the app can show ROTATE (retrying) vs fast BLINK (no retry possible) instead of always signalling error. (2) New **STA Reconnect Sequence**: the L3 watchdog's `l3_reconnect_work` now also fires directly on `NET_EVENT_WIFI_DISCONNECT_RESULT` (not just after the DHCP-bind timeout), so STA retries stored credentials on every disconnect on boards without `CONFIG_ZEGO_WIFI_BLE_PROV` (nRF7002DK, nRF5340 Audio DK); unchanged on nRF54LM20DK, where `wifi_ble_prov`'s own reconnect loop continues to own retry timing. STA with zero stored credentials (checked via `wifi_credentials_for_each_ssid()`) skips the connect attempt and reports `will_retry=false` instead of leaving the LED rotating forever. (3) P2P_GC pairing (button-triggered or auto-started) no longer gives up after `P2P_PAIR_MAX_FIND_CYCLES` — it now retries discovery indefinitely; the Kconfig-like constant is removed. (4) P2P_GC with no saved GO now auto-starts the pairing sequence at boot instead of idling. (5) Documented the AP inactivity-timeout worst case (~5 min) for undetected SoftAP/P2P_GO client loss, and that P2P_GO's "1 client" expectation is informational only (not enforced by the AP stack). (6) Corrected the P2P_GC peer-selection description in the Pairing Sequence: it was still documenting a `group_capab` GO-bit filter that supp_api.c has never reliably populated (confirmed `group_capab` is always 0 in the P2P_PEER response); the implemented and now-fixed logic simply joins the strongest-RSSI peer, preferring an already-saved GO MAC when it reappears — this spec had drifted from the code independent of today's PRD change. |
| 2026-06-30-13-04 | Reconciled to implemented code: WPS **PBC** (fixed PIN unsupported on nRF GO — `WIFI_WPS_PIN_SET` fails `wps_registrar_init()`); GO/GC use `pbc --join`. Root-cause + fix: `CONFIG_WIFI_NM_WPA_SUPPLICANT_GLOBAL_HEAP=y` (a dedicated supplicant heap starved the WPS Registrar). Added GC GO-capability peer filter (`group_capab` GO bit), pairing-trigger re-entrancy guard, and `zego_on_net_event_p2p_pairing(bool)` weak hook for pairing-active LED BREATHE. |
| 2026-06-29-21-44 | P2P pairing redesign: removed `CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC` (exact + prefix modes). P2P_GC now learns the GO MAC at runtime via a button-triggered WPS PBC pairing (`wifi_p2p_start_pairing()`) and persists it to NVS key `net/p2p_gc_go_mac`; reconnects to the saved MAC on disconnect and after power cycle. P2P_GO arms WPS PBC continuously and refreshes the pairing window on double-click. WPS PBC is the headless method per `nrf/samples/wifi/p2p`. Renamed `wifi_run_p2p_client_mode()`→`wifi_run_p2p_gc_mode()` and timeouts `*_P2P_CLIENT_*`→`*_P2P_GC_*`; added `P2P_PAIR_FIND_TIMEOUT_S`. P2P_CLIENT→P2P_GC naming aligned in touched sections. |
| 2026-06-17-09-44 | Added optional mDNS / DNS-SD support: `ZEGO_NETWORK_MDNS`, `ZEGO_NETWORK_MDNS_HTTP_PORT`; new `src/mdns.c`; `Kconfig.defaults` hostname defaults; updated Memory Estimate and Test Points |
| 2026-06-04-17-10 | Initial spec — reverse-designed from source |
| 2026-06-05-09-31 | Added Supported Hardware section; documented nRF5340 Audio DK + nRF7002EK |
| 2026-06-09-17-25 | P2P_CLIENT auto-connect: `wifi_run_p2p_client_mode()` starts peer discovery at boot; PBC first then PIN 12345678 fallback; 30 s retry, 5 s reconnect delay; added Kconfig, API table entry, test points |
| 2026-06-10-00-00 | Clarified P2P_GO/SoftAP shared AP handler; renamed `l2_softap_event_handler`→`l2_ap_event_handler`, `L2_SOFTAP_MASK`→`L2_AP_MASK`, `softap_event_cb`→`ap_event_cb`, `zego_on_net_event_softap_ready`→`zego_on_net_event_wifi_ap_enabled`, `zego_on_net_event_softap_sta_disconnected`→`zego_on_net_event_wifi_ap_sta_disconnected`; added P2P_GO vs SoftAP comparison table |
| 2026-06-11-13-52 | Expanded Weak-hook API section to all 6 hooks; added `zego_on_net_event_wifi_connect`, `zego_on_net_event_wifi_ap_sta_connected`, `zego_on_net_event_wifi_ap_enabled`, `zego_on_net_event_wifi_ap_sta_disconnected` with full signatures and trigger context |
| 2026-06-16-11-26 | P2P_CLIENT: added MAC-prefix auto-select mode (FR-108) — when CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC ends in :00:00:00, device runs P2P peer discovery, collects all GOs matching the 3-byte OUI prefix, and connects to the one with the highest RSSI; exact 6-byte MAC mode unchanged. New sequence section and test points added. Kconfig description updated |
| 2026-06-16-13-02 | P2P_CLIENT prefix mode: corrected implementation to Find+Query design — after P2P_FIND window (10 s), peer table is queried directly via WIFI_P2P_PEER+broadcast MAC instead of relying on NET_EVENT_WIFI_P2P_DEVICE_FOUND events (which silently skip cached peers). Updated sequence and test point logs. Parameters: P2P_PREFIX_FIND_TIMEOUT_S=10, P2P_PREFIX_MAX_CANDIDATES=5, reschedule at +12 s |
| 2026-06-16-11-21 | P2P_GO: phone-as-P2P-client dropped — WPS negotiation with Android fails; GO only accepts DK P2P_CLIENT connections. `wifi_run_p2p_go_mode()` description updated (PBC, no 5-min PIN timer). P2P_FIND removed from PBC arm path (always fails on running GO — radio is anchored to group channel) |
| 2026-06-14-00-21 | P2P_CLIENT: replaced discovery+PBC+PIN flow with direct --join using CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC; static IP 192.168.7.2/24 assigned at CONNECT_RESULT; connect retry 90 s; reconnect delay 15 s. P2P_GO: added PBC auto-rearm on client disconnect and every 110 s. Hook trigger map: zego_on_net_event_dhcp_bound for P2P_CLIENT now triggered from CONNECT_RESULT success (not DHCP_BOUND). Kconfig updated |

---

## Overview

`zego/network` is the unified Wi-Fi / network event management layer for all zego-based
applications. It handles the full lifecycle of all four Wi-Fi modes (SoftAP, STA, P2P_GO,
P2P_GC): waits for WPA supplicant ready, dispatches the selected mode's startup sequence,
monitors network events across all layers (L2–L4), and fires seven `__weak` callback hooks
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
 * Called when STA / P2P_GC association succeeds (L2 connected, before DHCP).
 * Device is associated but has no routable IP yet.
 */
void zego_on_net_event_wifi_connect(enum zego_wifi_mode mode);

/**
 * Called when STA / P2P_GC obtains its IP via DHCP (P2P_GC gets its lease from the
 * GO's DHCP server — a DK GO hands out 192.168.7.x, a phone GO its own subnet, e.g.
 * 192.168.49.x), or when the first SoftAP / P2P_GO client associates (static IP).
 */
void zego_on_net_event_dhcp_bound(enum zego_wifi_mode mode,
                                  const char *ip_addr,   /* NUL-terminated dotted decimal */
                                  const char *mac_addr,  /* "XX:XX:XX:XX:XX:XX" */
                                  const char *ssid);     /* NUL-terminated, max 32 chars */

/**
 * Called when Wi-Fi connectivity is lost (DISCONNECT_RESULT received), or when
 * STA mode starts with zero stored credentials (nothing to connect to).
 *
 * @param will_retry  true  - the module will keep retrying automatically and the
 *                            link is expected to recover without user action
 *                            (STA with >=1 stored credential; P2P_GC always, since
 *                            it either reconnects to its saved GO or auto-pairs
 *                            indefinitely). The app should show its "trying" LED
 *                            state (ROTATE).
 *                    false - no automatic retry is possible: STA has zero stored
 *                            Wi-Fi credentials. This is the only false case today.
 *                            The app should show its "action needed" LED state
 *                            (fast BLINK).
 */
void zego_on_net_event_wifi_disconnect(bool will_retry);

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

/**
 * Called by the P2P engine when a pairing attempt starts/ends, so the app can show a
 * pairing-active LED BREATHE indication.
 *   P2P_GC: active=true when discovery starts; active=false on connect success OR give-up.
 *   P2P_GO: active=true when wifi_p2p_start_pairing() opens the window; active=false when
 *           the window expires or a client connects.
 */
void zego_on_net_event_p2p_pairing(bool active);
```

**Hook trigger map:**

| Hook | Trigger event | Mode(s) |
|---|---|---|
| `zego_on_net_event_wifi_connect` | `NET_EVENT_WIFI_CONNECT_RESULT` success | STA, P2P_GC |
| `zego_on_net_event_dhcp_bound` | `NET_EVENT_IPV4_DHCP_BOUND` | STA, P2P_GC |
| `zego_on_net_event_dhcp_bound` | `NET_EVENT_WIFI_AP_STA_CONNECTED` (first client) | SoftAP, P2P_GO |
| `zego_on_net_event_wifi_disconnect(will_retry)` | `NET_EVENT_WIFI_DISCONNECT_RESULT`; also called directly (no event) when STA mode starts with zero stored credentials | STA, P2P_GC |
| `zego_on_net_event_wifi_ap_enabled` | `NET_EVENT_WIFI_AP_ENABLE_RESULT` success | SoftAP, P2P_GO |
| `zego_on_net_event_wifi_ap_sta_connected` | `NET_EVENT_WIFI_AP_STA_CONNECTED` | SoftAP, P2P_GO |
| `zego_on_net_event_wifi_ap_sta_disconnected` | `NET_EVENT_WIFI_AP_STA_DISCONNECTED` | SoftAP, P2P_GO |
| `zego_on_net_event_p2p_pairing(true)` | Pairing attempt starts (GC: discovery start; GO: pairing window opens) | P2P_GC, P2P_GO |
| `zego_on_net_event_p2p_pairing(false)` | Pairing attempt ends (GC: connect success or give-up; GO: window expires or client connects) | P2P_GC, P2P_GO |

> `zego_on_net_event_p2p_pairing(bool)`: `net_event_app.c` maps `active=true` to
> `APP_WIFI_STATE_PAIRING` and `active=false` back to the resolved current state, so the UX
> LED shows a BREATHE pattern while a pairing attempt is in flight.

> P2P_GC detection: the mode is known at boot from `WIFI_MODE_CHAN`; `zego_on_net_event_dhcp_bound()`
> is called with `mode=ZEGO_WIFI_MODE_P2P_GC` from the `NET_EVENT_IPV4_DHCP_BOUND` handler, exactly
> like STA — `CONNECT_RESULT` success only restarts the DHCP client, it does not call the hook.

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
   STA         → if wifi_credentials_for_each_ssid() finds >=1 entry: NET_REQUEST_WIFI_CONNECT_STORED
                 else: zego_on_net_event_wifi_disconnect(false) directly (nothing to connect to)
   P2P_GO      → wifi_run_p2p_go_mode()
   P2P_GC      → wifi_run_p2p_gc_mode()   (reconnect to saved GO MAC if any; else auto-start pairing)
```

---

## STA Reconnect Sequence

STA must keep retrying a stored network after any disconnect — the device should never sit
disconnected with credentials it could reconnect with. Two mechanisms cooperate, split by
whether `CONFIG_ZEGO_WIFI_BLE_PROV` is enabled, so neither board configuration ends up with
two competing reconnect loops:

| Board configuration | Owner | Trigger |
|---|---|---|
| `CONFIG_ZEGO_WIFI_BLE_PROV=y` (nRF54LM20DK) | `zego/wifi_ble_prov` | Its own `NET_EVENT_WIFI_DISCONNECT_RESULT` handler cycles through stored SSIDs (see `wifi-ble-prov-spec.md`). Unchanged by this update. |
| `CONFIG_ZEGO_WIFI_BLE_PROV=n` (nRF7002DK, nRF5340 Audio DK) | `zego/network` (`l3_reconnect_work`) | Now fires directly on `NET_EVENT_WIFI_DISCONNECT_RESULT`, **in addition to** the existing L3-DHCP-timeout watchdog path. |

```
NET_EVENT_WIFI_DISCONNECT_RESULT (STA, CONFIG_ZEGO_WIFI_BLE_PROV=n):
  → network_connected = false; l3_watchdog_cancel()
  → has_creds = wifi_credentials_for_each_ssid() finds >=1 entry
    (re-checked here, not assumed from the prior connection - a one-time shell
    `wifi connect` that was never saved via `wifi cred add` also disconnects with
    zero stored credentials)
  → zego_on_net_event_wifi_disconnect(has_creds)
  → if has_creds: k_work_reschedule(&l3_reconnect_work, K_SECONDS(2))
    else: do not schedule a retry (nothing to retry with)

l3_reconnect_handler (system workqueue):
  → if active_mode != STA or already connected: return (stale work item, no-op)
  → NET_REQUEST_WIFI_CONNECT_STORED
  → on failure: reschedule self in 5 s (retries indefinitely - never gives up on its own)
```

> **A failed connect attempt also needs a retry, not just a disconnect after being
> connected.** wpa_supplicant does **not** fire `NET_EVENT_WIFI_DISCONNECT_RESULT` after a
> failed `NET_EVENT_WIFI_CONNECT_RESULT` (auth failure, timeout, AP not found) — only after a
> *successful* connection later drops. `wifi_ble_prov` already accounts for this (see its own
> `CONNECT_RESULT` failure handler). `zego/network`'s STA path does the same: the
> `CONNECT_RESULT` failure branch also computes `has_creds` and calls
> `zego_on_net_event_wifi_disconnect(has_creds)`, and (when `!CONFIG_ZEGO_WIFI_BLE_PROV`)
> reschedules `l3_reconnect_work` — otherwise a STA that never associates in the first place
> (wrong password, AP briefly unreachable at boot) would sit forever with no further attempt.

> **Why not remove the `CONFIG_ZEGO_WIFI_BLE_PROV` gate entirely?** `wifi_ble_prov`'s reconnect
> loop already works correctly on nRF54LM20DK and additionally rotates through multiple stored
> SSIDs in priority order. Running `zego/network`'s `l3_reconnect_work` at the same time would
> race both mechanisms against the same `NET_REQUEST_WIFI_CONNECT_STORED` call, risking a
> "scan already in progress" wpa_supplicant error (the same failure class the P2P_GC pairing
> code above is careful to avoid). Keeping ownership split by board configuration fixes the
> nRF7002DK/nRF5340 Audio DK gap without touching the already-working nRF54LM20DK path.

> **Zero stored credentials**: checked once via `wifi_credentials_for_each_ssid()` at STA mode
> start (see Boot Sequence). If it ever transitions from zero to one-or-more (e.g. `wifi cred add`
> or BLE provisioning while showing the BLINK state), the existing `wifi cred auto_connect` /
> BLE-prov connect path issues `NET_REQUEST_WIFI_CONNECT_STORED` directly, which produces a normal
> `CONNECT_RESULT`/`DHCP_BOUND` and moves the LED to ROTATE→CONNECTED — no new mechanism required.

---

## P2P_GC Reconnect Sequence (saved GO MAC)

`wifi_run_p2p_gc_mode()` reconnects to a **previously-paired** GO without any shell or
button interaction. There is **no compile-time GO MAC** — the target is the MAC learned
during pairing and persisted in NVS (see [P2P_GC Pairing](#p2p_gc-pairing-sequence-button-triggered)).
At boot the saved MAC is loaded from settings key `net/p2p_gc_go_mac`.

```
0. Load saved GO MAC from NVS (net/p2p_gc_go_mac).
   → if empty (never paired): automatically start the pairing sequence below - no button
     press required (see P2P_GC Pairing Sequence). A double-click still works at any time
     to (re-)start pairing, e.g. to target a different GO.
   → if present: proceed to step 1.

1. Issue WIFI_P2P_CONNECT to the saved MAC using method=PBC and flag=--join
   ("wifi p2p connect <saved-MAC> pbc --join")
   → set p2p_gc_pending = true; schedule p2p_gc_timeout_work in 90 s

2a. NET_EVENT_WIFI_CONNECT_RESULT success →
    → wifi_p2p_gc_on_connect_result(true): cancel timeout work; p2p_gc_pending = false
    → net_dhcpv4_restart(wlan0) — GC is a normal DHCP client here, same as STA; this
      allows a DK GO (192.168.7.x pool) or a phone GO (its own subnet, e.g. 192.168.49.x)
      to hand out the lease
    → on the resulting NET_EVENT_IPV4_DHCP_BOUND: zego_on_net_event_dhcp_bound(P2P_GC, ip,
      mac, ssid) called — identical path to STA (see Event Handler Map below)

2b. NET_EVENT_WIFI_CONNECT_RESULT failure (GO not reachable) →
    → log warning; timeout work still armed

3.  p2p_gc_timeout_work fires after P2P_GC_CONNECT_TIMEOUT_S (90 s) →
    → wpa_supplicant has exhausted its 10 internal join-scan attempts and is idle
    → p2p_gc_pending = false; go back to step 1 (fresh P2P_CONNECT to saved MAC)

4.  NET_EVENT_WIFI_DISCONNECT_RESULT in P2P_GC mode →
    → cancel any pending connect timeout
    → zego_on_net_event_wifi_disconnect(true) called — P2P_GC always retries (saved-MAC
      reconnect or pairing discovery), so will_retry is unconditionally true here
    → schedule reconnect in P2P_GC_RECONNECT_DELAY_S (15 s) to allow wpa_supplicant
      background cleanup scan to drain before next connect attempt
    → go back to step 1
```

> **Reconnect after power cycle**: because the GO MAC is persisted, a freshly-booted GC with
> a saved MAC re-enters step 1 automatically. The paired GO keeps its WPS PBC armed
> continuously (see [P2P_GO](#p2p_go-pairing-window)), so the `pbc --join` succeeds
> without any button press on either device. Satisfies PRD FR-107 (5).

> **Why 90 s timeout / 15 s reconnect delay?** Same wpa_supplicant scan-cycle constraints as
> before: `P2P_MAX_JOIN_SCAN_ATTEMPTS` (10) scans of ~8–9 s, and a 5–17 s post-deauth cleanup
> scan that must drain before re-issuing `P2P_CONNECT`.

> **Real DHCP, not a hardcoded static IP**: P2P_GC uses `net_dhcpv4_restart()` on the wlan0
> iface after `CONNECT_RESULT` and waits for `NET_EVENT_IPV4_DHCP_BOUND`, exactly like STA.
> This is required because the GO is not always a DK on 192.168.7.0/24 — a phone GO runs its
> own DHCP server on a different subnet (typically 192.168.49.0/24). `wifi_p2p_gc_setup_static_ip()`
> (hardcoded 192.168.7.2/24) predates this and is no longer called anywhere.

LED feedback flows through `APP_WIFI_STATE_CHAN` in `net_event_app.c` — the UX module sees
`APP_WIFI_STATE_CONNECTING` during the connect and `APP_WIFI_STATE_CONNECTED` after
CONNECT_RESULT, driving the same ROTATE → solid-ON LED transitions as STA mode.

---

## P2P_GC Pairing Sequence (button-triggered)

Pairing replaces the old compile-time target-MAC mechanism. It is triggered either
**automatically** (`wifi_run_p2p_gc_mode()` at boot/mode-entry when no GO is saved — see step 0
above) or **manually** by `wifi_p2p_start_pairing()` (called from the UX module on a
double-click of Button 0 while in P2P_GC mode — see ux-module spec); both paths run the
identical sequence below. The GC discovers the GO that is currently in its WPS PBC
pairing window, joins it (`pbc --join`), and **persists the GO's MAC** so all future
reconnects use the saved-MAC path above.

```
Pairing sequence (on wifi_p2p_start_pairing in P2P_GC mode):

1. Issue WIFI_P2P_FIND (WIFI_P2P_FIND_ONLY_SOCIAL, timeout=P2P_PAIR_FIND_TIMEOUT_S=10 s)
   Set p2p_find_running=true; reschedule work item in (10 + 2) s.

   NOTE: NET_EVENT_WIFI_P2P_DEVICE_FOUND events are NOT used for candidate collection
   (cached peers never re-fire the event). The peer table is queried directly instead.

2. After the find window elapses (+2 s margin):
   Set p2p_find_running=false.
   Query peer table: WIFI_P2P_PEER + broadcast MAC (0xFF:FF:FF:FF:FF:FF) via
   NET_REQUEST_WIFI_P2P_OPER — equivalent to `wifi p2p peer`.
   Buffer: static peer_buf[P2P_PAIR_MAX_CANDIDATES=5] (BSS; memset before each query).

3. Select the peer to join:
   → among all discovered peers, keep the entry with the highest RSSI (join whichever
     candidate GO is nearest); if a GO MAC is already saved in NVS, prefer that exact
     MAC when it reappears in the peer table over the RSSI heuristic (re-pairing stays
     sticky to the previously-known GO across retries)
   → if none found: log warning; reschedule discovery (step 1) after a short delay —
     **retries indefinitely**; a pairing attempt never gives up or returns to idle on
     its own (started automatically or via double-click, it keeps searching until it
     succeeds or the mode is switched away from P2P_GC)
   → if a candidate is found: proceed to step 4

4. Issue WIFI_P2P_CONNECT to the selected MAC using method=PBC and flag=--join
   ("wifi p2p connect <selected-MAC> pbc --join")
   → set p2p_gc_pending = true; arm the 90 s timeout (as in the reconnect sequence)

5. On NET_EVENT_WIFI_CONNECT_RESULT success:
   → save the selected GO MAC to NVS: settings_save_one("net/p2p_gc_go_mac", mac, 6)
     (overwrites any previously-saved MAC — this is how re-pairing forgets the old GO)
   → from here the saved-MAC reconnect path (above) owns all future (re)connects
```

> **Why PBC?** The nRF DK acting as GO does **not** support a fixed WPS PIN: arming
> `WIFI_WPS_PIN_SET` fails the GO's WPS Registrar init (`wps_registrar_init()`), so the GO
> never reaches AP-ENABLED and cannot pair. PBC is the supported headless method (it is what
> `nrf/samples/wifi/p2p` uses), needs no out-of-band PIN transfer, and works for button-only
> pairing on both sides.

> **Why discovery?** Without a configured MAC the GC must find the GO at runtime. The double
> click on the GC is what expresses intent to pair; it joins whichever GO is currently armed.

> **Pairing-trigger re-entrancy**: in P2P_GC mode `wifi_p2p_start_pairing()` ignores repeat
> double-clicks while a pairing find/connect is already in flight (`p2p_find_running` /
> `p2p_gc_pending`), guarding against a "Scan already in progress" error from wpa_supplicant.
> A re-pair gesture while already connected is still honoured — it runs a fresh discovery and
> overwrites the saved GO MAC.

> **Multiple GOs**: if more than one GO is pairing simultaneously the highest-RSSI one wins.
> This is a deliberate simplification — the expected case is one GO + one GC pairing at a time.

---

## Event Handler Map

| Event | Handler | Action |
|---|---|---|
| `NET_EVENT_IF_UP` / `IF_DOWN` | `l2_iface_event_handler` | Log only |
| `NET_EVENT_SUPPLICANT_READY` | `l3_wpa_supp_event_handler` | `k_sem_give(&wpa_supp_ready_sem)` |
| `NET_EVENT_SUPPLICANT_NOT_READY` | `l3_wpa_supp_event_handler` | Log error |
| `NET_EVENT_WIFI_CONNECT_RESULT` success | `l2_wifi_conn_event_handler` | Log; if P2P_GO, start DHCP server |
| `NET_EVENT_WIFI_CONNECT_RESULT` failure | `l2_wifi_conn_event_handler` | Log error with reason code |
| `NET_EVENT_WIFI_DISCONNECT_RESULT` | `l2_wifi_conn_event_handler` | Clear `network_connected`; call `zego_on_net_event_wifi_disconnect(will_retry)` (STA: `will_retry` = stored credentials exist); if no BLE prov, schedule `l3_reconnect_work` |
| `NET_EVENT_WIFI_AP_ENABLE_RESULT` success | `l2_ap_event_handler` (AP guard) | Re-assert static IP; call `zego_on_net_event_wifi_ap_enabled()` |
| `NET_EVENT_WIFI_AP_STA_CONNECTED` | `l2_ap_event_handler` (AP guard) | Track station; `k_sem_give(&station_connected_sem)`; call `zego_on_net_event_dhcp_bound()` |
| `NET_EVENT_WIFI_AP_STA_DISCONNECTED` | `l2_ap_event_handler` (AP guard) | Remove station from table; call `zego_on_net_event_wifi_ap_sta_disconnected()` |
| `NET_EVENT_WIFI_CONNECT_RESULT` success (P2P_GC) | `l2_wifi_conn_event_handler` → `wifi_p2p_gc_on_connect_result(true)`, then `net_dhcpv4_restart()` | Cancel timeout work; restart DHCP client (waits for `NET_EVENT_IPV4_DHCP_BOUND` below); **if connect was a pairing attempt, persist the GO MAC to `net/p2p_gc_go_mac`** |
| `NET_EVENT_WIFI_DISCONNECT_RESULT` (P2P_GC) | `l2_wifi_conn_event_handler` → `wifi_p2p_gc_on_disconnect()` | Cancel timeout work; call `zego_on_net_event_wifi_disconnect(true)` (P2P_GC always retries); reschedule connect to saved MAC in 15 s |
| `NET_EVENT_IPV4_DHCP_BOUND` | `l3_ipv4_event_handler` | Log IP; re-query SSID; call `zego_on_net_event_dhcp_bound()` (STA and P2P_GC both go through this same path) |
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
| `wifi_run_p2p_go_mode()` | Create P2P group, arm WPS PBC (continuously re-armed); only DK P2P_GC connections supported |
| `wifi_setup_dhcp_server()` | Assign static IP + start DHCPv4 server (idempotent) |
| `wifi_utils_auto_connect_stored()` | Trigger `NET_REQUEST_WIFI_CONNECT_STORED` |
| `wifi_utils_ensure_gateway_softap_credentials()` | Write default SoftAP creds to settings if absent |
| `wifi_utils_get_last_ssid()` | Return last connected SSID string |
| `wifi_softap_cancel_remind_timer()` | Cancel SoftAP periodic reminder work |
| `wifi_p2p_go_cancel_wps_timer()` | Cancel P2P_GO WPS re-arm timer |
| `wifi_p2p_go_rearm_wps_pin()` | Re-arm WPS PBC immediately (name kept for ABI; arms PBC). Used on client disconnect and to refresh the pairing window |
| `wifi_run_p2p_gc_mode()` | If a GO MAC is saved in NVS, connect to it via `pbc --join` (reconnect path, retries indefinitely); else automatically start the pairing sequence (retries indefinitely until a GO is found and joined) |
| `wifi_p2p_start_pairing()` | Mode-aware pairing trigger (called by UX on Button 0 double-click in P2P modes). In **P2P_GO**: refresh the WPS PBC pairing window. In **P2P_GC**: run discovery, join the pairing GO via `pbc --join`, and persist its MAC to `net/p2p_gc_go_mac`; re-entrant double-clicks are ignored while a find/connect is in flight. No-op in STA/SoftAP |
| `wifi_p2p_gc_setup_static_ip()` | Assign 192.168.7.2/24 to wlan0; **unused** — P2P_GC now gets its IP from the GO's DHCP server via `net_dhcpv4_restart()` instead (a phone GO uses a different subnet than a DK GO) |
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
> **Removed**: `CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC` (and its exact-MAC / prefix-RSSI
> auto-connect behaviour) is deleted. There is no replacement Kconfig — the target GO MAC is
> learned at runtime during pairing and stored in NVS.

**P2P_GC timing constants** are compile-time `#define`s in `bricks/network/src/wifi_utils.c`
(not Kconfig):

| Constant | Value | Purpose |
|---|---|---|
| `P2P_GC_CONNECT_TIMEOUT_S` | 90 | Wait for CONNECT_RESULT before re-issuing P2P_CONNECT; must exceed wpa_supplicant's ~64 s 10-scan join cycle |
| `P2P_GC_RECONNECT_DELAY_S` | 15 | Wait after disconnect before reconnecting; lets the wpa_supplicant cleanup scan drain |
| `P2P_PAIR_FIND_TIMEOUT_S` | 10 | Pairing discovery window (social-channel `WIFI_P2P_FIND`) before the peer table is queried |
| `P2P_PAIR_MAX_CANDIDATES` | 5 | Peer-query buffer size |

> **Removed**: `P2P_PAIR_MAX_FIND_CYCLES` (previously 2 — empty discovery cycles before a
> pairing attempt gave up and returned to idle). Pairing now retries discovery indefinitely
> instead of giving up; see [P2P_GC Pairing Sequence](#p2p_gc-pairing-sequence-button-triggered).

**NVS persistence**: the learned GO MAC is stored at settings key **`net/p2p_gc_go_mac`**
(6 raw MAC bytes) under a dedicated `"net"` settings subtree owned by the network brick
(`SETTINGS_STATIC_HANDLER_DEFINE(zego_net_settings, "net", ...)`). A separate subtree from the
wifi mode selector's `"app"` handler is required — two static handlers cannot share one subtree
name. An empty/absent key means "never paired".

---

## P2P_GO Pairing Window

`wifi_run_p2p_go_mode()` creates the P2P group (`WIFI_P2P_GROUP_ADD`, `persistent=-1`) and
then arms **WPS PBC** (`WIFI_WPS_PBC` via `NET_REQUEST_WIFI_WPS_CONFIG`) so a P2P_GC can join.
PBC is used because the nRF DK-as-GO does **not** support a fixed WPS PIN — arming
`WIFI_WPS_PIN_SET` fails the GO's WPS Registrar init (`wps_registrar_init()`) so the GO never
reaches AP-ENABLED. PBC is the supported headless method (it is what `nrf/samples/wifi/p2p`
uses).

```
1. WIFI_P2P_GROUP_ADD                 → group created, SSID "DIRECT-xx"
2. Arm WPS PBC (p2p_go_wps_work)      → GO is connectable by `pbc --join`
3. Re-arm WPS PBC every P2P_GO_WPS_REARM_INTERVAL_S and on client disconnect
   (wifi_p2p_go_rearm_wps_pin) so the GO stays connectable for reconnects.
```

The GO keeps WPS PBC **continuously armed** (periodic re-arm + re-arm on disconnect). This is
required so a previously-paired GC can silently reconnect after a power cycle — the GO has no
stored memory of which client it paired with (per the design decision: *GC stores GO MAC,
GO stores nothing*), so it must remain connectable to any GC requesting PBC.

### Double-click pairing trigger

`wifi_p2p_start_pairing()` in P2P_GO mode opens/refreshes the pairing window:

```
wifi_p2p_start_pairing() [mode == P2P_GO]:
  → log "P2P_GO: pairing window open (~PAIR_WINDOW_S) — double-click a P2P_GC to pair"
  → wifi_p2p_go_rearm_wps_pin()   (refresh the WPS PBC pairing window immediately)
```

> The "~2-minute window" (PRD FR-006) is a UX announcement + fresh WPS PBC re-arm. Because PBC
> is also continuously re-armed for reconnect, the GO does not hard-close the window; the
> double click simply guarantees a fresh WPS state and signals intent in the log.

---

## P2P_GO WPS Registrar — heap requirement

The P2P_GO hostapd path calls `wps_registrar_init()` when arming WPS, which allocates from the
wpa_supplicant heap. With a **dedicated** supplicant K_HEAP
(`CONFIG_WIFI_NM_WPA_SUPPLICANT_GLOBAL_HEAP=n`) that allocation failed, producing this chain:

```
Failed to initialize WPS Registrar
→ Failed to initialize AP interface
→ GO never reaches AP-ENABLED → cannot pair
```

**Fix**: use the **shared system heap** —
`CONFIG_WIFI_NM_WPA_SUPPLICANT_GLOBAL_HEAP=y` (the NCS default), as `nrf/samples/wifi/p2p`
does. Confirmed on hardware (nRF5340 Audio DK): with the shared heap the WPS Registrar
initialises, the GO reaches AP-ENABLED, and pairing succeeds.

> **Trade-off**: with the global heap the supplicant no longer has a separate ZView pool — its
> allocations are accounted against the shared system heap rather than an isolated supplicant
> heap.

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
| Typical client count | 1–4 (`CONFIG_WIFI_MGMT_AP_MAX_NUM_STA`, 4th rejected) | 1 expected (**informational only** — the shared station table and AP stack do not reject a 2nd P2P_GC; nothing in practice initiates a second Wi-Fi Direct connection) |
| `NET_EVENT_WIFI_AP_*` events | Yes | Yes (same events, same handler) |

Common logic (station table bookkeeping, `station_connected_sem`, MAC logging) runs
unconditionally for both modes.

> **Undetected client loss (both modes)**: a station that disappears without sending a
> deauth/disassoc frame (power cut, crashed, battery pull) is not immediately reported —
> `NET_EVENT_WIFI_AP_STA_DISCONNECTED` only fires once hostapd's AP inactivity timer
> (`ap_max_inactivity`, default 300 s) evicts the station after failed keepalive probes. Worst
> case ~5 minutes between physical loss and the disconnect event / LED update. Lower it at
> runtime for faster detection: `wpa_cli -i wlan0 set ap_max_inactivity 30` (no Kconfig
> equivalent). A clean shutdown (normal reboot) sends a deauth and fires the event immediately.

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
| STA mode start, or STA disconnect, with zero stored credentials | Skip `NET_REQUEST_WIFI_CONNECT_STORED`; call `zego_on_net_event_wifi_disconnect(false)` directly so the app shows its "action needed" LED state instead of rotating forever |
| P2P_GC pairing discovery finds no candidate GO | Log warning; reschedule discovery — retries indefinitely, never gives up on its own |

---


## mDNS / DNS-SD

Optional feature — enabled with `CONFIG_ZEGO_NETWORK_MDNS=y`.

When enabled the device is reachable as `<hostname>.local` on any active Wi-Fi interface.
No application code is required; all behaviour is driven by Kconfig.

### Kconfig symbols

| Symbol | Type | Default | Description |
|---|---|---|---|
| `CONFIG_ZEGO_NETWORK_MDNS` | bool | n | Enable mDNS responder + DNS-SD support |
| `CONFIG_ZEGO_NETWORK_MDNS_HTTP_PORT` | int (0–65535) | 0 | When non-zero, registers an `_http._tcp.local` DNS-SD service record at this port. Leave at 0 if no HTTP server is present. |

`CONFIG_ZEGO_NETWORK_MDNS=y` automatically selects:
- `CONFIG_NET_HOSTNAME_ENABLE=y` — enables the hostname API; sets `CONFIG_NET_HOSTNAME` at boot
- `CONFIG_MDNS_RESPONDER=y` — Zephyr mDNS responder joins 224.0.0.251 on every UP interface
- `CONFIG_DNS_SD=y` — DNS Service Discovery support; `MDNS_RESPONDER_DNS_SD` defaults `y`

### Hostname

The hostname defaults to `"zego-device"` (→ `zego-device.local`) via `Kconfig.defaults`.
Override in `prj.conf`:

```
CONFIG_NET_HOSTNAME="myapp"   # device becomes myapp.local
```

`CONFIG_NET_HOSTNAME_UNIQUE` defaults to `n` (stable, predictable name).
Set to `y` in `prj.conf` if multiple devices share the same network segment.

### Per-mode behaviour

| Mode | mDNS reliability | Notes |
|---|---|---|
| SoftAP | Reliable | Multicast stays on 192.168.7.0/24; works as long as a client is connected |
| P2P_GO | Reliable | Same as SoftAP — GO is the AP |
| STA | Router-dependent | Most home routers pass mDNS multicast; enterprise APs sometimes block it |
| P2P_GC | Reliable | GO is a DK acting as AP; multicast stays on 192.168.7.0/24 |

### Enabling in prj.conf

Minimum (hostname resolution only):

```
CONFIG_ZEGO_NETWORK_MDNS=y
CONFIG_NET_HOSTNAME="myapp"          # optional; default is "zego-device"
```

With HTTP service discovery:

```
CONFIG_ZEGO_NETWORK_MDNS=y
CONFIG_NET_HOSTNAME="myapp"
CONFIG_ZEGO_NETWORK_MDNS_HTTP_PORT=80
```

### Implementation files

| File | Role |
|---|---|
| `Kconfig` | `ZEGO_NETWORK_MDNS`, `ZEGO_NETWORK_MDNS_HTTP_PORT` definitions |
| `Kconfig.defaults` | `NET_HOSTNAME default "zego-device"`, `NET_HOSTNAME_UNIQUE default n` |
| `CMakeLists.txt` | Conditional `zephyr_library_sources(src/mdns.c)` |
| `src/mdns.c` | `DNS_SD_REGISTER_SERVICE` (when `HTTP_PORT > 0`) + `SYS_INIT` boot log |

---

## Memory Estimate

| Item | Flash | RAM |
|---|---|---|
| `net_event_mgmt.c` | ~8 KB | ~1 KB (static state, semaphores, callbacks) |
| `wifi_utils.c` | ~4 KB | ~0.5 KB (SSID buffer, timer state) |
| **Total (base)** | **~12 KB** | **~1.5 KB** |
| `src/mdns.c` + `MDNS_RESPONDER` | +~6 KB | +~1.5 KB (socket, multicast state) — only when `ZEGO_NETWORK_MDNS=y` |

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
| P2P_GO pairing window | `[zego_wifi_utils] P2P_GO: pairing window open — double-click a P2P_GC to pair` |
| P2P_GC reconnect to saved GO | `[zego_wifi_utils] P2P_GC: reconnecting to saved GO F4:CE:36:00:AE:EC (pbc --join)` |
| P2P_GC no saved GO — auto-pairing at boot | `[zego_wifi_utils] P2P_GC: no saved GO - starting pairing discovery` |
| P2P_GC pairing scan start | `[zego_wifi_utils] P2P_GC: pairing - peer discovery (10 s)` |
| P2P_GC peer table query | `[zego_wifi_utils] P2P_GC: peer table has N entries, selecting GO` |
| P2P_GC candidate | `[zego_wifi_utils] P2P_GC:   [N] F4:CE:36:XX:XX:XX RSSI=-NN` |
| P2P_GC GO selected | `[zego_wifi_utils] P2P_GC: pairing with GO F4:CE:36:XX:XX:XX RSSI=-NN dBm` |
| P2P_GC no candidate found yet — retrying | `[zego_wifi_utils] P2P_GC: no peer found yet, retrying discovery` (never "giving up" — retries indefinitely) |
| P2P_GC MAC persisted | `[zego_wifi_utils] P2P_GC: saved GO F4:CE:36:XX:XX:XX to NVS` |
| P2P_GC connected | `[zego_net_event_mgmt] Wi-Fi connected: mode=P2P_GC ip=... ssid=DIRECT-...` |
| P2P_GC reconnect after disconnect | `[zego_wifi_utils] P2P_GC: disconnected from GO - reconnect in 15 s` |
| STA reconnect after disconnect (no BLE prov) | `[zego_net_event_mgmt] L3 watchdog...` or `NET_REQUEST_WIFI_CONNECT_STORED` retry log — device keeps retrying, LED stays ROTATE |
| STA zero stored credentials | `[zego_net_event_mgmt] No stored credentials - nothing to connect to` — LED shows fast BLINK, no connect attempt made |
| mDNS active (boot) | `[zego_mdns] mDNS: device reachable as <hostname>.local` |
| mDNS HTTP DNS-SD (boot) | `[zego_mdns] mDNS: DNS-SD _http._tcp.local port=<N>` — only when `MDNS_HTTP_PORT > 0` |
