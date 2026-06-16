# Engineering Specs Overview — Nordic Wi-Fi App Template

## Document Information

| Field | Value |
|---|---|
| Project | nordic-wifi-app-template |
| Version | 2026-06-16-11-26 |
| PRD Version | 2026-06-16-11-26 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF54LM20DK + nRF7002EB2, nRF7002DK, nRF5340 Audio DK + nRF7002EK |
| Status | Current |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-04-18-00 | Added UX module spec (ux.md); updated spec index and PRD mapping for FR-104/105/106; noted APP_WIFI_STATE_CHAN in architecture summary |
| 2026-06-04-22-00 | Updated ux.md and PRD for revised SoftAP LED behavior (ROTATE/solid ON) |
| 2026-06-05-09-38 | Added nRF5340 Audio DK + nRF7002EK target; updated Target Board(s), architecture.md, and ux.md; added board conf + DTS overlay + hci_ipc netcore conf |
| 2026-06-09-17-25 | Updated to PRD v2026-06-09-17-25: added FR-107 P2P_CLIENT auto-connect to PRD mapping; network-spec.md updated with auto-connect sequence, new API, Kconfig, test points |
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
| [architecture.md](architecture.md) | System overview, module map, Zbus channels, SYS_INIT boot sequence, memory budget | All |
| [zego/wifi — wifi-spec.md](https://github.com/chshzh/zego/blob/main/modules/wifi/docs/wifi-spec.md) | Startup banner, Wi-Fi mode selector, `app_wifi_mode` shell command, NVS persistence, weak override hooks | FR-001, FR-006, FR-007 |
| [zego/network — network-spec.md](https://github.com/chshzh/zego/blob/main/modules/network/docs/network-spec.md) | Wi-Fi event handling, STA/SoftAP/P2P_GO/P2P_CLIENT paths, net event mgmt, `zego_on_net_event_dhcp_bound` weak hook | FR-002–FR-008 |
| [zego/wifi_ble_prov — wifi-ble-prov-spec.md](https://github.com/chshzh/zego/blob/main/modules/wifi_ble_prov/docs/wifi-ble-prov-spec.md) | BLE provisioning (nRF Wi-Fi Provisioner), `WIFI_CHAN` owner, rotating credential reconnect | FR-004 |
| [zego/button — button-spec.md](https://github.com/chshzh/zego/blob/main/modules/button/docs/button-spec.md) | Gesture detection (click, double-click, long press), `BUTTON_CHAN` | FR-101 |
| [zego/led — led-spec.md](https://github.com/chshzh/zego/blob/main/modules/led/docs/led-spec.md) | Per-LED state machine (static, blink, breathe, rotate), `LED_CMD_CHAN` | FR-102 |
| [ux.md](ux.md) | Button 0 gesture map, LED 0 Wi-Fi state machine, `APP_WIFI_STATE_CHAN` definition, BLE prov toggle | FR-104, FR-105, FR-106 |

---

## 3. Architecture Summary

**Pattern**: Zbus modular — all modules start via `SYS_INIT`; inter-module communication is exclusively through Zbus channels. `main()` only calls `zego_wifi_print_banner()` then sleeps forever. Application logic lives in module-override files (`net_event_app.c`).

**Key design decisions:**

| Decision | Choice | Rationale |
|---|---|---|
| No application thread | `main()` calls banner then `k_sleep(K_FOREVER)` | All work is event-driven via SYS_INIT modules and zbus callbacks |
| Application customisation point | Weak-hook overrides in `src/modules/network/net_event_app.c` | Single predictable file to edit; no forking of shared zego modules |
| STA provisioning | Three parallel options (shell, cred, BLE) | Supports all use cases without requiring build-time choice |
| BLE prov on nRF7002DK / Audio DK | Disabled (`CONFIG_ZEGO_WIFI_BLE_PROV=n`) | BLE host stack + large app exceeds 1 MB flash; re-enable if flash allows |
| Default mode | P2P_GO | Enables out-of-box demo with no network infrastructure |
| All modules from zego | No in-tree application modules except `net_event_app.c` and `ux.c` | Template stays minimal; feature modules are shared across all zego apps |
| `APP_WIFI_STATE_CHAN` | Defined in `net_event_app.c`; declared in `messages.h` | Decouples network events from LED/UX logic without making the UX module depend on `zego/network` internals |
| P2P_CLIENT auto-connect strategy | Direct `--join` to known GO MAC (`CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC`) instead of P2P discovery | Discovery+PBC+PIN sequence is unreliable on nRF7002; `--join` is deterministic and avoids scan race conditions |
| P2P_CLIENT static IP | 192.168.7.2/24 assigned at `CONNECT_RESULT` instead of waiting for DHCP | `wpa_supplicant` on nRF7002 does not issue a DHCP lease for P2P_CLIENT; static assignment is instant and avoids a 15 s `SIGNAL_POLL` timeout |
| BLE provisioner mode gate | `wifi_ble_prov_init()` exits early when mode ≠ STA | Prevents BLE GATT notification spam in P2P and SoftAP modes; module remains compiled-in so runtime mode switch to STA still works |

---

## 4. PRD-to-Spec Mapping

| PRD requirement | Spec file | Status |
|---|---|---|
| FR-001 Build | [architecture.md](architecture.md) | Specified |
| FR-002 STA shell connect | [network-spec.md](../../network/docs/network-spec.md) | Specified |
| FR-003 STA saved credentials | [network-spec.md](../../network/docs/network-spec.md) | Specified |
| FR-004 STA BLE provisioning | [wifi-ble-prov-spec.md](../../wifi_ble_prov/docs/wifi-ble-prov-spec.md) | Specified |
| FR-005 SoftAP mode | [network-spec.md](../../network/docs/network-spec.md) | Specified |
| FR-006 P2P_GO mode | [network-spec.md](../../network/docs/network-spec.md) | Specified |
| FR-007 Mode persistence | [wifi-spec.md](../../wifi/docs/wifi-spec.md) | Specified |
| FR-008 Net event hook | [architecture.md](architecture.md) | Specified |
| FR-101 Button events | [button-spec.md](../../button/docs/button-spec.md) | Specified |
| FR-102 LED control | [led-spec.md](../../led/docs/led-spec.md) | Specified |
| FR-103 Heap monitor | [architecture.md](architecture.md) | Specified |
| FR-104 Button 0 mode cycle | [ux.md](ux.md) | Specified |
| FR-105 LED Wi-Fi state feedback | [ux.md](ux.md) | Specified |
| FR-106 BLE prov double-click | [ux.md](ux.md) | Specified |
| FR-107 P2P_CLIENT auto-connect | [network-spec.md](../../network/docs/network-spec.md) | Specified |
| FR-108 P2P_CLIENT MAC-prefix auto-select | [network-spec.md](../../network/docs/network-spec.md) | Specified |

---

## 5. Open Issues

No open issues.
