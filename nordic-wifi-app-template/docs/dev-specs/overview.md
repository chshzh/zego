# Engineering Specs Overview — Nordic Wi-Fi App Template

## Document Information

| Field | Value |
|---|---|
| Project | nordic-wifi-app-template |
| Version | 2026-06-04-17-09 |
| PRD Version | 2026-06-04-17-09 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB2 |
| Status | Current |

---

## Changelog

| Version | Summary of changes |
|---|---|
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
| [zego/wifi — wifi-spec.md](../../wifi/docs/wifi-spec.md) | Startup banner, Wi-Fi mode selector, `app_wifi_mode` shell command, NVS persistence, weak override hooks | FR-001, FR-006, FR-007 |
| [zego/network — network-spec.md](../../network/docs/network-spec.md) | Wi-Fi event handling, STA/SoftAP/P2P_GO/P2P_CLIENT paths, net event mgmt, `zego_network_on_wifi_connected` weak hook | FR-002–FR-008 |
| [zego/wifi_ble_prov — wifi-ble-prov-spec.md](../../wifi_ble_prov/docs/wifi-ble-prov-spec.md) | BLE provisioning (nRF Wi-Fi Provisioner), `WIFI_CHAN` owner, rotating credential reconnect | FR-004 |
| [zego/button — button-spec.md](../../button/docs/button-spec.md) | Gesture detection (click, double-click, long press), `BUTTON_CHAN` | FR-101 |
| [zego/led — led-spec.md](../../led/docs/led-spec.md) | Per-LED state machine (static, blink, breathe, marquee), `LED_CMD_CHAN` | FR-102 |

---

## 3. Architecture Summary

**Pattern**: Zbus modular — all modules start via `SYS_INIT`; inter-module communication is exclusively through Zbus channels. `main()` only calls `zego_wifi_print_banner()` then sleeps forever. Application logic lives in module-override files (`net_event_app.c`).

**Key design decisions:**

| Decision | Choice | Rationale |
|---|---|---|
| No application thread | `main()` calls banner then `k_sleep(K_FOREVER)` | All work is event-driven via SYS_INIT modules and zbus callbacks |
| Application customisation point | Weak-hook overrides in `src/modules/network/net_event_app.c` | Single predictable file to edit; no forking of shared zego modules |
| STA provisioning | Three parallel options (shell, cred, BLE) | Supports all use cases without requiring build-time choice |
| BLE prov on nRF7002DK | Disabled (`CONFIG_ZEGO_WIFI_BLE_PROV=n`) | BLE host stack + large app exceeds 1 MB flash; re-enable if flash allows |
| Default mode | P2P_GO | Enables out-of-box demo with no network infrastructure |
| All modules from zego | No in-tree application modules except `net_event_app.c` | Template stays minimal; feature modules are shared across all zego apps |

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

---

## 5. Open Issues

No open issues.
