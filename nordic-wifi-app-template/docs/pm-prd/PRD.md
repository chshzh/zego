# Product Requirements Document — Nordic Wi-Fi App Template

## Document Information

| Field | Value |
|---|---|
| Product Name | Nordic Wi-Fi App Template |
| Version | 2026-07-01-10-50 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF54LM20DK + nRF7002EB2, nRF7002DK, nRF5340 Audio DK + nRF7002EK |
| Status | Draft |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-04-17-09 | Initial PRD — template extracted from nordic-wifi-webdash; webserver removed; all four Wi-Fi modes + all three STA provisioning methods supported |
| 2026-06-04-18-00 | Added UX behaviors: Button 0 gestures (long-press mode cycle, double-click BLE prov toggle, single-click status), LED 0 Wi-Fi state feedback (rotate on boot/connecting, solid on connected, slow blink SoftAP, breathe BLE prov, fast blink error) |
| 2026-06-04-22-00 | Updated LED 0 SoftAP behavior: ROTATE when AP is up with no clients connected (was slow BLINK); solid ON when a client connects; back to ROTATE when last client disconnects |
| 2026-06-05-09-38 | Added nRF5340 Audio DK + nRF7002EK as a target board: 5 buttons (VOL-, VOL+, PLAY/PAUSE, BTN4, BTN5), 9 LEDs (ROTATE over RGB1 only); BLE prov disabled (1 MB flash); DTS overlay required for nRF7002EK SPI pinout |
| 2026-06-09-16-03 | nRF5340 Audio DK LED UX: ROTATE sweeps RGB1+RGB2 (6 LEDs) while connecting; solid green on RGB2 when connected (replaces plain solid-ON) |
| 2026-06-09-16-12 | Corrected nRF5340 Audio DK ROTATE range to RGB2 only [3,4,5]; added developer capability: ROTATE accepts an explicit LED index array so any subset of LEDs can rotate |
| 2026-06-09-17-25 | P2P_CLIENT mode: auto-start peer discovery at boot; try WPS PBC first, fall back to PIN 12345678; retry discovery every 30 s if no peer found; wait 5 s and re-discover on disconnect; added FR-107 |
| 2026-06-16-11-21 | P2P_GO: phone-as-P2P-client dropped — Android Wi-Fi Direct cannot connect to the DK acting as GO (WPS negotiation fails); P2P_GO only supports other DKs as clients. P2P_CLIENT mode still supports connecting to a phone acting as GO. Updated FR-006 |
| 2026-06-14-00-21 | P2P_CLIENT: revised auto-connect using target GO MAC (CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC Kconfig); direct --join connect skips discovery; static IP 192.168.7.2/24 assigned immediately; 90 s retry timeout; 15 s reconnect delay after disconnect. P2P_GO: PBC auto re-armed on client disconnect and every 110 s. BLE provisioner: init skipped in non-STA modes. Updated FR-107 |
| 2026-06-16-13-30 | SoftAP: max 3 simultaneous clients (FR-005 updated); net_event_app TODO log messages must clearly show clients now connected and remaining count for both connect and disconnect events |
| 2026-06-16-11-26 | P2P_CLIENT: added FR-108 MAC-prefix auto-select mode — when CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC ends in :00:00:00, device scans for all P2P GOs matching the 3-byte OUI prefix and connects to the one with the highest RSSI; exact-MAC mode unchanged |
| 2026-06-19-12-44 | FR-103 updated: heap-only logging replaced by zego/memonitor brick — periodic heap + thread stack watermark sampling via MEMONITOR_CHAN; ZView live monitoring support added. |
| 2026-06-30-13-00 | UX tweaks (FR-105/FR-104/FR-106/FR-107): LED 0 now BREATHEs during **P2P pairing** as well as BLE provisioning — both roles, while pairing is active, reverting when it ends. On the **nRF5340 Audio DK** all UX gestures (single/double/long, incl. the P2P-pairing double-click) move from VOL- (idx 0) to **BTN5 (idx 4)**. Added per-board UX-gesture-button mapping table. |
| 2026-06-29-23-06 | Doc correction (found during 4.2 validation): default mode on fresh flash is **STA** (matches `CONFIG_ZEGO_WIFI_DEFAULT_MODE_STA=y` in prj.conf), not P2P_GO. Button-0 long-press cycle corrected to STA → SoftAP → P2P_GO → P2P_GC → STA (P2P_GC was missing). |
| 2026-06-29-21-44 | P2P pairing UX overhaul: removed compile-time target-GO-MAC dependence (FR-108 retired, was MAC-prefix/exact-MAC auto-connect). New double-click Button 0 gesture in P2P modes — GO opens a ~2-min WPS PIN (12345678) pairing window, GC discovers and joins the pairing GO and saves its MAC to NVS (FR-006/FR-107 rewritten). Once paired, GC auto-reconnects to the saved GO on disconnect and after power cycle until a new double-click pairing overwrites it. Double-click remains BLE-prov toggle in STA/SoftAP modes (FR-106). Naming aligned P2P_CLIENT → P2P_GC. (WPS PIN, not PBC: the nRF DK-as-GO does not support PBC acceptance — confirmed against the nRF P2P sample and field testing.) |
| 2026-07-01-10-38 | Disconnection-handling overhaul across all four Wi-Fi modes (FR-003, FR-005, FR-006, FR-105, FR-107 updated): (1) STA now retries stored-credential reconnection indefinitely after any runtime disconnect, on every board — previously this only worked when BLE provisioning was compiled in (nRF54LM20DK); (2) LED 0 fast BLINK is now reserved for the single case where reconnection is genuinely not possible (STA with zero stored networks) — any in-progress retry (STA or P2P_GC, any disconnect reason) now shows ROTATE instead; (3) P2P_GC with no saved GO now auto-starts pairing discovery at boot with no button press required, and pairing (automatic or double-click-triggered) now retries indefinitely instead of giving up after a bounded number of discovery cycles; (4) documented that P2P_GO expects exactly one P2P_GC client (informational — not enforced in code) and the ~5 minute worst-case SoftAP/P2P_GO client-loss detection latency (relies on the AP's inactivity timeout when a client disappears without a clean disconnect); (5) corrected stale "WPS PIN 12345678" wording to **WPS PBC** throughout — the implemented pairing mechanism (PBC) diverged from this doc after the 2026-06-30-13-00 UX-spec correction, which was never propagated here. |
| 2026-07-01-10-50 | Restructured the §2.2 LED 0 table from state-first to **effect-first** (ROTATE / Solid ON / BREATHE / Fast BLINK as rows, one column per board) — the nRF5340 Audio DK lights different physical LEDs (RGB2 only) than the other two boards for the same effect, which the old state-first table plus a bolted-on board-differences note made harder to scan. Per-board LED indices verified directly against `CONFIG_APP_UX_ROTATE_FIRST_LED/COUNT/CONNECTED_LED/PAIRING_LED_IDX/ERROR_LED_IDX` Kconfig defaults and board `.conf` overrides (values unchanged, presentation only). |

---

## 1. Executive Summary

### 1.1 Product Overview

`nordic-wifi-app-template` is the **minimal, production-ready starting point** for new NCS Wi-Fi applications targeting nRF7x development kits. It wires up all four Wi-Fi modes (STA, SoftAP, P2P_GO, P2P_GC) and all three STA credential-provisioning methods (shell `wifi connect`, persistent `wifi cred`, BLE provisioning via the nRF Wi-Fi Provisioner app) out of the box — with no application logic beyond the connectivity layer.

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
| Clean build — nRF54LM20DK | `west build -p -b nrf54lm20dk/nrf54lm20a/cpuapp -d build_nrf54lm20dk -- -Dnordic-wifi-app-template_SNIPPET=wifi-p2p -DSHIELD=nrf7002eb2` succeeds |
| Clean build — nRF7002DK | `west build -p -b nrf7002dk/nrf5340/cpuapp -d build_nrf7002dk -- -Dnordic-wifi-app-template_SNIPPET=wifi-p2p` succeeds |
| Clean build — nRF5340 Audio DK | `west build -p -b nrf5340_audio_dk/nrf5340/cpuapp -d build_nrf5340_audio_dk -- -Dnordic-wifi-app-template_SNIPPET=wifi-p2p -DSHIELD=nrf7002ek` succeeds |
| Startup banner printed | All four modes display correct connection instructions at boot |
| STA via shell | `wifi connect -s <SSID> -p <pass> -k 1` connects the device |
| STA via BLE prov (nRF54LM20DK) | nRF Wi-Fi Provisioner app successfully provisions credentials |
| Mode persists | `zego_wifi_mode softap` + reboot brings up SoftAP; mode survives power cycle |
| Net event hook fires | `Wi-Fi connected: mode=... ip=...` logged in UART after connection |

---

## 2. Device Capabilities

### 2.1 Wi-Fi Connectivity

- [x] **STA mode** — device joins an existing Wi-Fi network; three independent provisioning paths:
  - **Shell one-time**: `wifi connect -s <SSID> -p <pass> -k 1` (session only)
  - **Saved credentials**: `wifi cred add <SSID> WPA2-PSK <pass> -k 1` — persisted in flash, auto-reconnects on every reboot
  - **BLE provisioning** (nRF54LM20DK only, `CONFIG_ZEGO_WIFI_BLE_PROV=y`): use the *nRF Wi-Fi Provisioner* phone app to push credentials over BLE; credentials saved to flash
  - **Reconnect**: on any runtime disconnect, the device automatically retries the stored network(s) indefinitely, on every board — independent of whether BLE provisioning is enabled; with zero stored networks, the device does not attempt to connect (see LED 0 behavior below)
- [x] **SoftAP mode** — device creates its own Wi-Fi hotspot; clients can connect to `192.168.7.1`; a client that disappears without a clean disconnect is still detected, worst case within the AP's ~5-minute inactivity timeout
- [x] **P2P_GO mode** — device is the Wi-Fi Direct Group Owner; auto-starts its group at boot; a **double-click on Button 0 opens a ~2-minute WPS PBC pairing window** so a P2P_GC device can find and join; expects exactly one P2P_GC client at a time (informational — not enforced); only other DKs (running P2P_GC mode) can connect — Android/iOS phones as P2P clients are not supported
- [x] **P2P_GC mode** — device joins a P2P_GO's group; **no target GO MAC is configured at build time**:
  - **Auto-pairing**: with no saved GO, the device **automatically starts pairing discovery at boot** — no button press required — and keeps retrying indefinitely until it finds and joins a GO that is in its pairing window (WPS PBC); on success the GO's MAC is **saved to NVS**; static IP 192.168.7.2/24
  - **Manual (re-)pairing**: a **double-click on Button 0** (re-)starts the same discovery-and-join process at any time, e.g. to pair with a different GO
  - **Auto-reconnect**: once paired, the device reconnects to the saved GO automatically on disconnect **and after a power cycle**, with no user interaction, retrying indefinitely until it succeeds
  - **Re-pair**: a new double-click pairing **overwrites** the saved GO MAC (re-pairing is also how an old pairing is forgotten)
- [x] **Runtime mode switching** — `zego_wifi_mode [sta|softap|p2p_go|p2p_gc]` saves to NVS and reboots
- [x] **Default mode on fresh flash**: STA

### 2.2 Buttons & LEDs

| Board | Buttons | LEDs |
|---|---|---|
| nRF54LM20DK + nRF7002EB2 | 3 (BUTTON0–2) | 4 |
| nRF7002DK | 2 (SW0, SW1) | 2 |
| nRF5340 Audio DK + nRF7002EK | 5 (VOL-, VOL+, PLAY/PAUSE, BTN4, BTN5) | 9 (RGB1 idx 0–2, RGB2 idx 3–5, mono idx 6–8) |

All buttons publish `BUTTON_CHAN` events. All LEDs accept `LED_CMD_CHAN` commands.

#### UX gesture button

The **UX gesture button** carries all the gestures below. Its index is board-specific:

| Board | UX gesture button |
|---|---|
| nRF54LM20DK + nRF7002EB2 | BUTTON0 (idx 0) |
| nRF7002DK | Button 1 / SW0 (idx 0) |
| nRF5340 Audio DK + nRF7002EK | **BTN5 (idx 4)** |

> On the nRF5340 Audio DK the UX gesture button is **BTN5 (idx 4)**, not VOL- (idx 0).

| Gesture | Action | Boards |
|---------|--------|--------|
| Single click | Print current Wi-Fi state (mode, IP, SSID) to UART shell | all |
| Double-click (STA / SoftAP modes) | Toggle BLE provisioning mode on/off | nRF54LM20DK only (`CONFIG_ZEGO_WIFI_BLE_PROV=y`) |
| Double-click (P2P_GO / P2P_GC modes) | Trigger P2P pairing — GO opens a ~2-min WPS PBC window; GC (re-)starts pairing discovery and joins the pairing GO, then saves its MAC to NVS. On P2P_GC this is optional — pairing already auto-starts at boot when no GO is saved (see FR-107) | all boards with P2P enabled |
| Long press (≥ 3 s) | Cycle Wi-Fi mode STA → SoftAP → P2P_GO → P2P_GC → STA; save to NVS; reboot | all |

#### LED 0 — Wi-Fi state feedback

Organized by **effect** rather than by state, since which physical LED(s) light up for a given effect differs by board (the nRF5340 Audio DK uses RGB2 only; the other boards use all available LEDs):

| Effect | nRF54LM20DK + nRF7002EB2 | nRF7002DK | nRF5340 Audio DK + nRF7002EK | When it happens |
|--------|--------------------------|-----------|-------------------------------|------------------|
| ROTATE | All 4 LEDs (idx 0–3) chase | Both LEDs (idx 0–1) chase | RGB2, all 3 channels chase (idx 3–5) | Boot / connecting; SoftAP or P2P_GO active with no clients yet; any automatic STA or P2P_GC reconnect / pairing-discovery retry in progress (started automatically or via double-click) — these retries never give up on their own |
| Solid ON | LED0 (idx 0) | LED1 (idx 0) | RGB2 Green only (idx 4); Red/Blue (idx 3, 5) held OFF | STA or P2P link connected; first SoftAP/P2P_GO client joins (stays solid until the last client leaves) |
| BREATHE | LED0 (idx 0) | LED1 (idx 0) | RGB2 Blue only (idx 5) | BLE provisioning active, or P2P pairing in progress (GO window open, or GC discovering/joining); reverts to the normal state when pairing completes or a client connects |
| Fast BLINK (100 ms half-period) | LED0 (idx 0) | LED1 (idx 0) | RGB2 Red only (idx 3) | **The only case where reconnection is not possible**: STA with zero stored Wi-Fi networks. P2P_GC never shows this effect — it always keeps retrying |

> RGB1 and the mono LEDs on the nRF5340 Audio DK remain off throughout, keeping RGB2 the dedicated Wi-Fi state indicator.
>
> **Developer capability**: `LED_CMD_CHAN` ROTATE commands accept an explicit LED index array, allowing any subset of LEDs (contiguous or not, e.g. `{0, 3, 7, 8}`) to participate in the rotation.

### 2.3 Application Customisation Point

- [x] `src/modules/network/net_event_app.c` — weak-hook overrides `zego_on_net_event_dhcp_bound()` and `zego_on_net_event_wifi_disconnect()`; contains TODO-annotated patterns for publishing app-level zbus channels.

### 2.4 Developer Features

- [x] **Startup banner** — app name, version, PRD version, specs version, build date, board, MAC, active mode, connection instructions, compiled module list
- [x] **Shell commands** — `zego_wifi_mode`, `wifi connect`, `wifi cred`, `wifi scan`, `wifi status`, `wifi p2p find/peer/connect`
- [x] **Heap monitor** — periodic heap high-water mark logging (configurable interval)

---

## 3. Functional Requirements

### P0 — Must Have

| ID | As a… | I want to… | So that… | Acceptance Criteria |
|---|---|---|---|---|
| FR-001 | developer | build the template cleanly for both boards | I have a verified starting point | `west build` succeeds; no errors |
| FR-002 | developer | connect in STA mode via the shell | I can verify basic Wi-Fi connectivity | `wifi connect -s <SSID> -p <pass> -k 1` → DHCP IP logged |
| FR-003 | developer | connect in STA mode with saved credentials | Credentials survive reboot and the device keeps trying rather than giving up | (1) `wifi cred add` → power cycle → auto-reconnect; (2) after any runtime disconnect (AP restart, out of range, rejected credentials, etc.) with a network stored, the device automatically retries the stored network(s) until it reconnects — no user interaction, and identical behavior on every board regardless of whether BLE provisioning is enabled; (3) LED 0 ROTATEs for the entire duration of (1) and (2); (4) with zero stored networks, the device does not attempt to connect and LED 0 shows the fast BLINK state instead of rotating forever (see FR-105) |
| FR-004 | developer | connect in STA mode via BLE provisioning (nRF54LM20DK) | No serial shell needed for provisioning | nRF Wi-Fi Provisioner app pushes credentials → device connects |
| FR-005 | developer | switch to SoftAP mode and have up to 3 clients connect | I can test AP mode with multiple devices | (1) `zego_wifi_mode softap` + reboot → up to 3 clients can join `192.168.7.1` simultaneously; (2) a 4th client is rejected by the AP; (3) on every client connect event the TODO log line clearly states the current count, e.g. `AP client connected: now 2/3 devices connected`; (4) on every client disconnect event the TODO log line clearly states remaining count, e.g. `AP client disconnected: now 1/3 devices connected`; (5) a client that disappears without a clean disconnect (e.g. power loss, no deauth frame sent) is still detected and removed — worst case within the AP's inactivity timeout (~5 minutes) |
| FR-006 | developer | switch to P2P_GO mode and pair a client with a button | Device auto-starts a P2P group and pairs on demand without shell commands | (1) P2P group created at boot; (2) a double-click on Button 0 opens a WPS PBC pairing window of ~2 min and the device logs that pairing is open; (3) a DK running P2P_GC mode that is also pairing connects within ~30 s; (4) the GO stays connectable so a previously-paired GC can reconnect; (5) phone-as-P2P-client is not supported; (6) the GO expects exactly one P2P_GC client at a time (informational — not enforced by the AP stack); (7) a client that disappears without a clean disconnect is still detected and removed, worst case within the AP's inactivity timeout (~5 minutes) |
| FR-007 | developer | have mode persist across power cycles | I don't re-enter mode on every boot | `zego_wifi_mode X` + power cycle → mode X at next boot |
| FR-008 | developer | see connection events in `net_event_app.c` | I have a clear hook to start my application logic | `zego_on_net_event_dhcp_bound()` called with correct `mode`, `ip_addr`, `mac_addr`, `ssid` |

### P1 — Should Have

| ID | As a… | I want to… | So that… | Acceptance Criteria |
|---|---|---|---|---|
| FR-101 | developer | read button events via `BUTTON_CHAN` | I can add button-driven application logic immediately | Button press publishes correct `button_msg` on `BUTTON_CHAN` |
| FR-102 | developer | control LEDs via `LED_CMD_CHAN` | I can add LED feedback immediately | `LED_CMD_CHAN` message changes LED state |
| FR-103 | developer | monitor heap and thread stack watermarks periodically | I detect memory leaks and stack overflows early | `zego/memonitor` brick samples all `k_heap` instances and thread stack HWMs every `CONFIG_ZEGO_MEMONITOR_INTERVAL_MS` (default 5 s) and publishes a `MEMONITOR_CHAN` zbus event; ZView live monitoring enabled when `CONFIG_ZEGO_MEMONITOR_ZVIEW=y` |
| FR-104 | evaluator | cycle Wi-Fi mode with a long button press | I can switch modes without a UART shell | The UX gesture button (idx 0; **BTN5/idx 4 on nRF5340 Audio DK**) held ≥ 3 s → next mode saved to NVS → device reboots into new mode |
| FR-105 | evaluator | see Wi-Fi connection state on LED 0 | I can tell at a glance whether the device is connected or still trying | (1) ROTATE while connecting, and **for as long as any automatic reconnect or pairing retry is in progress** (STA or P2P_GC, regardless of disconnect reason — these retries never give up on their own); (2) solid ON when connected (STA/P2P) or once the first SoftAP/P2P_GO client joins; (3) ROTATE again when the last SoftAP/P2P_GO client leaves; (4) **BREATHE during BLE provisioning OR while P2P pairing is in progress** (GO window open, or GC discovering/joining — whether pairing was started automatically at boot or via double-click), reverting to the normal state when pairing ends; (5) fast BLINK **only when reconnection is not possible at all** — today the only such case is STA with zero stored Wi-Fi networks (P2P_GC always keeps retrying, so it never shows BLINK); on nRF5340 Audio DK ROTATE uses RGB2 only ([3,4,5]), connected state shows solid green on RGB2 (index 4), and BLE prov / P2P pairing BREATHE on index 5 (blue channel only) |
| FR-106 | evaluator | toggle BLE provisioning with a double-click (nRF54LM20DK) | I can enter/exit provisioning mode without the shell | In STA/SoftAP modes, double-click on Button 0 toggles BLE provisioning advertising; in P2P_GO/P2P_GC modes the same gesture triggers P2P pairing instead (see FR-006 / FR-107) |
| FR-107 | developer | have a P2P_GC find and pair with a P2P_GO automatically, or on demand with a button press, and reconnect after reboot | I can use P2P without configuring a GO MAC at build time, and without needing a button press just to get connected | (1) no target-GO-MAC Kconfig is required; (2) a freshly-flashed P2P_GC with no saved GO **automatically starts pairing discovery at boot** — no button press required — and keeps retrying discovery indefinitely until a GO is found and joined; (3) a double-click on Button 0 also (re-)starts the same discovery-and-join process at any time, e.g. to pair with a different GO; (4) pairing connects via WPS PBC (`pbc --join`) to the GO currently in its pairing window → static IP 192.168.7.2/24 assigned and `zego_on_net_event_dhcp_bound()` called within ~30 s of finding a candidate GO; (5) on success the GO's MAC is saved to NVS; (6) on disconnect from a saved GO the device reconnects automatically, retrying indefinitely; (7) after a power cycle the device reconnects to the saved GO with no user interaction (both devices powered and in range) within ~30 s; (8) a new double-click pairing overwrites the saved GO MAC; (9) LED 0 ROTATEs while reconnecting to a saved GO, and BREATHEs for the entire duration of any pairing attempt, automatic or button-triggered (see FR-105) |
| FR-108 | — | *(removed)* MAC-prefix / target-MAC auto-select | superseded by runtime button pairing (FR-107) | The `CONFIG_ZEGO_WIFI_P2P_GC_TARGET_GO_MAC` Kconfig and its exact-MAC and prefix-RSSI auto-connect modes are removed; the GO is now selected at runtime via the pairing gesture and remembered in NVS |

---

## 4. Non-Functional Requirements

| Category | Requirement |
|---|---|
| Build | No compiler errors or warnings in template source files |
| Security | BLE provisioning credentials stored only in flash (Zephyr settings subsystem), never in source control |
| Portability | Template works on all three targets (nRF54LM20DK + nRF7002EB2, nRF7002DK, nRF5340 Audio DK + nRF7002EK); board `.conf` and `.overlay` files handle differences |
| Flash budget | nRF7002DK and nRF5340 Audio DK builds fit in 1 MB (BLE prov disabled); nRF54LM20DK build fits in 2 MB (BLE prov enabled) |

---

## 5. Hardware

| Board | Build target | BLE prov | Notes |
|---|---|---|---|
| nRF54LM20DK + nRF7002EB2 | `nrf54lm20dk/nrf54lm20a/cpuapp` + `-Dnordic-wifi-app-template_SNIPPET=wifi-p2p -DSHIELD=nrf7002eb2` | Enabled | 3 buttons, 4 LEDs |
| nRF7002DK | `nrf7002dk/nrf5340/cpuapp` + `-Dnordic-wifi-app-template_SNIPPET=wifi-p2p` | Disabled (1 MB flash too tight) | 2 buttons, 2 LEDs |
| nRF5340 Audio DK + nRF7002EK | `nrf5340_audio_dk/nrf5340/cpuapp` + `-Dnordic-wifi-app-template_SNIPPET=wifi-p2p -DSHIELD=nrf7002ek` | Disabled (1 MB flash too tight) | 5 buttons (VOL-, VOL+, PLAY/PAUSE, BTN4, BTN5), 9 LEDs; ROTATE on RGB2 only [3,4,5]; solid green (index 4) when connected |
