# Engineering Specs Overview — Nordic Wi-Fi App Template

## Document Information

| Field | Value |
|---|---|
| Project | nordic-wifi-app-template |
| Version | 2026-07-01-10-54 |
| PRD Version | 2026-07-01-10-50 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF54LM20DK + nRF7002EB2, nRF7002DK, nRF5340 Audio DK + nRF7002EK |
| Status | Current |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-07-02-00-00 | `ux` moved from an in-tree app module (`src/modules/ux/`, `CONFIG_APP_UX_MODULE`) to a first-class zego brick (`zego/bricks/ux/`, `CONFIG_ZEGO_UX`). Its Button 0 gesture actions (single click, double-click, long press) are now `__weak` functions apps can override individually, following the same pattern as `zego/network`'s `zego_on_net_event_*` hooks. The module now owns its own `ZEGO_UX_WIFI_STATE_CHAN` instead of the app-level `APP_WIFI_STATE_CHAN`; `net_event_app.c` publishes to it directly. See [ux-spec.md](../../../bricks/ux/docs/ux-spec.md). |
| 2026-07-01-10-54 | Updated to PRD v2026-07-01-10-50: disconnection-handling overhaul (FR-003, FR-005, FR-006, FR-105, FR-107). Added 2 design decisions (below): P2P_GC auto-pairing at boot, and the "retry indefinitely, BLINK only when impossible" LED philosophy. `network-spec.md`, `ux-module.md`, and `1-architecture.md` updated — see their own changelogs for details. |
| 2026-06-29-23-06 | Updated to PRD v2026-06-29-23-06: default-mode design-decision row corrected to STA (matches prj.conf); validation-found doc fix. |
| 2026-06-29-21-44 | Updated to PRD v2026-06-29-21-44: P2P pairing UX overhaul. FR-108 retired (removed `CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC`). FR-006/FR-107 rewritten — button-triggered WPS pairing; GC learns + persists GO MAC in NVS (`net/p2p_gc_go_mac`); reconnect on disconnect + power cycle. ux-module.md (mode-aware double-click → `wifi_p2p_start_pairing()`) and network-spec.md updated. (WPS method was reconciled to PBC on 2026-06-30 — see below.) P2P_CLIENT→P2P_GC naming aligned. |
| 2026-06-30-13-04 | Updated to PRD v2026-06-30-13-00. UX tweaks: board-configurable UX gesture button (`CONFIG_APP_UX_BUTTON_IDX`, =4/BTN5 on Audio DK); LED BREATHE during P2P pairing via new `zego_on_net_event_p2p_pairing(bool)` hook + `APP_WIFI_STATE_PAIRING`. Debug-session reconcile: WPS **PBC** (not fixed PIN — `WIFI_WPS_PIN_SET` fails the nRF GO's WPS Registrar init); root-cause fix `WIFI_NM_WPA_SUPPLICANT_GLOBAL_HEAP=y` (dedicated heap starved the registrar); GC GO-capability peer filter + pairing re-entrancy guard. ux-module.md + network-spec.md updated. |
| 2026-06-19-12-44 | Updated to PRD v2026-06-19-12-44: FR-103 updated to zego/memonitor brick — spec reference changed from architecture.md to memonitor-spec.md; description updated to heap + thread watermarks. |
| 2026-06-04-18-00 | Added UX module spec (ux-module.md); updated spec index and PRD mapping for FR-104/105/106; noted APP_WIFI_STATE_CHAN in architecture summary |
| 2026-06-04-22-00 | Updated ux.md and PRD for revised SoftAP LED behavior (ROTATE/solid ON) |
| 2026-06-05-09-38 | Added nRF5340 Audio DK + nRF7002EK target; updated Target Board(s), architecture.md, and ux.md; added board conf + DTS overlay + hci_ipc netcore conf |
| 2026-06-09-17-25 | Updated to PRD v2026-06-09-17-25: added FR-107 P2P_CLIENT auto-connect to PRD mapping; network-spec.md updated with auto-connect sequence, new API, Kconfig, test points |
| 2026-06-16-13-30 | Updated to PRD v2026-06-16-13-30: FR-005 SoftAP max 3 clients — `CONFIG_WIFI_MGMT_AP_MAX_NUM_STA=3` added to architecture.md; TODO log format specified for connect/disconnect events |
| 2026-06-16-11-26 | Updated to PRD v2026-06-16-11-26: FR-108 MAC-prefix auto-select added; network-spec.md updated with prefix sequence, Kconfig, test points |
| 2026-06-16-11-21 | Updated to PRD v2026-06-16-11-21: P2P_GO phone-as-client dropped; FR-006 acceptance criteria updated; network-spec.md updated |
| 2026-06-14-00-21 | Updated to PRD v2026-06-14-00-21: P2P_CLIENT --join auto-connect, static IP, 90 s timeout, 15 s reconnect; P2P_GO PBC auto-rearm; BLE provisioner STA-only init; network-spec.md and wifi-ble-prov-spec.md updated |
| 2026-06-04-17-09 | Initial overview — template extracted from nordic-wifi-webdash; webserver removed; all four Wi-Fi modes + three STA provisioning paths; architecture.md added |

---

## 1. Purpose

This document is the engineering entry point for `nordic-wifi-app-template`.
It maps product requirements to spec files and captures top-level design decisions.

For product requirements, see [`docs/pm-prd/PRD.md`](../pm-prd/PRD.md).

---

## 2. Spec Index

| Spec | Covers | PRD sections |
|---|---|---|
| [1-architecture.md](1-architecture.md) | System overview, module map, Zbus channels, SYS_INIT boot sequence, memory budget | All |
| [zego/wifi — wifi-spec.md](https://github.com/chshzh/zego/blob/main/modules/wifi/docs/wifi-spec.md) | Startup banner, Wi-Fi mode selector, `zego_wifi_mode` shell command, NVS persistence, weak override hooks | FR-001, FR-006, FR-007 |
| [zego/network — network-spec.md](https://github.com/chshzh/zego/blob/main/modules/network/docs/network-spec.md) | Wi-Fi event handling, STA/SoftAP/P2P_GO/P2P_GC paths, P2P button pairing + NVS-saved GO MAC, net event mgmt, `zego_on_net_event_dhcp_bound` weak hook | FR-002–FR-008, FR-107 |
| [zego/wifi_ble_prov — wifi-ble-prov-spec.md](https://github.com/chshzh/zego/blob/main/modules/wifi_ble_prov/docs/wifi-ble-prov-spec.md) | BLE provisioning (nRF Wi-Fi Provisioner), `WIFI_CHAN` owner, rotating credential reconnect | FR-004 |
| [zego/button — button-spec.md](https://github.com/chshzh/zego/blob/main/modules/button/docs/button-spec.md) | Gesture detection (click, double-click, long press), `BUTTON_CHAN` | FR-101 |
| [zego/led — led-spec.md](https://github.com/chshzh/zego/blob/main/modules/led/docs/led-spec.md) | Per-LED state machine (static, blink, breathe, rotate), `LED_CMD_CHAN` | FR-102 |
| [ux-spec.md](../../../bricks/ux/docs/ux-spec.md) | UX gesture button (board-configurable index `CONFIG_ZEGO_UX_BUTTON_IDX`; mode-aware double-click: BLE prov toggle or P2P pairing trigger, each a `__weak` override point), LED 0 Wi-Fi state machine incl. pairing BREATHE (`ZEGO_UX_WIFI_STATE_PAIRING`), `ZEGO_UX_WIFI_STATE_CHAN` definition | FR-104, FR-105, FR-106, FR-107 |

---

## 3. Architecture Summary

**Pattern**: Zbus modular — all modules start via `SYS_INIT`; inter-module communication is exclusively through Zbus channels. `main()` only calls `zego_wifi_print_banner()` then sleeps forever. Application logic lives in module-override files (`net_event_app.c`).

**Key design decisions:**

| Decision | Choice | Rationale |
|---|---|---|
| No application thread | `main()` calls banner then `k_sleep(K_FOREVER)` | All work is event-driven via SYS_INIT modules and zbus callbacks |
| Application customisation point | Weak-hook overrides in `src/modules/network/net_event_app.c` (network events) and `zego/bricks/ux/src/ux.c` (`zego_ux_on_*` gesture hooks) | Single predictable file to edit per concern; no forking of shared zego bricks |
| STA provisioning | Three parallel options (shell, cred, BLE) | Supports all use cases without requiring build-time choice |
| BLE prov on nRF7002DK / Audio DK | Disabled (`CONFIG_ZEGO_WIFI_BLE_PROV=n`) | BLE host stack + large app exceeds 1 MB flash; re-enable if flash allows |
| Default mode | STA | Set via `CONFIG_ZEGO_WIFI_DEFAULT_MODE_STA=y`; STA is the most common first-use path. Switch to P2P/SoftAP via `zego_wifi_mode` or a Button 0 long-press |
| All modules from zego | No in-tree application modules except `net_event_app.c` | Template stays minimal; feature modules (including `ux`) are shared across all zego apps |
| `ZEGO_UX_WIFI_STATE_CHAN` | Owned by the `zego/ux` brick; published from `net_event_app.c` | Decouples network events from LED/UX logic without making the UX brick depend on `zego/network` internals |
| P2P pairing WPS method | WPS **PBC** (Push Button Config), not a fixed PIN | A fixed PIN via `WIFI_WPS_PIN_SET` **fails the nRF GO's `wps_registrar_init()`** (confirmed on hardware), so it is unusable. PBC is the supported headless method on the nRF GO (per `nrf/samples/wifi/p2p`) and needs no out-of-band secret — the GC joins with `pbc --join`. |
| P2P_GO WPS Registrar heap | Supplicant uses the **shared system heap** (`WIFI_NM_WPA_SUPPLICANT_GLOBAL_HEAP=y`, the NCS default) | A dedicated supplicant K_HEAP (`=n`, originally chosen for per-pool ZView monitoring) **starved `wps_registrar_init()`** → "Failed to initialize WPS Registrar" → the GO AP never reached AP-ENABLED. The global heap fixes it and freed ~10% RAM. Root-caused on hardware via supplicant DEBUG logging. |
| P2P pairing model | Button-triggered pairing; GC learns the GO MAC at runtime and persists it to NVS (`net/p2p_gc_go_mac`); no compile-time MAC | Removes the `CONFIG_..._TARGET_GO_MAC` build dependency; double-click on both devices matches a "press a button to pair" UX; reconnect reuses the deterministic `pbc --join`-to-known-MAC path. The GC filters discovered peers to actual GOs (`group_capab` GO bit) so it doesn't lock onto nearby non-GO P2P devices. |
| P2P silent reconnect mechanism | GO keeps WPS PBC **continuously armed**; GC reconnects to the saved MAC (no pairing gesture on reconnect) | Forced by the *GC-stores-MAC, GO-stores-nothing* persistence choice: the GO has no memory of its client, so it must stay connectable for the GC to rejoin after a power cycle. Persistent-group/invitation reconnect was rejected as higher-risk and would require GO-side peer storage. |
| UX gesture button index | Board-configurable via `CONFIG_ZEGO_UX_BUTTON_IDX` (default 0; **4 / BTN5 on nRF5340 Audio DK**) | Lets each board pick which physical button carries the UX gestures without forking `ux.c`; the Audio DK frees VOL- (idx 0) for the application and uses BTN5. |
| Pairing LED feedback | LED 0 BREATHEs during P2P pairing (both roles), via the `zego_on_net_event_p2p_pairing(bool)` weak hook → `APP_WIFI_STATE_PAIRING` | Keeps the LED state machine driven by network events (single source of truth) rather than a local UX timer, so the breathe reliably ends when pairing connects/fails. |
| P2P_GC static IP | 192.168.7.2/24 assigned at `CONNECT_RESULT` instead of waiting for DHCP | `wpa_supplicant` on nRF7002 does not issue a DHCP lease for P2P_GC; static assignment is instant and avoids a 15 s `SIGNAL_POLL` timeout |
| BLE provisioner mode gate | `wifi_ble_prov_init()` exits early when mode ≠ STA | Prevents BLE GATT notification spam in P2P and SoftAP modes; module remains compiled-in so runtime mode switch to STA still works |
| LED retry philosophy | ROTATE for as long as any automatic reconnect/pairing retry is in progress; fast BLINK reserved for the single case where reconnection is structurally impossible (STA, zero stored credentials) | BLINK on every disconnect (the prior behavior) misrepresented transient AP reboots and out-of-range periods as an error needing attention, when the firmware was already about to retry on its own. Narrowing BLINK's meaning to "action needed" makes it an actionable signal again. |
| P2P_GC auto-pairing at boot | With no saved GO, `wifi_run_p2p_gc_mode()` starts pairing discovery immediately (no button press) and retries indefinitely; the double-click gesture remains available for manual (re-)pairing | The original button-only design (FR-107, 2026-06-29) required a physical gesture even for a device with no prior pairing, which is unnecessary friction when there is only one sensible action (find a GO and join it). The double-click stays useful for re-pairing with a different GO. |

---

## 4. PRD-to-Spec Mapping

| PRD requirement | Spec file | Status |
|---|---|---|
| FR-001 Build | [1-architecture.md](1-architecture.md) | Specified |
| FR-002 STA shell connect | [network-spec.md](../../network/docs/network-spec.md) | Specified |
| FR-003 STA saved credentials | [network-spec.md](../../network/docs/network-spec.md) | Specified |
| FR-004 STA BLE provisioning | [wifi-ble-prov-spec.md](../../wifi_ble_prov/docs/wifi-ble-prov-spec.md) | Specified |
| FR-005 SoftAP mode | [network-spec.md](../../network/docs/network-spec.md) | Specified |
| FR-006 P2P_GO mode + pairing window | [network-spec.md](../../network/docs/network-spec.md) | Specified |
| FR-007 Mode persistence | [wifi-spec.md](../../wifi/docs/wifi-spec.md) | Specified |
| FR-008 Net event hook | [1-architecture.md](1-architecture.md) | Specified |
| FR-101 Button events | [button-spec.md](../../button/docs/button-spec.md) | Specified |
| FR-102 LED control | [led-spec.md](../../led/docs/led-spec.md) | Specified |
| FR-103 Heap + thread watermarks | [zego/memonitor — memonitor-spec.md](https://github.com/chshzh/zego/blob/main/bricks/memonitor/docs/memonitor-spec.md) | Specified |
| FR-104 Button 0 mode cycle | [ux-module.md](ux-module.md) | Specified |
| FR-105 LED Wi-Fi state feedback (incl. pairing BREATHE) | [ux-module.md](ux-module.md) + [network-spec.md](../../network/docs/network-spec.md) | Specified |
| FR-106 BLE prov double-click (STA/SoftAP only) | [ux-module.md](ux-module.md) | Specified |
| FR-107 P2P_GC button pairing + NVS reconnect | [network-spec.md](../../network/docs/network-spec.md) + [ux-module.md](ux-module.md) | Specified |
| FR-108 *(removed — superseded by FR-107 pairing)* | — | Retired |

---

## 5. Open Issues

No open issues.
