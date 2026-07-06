# Architecture Specification — Nordic Wi-Fi App Template

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
| 2026-07-02-00-00 | `ux` moved to a zego brick (`zego/bricks/ux/`, `CONFIG_ZEGO_UX`); gesture actions are now `__weak` overridable hooks; module dependency diagram updated (`app_ux` → `zego/ux`). See `zego/bricks/ux/docs/ux-spec.md`. |
| 2026-06-04-17-09 | Initial spec |
| 2026-06-05-09-38 | Added nRF5340 Audio DK + nRF7002EK to Board Differences table; updated BLE prov note |
| 2026-06-09-17-25 | Updated to PRD v2026-06-09-17-25: fixed Board Differences — nRF5340 Audio DK ROTATE is RGB2 only [3–5], not RGB1 |
| 2026-06-16-13-30 | Updated to PRD v2026-06-16-13-30: FR-005 — SoftAP max 3 clients (`CONFIG_WIFI_MGMT_AP_MAX_NUM_STA=3`); net_event_app TODO log format specified for connect/disconnect events |
| 2026-06-29-23-06 | Updated to PRD v2026-06-29-23-06: corrected "default on fresh flash" to `ZEGO_WIFI_MODE_STA` (0); mode-cycle text adds P2P_GC (validation-found doc fix). |
| 2026-06-29-21-44 | Updated to PRD v2026-06-29-21-44: P2P pairing UX — added NVS settings key `net/p2p_gc_go_mac` (learned GO MAC, network brick's own `"net"` subtree) to the Wi-Fi mode selector section; module map and weak-hook mode column P2P_CLIENT→P2P_GC; noted UX→network `wifi_p2p_start_pairing()` call. |
| 2026-06-30-13-04 | Updated to PRD v2026-06-30-13-00: added 7th weak hook `zego_on_net_event_p2p_pairing(bool)` (drives `APP_WIFI_STATE_PAIRING` → LED BREATHE on both roles); noted board-configurable UX gesture button (`CONFIG_APP_UX_BUTTON_IDX`, =4 on Audio DK). Debug-session reconcile: P2P WPS method is PBC; `CONFIG_WIFI_NM_WPA_SUPPLICANT_GLOBAL_HEAP=y` (system heap) required for the P2P_GO WPS Registrar. |
| 2026-07-01-10-54 | Updated to PRD v2026-07-01-10-50: `zego_on_net_event_wifi_disconnect()` gains a `bool will_retry` parameter (§3.1); clarified the SoftAP TODO log format's "/3" is SoftAP-specific and does not apply to P2P_GO's client count; added §5 note on P2P_GC auto-pairing at boot. See `network-spec.md` and `ux-module.md` for the full disconnection-handling and LED-trigger changes. |
| 2026-07-03-13-55 | Startup banner moved from `zego/wifi` to `zego/bricks/ux` (`zego_wifi_print_banner()` → `zego_ux_print_banner()`), called from `main()`. `zego/wifi` now provides only the Wi-Fi mode selector. See `zego/bricks/ux/docs/ux-spec.md`. |

---

## 1. Overview

`nordic-wifi-app-template` is a minimal NCS Wi-Fi application skeleton. It wires up the complete
connectivity layer (all four Wi-Fi modes, all three STA provisioning methods) and exposes a single
hook file (`net_event_app.c`) where application-specific logic goes.

`main()` is deliberately empty — it prints the startup banner and returns. All modules initialise
themselves via `SYS_INIT` and communicate exclusively through Zbus channels.

```c
/* src/main.c — the entire application thread */
int main(void)
{
    zego_ux_print_banner();
    return 0;
}
```

---

## 2. Module Map

All feature modules are provided by the `zego/` shared library. The template registers five:

| Module dir | Kconfig symbol | Provides |
|---|---|---|
| `zego/wifi` | `CONFIG_ZEGO_WIFI` | Wi-Fi mode selector, `zego_wifi_mode` shell command, NVS persistence |
| `zego/network` | `CONFIG_ZEGO_NETWORK` | Wi-Fi event management (STA / SoftAP / P2P_GO / P2P_GC), P2P button pairing + NVS-saved GO MAC, DHCP, net mgmt callbacks, weak-hook API |
| `zego/button` | `CONFIG_ZEGO_BUTTON` | GPIO button driver, gesture detection, `BUTTON_CHAN` publisher |
| `zego/led` | `CONFIG_ZEGO_LED` | Per-LED state machine, `LED_CMD_CHAN` subscriber |
| `zego/ux` | `CONFIG_ZEGO_UX` | Button gestures, LED Wi-Fi feedback, startup banner (`zego_ux_print_banner()`) |
| `zego/wifi_ble_prov` | `CONFIG_ZEGO_WIFI_BLE_PROV` | BLE GATT provisioning service (nRF Wi-Fi Provisioner compatible) |

Plus one in-tree application file:

| File | Purpose |
|---|---|
| `src/modules/network/net_event_app.c` | Strong overrides of the two `__weak` hooks from `zego/network`; application reaction to connectivity events lives here |

---

## 3. Zbus Channels

| Channel | Message type | Publisher | Subscriber(s) | Description |
|---|---|---|---|---|
| `WIFI_MODE_CHAN` | `enum zego_wifi_mode` | `zego/wifi` (at `SYS_INIT`) | `zego/network` | Active Wi-Fi mode read once at boot |
| `BUTTON_CHAN` | `struct button_msg` | `zego/button` | Application (add your own) | Button events (click, double-click, long press) |
| `LED_CMD_CHAN` | `struct led_cmd_msg` | Application (add your own) | `zego/led` | LED control commands |

> `zego/network` uses a **weak-hook pattern** instead of a Zbus channel for the connectivity event. Override the hooks in `net_event_app.c`; publish your own channel from there if needed.

### 3.1 Weak-hook API (`zego/network`)

```c
/* Defined as __weak no-ops in zego/network; override in net_event_app.c */

void zego_on_net_event_dhcp_bound(enum zego_wifi_mode mode,
                                    const char *ip_addr,
                                    const char *mac_addr,
                                    const char *ssid);

/* will_retry: true if the module will keep retrying automatically (STA with
 * >=1 stored credential; P2P_GC always). false only for STA with zero stored
 * credentials - the one case where reconnection isn't possible. Drives
 * APP_WIFI_STATE_CONNECTING (ROTATE) vs APP_WIFI_STATE_ERROR (BLINK). */
void zego_on_net_event_wifi_disconnect(bool will_retry);

/* P2P pairing started (true) / ended (false). net_event_app.c maps this to
 * APP_WIFI_STATE_PAIRING so the UX LED breathes while pairing is active. */
void zego_on_net_event_p2p_pairing(bool active);
```

**Trigger events by mode:**

| Mode | Hook called when… |
|---|---|
| STA | DHCP bound (IPv4 address assigned) |
| SoftAP | First client station associates (up to `CONFIG_WIFI_MGMT_AP_MAX_NUM_STA` = **3** clients; 4th is rejected) |
| P2P_GO | First P2P client associates |
| P2P_GC | Connected to GO (static IP 192.168.7.2 assigned at `CONNECT_RESULT`) |

**SoftAP max-client limit — required `prj.conf` entry:**

```kconfig
# Limit SoftAP to 3 simultaneous clients (Zephyr default is 4)
CONFIG_WIFI_MGMT_AP_MAX_NUM_STA=3
```

**Required TODO log format in `net_event_app.c`:**

| Callback | Required `LOG_INF` format |
|---|---|
| `zego_on_net_event_wifi_ap_sta_connected(sta_count)` | `"AP client connected: now %d/3 devices connected"` |
| `zego_on_net_event_wifi_ap_sta_disconnected(remaining_clients)` | `"AP client disconnected: now %d/3 devices connected"` |

> The `/3` in the log format is **SoftAP-specific** (`CONFIG_WIFI_MGMT_AP_MAX_NUM_STA=3`). Both
> callbacks are shared with P2P_GO (see the SoftAP/P2P_GO shared AP handler note above), which
> has no fixed client cap — in practice exactly one P2P_GC connects, but this is informational
> only and not enforced. The literal `/3` text is still fine to log in P2P_GO context (it is
> template example code the developer is expected to customise), but don't read it as a P2P_GO
> limit.

### 3.2 Extending with a custom Zbus channel

The `net_event_app.c` file contains a 4-step TODO guide:

1. Define `struct my_wifi_msg` and `ZBUS_CHAN_DECLARE(MY_WIFI_CHAN)` in `src/modules/messages.h`
2. Own the channel with `ZBUS_CHAN_DEFINE(...)` in exactly one `.c` file
3. Replace the `LOG_INF` calls in the hooks with `zbus_chan_pub(&MY_WIFI_CHAN, &msg, K_NO_WAIT)`
4. Add subscribers with `ZBUS_SUBSCRIBER_DEFINE` / `ZBUS_LISTENER_DEFINE` in other modules

---

## 4. SYS_INIT Boot Sequence

Modules initialise in priority order. Lower numbers run first.

| Priority | Module | Action |
|---|---|---|
| 41 | `zego/wifi` | Read NVS for saved Wi-Fi mode; publish `WIFI_MODE_CHAN` |
| 42 | `zego/network` | Subscribe to `WIFI_MODE_CHAN`; launch selected Wi-Fi path (STA / SoftAP / P2P) |
| 45 | `zego/button` | Register GPIO interrupt callbacks; start `BUTTON_CHAN` publisher |
| 45 | `zego/led` | Subscribe to `LED_CMD_CHAN`; initialise LED GPIO |
| 45 | `zego/wifi_ble_prov` | Start BLE stack; register GATT provisioning service (if `CONFIG_ZEGO_WIFI_BLE_PROV=y`) |

`main()` runs after all `SYS_INIT` callbacks complete. It calls `zego_ux_print_banner()` to emit the startup banner and connectivity instructions, then returns (Zephyr keeps the system alive).

---

## 5. Wi-Fi Mode Selector

Implemented entirely in `zego/wifi`. See [zego/wifi — wifi-spec.md](https://github.com/chshzh/zego/blob/main/modules/wifi/docs/wifi-spec.md) for full details.

**Summary:**
- NVS settings key: `"app/zego_wifi_mode"` (uint8_t)
- Default on fresh flash: `ZEGO_WIFI_MODE_STA` (0) — set by `CONFIG_ZEGO_WIFI_DEFAULT_MODE_STA=y` in `prj.conf`
- Shell command: `uart:~$ zego_wifi_mode [sta|softap|p2p_go|p2p_gc]`
- Mode is written to NVS immediately; takes effect on next reboot

**P2P_GC learned-GO persistence (added 2026-06-29):**
- NVS settings key: `"net/p2p_gc_go_mac"` (6 raw MAC bytes), in the network brick's own `"net"` settings subtree (distinct from the mode selector's `"app"` subtree — two static handlers cannot share a subtree name), owned by `zego/network`
- Written by `wifi_p2p_gc_on_connect_result()` after a successful pairing connect; read at boot by `wifi_run_p2p_gc_mode()`
- Empty/absent = never paired — **`wifi_run_p2p_gc_mode()` now auto-starts pairing discovery immediately** (no button press required) and retries indefinitely until a GO is found; a double-click still works at any time to (re-)pair, e.g. with a different GO. See `network-spec.md` for the full sequence.
- A new pairing overwrites the saved MAC
- Negligible footprint (6 bytes); no partition-layout change

---

## 6. STA Provisioning Paths

All three paths are enabled by default; they are independent and coexist in the same binary.

| Method | Kconfig guard | How it works |
|---|---|---|
| Shell one-time | `CONFIG_NET_L2_WIFI_SHELL=y` | `wifi connect -s <SSID> -p <pass> -k 1` — session only, not persisted |
| Saved credentials | `CONFIG_WIFI_CREDENTIALS=y` | `wifi cred add <SSID> WPA2-PSK <pass> -k 1` — stored in flash, auto-reconnect on every boot |
| BLE provisioning | `CONFIG_ZEGO_WIFI_BLE_PROV=y` | nRF Wi-Fi Provisioner app pushes credentials over BLE; stored in flash via settings subsystem |

BLE provisioning is **disabled on nRF7002DK and nRF5340 Audio DK** (`boards/*_nrf5340_cpuapp.conf`) because the
combined flash of BLE host stack + P2P snippet + app exceeds the 1 MB limit. Re-enable by removing
`CONFIG_ZEGO_WIFI_BLE_PROV=n` from the relevant board conf if flash headroom allows.

---

## 7. Board Differences

| Feature | nRF54LM20DK + nRF7002EB2 | nRF7002DK | nRF5340 Audio DK + nRF7002EK |
|---|---|---|---|
| Flash | 2 MB | 1 MB | 1 MB |
| RAM | 512 KB | 448 KB (app core) | 512 KB (app core) |
| Buttons | 3 (BUTTON0–2) | 2 (SW0, SW1) | 5 (VOL-, VOL+, PLAY/PAUSE, BTN4, BTN5) |
| LEDs | 4 | 2 | 9 (RGB1 idx 0–2, RGB2 idx 3–5, mono idx 6–8; ROTATE on **RGB2 only** [3–5]; solid green on idx 4 when connected) |
| BLE provisioning | Enabled | Disabled (flash) | Disabled (flash) |
| Network core | Single-core; `hci_ipc` build is harmless no-op | nRF5340 netcore runs `hci_ipc` for BLE | nRF5340 netcore runs `hci_ipc` for BLE |
| Build shield | `-DSHIELD=nrf7002eb2` | — | `-DSHIELD=nrf7002ek` |
| DTS overlay | — | — | Required: disables `gpio_fwd`, adds SPI4 `bias-pull-down` |

---

## 8. Memory Budget (nRF7002DK, `wifi-p2p` snippet, BLE prov off)

Measured from last verified build:

| Region | Used | Available | % |
|---|---|---|---|
| Flash | ~970 KB | 1024 KB | ~95% |
| RAM | ~370 KB | 448 KB | ~83% |

> These figures will grow as application logic is added to `net_event_app.c` and new modules are
> introduced. Monitor with `west build --cmake-only` + `python3 zephyr/scripts/footprint/...`
> or the `size` command on the `.elf`.

---

## 9. CMakeLists.txt Pattern

The template lives inside `zego/`, so sibling modules are at `../`:

```cmake
get_filename_component(ZEGO_BUTTON_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/../button        REALPATH)
get_filename_component(ZEGO_LED_DIR           ${CMAKE_CURRENT_SOURCE_DIR}/../led           REALPATH)
get_filename_component(ZEGO_WIFI_DIR          ${CMAKE_CURRENT_SOURCE_DIR}/../wifi          REALPATH)
get_filename_component(ZEGO_NETWORK_DIR       ${CMAKE_CURRENT_SOURCE_DIR}/../network       REALPATH)
get_filename_component(ZEGO_WIFI_BLE_PROV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../wifi_ble_prov REALPATH)
list(APPEND EXTRA_ZEPHYR_MODULES
  ${ZEGO_BUTTON_DIR} ${ZEGO_LED_DIR} ${ZEGO_WIFI_DIR}
  ${ZEGO_NETWORK_DIR} ${ZEGO_WIFI_BLE_PROV_DIR}
)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
```

When copying the template to a standalone project outside `zego/`, update the paths to point at the
actual zego directory (e.g. `../../zego/modules/button`).

---

## 10. Module Dependency Diagram

```
          ┌─────────────────────────┐
          │     NVS / Flash         │
          └──────────┬──────────────┘
                     │ read/write
          ┌──────────▼──────────────┐
          │       zego/wifi         │◄── uart: zego_wifi_mode command
          │    (mode selector)      │
          └──────────┬──────────────┘
                     │ WIFI_MODE_CHAN
          ┌──────────▼──────────────────────────────────────┐
          │                  zego/network                    │
          │  SoftAP path │ STA path │ P2P_GO │ P2P_GC       │
          └──┬───────────────────────────────────────────────┘
             │ weak hooks
  ┌──────────▼───────────────┐
  │    net_event_app.c        │   ← application customisation point
  │  (in src/modules/network) │
  └──────────┬───────────────┘
             │ app zbus channel (add your own)
  ┌──────────▼───────────────┐
  │  your application module  │
  └───────────────────────────┘

  zego/button ──BUTTON_CHAN──► zego/ux (weak-hook gestures) ──► zego/led (LED_CMD_CHAN)
                                     └─ double-click in P2P mode ──► zego/network wifi_p2p_start_pairing()
  zego/led    ◄──LED_CMD_CHAN── (add your own publisher)
  zego/wifi_ble_prov ──► (saves creds via settings → triggers STA connect)
```
