# Engineering Specs Overview — Nordic Wi-Fi App Template

## Document Information

| Field | Value |
|---|---|
| Project | nordic-wifi-app-template |
| Version | 2026-06-05-09-38 |
| PRD Version | 2026-06-05-09-38 |
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
| [zego/network — network-spec.md](https://github.com/chshzh/zego/blob/main/modules/network/docs/network-spec.md) | Wi-Fi event handling, STA/SoftAP/P2P_GO/P2P_CLIENT paths, net event mgmt, `zego_network_on_wifi_connected` weak hook | FR-002–FR-008 |
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

---

## 5. Open Issues

No open issues.
