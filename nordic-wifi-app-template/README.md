# Nordic Wi-Fi App Template

[![Validation](https://github.com/chshzh/zego/actions/workflows/validation.yml/badge.svg)](https://github.com/chshzh/zego/actions/workflows/validation.yml)
[![Latest Release](https://img.shields.io/github/v/release/chshzh/zego?label=Latest%20Release&color=skyblue)](https://github.com/chshzh/zego/releases/latest)

## Project Overview

### Introduction

`nordic-wifi-app-template` is the minimal, production-ready starting point for new NCS Wi-Fi applications targeting nRF7x development kits. It is aimed at embedded developers who need a verified connectivity foundation — all four Wi-Fi modes, three STA provisioning methods, persistent mode storage, and button/LED channels — without any application logic on top. Clone the template, fill in `net_event_mgmt_app.c` to react to connectivity events, and the rest is already working.

### Supported hardware

| Board | Build target | Notes |
|-------|--------------|-------|
| nRF54LM20DK + nRF7002EB2 | `nrf54lm20dk/nrf54lm20a/cpuapp` + `-DSHIELD=nrf7002eb2` | BLE provisioning enabled by default |
| nRF7002DK | `nrf7002dk/nrf5340/cpuapp` | BLE provisioning optional (flash-constrained; see overlay build) |
| nRF5340 Audio DK + nRF7002EK | `nrf5340_audio_dk/nrf5340/cpuapp` + `-DSHIELD=nrf7002ek` | BLE provisioning optional (flash-constrained; see overlay build) |

### Features

- **Four Wi-Fi modes (STA, SoftAP, P2P_GO, P2P_GC)** — covers every nRF7x connectivity scenario; mode is selected at runtime, not at build time.
- **Three STA provisioning methods** — shell `wifi connect` for one-off testing, `wifi cred add` for saved auto-connect credentials, and BLE provisioning via the *nRF Wi-Fi Provisioner* phone app (no USB cable required).
- **Runtime mode switching with `zego_wifi_mode`** — persisted in NVS flash; survives power cycles so the device reconnects in the same mode after reboot.
- **Button and LED channels (`BUTTON_CHAN`, `LED_CMD_CHAN`)** — Zbus channels wired up and ready for application logic immediately.
- **LED 0 Wi-Fi state feedback** — ROTATE while connecting or while automatically retrying a reconnect/pairing, solid ON when connected, BREATHE during BLE provisioning or P2P pairing (all boards); fast BLINK is reserved for the one case where reconnection isn't possible at all (STA with no stored network). nRF5340 Audio DK uses RGB2 (idx 3–5) with Blue (idx 5) BREATHE for provisioning/pairing.
- **UX gesture button** — single-click prints Wi-Fi status to UART; long press (≥ 3 s) cycles modes; double-click is mode-aware — it toggles BLE provisioning advertising in STA/SoftAP modes (nRF54LM20DK only) and (re-)triggers P2P pairing in P2P_GO/P2P_GC modes. The gesture button is idx 0 on nRF54LM20DK/nRF7002DK and **BTN5 (idx 4) on the nRF5340 Audio DK**.
- **Automatic P2P pairing and reconnection, no configured MAC** — in P2P modes, the GO opens a ~2-min WPS PBC window on double-click, and the GC auto-starts pairing discovery at boot whenever it has no saved GO (no button press required) and keeps retrying until it joins one, saving the GO's MAC to NVS. Once paired, the GC reconnects automatically on disconnect and after power cycle, retrying indefinitely; a double-click always available to (re-)pair with a different GO.
- **Resilient Wi-Fi reconnection** — STA keeps retrying stored networks after any disconnect, on every board, independent of BLE provisioning; P2P_GC keeps retrying its saved GO or pairing search indefinitely. Neither gives up on its own.
- **Startup banner** — prints firmware version, board, MAC address, active mode, and per-mode connection instructions at every boot.
- **Single customisation point** — `src/modules/network/net_event_mgmt_app.c` with inline guide and TODO-annotated patterns for publishing your own Zbus channel on connect/disconnect.
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
P2P_GO: double-click Button 0 to open the PBC pairing window for a P2P_GC device
```

The firmware then starts the Wi-Fi subsystem and LED 0 begins ROTATING. Connection-mode instructions:

- **P2P_GO**: device creates its own Wi-Fi Direct group at boot and keeps WPS PBC continuously armed (re-armed every 5 min and after any client disconnects), so it stays connectable at all times — double-clicking Button 0 only opens a ~2-minute LED pairing cue, it does not gate whether a peer can join. (Only other DKs running P2P_GC mode can join directly; a phone cannot connect by initiating from its own Wi-Fi Direct screen — the nRF7x WPS Registrar does not support phone-initiated joins. To connect a phone, put the DK in P2P_GC mode and pair with the phone acting as P2P_GO instead. The GO expects exactly one P2P_GC client at a time.)
- **STA**: run `wifi connect -s <SSID> -p <pass> -k 1` in the UART shell; on success the terminal logs `Wi-Fi connected: mode=STA ip=192.168.x.x`. If the connection later drops, the device automatically keeps retrying the stored network — no button press needed.
- **SoftAP**: phone connects to the hotspot advertised by the device; device IP is `192.168.7.1`.
- **P2P_GC**: joins a P2P_GO's group; no GO MAC is configured at build time. Auto-pair/reconnect (below) only ever targets **Nordic-OUI MACs** (`F4:CE:36:xx:xx:xx` — nRF7002/nRF54LM20 factory MACs): the firmware filters the discovered-peer list to this OUI before picking the strongest-RSSI candidate, since `pbc --join` only works against an already-running DK P2P_GO. This is enforced in code (`wifi_utils.c`), not just a naming convention — a phone or any other nearby P2P device (printers, etc.) can never be picked as the auto-pair target.
  - **DK as P2P_GO — Auto-pair**: with no saved GO, the device automatically starts pairing discovery at boot and keeps retrying until it joins a Nordic-OUI peer — no button press required.
  - **DK as P2P_GO — Pair (manual/re-pair)**: double-click Button 0 on the GO (opens its pairing window), then double-click Button 0 on the GC to (re-)start discovery. The GC joins the pairing GO via WPS PBC (`pbc --join`), gets static IP `192.168.7.2/24`, and saves the GO's MAC to NVS.
  - **DK as P2P_GO — Reconnect**: after pairing, the GC reconnects to the saved GO automatically on disconnect and after a power cycle, retrying indefinitely — no button press needed. A new double-click pairing overwrites the saved GO.
  - **DK as P2P_GO — Manual (advanced)**: run `wifi p2p find`, then `wifi p2p peer` to list discovered peers (GO MAC starts with `F4:CE`), then `wifi p2p connect <GO-MAC> pbc --join`.
  - **Phone as P2P_GO**: not covered by auto-pair (filtered out by the Nordic-OUI check above) — connect manually. Put the phone in Wi-Fi Direct host/GO mode first, then run `wifi p2p find` and `wifi p2p peer` and look for a MAC that does **not** start with `F4:CE`, then `wifi p2p connect <phone-MAC> pbc -g 0` and accept the invitation on the phone.

Switch mode with `zego_wifi_mode [sta|softap|p2p_go|p2p_gc]` — the device reboots into the new mode and persists the setting.

**2. Buttons & LEDs**

### Buttons

| Board | Button | Gesture | Action |
|-------|--------|---------|--------|
| nRF54LM20DK + nRF7002EB2 | BUTTON0 (idx 0) | Single click | Print current Wi-Fi state to UART |
| | | Double-click (STA / SoftAP) | Toggle BLE provisioning advertising on/off |
| | | Double-click (P2P_GO / P2P_GC) | Trigger P2P pairing (GO opens WPS PBC window; GC (re-)starts discovery and joins) — optional on P2P_GC, which already auto-pairs at boot when no GO is saved |
| | | Long press ≥ 3 s | Cycle mode; save to NVS; reboot |
| | BUTTON1–2 (idx 1–2) | Any | Available via `BUTTON_CHAN` (no default UX function) |
| nRF7002DK | Button 1 / SW0 (idx 0) | Single click | Print current Wi-Fi state (mode, IP, SSID) to UART |
| | | Double-click (P2P_GO / P2P_GC) | Trigger P2P pairing (GO opens WPS PBC window; GC (re-)starts discovery and joins) — optional on P2P_GC, which already auto-pairs at boot when no GO is saved |
| | | Long press ≥ 3 s | Cycle mode STA → SoftAP → P2P_GO → P2P_GC → STA; save to NVS; reboot |
| | Button 2 / SW1 (idx 1) | Any | Available via `BUTTON_CHAN` (no default UX function) |
| nRF5340 Audio DK + nRF7002EK | **BTN5 (idx 4)** | Single click | Print current Wi-Fi state to UART |
| | | Double-click (P2P_GO / P2P_GC) | Trigger P2P pairing (GO opens WPS PBC window; GC (re-)starts discovery and joins) — optional on P2P_GC, which already auto-pairs at boot when no GO is saved |
| | | Long press ≥ 3 s | Cycle mode; save to NVS; reboot |
| | VOL-, VOL+, PLAY/PAUSE, BTN4 (idx 0–3) | Any | Available via `BUTTON_CHAN` (no default UX function) |

### LEDs



LED effects by board — organized by **effect** since which physical LED(s) light up differs by board (the nRF5340 Audio DK dedicates RGB2 to Wi-Fi state; the other boards use all available LEDs):

| Effect | nRF54LM20DK + nRF7002EB2 | nRF7002DK | nRF5340 Audio DK + nRF7002EK | When it happens |
|--------|--------------------------|-----------|-------------------------------|------------------|
| ROTATE | All 4 LEDs (idx 0–3) chase | Both LEDs (idx 0–1) chase | RGB2, all 3 channels chase (idx 3–5) | Boot / connecting; SoftAP or P2P_GO active with no clients yet; any automatic STA or P2P_GC reconnect / pairing-discovery retry in progress (started automatically or via double-click) — these retries never give up on their own |
| SOLID-ON | LED0 (idx 0) | LED1 (idx 0) | RGB2 Green only (idx 4); Red/Blue (idx 3, 5) held OFF | STA or P2P link connected; first SoftAP/P2P_GO client joins (stays solid until the last client leaves) |
| BREATHE | LED0 (idx 0) | LED1 (idx 0) | RGB2 Blue only (idx 5) | BLE provisioning active, or P2P pairing in progress (GO window open, or GC discovering/joining) — reverts to the normal state when pairing completes or a client connects |
| BLINK-FAST (100 ms half-period) | LED0 (idx 0) | LED1 (idx 0) | RGB2 Red only (idx 3) | **The only case where reconnection is not possible**: STA with zero stored Wi-Fi networks. P2P_GC never shows this effect — it always keeps retrying |

> RGB1 and the mono LEDs on the nRF5340 Audio DK remain off throughout, keeping RGB2 the dedicated Wi-Fi state indicator.

**3. Application logic**

Once Wi-Fi connects, the template logs the event from `net_event_mgmt_app.c`:

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
│       └── network/
│           └── net_event_mgmt_app.c   ← customisation point: Wi-Fi connect/disconnect hooks
└── sysbuild/
    └── hci_ipc/                      ← netcore config for nRF5340 dual-core boards
```

External zego bricks (referenced via `EXTRA_ZEPHYR_MODULES` in `CMakeLists.txt`):

```text
../bricks/wifi/          ← Wi-Fi mode selector, NVS persistence, `zego_wifi_mode` shell command
../bricks/network/       ← Wi-Fi event dispatcher, DHCP handling, `zego_on_net_event_wifi_*` callbacks
../bricks/button/        ← GPIO debounce, BUTTON_CHAN publish
../bricks/led/           ← LED_CMD_CHAN subscriber, ROTATE/BLINK/BREATHE effects
../bricks/wifi_ble_prov/ ← BLE provisioning server (nRF54LM20DK; optional overlay on others)
../bricks/ux/            ← Button gesture detection + LED state machine; weak-hook gesture API
../bricks/ntp/           ← SNTP time synchronization; real-world timestamps via CLOCK_REALTIME
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

# nRF7002DK — all four modes including P2P_GO / P2P_GC
west build -p -b nrf7002dk/nrf5340/cpuapp -d build_nrf7002dk -- \
  -Dnordic-wifi-app-template_SNIPPET=wifi-p2p

# nRF5340 Audio DK + nRF7002EK — all four modes including P2P
west build -p -b nrf5340_audio_dk/nrf5340/cpuapp -d build_nrf5340_audio_dk -- \
  -Dnordic-wifi-app-template_SNIPPET=wifi-p2p -DSHIELD=nrf7002ek
```

> The image-scoped `-Dnordic-wifi-app-template_SNIPPET=wifi-p2p` applies the snippet only to the app image (not to `hci_ipc`), avoiding spurious Kconfig warnings on the net core. The snippet adds `CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P=y` and `CONFIG_NRF70_P2P_MODE=y`. Without it, `p2p_go` and `p2p_gc` modes are unavailable at runtime.

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

- **Default mode on fresh flash is STA.** Switch modes with `zego_wifi_mode [sta|softap|p2p_go|p2p_gc]` (or cycle with a Button 0 long-press). The current mode is always printed in the startup banner.
- **BLE provisioning is disabled by default on nRF7002DK and nRF5340 Audio DK** due to 1 MB flash constraints. Use the `overlay-nrf5340-wifi-ble-prov.conf` overlay to enable it; this disables P2P to recover flash headroom. On nRF54LM20DK (2 MB), BLE provisioning is on by default.
- **NVS erase resets everything.** `--erase` (nRF7002DK / nRF5340 Audio DK) and `--recover` (nRF54LM20DK) wipe NVS — the device wakes in the default STA mode, any saved P2P_GC GO pairing is forgotten, and Wi-Fi credentials must be re-entered.
- **nRF5340 Audio DK LED ROTATE** uses RGB2 (channels idx 3–5); idx 5 Blue BREATHE indicates BLE provisioning or P2P pairing active. RGB1 (idx 0–2) and the mono LEDs stay available for application use.
- **nRF5340 Audio DK DTS overlay** (`boards/nrf5340_audio_dk_nrf5340_cpuapp.overlay`) maps the nRF7002EK SPI bus to the Audio DK's Arduino header — this is required; the nRF54LM20DK shield handles its own pinout without an overlay.
- **Customisation entry point** is `src/modules/network/net_event_mgmt_app.c`. Override `zego_on_net_event_dhcp_bound()` and `zego_on_net_event_wifi_disconnect()` — the file contains inline TODO comments and a 4-step example for publishing a Zbus channel.
- **Heap monitor** logs the high-water mark periodically (interval configurable via `CONFIG_APP_HEAP_MONITOR_INTERVAL_S`). Watch for steady growth as an early sign of leaks.
- **P2P pairing (button-driven or automatic, no configured MAC):** pairing uses WPS PBC (no PIN to enter). Double-click Button 0 on the GO to open a ~2-min PBC window. The GC either auto-starts pairing discovery at boot (when it has no saved GO) or (re-)starts it on a double-click; either way it keeps retrying until it discovers and joins the pairing GO. The GC saves the GO's MAC to NVS and reconnects automatically thereafter (on disconnect and across power cycles, retrying indefinitely) until a new pairing overwrites it.
  - **Manual (advanced):** `wifi p2p find` → `wifi p2p peer` → `wifi p2p connect <GO-MAC> pbc --join` on the GC, with the GO's PBC window open.
- **Reconnection philosophy:** STA and P2P_GC never give up retrying on their own — LED 0 ROTATEs for as long as a reconnect or pairing attempt is in flight. The fast BLINK error state is reserved for the one case where reconnection is genuinely not possible: STA with zero stored Wi-Fi networks.
- **Live memory and thread watermark monitoring with ZView:**
  Run ZView from the project root while the board is live. Replace the `-s` serial with your board's J-Link serial number (`nrfjprog --ids`).

  **nRF54LM20DK + nRF7002EB2:**
  ```bash
  west zview live \
    -e build_nrf54lm20dk/nordic-wifi-app-template/zephyr/zephyr.elf \
    -r jlink \
    -t nRF54LM20A_M33 \
    -s 1051839157
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
  2. Switch to STA (`zego_wifi_mode sta`) → `wifi connect` → `wifi disconnect`
  3. Switch to P2P_GO (`zego_wifi_mode p2p_go`) → double-click Button 0 to open pairing → connect DK client
  4. Switch to P2P_GC (`zego_wifi_mode p2p_gc`) → let it auto-pair, or double-click Button 0 to pair (or `wifi p2p connect <GO MAC> pbc --join`)

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
| [zego/wifi ↗](https://github.com/chshzh/zego/blob/main/bricks/wifi/docs/wifi-spec.md) | Wi-Fi mode selector — NVS persistence, `zego_wifi_mode` command, mode lifecycle |
| [zego/network ↗](https://github.com/chshzh/zego/blob/main/bricks/network/docs/network-spec.md) | Network module — Wi-Fi event dispatch, DHCP handling, callback contract |
| [zego/wifi_ble_prov ↗](https://github.com/chshzh/zego/blob/main/bricks/wifi_ble_prov/docs/wifi-ble-prov-spec.md) | BLE provisioning — GATT service, credential storage, nRF Wi-Fi Provisioner integration |
| [zego/button ↗](https://github.com/chshzh/zego/blob/main/bricks/button/docs/button-spec.md) | Button module — GPIO debounce, BUTTON_CHAN publish, gesture detection |
| [zego/led ↗](https://github.com/chshzh/zego/blob/main/bricks/led/docs/led-spec.md) | LED module — LED_CMD_CHAN subscriber, ROTATE/BLINK/BREATHE effect engine |

---

## Methodology

Developed with [chsh-sk-ncs-0-workflow](https://github.com/chshzh/claude/blob/main/skills/chsh-sk-ncs-0-workflow/SKILL.md) — a four-phase PRD → Specs → Implementation → V&V lifecycle for NCS/Zephyr IoT projects.

---

## License

[SPDX-License-Identifier: LicenseRef-Nordic-5-Clause](../LICENSE)
