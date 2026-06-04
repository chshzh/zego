# Product Requirements Document — Nordic Wi-Fi App Template

## Document Information

| Field | Value |
|---|---|
| Product Name | Nordic Wi-Fi App Template |
| Version | 2026-06-04-18-00 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF7002DK, nRF54LM20DK + nRF7002EB2 |
| Status | Draft |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-04-17-09 | Initial PRD — template extracted from nordic-wifi-webdash; webserver removed; all four Wi-Fi modes + all three STA provisioning methods supported |
| 2026-06-04-18-00 | Added UX behaviors: Button 0 gestures (long-press mode cycle, double-click BLE prov toggle, single-click status), LED 0 Wi-Fi state feedback (marquee on boot/connecting, solid on connected, slow blink SoftAP, breathe BLE prov, fast blink error) |

---

## 1. Executive Summary

### 1.1 Product Overview

`nordic-wifi-app-template` is the **minimal, production-ready starting point** for new NCS Wi-Fi applications targeting nRF7x development kits. It wires up all four Wi-Fi modes (STA, SoftAP, P2P_GO, P2P_CLIENT) and all three STA credential-provisioning methods (shell `wifi connect`, persistent `wifi cred`, BLE provisioning via the nRF Wi-Fi Provisioner app) out of the box — with no application logic beyond the connectivity layer.

A developer clones the template, renames the project, and fills in `net_event_app.c` to react to connectivity events. Everything else — mode selection, NVS persistence, shell commands, BLE provisioning, buttons, LEDs, startup banner — is already working.

### 1.2 Problem Statement

Starting a new nRF7x Wi-Fi project from a blank Zephyr sample requires setting up: Wi-Fi mode switching, NVS persistence, BLE provisioning, button/LED zbus channels, and a correct SYS_INIT boot sequence. Each of these is non-trivial and requires reading multiple spec files. The template solves this once and keeps all the machinery inside reusable zego modules so application projects stay thin.

### 1.3 Target Users

| User | Description |
|---|---|
| Application Developer | Embedded developer starting a new nRF7x Wi-Fi product; uses this as the code skeleton |
| Evaluator | FAE or student exploring nRF7x connectivity options on hardware, without building an application |

### 1.4 Success Metrics

| Metric | Target |
|---|---|
| Clean build — nRF7002DK | `west build -p -b nrf7002dk/nrf5340/cpuapp -d build_nrf7002dk -- -Dnordic-wifi-app-template_SNIPPET=wifi-p2p` succeeds |
| Clean build — nRF54LM20DK | `west build -p -b nrf54lm20dk/nrf54lm20a/cpuapp -d build_nrf54lm20dk -- -Dnordic-wifi-app-template_SNIPPET=wifi-p2p -DSHIELD=nrf7002eb2` succeeds |
| Startup banner printed | All four modes display correct connection instructions at boot |
| STA via shell | `wifi connect -s <SSID> -p <pass> -k 1` connects the device |
| STA via BLE prov (nRF54LM20DK) | nRF Wi-Fi Provisioner app successfully provisions credentials |
| Mode persists | `app_wifi_mode softap` + reboot brings up SoftAP; mode survives power cycle |
| Net event hook fires | `Wi-Fi connected: mode=... ip=...` logged in UART after connection |

---

## 2. Device Capabilities

### 2.1 Wi-Fi Connectivity

- [x] **STA mode** — device joins an existing Wi-Fi network; three independent provisioning paths:
  - **Shell one-time**: `wifi connect -s <SSID> -p <pass> -k 1` (session only)
  - **Saved credentials**: `wifi cred add <SSID> WPA2-PSK <pass> -k 1` — persisted in flash, auto-reconnects on every reboot
  - **BLE provisioning** (nRF54LM20DK only, `CONFIG_ZEGO_WIFI_BLE_PROV=y`): use the *nRF Wi-Fi Provisioner* phone app to push credentials over BLE; credentials saved to flash
- [x] **SoftAP mode** — device creates its own Wi-Fi hotspot; clients can connect to `192.168.7.1`
- [x] **P2P_GO mode** — device is the Wi-Fi Direct Group Owner; auto-starts at boot with WPS PIN `12345678`
- [x] **P2P_CLIENT mode** — device joins a phone's P2P group via `wifi p2p connect <MAC> pbc`
- [x] **Runtime mode switching** — `app_wifi_mode [sta|softap|p2p_go|p2p_client]` saves to NVS and reboots
- [x] **Default mode on fresh flash**: P2P_GO

### 2.2 Buttons & LEDs

| Board | Buttons | LEDs |
|---|---|---|
| nRF7002DK | 2 (SW0, SW1) | 2 |
| nRF54LM20DK + nRF7002EB2 | 3 (BUTTON0–2) | 4 |

All buttons publish `BUTTON_CHAN` events. All LEDs accept `LED_CMD_CHAN` commands.

#### Button 0 — UX gestures

| Gesture | Action | Boards |
|---------|--------|--------|
| Single click | Print current Wi-Fi state (mode, IP, SSID) to UART shell | both |
| Double-click | Toggle BLE provisioning mode on/off | nRF54LM20DK only (`CONFIG_ZEGO_WIFI_BLE_PROV=y`) |
| Long press (≥ 3 s) | Cycle Wi-Fi mode STA → SoftAP → P2P_GO → STA; save to NVS; reboot | both |

#### LED 0 — Wi-Fi state feedback

| State | Effect | Description |
|-------|--------|-------------|
| Boot / connecting | MARQUEE (all LEDs) | Starts immediately at boot; continues until a connection is established |
| Connected (STA / P2P) | Solid ON | Clear "all good" |
| SoftAP active | Slow BLINK (500 ms half-period) | AP is up and accepting clients |
| BLE provisioning active | BREATHE | Matches BLE convention |
| Disconnected / error | Fast BLINK (100 ms half-period) | Attention needed |

> On nRF7002DK (2 LEDs) the MARQUEE effect chases across both LEDs. On nRF54LM20DK (4 LEDs) it chases across all four.

### 2.3 Application Customisation Point

- [x] `src/modules/network/net_event_app.c` — weak-hook overrides `zego_network_on_wifi_connected()` and `zego_network_on_wifi_disconnected()`; contains TODO-annotated patterns for publishing app-level zbus channels.

### 2.4 Developer Features

- [x] **Startup banner** — app name, version, PRD version, specs version, build date, board, MAC, active mode, connection instructions, compiled module list
- [x] **Shell commands** — `app_wifi_mode`, `wifi connect`, `wifi cred`, `wifi scan`, `wifi status`, `wifi p2p find/peer/connect`
- [x] **Heap monitor** — periodic heap high-water mark logging (configurable interval)

---

## 3. Functional Requirements

### P0 — Must Have

| ID | As a… | I want to… | So that… | Acceptance Criteria |
|---|---|---|---|---|
| FR-001 | developer | build the template cleanly for both boards | I have a verified starting point | `west build` succeeds; no errors |
| FR-002 | developer | connect in STA mode via the shell | I can verify basic Wi-Fi connectivity | `wifi connect -s <SSID> -p <pass> -k 1` → DHCP IP logged |
| FR-003 | developer | connect in STA mode with saved credentials | Credentials survive reboot | `wifi cred add` → power cycle → auto-reconnect |
| FR-004 | developer | connect in STA mode via BLE provisioning (nRF54LM20DK) | No serial shell needed for provisioning | nRF Wi-Fi Provisioner app pushes credentials → device connects |
| FR-005 | developer | switch to SoftAP mode and have a client connect | I can test AP mode | `app_wifi_mode softap` + reboot → client joins `192.168.7.1` |
| FR-006 | developer | switch to P2P_GO mode | Device auto-starts P2P group at boot | WPS PIN logged; phone joins via Wi-Fi Direct |
| FR-007 | developer | have mode persist across power cycles | I don't re-enter mode on every boot | `app_wifi_mode X` + power cycle → mode X at next boot |
| FR-008 | developer | see connection events in `net_event_app.c` | I have a clear hook to start my application logic | `zego_network_on_wifi_connected()` called with correct `mode`, `ip_addr`, `mac_addr`, `ssid` |

### P1 — Should Have

| ID | As a… | I want to… | So that… | Acceptance Criteria |
|---|---|---|---|---|
| FR-101 | developer | read button events via `BUTTON_CHAN` | I can add button-driven application logic immediately | Button press publishes correct `button_msg` on `BUTTON_CHAN` |
| FR-102 | developer | control LEDs via `LED_CMD_CHAN` | I can add LED feedback immediately | `LED_CMD_CHAN` message changes LED state |
| FR-103 | developer | see heap usage logged periodically | I detect memory leaks early | Heap high-water mark logged every N minutes |
| FR-104 | evaluator | cycle Wi-Fi mode with a long button press | I can switch modes without a UART shell | Button 0 held ≥ 3 s → next mode saved to NVS → device reboots into new mode |
| FR-105 | evaluator | see Wi-Fi connection state on LED 0 | I can tell at a glance whether the device is connected | MARQUEE while connecting → solid ON when connected (STA/P2P) / slow blink when SoftAP active / breathe in BLE prov / fast blink on error |
| FR-106 | evaluator | toggle BLE provisioning with a double-click (nRF54LM20DK) | I can enter/exit provisioning mode without the shell | Double-click on Button 0 toggles BLE provisioning advertising |

---

## 4. Non-Functional Requirements

| Category | Requirement |
|---|---|
| Build | No compiler errors or warnings in template source files |
| Security | BLE provisioning credentials stored only in flash (Zephyr settings subsystem), never in source control |
| Portability | Template works unchanged on both nRF7002DK and nRF54LM20DK + nRF7002EB2 (board `.conf` files handle differences) |
| Flash budget | nRF7002DK build fits in 1 MB (BLE prov disabled); nRF54LM20DK build fits in 2 MB (BLE prov enabled) |

---

## 5. Hardware

| Board | Build target | BLE prov | Notes |
|---|---|---|---|
| nRF7002DK | `nrf7002dk/nrf5340/cpuapp` + `-Dnordic-wifi-app-template_SNIPPET=wifi-p2p` | Disabled (1 MB flash too tight) | 2 buttons, 2 LEDs |
| nRF54LM20DK + nRF7002EB2 | `nrf54lm20dk/nrf54lm20a/cpuapp` + `-Dnordic-wifi-app-template_SNIPPET=wifi-p2p -DSHIELD=nrf7002eb2` | Enabled | 3 buttons, 4 LEDs |
