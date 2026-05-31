# Button Module Spec — `zego/button`

**Status:** Stable  
**Spec version:** 2026-05-31  
**Boards:** nrf7002dk/nrf5340/cpuapp · nrf54lm20dk/nrf54lm20a/cpuapp · native_sim

---

## Purpose

Monitors DK buttons using an SMF state machine and publishes debounced
BUTTON_PRESSED / BUTTON_RELEASED events on `BUTTON_CHAN` (zbus).  Any number
of application modules can subscribe to this channel without modifying the
button module itself.

---

## zbus Interface

| Channel      | Direction | Message type       | Description                          |
|--------------|-----------|--------------------|--------------------------------------|
| `BUTTON_CHAN` | **out**   | `struct button_msg` | Published on every press and release |

```c
struct button_msg {
    enum button_msg_type type;   /* BUTTON_PRESSED or BUTTON_RELEASED      */
    uint8_t  button_number;      /* 0-based button index                    */
    uint32_t duration_ms;        /* Hold time in ms; 0 for BUTTON_PRESSED   */
    uint32_t press_count;        /* Cumulative count for this button        */
    uint32_t timestamp;          /* k_uptime_get_32() at event time         */
};
```

---

## State Machine (per button)

```
      ┌─────────────────────────────────────────────────────┐
      │                     IDLE                            │
      │  run: if current=true && prev=false → set PRESSED   │
      └──────────────────────────┬──────────────────────────┘
                                 │ press detected
                                 ▼
      ┌─────────────────────────────────────────────────────┐
      │                    PRESSED                          │
      │  entry: pub BUTTON_PRESSED, record timestamp        │
      │  run:   if current=false && prev=true → set RELEASED│
      └──────────────────────────┬──────────────────────────┘
                                 │ release detected
                                 ▼
      ┌─────────────────────────────────────────────────────┐
      │                   RELEASED                          │
      │  entry: pub BUTTON_RELEASED (with duration_ms)      │
      │         → immediately set IDLE                      │
      └─────────────────────────────────────────────────────┘
```

---

## Long press

The module always sets `duration_ms` in every `BUTTON_RELEASED` event — long-press
detection is the **application's responsibility**.  The module enforces no threshold so
each app can configure its own.

**Recommended pattern:**

1. Define a threshold in the app's `Kconfig`:

```kconfig
config APP_BUTTON_LONG_PRESS_MS
    int "Long-press threshold (ms)"
    default 3000
    help
      Button press duration that triggers a long-press action.
```

2. Check in the `BUTTON_RELEASED` handler:

```c
if (msg->type == BUTTON_RELEASED) {
    bool long_press = (msg->duration_ms >= CONFIG_APP_BUTTON_LONG_PRESS_MS);
    if (long_press) { /* ... */ } else { /* ... */ }
}
```

> `nordic-wifi-memfault` uses this pattern with `CONFIG_APP_BUTTON_LONG_PRESS_MS=3000`:
> button 0 long-press triggers a stack overflow crash demo; button 1 long-press
> triggers division-by-zero.

---

## Configuration

| Kconfig symbol                  | Default | Description                                |
|---------------------------------|---------|--------------------------------------------|
| `CONFIG_ZEGO_BUTTON`            | n       | Enable the module                          |
| `CONFIG_ZEGO_BUTTON_NUM_BUTTONS`| 4       | Number of buttons (board conf overrides)   |
| `CONFIG_ZEGO_BUTTON_INIT_PRIORITY` | 90   | SYS_INIT APPLICATION level priority       |
| `CONFIG_ZEGO_BUTTON_LOG_LEVEL`  | info    | Log verbosity                              |

Board-specific defaults (set in `boards/<board>.conf`):

| Board                          | NUM_BUTTONS |
|--------------------------------|-------------|
| nrf7002dk/nrf5340/cpuapp       | 2           |
| nrf54lm20dk/nrf54lm20a/cpuapp  | 3           |

---

## Integration

### 1. Register the module (CMakeLists.txt)

Register as a Zephyr module **before** `find_package(Zephyr ...)`.  Kconfig is
picked up automatically via `zephyr/module.yml` — no `rsource` needed.

```cmake
get_filename_component(ZEGO_BUTTON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zego/button REALPATH)
list(APPEND EXTRA_ZEPHYR_MODULES ${ZEGO_BUTTON_DIR})
```

### 2. Enable (prj.conf / board overlay)

```
CONFIG_ZEGO_BUTTON=y
```

```
# boards/<board>.conf
CONFIG_ZEGO_BUTTON_NUM_BUTTONS=2   # override default of 4
```

### 3. Subscribe in application code

```c
#include "button.h"  /* path added by CMakeLists.txt */

static void on_button(const struct zbus_channel *chan)
{
    const struct button_msg *msg = zbus_chan_const_msg(chan);

    if (msg->type == BUTTON_RELEASED && msg->button_number == 0) {
        if (msg->duration_ms >= CONFIG_APP_BUTTON_LONG_PRESS_MS) {
            /* long press */
        } else {
            /* short press */
        }
    }
}

ZBUS_LISTENER_DEFINE(my_button_listener, on_button);
ZBUS_CHAN_ADD_OBS(BUTTON_CHAN, my_button_listener, 0);
```

---

## Migrating from app-inline button modules

Apps that have `src/modules/button/button.c` with their own `ZBUS_CHAN_DEFINE(BUTTON_CHAN, ...)`:

1. Remove `src/modules/button/` from the app.
2. Remove `rsource "src/modules/button/Kconfig.button"` from app Kconfig.
3. Remove `add_subdirectory(src/modules/button)` from app CMakeLists.txt.
4. Add the integration steps above.
5. Replace `#include "../button/button.h"` with `#include "button.h"`.
6. If button numbers were 1-based in the old module, shift all comparisons by −1 (zego is 0-based).

---

## Testing

```bash
# Automated — no hardware required
west twister -T zego/button/test -p native_sim/native/64 --inline-logs

# Manual build + run
west build -b native_sim/native/64 zego/button/test
./build/zephyr/zephyr.exe
```

Tests in `test/src/test_button.c`:

| Test                                  | Verifies                                   |
|---------------------------------------|--------------------------------------------|
| `test_press_publishes_pressed_event`  | Press → BUTTON_PRESSED, duration_ms = 0    |
| `test_release_publishes_released_event` | Release → BUTTON_RELEASED               |
| `test_press_count_increments`         | press_count increments on each press       |
| `test_press_count_consistent_across_cycle` | Same press_count in press+release    |
| `test_multiple_buttons_independent`   | Button FSMs are independent                |
| `test_inject_out_of_range_ignored`    | Out-of-range inject is silently dropped    |
