# Nordic Wi-Fi App Template

[![Validation](https://github.com/chshzh/zego/actions/workflows/validation.yml/badge.svg)](https://github.com/chshzh/zego/actions/workflows/validation.yml)
[![Latest Release](https://img.shields.io/github/v/release/chshzh/zego?label=Latest%20Release&color=skyblue)](https://github.com/chshzh/zego/releases/latest)

## Project Overview

### Introduction

`nordic-wifi-app-template` is the minimal, production-ready starting point for new NCS Wi-Fi applications targeting nRF7x development kits. It is aimed at embedded developers who need a verified connectivity foundation — all four Wi-Fi modes, three STA provisioning methods, persistent mode storage, and button/LED channels — without any application logic on top. Clone the template, fill in `net_event_app.c` to react to connectivity events, and the rest is already working.

### Supported hardware

| Board | Build target | Notes |
|-------|--------------|-------|
| nRF54LM20DK + nRF7002EB2 | `nrf54lm20dk/nrf54lm20a/cpuapp` + `-DSHIELD=nrf7002eb2` | BLE provisioning enabled by default |
| nRF7002DK | `nrf7002dk/nrf5340/cpuapp` | BLE provisioning optional (flash-constrained; see overlay build) |
| nRF5340 Audio DK + nRF7002EK | `nrf5340_audio_dk/nrf5340/cpuapp` + `-DSHIELD=nrf7002ek` | BLE provisioning optional (flash-constrained; see overlay build) |

### Features

- **Four Wi-Fi modes (STA, SoftAP, P2P_GO, P2P_CLIENT)** — covers every nRF7x connectivity scenario; mode is selected at runtime, not at build time.
- **Three STA provisioning methods** — shell `wifi connect` for one-off testing, `wifi cred add` for saved auto-connect credentials, and BLE provisioning via the *nRF Wi-Fi Provisioner* phone app (no USB cable required).
- **Runtime mode switching with `app_wifi_mode`** — persisted in NVS flash; survives power cycles so the device reconnects in the same mode after reboot.
- **Button and LED channels (`BUTTON_CHAN`, `LED_CMD_CHAN`)** — Zbus channels wired up and ready for application logic immediately.
- **LED 0 Wi-Fi state feedback** — ROTATE while connecting, solid ON when connected, BREATHE during BLE provisioning, fast blink on error; works the same across all three boards.
- **Button 0 gesture UX** — single-click prints Wi-Fi status to UART; long press (≥ 3 s) cycles modes; double-click toggles BLE provisioning advertising (nRF54LM20DK only).
- **Startup banner** — prints firmware version, board, MAC address, active mode, and per-mode connection instructions at every boot.
- **Single customisation point** — `src/modules/network/net_event_app.c` with inline guide and TODO-annotated patterns for publishing your own Zbus channel on connect/disconnect.
- **Live memory monitoring via [zego/memonitor](../bricks/memonitor)** — samples all `k_heap` instances and thread stack HWMs every 5 s; publishes `MEMONITOR_CHAN` zbus event on each sample; ZView live view available over JLink when `ZEGO_MEMONITOR_ZVIEW=y`.

### Target Users

- **Evaluator** — grab a pre-built `.hex` from the [Releases](https://github.com/chshzh/zego/releases/latest) page, flash it, and follow the [Evaluator Quick Start](#evaluator-quick-start) guide to reach a connected device in under 5 minutes.
- **Developer** — clone the workspace, build from source, and customise the firmware; see [Developer Guide](#developer-guide) for build setup and [Documentation](#documentation) for product requirements, architecture, and per-module specs.

---

## Evaluator Quick Start

### Step 1 — Flash the firmware

Download the pre-built `.hex` for your board from the [Releases](https://github.com/chshzh/zego/releases/latest) page:

| Board | File |
|-------|------|
| nRF54LM20DK + nRF7002EB2 | `nordic-wifi-app-template-nrf54lm20dk-nrf7002eb2-<version>.hex` |
| nRF7002DK | `nordic-wifi-app-template-nrf7002dk-<version>.hex` |
| nRF5340 Audio DK + nRF7002EK | `nordic-wifi-app-template-nrf5340-audio-dk-nrf7002ek-<version>.hex` |

Flash using **nRF Connect for Desktop → Programmer** (Erase & Write), or via CLI:

```sh
nrfutil device program --firmware nordic-wifi-app-template-<board>-<version>.hex --verify
```

> **BLE provisioning (nRF54LM20DK only):** After flashing, open the *nRF Wi-Fi Provisioner* phone app, select the device shown in the startup banner (e.g. `PV4A2F1B`), and enter your Wi-Fi credentials. The device connects and saves credentials to flash. This step is only needed once; subsequent reboots auto-connect.

### Step 2 — Verify

**1. UART log** — open a serial terminal at 115200 baud:

| Board | Port | Baud |
|-------|------|------|
| nRF54LM20DK + nRF7002EB2 | VCOM0 (`/dev/tty.usbmodem*1`) | 115200 |
| nRF7002DK | VCOM1 (`/dev/tty.usbmodem*3`) | 115200 |
| nRF5340 Audio DK + nRF7002EK | VCOM0 (`/dev/tty.usbmodem*1`) | 115200 |

At boot you will see a startup banner like:

```
*** Nordic Wi-Fi App Template v3.3.0.1 | NCS v3.3.0 ***
Board: nrf7002dk  MAC: AA:BB:CC:DD:EE:FF
Mode: P2P_GO  |  Modules: [wifi] [network] [ux] [button] [led]
P2P_GO: WPS PIN = 12345678 — connect your phone via Wi-Fi Direct settings
```

The firmware then starts the Wi-Fi subsystem and LED 0 begins ROTATING. Connection-mode instructions:

- **P2P_GO** (default on fresh flash): device starts its own Wi-Fi Direct group automatically — connect your phone via *Wi-Fi Direct* settings using WPS PIN `12345678`; look for `DIRECT-xx` in your phone's network list.
- **STA**: run `wifi connect -s <SSID> -p <pass> -k 1` in the UART shell; on success the terminal logs `Wi-Fi connected: mode=STA ip=192.168.x.x`.
- **SoftAP**: phone connects to the hotspot advertised by the device; device IP is `192.168.7.1`.
- **P2P_CLIENT**: connects to a P2P_GO (another DK or phone acting as GO).
  - **Auto-connect (recommended)**: set `CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC` in `prj.conf` and rebuild. Two modes:
    - **Exact MAC** (`F4:CE:36:00:AE:EC`): issues `--join` directly at boot; retries every 90 s.
    - **MAC-prefix** (`F4:CE:36:00:00:00` — last 3 bytes = `00:00:00`): runs a 10 s social-channel scan, then queries the wpa_supplicant peer table; connects to the GO with the strongest RSSI that matches the 3-byte OUI prefix; useful for multi-GO deployments where all GOs share the same manufacturer OUI.
  - **Manual**: run `wifi p2p find`, then `wifi p2p peer` to list discovered peers, then `wifi p2p connect <GO-MAC> pbc --join`.

Switch mode with `app_wifi_mode [sta|softap|p2p_go|p2p_client]` — the device reboots into the new mode and persists the setting.

**2. Buttons & LEDs**

### Buttons

| Board | Button | Gesture | Action |
|-------|--------|---------|--------|
| nRF54LM20DK + nRF7002EB2 | BUTTON0 (idx 0) | Single click | Print current Wi-Fi state to UART |
| | | Double-click | Toggle BLE provisioning advertising on/off |
| | | Long press ≥ 3 s | Cycle mode; save to NVS; reboot |
| | BUTTON1–2 (idx 1–2) | Any | Available via `BUTTON_CHAN` (no default UX function) |
| nRF7002DK | Button 1 / SW0 (idx 0) | Single click | Print current Wi-Fi state (mode, IP, SSID) to UART |
| | | Long press ≥ 3 s | Cycle mode STA → SoftAP → P2P_GO → STA; save to NVS; reboot |
| | Button 2 / SW1 (idx 1) | Any | Available via `BUTTON_CHAN` (no default UX function) |
| nRF5340 Audio DK + nRF7002EK | VOL- (idx 0) | Single click | Print current Wi-Fi state to UART |
| | | Long press ≥ 3 s | Cycle mode; save to NVS; reboot |
| | VOL+, PLAY/PAUSE, BTN4, BTN5 (idx 1–4) | Any | Available via `BUTTON_CHAN` (no default UX function) |

### LEDs

LED 0 reflects the Wi-Fi connection state. All other LEDs are available for application use via `LED_CMD_CHAN`.

| Board | LED 0 (idx 0) | Other LEDs |
|-------|---------------|------------|
| nRF54LM20DK + nRF7002EB2 | LED0 — Wi-Fi state | LED1–3 (idx 1–3) — free |
| nRF7002DK | LED1 — Wi-Fi state (see table below) | LED2 (idx 1) — free |
| nRF5340 Audio DK + nRF7002EK | RGB1 R/G/B (idx 0–2) — ROTATE across all three for state | RGB2 (idx 3–5), LED1–3 (idx 6–8) — free |

LED 0 Wi-Fi state effects:

| State | Effect |
|-------|--------|
| Boot / connecting | ROTATE (all LEDs in group) |
| Connected (STA / P2P) | Solid ON |
| SoftAP active, no clients | ROTATE |
| SoftAP client connected | Solid ON |
| BLE provisioning active | BREATHE |
| Disconnected / error | Fast BLINK (100 ms half-period) |

**3. Application logic**

Once Wi-Fi connects, the template logs the event from `net_event_app.c`:

```
Wi-Fi connected: mode=STA ip=192.168.1.42 mac=AA:BB:CC:DD:EE:FF ssid=MyNetwork
```

This confirms the `zego_on_net_event_dhcp_bound()` callback fired — the hook where you add your application logic.

---

## Developer Guide

### Project Structure

```text
nordic-wifi-app-template/
├── CMakeLists.txt                    ← registers zego modules; project entry point
├── Kconfig                           ← project Kconfig
├── prj.conf                          ← default Kconfig configuration (all modules enabled)
├── sysbuild.conf                     ← disables Partition Manager; enables MCUboot
├── overlay-nrf5340-wifi-ble-prov.conf ← optional: enable BLE prov on flash-constrained boards
├── boards/
│   ├── nrf7002dk_nrf5340_cpuapp.conf           ← nRF7002DK Kconfig (BLE prov off)
│   ├── nrf54lm20dk_nrf54lm20a_cpuapp.conf      ← nRF54LM20DK Kconfig (BLE prov on)
│   ├── nrf5340_audio_dk_nrf5340_cpuapp.conf    ← Audio DK Kconfig (BLE prov off)
│   └── nrf5340_audio_dk_nrf5340_cpuapp.overlay ← Audio DK DTS: nRF7002EK SPI pinout
├── docs/
│   ├── pm-prd/PRD.md                 ← Product Requirements Document
│   └── dev-specs/
│       ├── overview.md               ← Start here — spec index, PRD-to-spec mapping
│       ├── architecture.md           ← Module map, Zbus channels, SYS_INIT boot order
│       └── ux.md                     ← Button gestures and LED state machine spec
├── src/
│   ├── main.c                        ← boot banner, SYS_INIT registration
│   └── modules/
│       ├── messages.h                ← Zbus channel and message type definitions
│       ├── network/
│       │   └── net_event_app.c       ← customisation point: Wi-Fi connect/disconnect hooks
│       └── ux/
│           └── ux.c                  ← button gesture detection and LED state machine
└── sysbuild/
    └── hci_ipc/                      ← netcore config for nRF5340 dual-core boards
```

External zego bricks (referenced via `EXTRA_ZEPHYR_MODULES` in `CMakeLists.txt`):

```text
../bricks/wifi/          ← Wi-Fi mode selector, NVS persistence, `app_wifi_mode` shell command
../bricks/network/       ← Wi-Fi event dispatcher, DHCP handling, `zego_on_net_event_wifi_*` callbacks
../bricks/button/        ← GPIO debounce, BUTTON_CHAN publish
../bricks/led/           ← LED_CMD_CHAN subscriber, ROTATE/BLINK/BREATHE effects
../bricks/wifi_ble_prov/ ← BLE provisioning server (nRF54LM20DK; optional overlay on others)
```

### Workspace Setup

West workspace is driven by the repo-level [west.yml](../west.yml), which pins the NCS version:

```sh
- name: sdk-nrf
  path: nrf
  revision: v3.3.0
  import: true
  remote: ncs
```

Release versions follow the NCS version with a build counter suffix: `v<ncs-version>.<build>` (e.g. `v3.3.0.1`, `v3.3.0.2`). The major/minor/patch components always match the NCS version the firmware is based on.

Use nRF Connect for VS Code or a shell initialized with the NCS toolchain.

#### Method 1 (Preferred) — Add to an existing NCS installation

If you already have NCS v3.3.0 installed, reuse it directly — no re-downloading required.

```sh
cd /opt/nordic/ncs/v3.3.0   # your existing NCS workspace root

git clone https://github.com/chshzh/zego

# Switch the workspace manifest to the zego west.yml (one-time change)
west config manifest.path zego

# Sync — NCS repos already present, only new project repos are cloned
west update
```

#### Method 2 — Fresh installation as a Workspace Application

##### Option A: nRF Connect for VS Code

Follow the [custom repository guide](https://docs.nordicsemi.com/bundle/nrf-connect-vscode/page/guides/extension_custom_repo.html).

##### Option B: CLI

```sh
west init -m https://github.com/chshzh/zego --mr main <workspace-dir>
cd <workspace-dir>
west update
```

See the Nordic guide on [Workspace Application Setup](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/dev_model_and_contributions/adding_code.html#workflow_4_workspace_application_repository_recommended) for details.

### Build

```sh
# Go to app root first
cd zego/nordic-wifi-app-template

# nRF54LM20DK + nRF7002EB2 — all four modes including P2P
west build -p -b nrf54lm20dk/nrf54lm20a/cpuapp -d build_nrf54lm20dk -- \
  -Dnordic-wifi-app-template_SNIPPET=wifi-p2p -DSHIELD=nrf7002eb2

# nRF7002DK — all four modes including P2P_GO / P2P_CLIENT
west build -p -b nrf7002dk/nrf5340/cpuapp -d build_nrf7002dk -- \
  -Dnordic-wifi-app-template_SNIPPET=wifi-p2p

# nRF5340 Audio DK + nRF7002EK — all four modes including P2P
west build -p -b nrf5340_audio_dk/nrf5340/cpuapp -d build_nrf5340_audio_dk -- \
  -Dnordic-wifi-app-template_SNIPPET=wifi-p2p -DSHIELD=nrf7002ek
```

> The image-scoped `-Dnordic-wifi-app-template_SNIPPET=wifi-p2p` applies the snippet only to the app image (not to `hci_ipc`), avoiding spurious Kconfig warnings on the net core. The snippet adds `CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P=y` and `CONFIG_NRF70_P2P_MODE=y`. Without it, `p2p_go` and `p2p_client` modes are unavailable at runtime.

> **Toolchain wrapper** (if not in a west shell):
> ```sh
> nrfutil sdk-manager toolchain launch --ncs-version=v3.3.0 -- west build ...
> ```

#### Feature Overlay Builds

| Overlay | Purpose |
|---------|---------|
| `overlay-nrf5340-wifi-ble-prov.conf` | Enable BLE provisioning on nRF7002DK and nRF5340 Audio DK (disables P2P to recover flash headroom) |

```sh
# nRF7002DK — BLE provisioning enabled, P2P disabled
west build -p -b nrf7002dk/nrf5340/cpuapp -d build_nrf7002dk_bleprov -- \
  -DEXTRA_CONF_FILE=overlay-nrf5340-wifi-ble-prov.conf

# nRF5340 Audio DK + nRF7002EK — BLE provisioning enabled, P2P disabled
west build -p -b nrf5340_audio_dk/nrf5340/cpuapp -d build_nrf5340_audio_dk_bleprov -- \
  -DEXTRA_CONF_FILE=overlay-nrf5340-wifi-ble-prov.conf -DSHIELD=nrf7002ek
```

### Flash

First-time flash (erases NVS — Wi-Fi credentials and mode must be re-entered after):

```sh
west flash -d build_nrf54lm20dk --recover      # nRF54LM20DK
west flash -d build_nrf7002dk --erase          # nRF7002DK
west flash -d build_nrf5340_audio_dk --erase   # nRF5340 Audio DK
```

Subsequent flashes (preserves NVS — credentials and mode survive):

```sh
west flash -d build_nrf54lm20dk
west flash -d build_nrf7002dk
west flash -d build_nrf5340_audio_dk
```

### CI / CD

| Workflow | Trigger | Jobs |
|----------|---------|------|
| [Validation](https://github.com/chshzh/zego/actions/workflows/validation.yml) | push / PR → `main` | `build-and-test` — 3-board parallel matrix (nRF54LM20DK, nRF7002DK, nRF5340 Audio DK) using the NCS toolchain Docker image; uploads build logs, size reports, and merged hex as artifacts. `static-analysis` — Zephyr checkpatch on changed source files. |
| [Release](https://github.com/chshzh/zego/actions/workflows/release.yml) | push `v*` tag (e.g. `v3.3.0.1`) | `build-release` — sequential 3-board build with `APP_VERSION_STRING` set to the tag; resolves `merged.hex` per board. `create-release` — publishes a GitHub Release with one `.hex` and one `.elf` per board, a memory-usage table, and an auto-generated changelog. |

The tag format is `v<ncs-version>.<build>` (e.g. `v3.3.0.1`). Major/minor/patch always match the NCS version the firmware is based on.

### Developer Notes

- **Default mode on fresh flash is P2P_GO.** Switch to STA with `app_wifi_mode sta` before running `wifi connect`. The current mode is always printed in the startup banner.
- **BLE provisioning is disabled by default on nRF7002DK and nRF5340 Audio DK** due to 1 MB flash constraints. Use the `overlay-nrf5340-wifi-ble-prov.conf` overlay to enable it; this disables P2P to recover flash headroom. On nRF54LM20DK (2 MB), BLE provisioning is on by default.
- **NVS erase resets everything.** `--erase` (nRF7002DK / nRF5340 Audio DK) and `--recover` (nRF54LM20DK) wipe NVS — the device wakes in P2P_GO mode and Wi-Fi credentials must be re-entered.
- **nRF5340 Audio DK LED ROTATE** uses only RGB1 (channels idx 0–2) so RGB2 and the mono LEDs stay available for application use.
- **nRF5340 Audio DK DTS overlay** (`boards/nrf5340_audio_dk_nrf5340_cpuapp.overlay`) maps the nRF7002EK SPI bus to the Audio DK's Arduino header — this is required; the nRF54LM20DK shield handles its own pinout without an overlay.
- **Customisation entry point** is `src/modules/network/net_event_app.c`. Override `zego_on_net_event_dhcp_bound()` and `zego_on_net_event_wifi_disconnect()` — the file contains inline TODO comments and a 4-step example for publishing a Zbus channel.
- **Heap monitor** logs the high-water mark periodically (interval configurable via `CONFIG_APP_HEAP_MONITOR_INTERVAL_S`). Watch for steady growth as an early sign of leaks.
- **P2P connection method differences:**
  - **PBC (Push Button Config):** the GO must press/accept the WPS button to admit the client. On DK-as-GO this happens automatically; on phone-as-GO, accept the incoming connection request on the phone.
  - **PIN:** the P2P_CLIENT side must enter the PIN code displayed on (or configured in) the GO. On DK-as-GO the PIN is `12345678`; enter it on the phone's Wi-Fi Direct screen when prompted. On DK-as-P2P_CLIENT pass it with `wifi p2p connect <MAC> pin 12345678 --join`.
- **Live memory and thread watermark monitoring with ZView:**
  Run ZView from the project root while the board is live. Replace the `-s` serial with your board's J-Link serial number (`nrfjprog --ids`).

  **nRF54LM20DK + nRF7002EB2:**
  ```bash
  west zview live \
    -e build_nrf54lm20dk/nordic-wifi-app-template/zephyr/zephyr.elf \
    -r jlink \
    -t nRF54LM20A_M33 \
    -s 1051869687
  ```

  **nRF7002DK:**
  ```bash
  west zview live \
    -e build_nrf7002dk/nordic-wifi-app-template/zephyr/zephyr.elf \
    -r jlink \
    -t nRF5340_xxAA \
    -s 1050793110
  ```
  Targets the application core (M33); the network core runs `hci_ipc` which has no Zephyr kernel objects to monitor.

  **How to get representative watermarks:** Exercise all modes in one session before reading peak values. A typical sequence:
  1. Boot in SoftAP → connect 3 clients → disconnect one by one
  2. Switch to STA (`app_wifi_mode sta`) → `wifi connect` → `wifi disconnect`
  3. Switch to P2P_GO (`app_wifi_mode p2p_go`) → connect DK client
  4. Switch to P2P_CLIENT (`app_wifi_mode p2p_client`) → `wifi p2p connect <GO MAC> pbc --join`

  ZView accumulates **high-water marks** (HWM) across the session — the peaks shown after this full cycle represent worst-case usage across all modes.

  **Key threads to watch on nRF7002DK (nRF54LM20DK values are higher due to BLE):**

  | Thread | Expected HWM | Note |
  |---|---|---|
  | `hostap_handler` | ~7500 / 8304 (90 %) | High by design — drives WPA supplicant. Monitor for growth across firmware versions. |
  | `hostap_iface_wq` | ~3552 / 3952 (90 %) | WPA supplicant work queue. Stable. |
  | `nrf70_bh_wq` | ~1168 / 1304 (90 %) | nRF70 bottom half. Peaks in P2P_GO when client connects. |
  | `sysworkq` | ~4096 / 5072 (SoftAP peak) | Peaks in SoftAP/P2P mode; lower in STA-idle. |
  | `net_mgmt` | ~2536 / 3272 (STA peak) | Peaks during DHCP in STA mode. |
  | `net_socket_service` | ~1960 / 2400 (SoftAP) | Peaks in SoftAP when serving DHCP leases. |

  **Key heaps on nRF54LM20DK:**

  | Heap | Peak mode | HWM observed |
  |---|---|---|
  | `_system_heap` | P2P_GO (DK client connected) | ~94 KB / 110 KB |
  | `_system_heap` | SoftAP (3 clients) | ~89 KB / 110 KB |
  | `_system_heap` | STA (connected) | ~66 KB / 110 KB |

  **Memory sizing rules** — read HWM after exercising all four Wi-Fi modes in one session (worst-case coverage):

  *Thread stacks:*
  - Resize if HWM > **80 %** of allocated stack size.
  - For large, well-characterised stacks (> 2048 B) — `hostap_handler`, `hostap_iface_wq`, `nrf70_bh_wq` — the practical threshold is **90 %**; these sit at 85–90 % by design and are stable.
  - Sizing formula: `CONFIG_<THREAD>_STACK_SIZE = HWM / 0.9` (≈ 10 % headroom).

  *System heap:*
  - Resize if heap HWM > **80 %** of total heap size.
  - Sizing formula: `CONFIG_HEAP_MEM_POOL_SIZE = HWM / 0.8` (≈ 20 % headroom).

  Update `prj.conf` or `boards/nrf54lm20dk_nrf54lm20a_cpuapp.conf` with new measurements after each firmware change that touches network paths.

---

## Documentation

The full design documentation lives under `docs/`. Start with [docs/dev-specs/overview.md](docs/dev-specs/overview.md), which maps every PRD requirement to the spec file that implements it and provides an architecture summary.

| Document | Description |
|---|---|
| [docs/pm-prd/PRD.md](docs/pm-prd/PRD.md) | Product Requirements — user-perspective features, behavior, acceptance criteria, changelog |
| [docs/dev-specs/overview.md](docs/dev-specs/overview.md) | **Start here** — technical spec index, PRD-to-spec mapping, architecture summary, design decisions |
| [docs/dev-specs/architecture.md](docs/dev-specs/architecture.md) | System architecture — module map, Zbus channels, SYS_INIT boot sequence, memory budget |
| [docs/dev-specs/ux.md](docs/dev-specs/ux.md) | UX spec — button gesture state machine, LED effect definitions per Wi-Fi state |
| [zego/wifi ↗](https://github.com/chshzh/zego/blob/main/bricks/wifi/docs/wifi-spec.md) | Wi-Fi mode selector — NVS persistence, `app_wifi_mode` command, mode lifecycle |
| [zego/network ↗](https://github.com/chshzh/zego/blob/main/bricks/network/docs/network-spec.md) | Network module — Wi-Fi event dispatch, DHCP handling, callback contract |
| [zego/wifi_ble_prov ↗](https://github.com/chshzh/zego/blob/main/bricks/wifi_ble_prov/docs/wifi-ble-prov-spec.md) | BLE provisioning — GATT service, credential storage, nRF Wi-Fi Provisioner integration |
| [zego/button ↗](https://github.com/chshzh/zego/blob/main/bricks/button/docs/button-spec.md) | Button module — GPIO debounce, BUTTON_CHAN publish, gesture detection |
| [zego/led ↗](https://github.com/chshzh/zego/blob/main/bricks/led/docs/led-spec.md) | LED module — LED_CMD_CHAN subscriber, ROTATE/BLINK/BREATHE effect engine |

---

## Methodology

This project was developed using the [chsh-sk-ncs-0-workflow skill](https://github.com/chshzh/claude/blob/main/skills/chsh-sk-ncs-0-workflow/SKILL.md) — a four-phase lifecycle for NCS/Zephyr IoT projects where each phase has a dedicated AI skill:

| Phase | Focus | Skill | Output |
|-------|-------|-------|--------|
| 1 — Product Definition | What the device should do, for whom, and why | `chsh-sk-ncs-1-prd` | `docs/pm-prd/PRD.md` |
| 2 — Technical Design | Translate PRD into engineering specs | `chsh-sk-ncs-2-spec` | `docs/dev-specs/*.md` |
| 3 — Implementation | Implement, debug, and optimise code from approved specs | `chsh-sk-ncs-3.1-coding` · `chsh-sk-ncs-3.2-debug` · `chsh-sk-ncs-3.3-memopt` | `src/`, passing build |
| 4 — V&V | Verify code quality (no HW), then validate on hardware against PRD criteria | `chsh-sk-ncs-4.1-verification` · `chsh-sk-ncs-4.2-validation` | `docs/qa-test/VERIFICATION-*.md` + `docs/qa-test/VALIDATION-*.md` |

Each phase feeds the next: requirements drive specs, specs drive code, code drives tests. Issues loop back to the right phase — code bugs to Phase 3, spec gaps to Phase 2, new requirements to Phase 1.

---

## License

[SPDX-License-Identifier: LicenseRef-Nordic-5-Clause](../LICENSE)
