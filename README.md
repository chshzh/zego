# Zego — Reusable NCS/Zephyr modules

[![Validation](https://github.com/chshzh/zego/actions/workflows/validation.yml/badge.svg)](https://github.com/chshzh/zego/actions/workflows/validation.yml)
[![Latest Release](https://img.shields.io/github/v/release/chshzh/zego?label=Latest%20Release&color=skyblue)](https://github.com/chshzh/zego/releases/latest)

**Zego** is the *Zephyr + LEGO* concept applied to embedded firmware: build applications
by snapping together self-contained modules the way you assemble LEGO bricks.
Each module is an independent Zephyr module with its own Kconfig, CMake, and zbus
channel definitions. Applications declare which bricks they need; the build system
wires them automatically — no code copying, no manual glue.

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│                                   Application                                    │
│                      (prj.conf: CONFIG_ZEGO_BUTTON=y, etc.)                      │
├────────┬────────┬────────┬─────────┬───────────────┬───────────┬────────┬────────┤
│ button │  led   │  wifi  │ network │ wifi_ble_prov │ memonitor │   ux   │  ntp   │  ← zego bricks
│ module │ module │ module │ module  │    module     │  module   │ module │ module │
├────────┴────────┴────────┴─────────┴───────────────┴───────────┴────────┴────────┤
│                               Zephyr kernel + zbus                               │
└──────────────────────────────────────────────────────────────────────────────────┘
```

### Design principles

| Principle | How zego applies it |
|-----------|---------------------|
| **Single responsibility** | Each module owns exactly one hardware abstraction or service. |
| **Loose coupling** | Modules never call each other directly — they communicate only through typed zbus channels. |
| **Opt-in composition** | A module is compiled in only when its `CONFIG_ZEGO_*` Kconfig symbol is enabled. Unused bricks add zero code. |
| **Portable backends** | Hardware-specific code is isolated behind a Kconfig-selectable backend (e.g. `ZEGO_BUTTON_BACKEND_DK` vs `ZEGO_BUTTON_BACKEND_GPIO`). Swap the board; keep the module. |
| **Self-contained specs** | Every module ships its own `docs/<module>-spec.md` — API, channels, Kconfig reference, and hardware test guide in one place. |

Shared module library for Nordic nRF Connect SDK applications.
Capabilities are composed by enabling Kconfig symbols — no code copying.

---

## Bricks

| Brick | Directory | zbus channels | Spec |
|-------|-----------|---------------|------|
| button | `zego/bricks/button/` | `BUTTON_CHAN` (out) | [button-spec.md](bricks/button/docs/button-spec.md) |
| led | `zego/bricks/led/` | `LED_CMD_CHAN` (in) · `LED_STATE_CHAN` (out) | [led-spec.md](bricks/led/docs/led-spec.md) |
| wifi | `zego/bricks/wifi/` | `WIFI_MODE_CHAN` (out) | [wifi-spec.md](bricks/wifi/docs/wifi-spec.md) |
| network | `zego/bricks/network/` | — (weak-hook API) | [network-spec.md](bricks/network/docs/network-spec.md) |
| wifi_ble_prov | `zego/bricks/wifi_ble_prov/` | — | [wifi-ble-prov-spec.md](bricks/wifi_ble_prov/docs/wifi-ble-prov-spec.md) |
| memonitor | `zego/bricks/memonitor/` | `MEMONITOR_CHAN` (out) | [memonitor-spec.md](bricks/memonitor/docs/memonitor-spec.md) |
| ux | `zego/bricks/ux/` | `ZEGO_UX_WIFI_STATE_CHAN` (in) · weak-hook gesture API | [ux-spec.md](bricks/ux/docs/ux-spec.md) |
| ntp | `zego/bricks/ntp/` | `ZEGO_NTP_NET_CHAN` (in) | [ntp-spec.md](bricks/ntp/docs/ntp-spec.md) |

See each spec for the full API, Kconfig reference, and hardware test guide.

### Brick zbus channel map

Most bricks never call each other directly — they only exchange typed messages over
zbus channels (🟨). `network` has no channel of its own; instead it fires `__weak`
callback hooks that the application overrides to publish onto app-level or brick-owned
channels (dashed arrows), which is how `ux` and `ntp` learn about connectivity changes.

```
button ──publishes──► BUTTON_CHAN ──consumed by──► ux
ux     ──publishes──► LED_CMD_CHAN ──consumed by──► led ──publishes──► LED_STATE_CHAN
wifi   ──publishes──► WIFI_MODE_CHAN ──consumed by──► network

network ──fires __weak hooks──► application (net_event_*.c overrides)
                                    │
                                    ├──publishes──► ZEGO_UX_WIFI_STATE_CHAN ──► ux
                                    └──publishes──► ZEGO_NTP_NET_CHAN (CONFIG_ZEGO_NTP=y) ──► ntp

wifi_ble_prov ──publishes──► BLE_PROV_CONN_CHAN ──consumed by──► ux

memonitor ──publishes──► MEMONITOR_CHAN  (periodic sampler, no upstream brick input;
                                          consumed by application-level metrics/dashboard code)
```

---

## Example application

| App | Directory | Description |
|-----|-----------|-------------|
| [nordic-wifi-app-template](nordic-wifi-app-template/README.md) | `zego/nordic-wifi-app-template/` | NCS Wi-Fi app template using all zego modules (STA / SoftAP / P2P + BLE provisioning) |

---

## Live memory monitoring — memonitor brick

`memonitor` is a zego brick that periodically samples all `k_heap` instances and Zephyr thread stack watermarks. It fires every `CONFIG_ZEGO_MEMONITOR_INTERVAL_MS` on the system work queue, stores both snapshots in a spinlock-protected static cache, and publishes a small `MEMONITOR_CHAN` zbus notification so subscribers know fresh data is ready.

| Kconfig symbol | Default | Purpose |
|----------------|---------|---------|
| `CONFIG_ZEGO_MEMONITOR` | `n` | Enable the brick |
| `CONFIG_ZEGO_MEMONITOR_INTERVAL_MS` | `2000` | Sampling interval in ms (100–60 000) |
| `CONFIG_ZEGO_MEMONITOR_LOG_LEVEL` | `3` (INF) | Log verbosity 0–4 |

Requires: `CONFIG_ZBUS=y`, `CONFIG_SYS_HEAP_RUNTIME_STATS=y`, `CONFIG_THREAD_STACK_INFO=y`, `CONFIG_INIT_STACKS=y`, `CONFIG_STACK_SENTINEL=n`.

Consumers call `memonitor_get_heaps()` and `memonitor_get_threads()` to obtain a thread-safe point-in-time copy — safe from any context including HTTP handlers. The primary validated consumer is `nordic-wifi-webdash`, which serves the snapshots as `/api/heaps` and `/api/threads` JSON endpoints.

See [memonitor-spec.md](bricks/memonitor/docs/memonitor-spec.md) for the full API, Kconfig reference, and integration guide.

### ZView vs memonitor

| | [ZView](https://github.com/chshzh/zview) | memonitor brick |
|---|---|---|
| **Transport** | JLink RTT — requires a physical debug probe | None — self-contained in firmware |
| **When it works** | Development only (probe attached) | Any time, including deployed devices |
| **Firmware cost** | Near-zero ROM/RAM — RTT buffer only | ~4 KB static BSS; small periodic work-queue task |
| **Data access** | Host-side tool on the developer's PC | Firmware-internal: zbus subscribers, HTTP handlers, loggers |
| **Primary use** | Measure stack watermarks → tune `boards/<board>.conf` | Runtime heap/thread health visible to the application |

Use ZView during development to size stacks correctly. Use memonitor at runtime to expose live memory health to the application (e.g. a web dashboard or Memfault metric).

---

## CI / CD

| Workflow | Trigger | What it does |
|----------|---------|--------------|
| [Validation](https://github.com/chshzh/zego/actions/workflows/validation.yml) | push / PR → `main` | Builds all 3 boards in parallel; runs Zephyr checkpatch on changed source files |
| [Release](https://github.com/chshzh/zego/actions/workflows/release.yml) | push `v*` tag | Builds all 3 boards, stamps firmware with the tag version, publishes a GitHub Release with `.hex` + `.elf` per board |

See [nordic-wifi-app-template/README.md — CI / CD](nordic-wifi-app-template/README.md#ci--cd) for workflow details and artifact layout.

---

## Workspace setup

### Add to an existing NCS installation

```sh
cd /opt/nordic/ncs/<ncs-version>
git clone https://github.com/chshzh/zego.git
west config manifest.path zego
west update
```

### Fresh workspace

```sh
west init -m https://github.com/chshzh/zego --mr main <workspace-dir>
cd <workspace-dir>
west update
```

### Vendored SDK patches

`west update` checks out a plain, unpatched `nrf` module — it will silently
drop any local edits made under `nrf/`. This repo carries small vendored
patches to fix upstream gaps; reapply them after every `west update`:

```sh
cd nrf
git apply ../zego/patches/nrf_security/0001-forward-mbedtls-memory-debug.patch
```

See [`patches/nrf_security/README.md`](patches/nrf_security/README.md) for
what each patch does and why. CI (`validation.yml` / `release.yml`) applies
these automatically after its own `west update` step — a fresh, unpatched
`nrf` checkout that skips this step will fail to build once
`CONFIG_MBEDTLS_MEMORY_DEBUG` is auto-selected by `bricks/memonitor`.

---

## Integration

### Start from the template (recommended)

The fastest way to build a new zego-based application is to copy
[`nordic-wifi-app-template/`](nordic-wifi-app-template/) and trim it down
to the modules you need.  The template already has the full `CMakeLists.txt`
wiring, `prj.conf` stubs, `boards/` overlays, and a working `west.yml` —
so you get a clean build on the first try without reading the details below.

```sh
cp -r zego/nordic-wifi-app-template my-app
cd my-app
# edit CMakeLists.txt: remove modules you don't need
# edit prj.conf: set CONFIG_ZEGO_* symbols
west build -p -b nrf7002dk/nrf5340/cpuapp
```

### Wire modules manually (from scratch)

If you prefer to start from a blank NCS application, register modules as
first-class Zephyr modules via `EXTRA_ZEPHYR_MODULES` in the app's
`CMakeLists.txt` **before** `find_package(Zephyr ...)`.
Kconfig is wired automatically — no `rsource` needed.

```cmake
cmake_minimum_required(VERSION 3.20.0)

get_filename_component(ZEGO_BUTTON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zego/bricks/button REALPATH)
get_filename_component(ZEGO_LED_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/../zego/bricks/led    REALPATH)
list(APPEND EXTRA_ZEPHYR_MODULES ${ZEGO_BUTTON_DIR} ${ZEGO_LED_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
```

`prj.conf`:

```conf
CONFIG_ZEGO_BUTTON=y
CONFIG_ZEGO_LED=y
```

`boards/<board>.conf`:

```conf
# nRF7002DK
CONFIG_ZEGO_BUTTON_NUM_BUTTONS=2
CONFIG_ZEGO_BUTTON_BACKEND_DK=y
CONFIG_ZEGO_LED_NUM_LEDS=2
CONFIG_ZEGO_LED_BACKEND_DK=y

# nRF54LM20DK
CONFIG_ZEGO_BUTTON_NUM_BUTTONS=4
CONFIG_ZEGO_BUTTON_BACKEND_DK=y
CONFIG_ZEGO_LED_NUM_LEDS=4
CONFIG_ZEGO_LED_BACKEND_DK=y
```

To use the portable Zephyr backends instead (requires `gpio-leds` / `gpio-keys` DTS nodes):

```conf
CONFIG_ZEGO_BUTTON_BACKEND_GPIO=y
CONFIG_ZEGO_LED_BACKEND_ZEPHYR=y
```

> **Button and LED indices are 0-based.** Display names ("Button 1", "LED1") are
> board-specific and belong in the application.

---

## Repository layout

```
zego/
├── bricks/
│   ├── button/        ← GPIO button driver, gesture detection, BUTTON_CHAN publisher
│   ├── led/           ← per-LED state machine, LED_CMD_CHAN subscriber
│   ├── wifi/          ← Wi-Fi mode selector + NVS persistence
│   ├── network/       ← Wi-Fi event management, DHCP, weak-hook API
│   ├── wifi_ble_prov/ ← BLE GATT provisioning service
│   └── memonitor/     ← periodic heap and thread-stack sampler; publishes MEMONITOR_CHAN
├── nordic-wifi-app-template/  ← example app (STA / SoftAP / P2P + BLE prov)
└── west.yml   ← workspace manifest (contain NCS version based on)
```

---

## Versioning

`zego` versions match the NCS version they target: `v<ncs-version>` (e.g. `v3.3.0`).
No build counter suffix is used — one release tag per NCS version.
The tag is pinned in each consuming application's `west.yml`.
Applications are released independently from their own repositories.

## License

[SPDX-License-Identifier: LicenseRef-Nordic-5-Clause](LICENSE)
