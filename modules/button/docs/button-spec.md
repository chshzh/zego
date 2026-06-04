# Button Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | `zego/button` |
| Version | 2026-06-04-00-00 |
| PRD Version | N/A (standalone library module) |
| Status | Stable |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-05-31-00-00 | Initial module spec (3-state FSM: IDLE/PRESSED/RELEASED) |
| 2026-06-01-08-54 | 5-state FSM: added BUTTON_SINGLE_CLICK, BUTTON_DOUBLE_CLICK, BUTTON_LONG_PRESS gesture classification; BUTTON_PRESSED / BUTTON_RELEASED raw events retained; long-press default 3000 ms |
| 2026-06-03-00-00 | Added HAL backend section (DK / GPIO-keys); added BACKEND Kconfig symbols to table; fixed nRF54LM20DK NUM_BUTTONS default (4, not 3); removed stale duplicate spec block |
| 2026-06-04-00-00 | Expanded state machine section: clarified execution context (all SMF runs on system workqueue); added Entry/Exit/Timer action tables; added timing diagram |

---

## Overview

The `zego/button` module monitors hardware buttons using a per-button Zephyr SMF
state machine.  It publishes two layers of events on `BUTTON_CHAN` (zbus):

- **Raw events** (`BUTTON_PRESSED`, `BUTTON_RELEASED`) — fired immediately on every
  physical press and release, identical to the previous behavior.
- **Gesture events** (`BUTTON_SINGLE_CLICK`, `BUTTON_DOUBLE_CLICK`, `BUTTON_LONG_PRESS`)
  — fired after the FSM classifies the press sequence using configurable timers.

Subscribers may listen to either layer, or both.  The module does **not** drive LEDs,
manage connectivity state, or contain any application-specific logic.

A **hardware abstraction layer** (`button_hw.h`) decouples the FSM from the physical
driver.  Choose a backend via Kconfig:

| Backend | Kconfig symbol | Description |
|---------|----------------|-------------|
| DK library (default) | `CONFIG_ZEGO_BUTTON_BACKEND_DK=y` | Uses `dk_buttons_and_leds`; works out-of-the-box on nRF7002DK and nRF54LM20DK |
| Zephyr Input (portable) | `CONFIG_ZEGO_BUTTON_BACKEND_GPIO=y` | Uses the Zephyr Input subsystem with `gpio-keys`; portable to any board with a `gpio-keys` DTS node; requires consecutive `linux,code` values starting from 0 |

---

## Location

- **Path**: `zego/button/`
- **Files**: `src/button.c`, `src/button.h`, `src/button_hw.h` (HAL interface),
  `src/button_hw_dk.c` (DK backend), `src/button_hw_gpio.c` (GPIO/Input backend),
  `Kconfig`, `CMakeLists.txt`, `zephyr/module.yml`, `sample/`, `docs/`

---

## Module Type

- [x] **Application module** — SMF per-button state machine, driven by the selected
  hardware backend callback on the system work queue; publishes to `BUTTON_CHAN` (zbus).
  Hardware input is abstracted via `button_hw.h`; the DK and GPIO/Input backends share
  the same FSM logic in `button.c`.

---

## Supported Hardware

| Board | Build target | Buttons available | Notes |
|-------|-------------|-------------------|-------|
| nRF7002DK | `nrf7002dk/nrf5340/cpuapp` | BUTTON1 (idx 0), BUTTON2 (idx 1) | 2 buttons |
| nRF54LM20DK | `nrf54lm20dk/nrf54lm20a/cpuapp` | BUTTON0–BUTTON3 (idx 0–3) | 4 buttons; when built with `-DSHIELD=nrf7002eb2` BUTTON3 pin is occupied by the shield — the application overrides `CONFIG_ZEGO_BUTTON_NUM_BUTTONS=3` in its own board conf |

---

## Zbus Integration

**Publishes to**: `BUTTON_CHAN`

```c
enum button_msg_type {
    BUTTON_PRESSED,      /* Raw: fired immediately on press.    duration_ms = 0        */
    BUTTON_RELEASED,     /* Raw: fired immediately on release.  duration_ms = hold time */
    BUTTON_SINGLE_CLICK, /* Gesture: confirmed after double-click window expires        */
    BUTTON_DOUBLE_CLICK, /* Gesture: two presses within DOUBLE_CLICK_WINDOW_MS          */
    BUTTON_LONG_PRESS,   /* Gesture: held >= LONG_PRESS_MS; fires while still held      */
};

struct button_msg {
    enum button_msg_type type;
    uint8_t  button_number; /* 0-based button index                              */
    uint32_t duration_ms;   /* Hold time in ms; semantics depend on type (below) */
    uint32_t press_count;   /* Cumulative physical-press count for this button   */
    uint32_t timestamp;     /* k_uptime_get_32() at publication time             */
};
```

**`duration_ms` semantics:**

| Event type           | `duration_ms` value                                  |
|----------------------|------------------------------------------------------|
| `BUTTON_PRESSED`     | Always 0                                             |
| `BUTTON_RELEASED`    | Hold time: `release_ms − press_ms`                   |
| `BUTTON_SINGLE_CLICK`| Hold time of the press (same as the preceding BUTTON_RELEASED) |
| `BUTTON_DOUBLE_CLICK`| Hold time of the 2nd press                          |
| `BUTTON_LONG_PRESS`  | `CONFIG_ZEGO_BUTTON_LONG_PRESS_MS`                   |

**Subscribes to**: nothing — driven entirely by `dk_buttons_init` hardware callback.

---

## State Machine

Each button has an independent FSM.  Two `k_work_delayable` timers deliver timer
events by setting a flag and calling `smf_run_state()` on the system work queue
(the same queue as the DK callback) — no mutex is needed.

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> PRESSED : press\n→ pub BUTTON_PRESSED

    PRESSED --> CLICK_WAIT  : release\n→ pub BUTTON_RELEASED
    PRESSED --> LONG_PRESS  : long_press_work fires

    CLICK_WAIT --> PRESSED2 : 2nd press within window\n→ pub BUTTON_PRESSED
    CLICK_WAIT --> IDLE     : double_click_work fires\n→ pub BUTTON_SINGLE_CLICK

    PRESSED2 --> IDLE : release\n→ pub BUTTON_RELEASED\n→ pub BUTTON_DOUBLE_CLICK

    LONG_PRESS --> IDLE : release\n→ pub BUTTON_RELEASED

    note right of PRESSED
        entry: press_count++, record press_timestamp
               schedule long_press_work
        exit:  cancel long_press_work
               record release_timestamp
    end note

    note right of CLICK_WAIT
        entry: schedule double_click_work
    end note

    note right of LONG_PRESS
        entry: pub BUTTON_LONG_PRESS
    end note
```

**State descriptions:**

| State | Description | Entry action | Exit action |
|-------|-------------|--------------|-------------|
| `IDLE` | Waiting for a press | — | — |
| `PRESSED` | Button held; awaiting release or long-press timer | `press_count++`, record `press_timestamp`, schedule `long_press_work`, pub `BUTTON_PRESSED` | Cancel `long_press_work`, record `release_timestamp` |
| `CLICK_WAIT` | First release detected; waiting for 2nd press or timeout | Schedule `double_click_work` | — |
| `PRESSED2` | Second press detected within window | `press_count++`, record `press_timestamp`, pub `BUTTON_PRESSED` | — |
| `LONG_PRESS` | Button held past long-press threshold | Pub `BUTTON_LONG_PRESS` | — |

**Timer summary:**

| Timer | Scheduled in | Fires after | Effect |
|-------|-------------|-------------|--------|
| `long_press_work` | `PRESSED` entry | `CONFIG_ZEGO_BUTTON_LONG_PRESS_MS` | Sets `long_press_fired`; runs SMF |
| `double_click_work` | `CLICK_WAIT` entry | `CONFIG_ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS` | Sets `click_timeout`; runs SMF |

### Execution Context

All SMF execution happens on the **system workqueue**.  No additional locking is needed.

```mermaid
sequenceDiagram
    participant HW as GPIO / DK HW
    participant CB as button_handler callback (sysworkq)
    participant SM as Per-button SMF (sysworkq)
    participant WQ as long_press_work / double_click_work (sysworkq)
    participant ZB as BUTTON_CHAN (zbus)

    HW->>CB: dk_buttons callback fires (system workqueue)
    CB->>SM: smf_run_state() — updates current_state, may transition
    SM->>ZB: zbus_chan_pub(BUTTON_PRESSED / RELEASED)

    Note over CB,WQ: timer work also on system workqueue
    WQ->>SM: set long_press_fired / click_timeout, smf_run_state()
    SM->>ZB: zbus_chan_pub(SINGLE_CLICK / DOUBLE_CLICK / LONG_PRESS)
```

> **Why this matters:** Because both the DK callback and the timer work items run on the same system workqueue, they are automatically serialised — no mutex or spinlock is needed to protect the `button_sm_object` fields.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_ZEGO_BUTTON` | bool | `n` | Enable the module |
| `CONFIG_ZEGO_BUTTON_BACKEND_DK` | bool | `y` | Hardware backend: `dk_buttons_and_leds` (default) |
| `CONFIG_ZEGO_BUTTON_BACKEND_GPIO` | bool | `n` | Hardware backend: Zephyr Input subsystem / `gpio-keys` (portable) |
| `CONFIG_ZEGO_BUTTON_NUM_BUTTONS` | int | `4` | Number of buttons; board overlays override |
| `CONFIG_ZEGO_BUTTON_LONG_PRESS_MS` | int | `3000` | Hold time (ms) that triggers `BUTTON_LONG_PRESS` |
| `CONFIG_ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS` | int | `400` | Max gap (ms) between two presses for double-click |
| `CONFIG_ZEGO_BUTTON_INIT_PRIORITY` | int | `90` | `SYS_INIT` APPLICATION level priority |
| `CONFIG_ZEGO_BUTTON_LOG_LEVEL` | choice | `info` | Log verbosity |

Board-specific defaults (`boards/<board>.conf`):

| Board | `NUM_BUTTONS` |
|-------|--------------|
| `nrf7002dk/nrf5340/cpuapp` | 2 |
| `nrf54lm20dk/nrf54lm20a/cpuapp` | 4 (3 when `-DSHIELD=nrf7002eb2` — app overrides in its own board conf) |

---

## API / Public Interface

```c
/* Declared in src/button.h; available to subscribers */

/* Channel declaration — subscribe with ZBUS_CHAN_ADD_OBS */
ZBUS_CHAN_DECLARE(BUTTON_CHAN);

/* Test builds only (CONFIG_ZTEST) */
void zego_button_inject(uint8_t btn_num, bool pressed);
void zego_button_inject_long_press_timer(uint8_t btn_num);
void zego_button_inject_double_click_timer(uint8_t btn_num);
```

**Integration pattern:**

```c
#include "button.h"

static void on_button(const struct zbus_channel *chan)
{
    const struct button_msg *msg = zbus_chan_const_msg(chan);

    switch (msg->type) {
    case BUTTON_SINGLE_CLICK:
        /* short action */
        break;
    case BUTTON_DOUBLE_CLICK:
        /* double-click action */
        break;
    case BUTTON_LONG_PRESS:
        /* long-press action (fires while button still held) */
        break;
    default:
        break; /* ignore raw BUTTON_PRESSED / BUTTON_RELEASED if not needed */
    }
}

ZBUS_LISTENER_DEFINE(my_listener, on_button);
ZBUS_CHAN_ADD_OBS(BUTTON_CHAN, my_listener, 0);
```

Register the module in `CMakeLists.txt` before `find_package(Zephyr ...)`:

```cmake
get_filename_component(ZEGO_BUTTON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zego/button REALPATH)
list(APPEND EXTRA_ZEPHYR_MODULES ${ZEGO_BUTTON_DIR})
```

Enable in `prj.conf`:

```
CONFIG_ZEGO_BUTTON=y
```

---

## Error Handling

| Error Condition | Detection | Response |
|----------------|-----------|----------|
| `dk_buttons_init` fails | Non-zero return in `button_module_init` | `LOG_ERR`, return error code (boot continues) |
| `zbus_chan_pub` fails | Non-zero return in `publish_event` | `LOG_ERR`, event dropped |
| SMF `smf_run_state` fails | Non-zero return in `button_handler` or timer callbacks | `LOG_ERR`, FSM left in current state |
| Out-of-range button inject | `btn_num >= NUM_BUTTONS` in test shim | `LOG_WRN`, silently ignored |

---

## Memory Estimate

| Resource | Value | Notes |
|----------|-------|-------|
| Flash | ~2 KB | Code + read-only state table |
| RAM (static) | ~`NUM_BUTTONS × 80` bytes | Per-button `button_sm_object` structs |
| Stack | None | Runs on system work queue (no dedicated thread) |

---

## Test Points

| Scenario | UART log expected | Pass condition |
|----------|-------------------|----------------|
| Module init | `[zego_button] Initializing zego_button (N buttons)` | Always on boot |
| Module init complete | `[zego_button] zego_button initialized` | Always on boot |
| Press detected | `[zego_button] Button N press #M` | Each physical press |
| Single click | `[zego_button] Button N single click (X ms)` | After double-click window expires |
| Double click | `[zego_button] Button N double click` | On 2nd release within window |
| Long press | `[zego_button] Button N long press` | After `LONG_PRESS_MS` while held |
| zbus publish error | `[zego_button] Failed to publish btn event ...` | On zbus timeout (should not occur) |

---

## Testing

```bash
# Automated — no hardware required
west twister -T zego/button/test -p native_sim/native/64 --inline-logs

# Manual
west build -b native_sim/native/64 zego/button/test
./build/zephyr/zephyr.exe
```

Tests in `test/src/test_button.c`:

| Test | Verifies |
|------|----------|
| `test_single_click` | press+release+timer → `BUTTON_SINGLE_CLICK` |
| `test_double_click` | 2× press+release → `BUTTON_DOUBLE_CLICK` |
| `test_long_press` | press+long-press timer → `BUTTON_LONG_PRESS` while held; release does not publish a click |
| `test_press_count_increments` | `press_count` increments once per single-click cycle |
| `test_press_count_double_click` | `press_count` increments twice (two physical presses) |
| `test_multiple_buttons_independent` | Per-button FSMs are independent |
| `test_inject_out_of_range_ignored` | Out-of-range inject is silently dropped |

---

## Migrating from BUTTON_PRESSED / BUTTON_RELEASED only

Apps that previously checked `duration_ms` on `BUTTON_RELEASED` to detect long press:

1. Raw events are still published — existing listeners that check `BUTTON_PRESSED` / `BUTTON_RELEASED` continue to work unchanged.
2. To use gesture events instead, replace the `duration_ms` threshold check with a direct check on `msg->type == BUTTON_LONG_PRESS` / `BUTTON_SINGLE_CLICK`.
3. Remove any app-level `CONFIG_APP_BUTTON_LONG_PRESS_MS` Kconfig symbol — the threshold is now `CONFIG_ZEGO_BUTTON_LONG_PRESS_MS` in the module.
4. `BUTTON_LONG_PRESS` fires **while the button is still held** — a `BUTTON_RELEASED` follows when the user releases.

---

## Open Issues / TBD

- [ ] Double-click is not classified for the 2nd press of a long-press sequence (by design; document if this becomes a user question).

---

*(Changelog is maintained at the top of this document.)*
