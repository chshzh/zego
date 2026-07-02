# UX Module Specification

## Document Information

| Field | Value |
|---|---|
| Module | `zego/ux` |
| Version | 2026-07-02-00-00 |
| PRD Version | N/A (standalone library module) |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF54LM20DK + nRF7002EB2, nRF7002DK, nRF5340 Audio DK + nRF7002EK |
| Status | Current |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-07-02-00-00 | **Moved from `nordic-wifi-app-template/src/modules/ux/` to `zego/bricks/ux/` as a first-class zego brick.** `CONFIG_APP_UX_*` Kconfig symbols renamed to `CONFIG_ZEGO_UX_*`. The module now owns its own zbus channel `ZEGO_UX_WIFI_STATE_CHAN` (`struct zego_ux_wifi_state_msg`, `enum zego_ux_wifi_state`) instead of depending on the app-level `APP_WIFI_STATE_CHAN` / `messages.h` — apps publish to this channel from their `zego/network` weak-hook overrides (see `net_event_app.c`). The three Button 0 gesture actions (single click, double-click, long press) are now `__weak` functions (`zego_ux_on_single_click()`, `zego_ux_on_double_click()`, `zego_ux_on_long_press()`) with the previous behaviour kept as the default — apps can override any one of them with a strong definition to fully replace that gesture's action, following the same weak-hook pattern as `zego/network`'s `zego_on_net_event_*()` callbacks. No behavioural change for existing app-template consumers. |
| 2026-07-01-10-54 | Updated to PRD v2026-07-01-10-50: (1) `APP_WIFI_STATE_ERROR` (fast BLINK) is now driven by `zego_on_net_event_wifi_disconnect(false)` specifically — the STA-zero-stored-credentials case — instead of firing on every disconnect; a disconnect that will auto-retry now publishes `APP_WIFI_STATE_CONNECTING` (`zego_on_net_event_wifi_disconnect(true)`) so LED 0 ROTATEs instead. P2P_GC never reaches `ERROR` since it always retries. (2) P2P pairing BREATHE now also covers auto-started pairing (no saved GO at boot), not just the double-click-triggered case — no state-machine change, since both paths already published the same `APP_WIFI_STATE_PAIRING`. (3) Restructured §4 from a single state-keyed table into a state→effect table plus a new effect→per-board-LED-index table (with exact Kconfig symbols), replacing the prose board-difference notes, for easier scanning given how differently the nRF5340 Audio DK maps effects to physical LEDs. |
| 2026-06-04-18-00 | Initial spec — Button 0 gestures, LED 0 Wi-Fi state feedback |
| 2026-06-04-22-00 | SoftAP LED behavior: ROTATE when no clients (was slow BLINK); solid ON when client connected; ROTATE again when last client disconnects. Added `zego_on_net_event_softap_sta_disconnected()` hook. Updated APP_WIFI_STATE_CHAN table and state machine diagram. |
| 2026-06-05-09-38 | Added nRF5340 Audio DK + nRF7002EK: Button 0 = VOL-; ROTATE chases RGB1 only (3 LEDs); BLE prov double-click disabled (1 MB flash); board differences note added |
| 2026-06-09-16-03 | nRF5340 Audio DK: ROTATE extends to RGB1+RGB2 (`CONFIG_ZEGO_LED_ROTATE_NUM_LEDS=6`); connected state drives LED 4 (green channel of RGB2) ON + LEDs 3 and 5 OFF, replacing plain LED_COMMAND_ON; new Kconfig `CONFIG_APP_UX_CONNECTED_LED` and `CONFIG_APP_UX_CONNECTED_LED_GREEN_ONLY` |
| 2026-06-09-16-23 | Corrected nRF5340 Audio DK ROTATE to RGB2 only (indices 3–5); added `CONFIG_APP_UX_ROTATE_FIRST_LED` and `CONFIG_APP_UX_ROTATE_COUNT` Kconfig symbols; `ux.c` now uses `led_rotate()` + `led_connected()` helpers; long-press ack uses first rotate LED; removed `CONFIG_ZEGO_LED_ROTATE_NUM_LEDS=3` from board conf |
| 2026-06-29-21-44 | Updated to PRD v2026-06-29-21-44: Button 0 double-click is now mode-aware — BLE-prov toggle in STA/SoftAP, P2P pairing trigger in P2P_GO/P2P_GC (calls `wifi_p2p_start_pairing()` in zego/network). P2P_CLIENT→P2P_GC naming aligned. |
| 2026-06-30-15-11 | Add `CONFIG_APP_UX_PAIRING_LED_IDX`: nRF5340 Audio DK BREATHE (BLE prov + P2P pairing) targets index 5 (blue channel of RGB2) only. PRD Version bumped to 2026-06-30-15-11. |
| 2026-06-30-13-04 | Updated to PRD v2026-06-30-13-00: (1) UX gesture button is now board-configurable via `CONFIG_APP_UX_BUTTON_IDX` (default 0; **=4 / BTN5 on nRF5340 Audio DK** instead of VOL-). (2) LED 0 BREATHEs during P2P pairing as well as BLE prov — new `APP_WIFI_STATE_PAIRING` state, driven by the network brick's `zego_on_net_event_p2p_pairing(bool)` weak hook (both roles, while pairing active). Reconciled WPS PIN → **PBC** wording to match the implemented code. |

---

## 1. Overview

`zego/ux` provides out-of-box UX for a single button and a single LED using
`BUTTON_CHAN` (input) and `LED_CMD_CHAN` (output). No application code changes
are needed for basic connectivity feedback — enable with `CONFIG_ZEGO_UX=y`.

The module has no zbus channels of its own for Wi-Fi state; instead it declares
`ZEGO_UX_WIFI_STATE_CHAN` (see §2) which the application publishes to — typically
from its `zego/network` weak-hook overrides (`net_event_app.c` in
`nordic-wifi-app-template`), following the same decoupling pattern used between
`zego/network` and the rest of the app.

---

## Location

- **Path**: `zego/bricks/ux/`
- **Files**: `src/ux.c`, `src/ux.h`, `Kconfig`, `CMakeLists.txt`, `zephyr/module.yml`, `docs/`

---

## Module Type

- [x] **Application module** — SYS_INIT-registered zbus listener/publisher.
  Subscribes to `BUTTON_CHAN` and `ZEGO_UX_WIFI_STATE_CHAN`; publishes to
  `LED_CMD_CHAN`. No dedicated thread; runs in the button publisher's context
  (gestures) and the system workqueue (deferred LED commands).

---

## 2. Zbus Integration

**Subscribes to**: `BUTTON_CHAN` (zego/button), `ZEGO_UX_WIFI_STATE_CHAN` (own channel),
`BLE_PROV_CONN_CHAN` (zego/wifi_ble_prov, only if `CONFIG_ZEGO_WIFI_BLE_PROV=y`).

**Publishes to**: `LED_CMD_CHAN` (zego/led).

**Declares**: `ZEGO_UX_WIFI_STATE_CHAN` — carries `struct zego_ux_wifi_state_msg { enum
zego_ux_wifi_state state; enum zego_wifi_mode mode; }`. The application (or any module
that tracks connectivity) publishes here to drive LED 0. In `nordic-wifi-app-template`
this is done from `net_event_app.c`, which overrides `zego/network`'s weak hooks and
republishes onto `ZEGO_UX_WIFI_STATE_CHAN`.

```c
#include <ux.h>

struct zego_ux_wifi_state_msg msg = {
        .state = ZEGO_UX_WIFI_STATE_CONNECTED,
        .mode = ZEGO_WIFI_MODE_STA,
};
zbus_chan_pub(&ZEGO_UX_WIFI_STATE_CHAN, &msg, K_NO_WAIT);
```

---

## 3. Weak-hook API — gesture actions

Each Button 0 gesture action is a `__weak` function defined in `ux.c` with the
previous (default) behaviour. Override any of them with a strong definition
elsewhere in the application to fully replace that gesture's action; the LED
state machine and `CONFIG_ZEGO_UX_BUTTON_IDX` filtering are unaffected by the
override — only the action taken when that gesture fires changes.

```c
/* ux.h — override with a strong definition in the application */

/** Button 0 single-click action. Default: log the current Wi-Fi mode. */
void zego_ux_on_single_click(void);

/** Button 0 double-click action. Default: BLE-prov toggle (STA/SoftAP) or
 *  P2P pairing trigger (P2P_GO/P2P_GC), mode-aware. */
void zego_ux_on_double_click(void);

/** Button 0 long-press action. Default: cycle Wi-Fi mode, persist to NVS,
 *  reboot. */
void zego_ux_on_long_press(void);
```

Example override — replace the long-press action without touching `ux.c`:

```c
/* src/modules/ux/ux_app.c (application file, not part of the brick) */
#include <ux.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_ux_override, LOG_LEVEL_INF);

void zego_ux_on_long_press(void)
{
        /* Strong definition wins over the __weak default in zego/bricks/ux/src/ux.c */
        LOG_INF("Long press: custom action (mode cycle disabled)");
}
```

---

## 4. Kconfig Symbols

| Symbol | Default | Description |
|--------|---------|--------------|
| `CONFIG_ZEGO_UX` | `n` | Enable the UX module |
| `CONFIG_ZEGO_UX_INIT_PRIORITY` | `95` | `SYS_INIT` priority; must be > `ZEGO_LED_INIT_PRIORITY` (91) |
| `CONFIG_ZEGO_UX_BUTTON_IDX` | `0` | Index of the button that carries the UX gestures (single/double/long). Set to `4` on the nRF5340 Audio DK board conf so gestures use **BTN5** instead of VOL- (idx 0). The handler ignores `BUTTON_CHAN` events whose `button_number` != this. |
| `CONFIG_ZEGO_UX_ROTATE_FIRST_LED` | `0` | First LED index in the Wi-Fi ROTATE sweep; consecutive indices used up to `ROTATE_COUNT`. |
| `CONFIG_ZEGO_UX_ROTATE_COUNT` | `0` | Number of LEDs in the ROTATE sweep. `0` = LED module default (`ROTATE_NUM_LEDS` from idx 0). |
| `CONFIG_ZEGO_UX_CONNECTED_LED` | `0` | LED index turned ON for the CONNECTED state. Set to `4` on nRF5340 Audio DK (green channel of RGB2). |
| `CONFIG_ZEGO_UX_CONNECTED_LED_GREEN_ONLY` | `n` | When `y`, also send OFF to `CONNECTED_LED-1` and `CONNECTED_LED+1` for a pure-colour indicator. Set `y` on nRF5340 Audio DK. |
| `CONFIG_ZEGO_UX_PAIRING_LED_IDX` | `0` | LED index that BREATHEs during BLE provisioning and P2P pairing. Set to `5` on nRF5340 Audio DK (blue channel of RGB2). |
| `CONFIG_ZEGO_UX_ERROR_LED_IDX` | `0` | LED index that blinks (100 ms half-period) for `ZEGO_UX_WIFI_STATE_ERROR`. Set to `3` on nRF5340 Audio DK (red channel of RGB2). |

---

## 5. UX Gesture Button & Gesture Map

All gestures are carried by a single **UX gesture button**, selected by
`CONFIG_ZEGO_UX_BUTTON_IDX` (default `0`; `4` / BTN5 on the nRF5340 Audio DK). The handler
ignores `BUTTON_CHAN` events whose `button_number != CONFIG_ZEGO_UX_BUTTON_IDX`.

| Gesture | Threshold | Default action (weak hook) |
|---------|-----------|------------------------------|
| Single click | — | `zego_ux_on_single_click()` — print current Wi-Fi mode to UART log |
| Double-click (STA / SoftAP modes) | `ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS` | `zego_ux_on_double_click()` — toggle BLE provisioning (BREATHE ↔ last Wi-Fi state LED) + `zego_wifi_ble_prov_advertise()` — nRF54LM20DK only (`CONFIG_ZEGO_WIFI_BLE_PROV=y`) |
| Double-click (P2P_GO / P2P_GC modes) | `ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS` | `zego_ux_on_double_click()` — trigger P2P pairing: call `wifi_p2p_start_pairing()` (zego/network) |
| Long press | `ZEGO_BUTTON_LONG_PRESS_MS` (default 3000 ms) | `zego_ux_on_long_press()` — cycle mode STA → SoftAP → P2P_GO → P2P_GC → STA; save via `settings_save_one("app/zego_wifi_mode")`; `sys_reboot(SYS_REBOOT_COLD)` |

> **Double-click dispatch is mode-aware.** The default `zego_ux_on_double_click()` reads
> `zego_wifi_get_mode()` and branches: in `P2P_GO`/`P2P_GC` it calls `wifi_p2p_start_pairing()`;
> otherwise it falls through to the BLE-prov toggle (compiled in only when
> `CONFIG_ZEGO_WIFI_BLE_PROV=y`).

> **Long-press acknowledgement**: LED 0 is turned OFF for 300 ms before reboot so the user gets visual confirmation the gesture was registered.

---

## 6. LED 0 State Machine

LED 0 is driven by `ZEGO_UX_WIFI_STATE_CHAN` and by the double-click toggle in this module.

```
Boot
 │
 ▼
[ROTATE] ◄── ZEGO_UX_WIFI_STATE_CONNECTING published (at SYS_INIT, and on any disconnect
 │            that will auto-retry)
 │
 ├──► ZEGO_UX_WIFI_STATE_CONNECTED  ──► [Solid ON]
 │       │
 │       └── disconnect, will_retry=true ──► CONNECTING ──► [ROTATE]
 │
 ├──► ZEGO_UX_WIFI_STATE_SOFTAP     ──► [ROTATE]  (AP up, no clients)
 │       │
 │       └── CONNECTED ──► [Solid ON]  (client joined)
 │               │
 │               └── SOFTAP ──► [ROTATE]  (last client left)
 │
 ├──► ZEGO_UX_WIFI_STATE_ERROR      ──► [Fast BLINK 100 ms]
 │       (STA mode start or disconnect with zero stored Wi-Fi credentials;
 │        P2P_GC never reaches this state — it always retries)
 │
 └──► ZEGO_UX_WIFI_STATE_PAIRING    ──► [BREATHE]   (P2P pairing in progress)
         └── pairing ends → next state event (CONNECTED → Solid ON, else SOFTAP/CONNECTING)

Double-click (nRF54LM20DK, CONFIG_ZEGO_WIFI_BLE_PROV=y):
  ble_prov_led_active = false → true   ──► [BREATHE]
  ble_prov_led_active = true  → false  ──► restore last Wi-Fi state LED
```

---

## 7. Dependency

`CONFIG_ZEGO_UX` depends on `ZEGO_BUTTON && ZEGO_LED && ZEGO_WIFI && REBOOT && SETTINGS`.

---

## 8. Integration

Register the brick via `EXTRA_ZEPHYR_MODULES` in the app's `CMakeLists.txt` (see
`nordic-wifi-app-template/CMakeLists.txt`):

```cmake
get_filename_component(ZEGO_UX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../bricks/ux REALPATH)
list(APPEND EXTRA_ZEPHYR_MODULES ${ZEGO_UX_DIR})
```

Enable in `prj.conf`:

```kconfig
CONFIG_ZEGO_UX=y
```

Publish Wi-Fi state transitions from wherever the app tracks connectivity (typically
`net_event_app.c`, overriding `zego/network`'s weak hooks):

```c
#include <ux.h>
/* ... */
struct zego_ux_wifi_state_msg msg = { .state = ZEGO_UX_WIFI_STATE_CONNECTED, .mode = mode };
zbus_chan_pub(&ZEGO_UX_WIFI_STATE_CHAN, &msg, K_NO_WAIT);
```
