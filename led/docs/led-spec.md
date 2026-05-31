# LED Module Spec — `zego/led`

**Status:** Stable  
**Spec version:** 2026-05-31  
**Boards:** nrf7002dk/nrf5340/cpuapp · nrf54lm20dk/nrf54lm20a/cpuapp · native_sim

---

## Purpose

Controls DK LEDs via zbus commands.  Any application module publishes a command
to `LED_CMD_CHAN`; the LED module subscribes, runs a per-LED SMF, and drives the
hardware.  State changes are reported on `LED_STATE_CHAN`.

The LED module is a **pure input module** from the application's perspective:
it consumes commands, produces state notifications.  Policy (which events light
which LED) lives in the application, not in this module.

---

## zbus Interface

| Channel         | Direction | Message type          | Description                  |
|-----------------|-----------|----------------------|------------------------------|
| `LED_CMD_CHAN`  | **in**    | `struct led_msg`      | Publish here to command an LED |
| `LED_STATE_CHAN`| **out**   | `struct led_state_msg`| Published after every state change |

```c
/* Command (publish on LED_CMD_CHAN) */
struct led_msg {
    enum led_msg_type type;    /* see below              */
    uint8_t led_number;        /* 0-based; ignored for MARQUEE */
    uint16_t period_ms;        /* effect period; 0 = use Kconfig default */
};

/* Notification (subscribe to LED_STATE_CHAN) */
struct led_state_msg {
    uint8_t led_number;
    bool    is_on;
};
```

### Command types

| Command                | Effect                                                              |
|------------------------|---------------------------------------------------------------------|
| `LED_COMMAND_ON`       | Turn LED on (static)                                                |
| `LED_COMMAND_OFF`      | Turn LED off (static)                                               |
| `LED_COMMAND_TOGGLE`   | Toggle between on/off (static)                                      |
| `LED_COMMAND_BLINK`    | Blink: equal on/off half-cycles at `period_ms`                      |
| `LED_COMMAND_BREATHE`  | Breathing pulse: on for `BREATHE_ON_PCT`% of `period_ms`, off for the rest |
| `LED_COMMAND_MARQUEE`  | Cycle all LEDs in sequence, one lit at a time, at `period_ms` per step |

---

## State Machine (per LED)

Static commands (`ON`/`OFF`/`TOGGLE`) use an SMF:

```
      LED_OFF  ↔  LED_ON
```

Dynamic effects (`BLINK` / `BREATHE`) bypass the SMF and use a
`k_work_delayable` per LED.  `MARQUEE` uses a single global
`k_work_delayable` that owns all LEDs.  Any static command cancels an
active effect and resumes SMF control.

### Blink

The LED toggles every `period_ms` milliseconds.  Duty cycle is 50%.

```c
struct led_msg cmd = {
    .type       = LED_COMMAND_BLINK,
    .led_number = 0,
    .period_ms  = 500,  /* 0 = CONFIG_ZEGO_LED_BLINK_PERIOD_MS */
};
zbus_chan_pub(&LED_CMD_CHAN, &cmd, K_NO_WAIT);
```

### Breathe

A simulated breathing effect using an asymmetric duty cycle
(`CONFIG_ZEGO_LED_BREATHE_ON_PCT`% on, rest off).  DK board LEDs are
GPIO-only (no PWM), so this produces a "slow pulse" rather than a
smooth fade.  Default: 70% on, 30% off with a 3 s cycle — a sustained
glow with a brief dark phase.

```c
struct led_msg cmd = {
    .type       = LED_COMMAND_BREATHE,
    .led_number = 1,
    .period_ms  = 3000,  /* 0 = CONFIG_ZEGO_LED_BREATHE_PERIOD_MS */
};
zbus_chan_pub(&LED_CMD_CHAN, &cmd, K_NO_WAIT);
```

To cancel any effect and return to static off:

```c
struct led_msg stop = { .type = LED_COMMAND_OFF, .led_number = 1 };
zbus_chan_pub(&LED_CMD_CHAN, &stop, K_NO_WAIT);
```

### Marquee

Cycles through all LEDs one at a time with one LED lit per step.  Takes
over all LEDs; any subsequent per-LED command (ON/OFF/BLINK/BREATHE)
automatically cancels the marquee.

```c
struct led_msg cmd = {
    .type      = LED_COMMAND_MARQUEE,
    .period_ms = 300,  /* 0 = CONFIG_ZEGO_LED_MARQUEE_PERIOD_MS */
};
zbus_chan_pub(&LED_CMD_CHAN, &cmd, K_NO_WAIT);

/* Stop marquee — send any per-LED command */
struct led_msg stop = { .type = LED_COMMAND_OFF, .led_number = 0 };
zbus_chan_pub(&LED_CMD_CHAN, &stop, K_NO_WAIT);
```

---

## Configuration

| Kconfig symbol                     | Default | Description                              |
|------------------------------------|---------|------------------------------------------|
| `CONFIG_ZEGO_LED`                  | n       | Enable the module                        |
| `CONFIG_ZEGO_LED_NUM_LEDS`         | 4       | Number of LEDs (board conf overrides)    |
| `CONFIG_ZEGO_LED_INIT_PRIORITY`    | 91      | SYS_INIT APPLICATION level priority     |
| `CONFIG_ZEGO_LED_LOG_LEVEL`        | info    | Log verbosity                            |
| `CONFIG_ZEGO_LED_BLINK_PERIOD_MS`  | 250     | Blink half-period (ms); full cycle = 2x  |
| `CONFIG_ZEGO_LED_BREATHE_PERIOD_MS`| 3000    | Breathe full on+off cycle (ms)           |
| `CONFIG_ZEGO_LED_BREATHE_ON_PCT`   | 70      | On-time fraction for breathe effect (%)  |
| `CONFIG_ZEGO_LED_MARQUEE_PERIOD_MS`| 300     | Marquee step duration per LED (ms)       |

Board-specific defaults (set in `boards/<board>.conf`):

| Board                          | NUM_LEDS |
|--------------------------------|----------|
| nrf7002dk/nrf5340/cpuapp       | 2        |
| nrf54lm20dk/nrf54lm20a/cpuapp  | 4        |

---

## Integration

### 1. Register the module (CMakeLists.txt)

Register as a Zephyr module **before** `find_package(Zephyr ...)`.  Kconfig is
picked up automatically via `zephyr/module.yml` — no `rsource` needed.

```cmake
get_filename_component(ZEGO_LED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zego/led REALPATH)
list(APPEND EXTRA_ZEPHYR_MODULES ${ZEGO_LED_DIR})
```

### 2. Enable (prj.conf / board overlay)

```
CONFIG_ZEGO_LED=y
```

```
# boards/<board>.conf
CONFIG_ZEGO_LED_NUM_LEDS=2
```

### 3. Command LEDs from application code

```c
#include "led.h"  /* path added by CMakeLists.txt */

/* Static */
struct led_msg on  = { .type = LED_COMMAND_ON,  .led_number = 0 };
struct led_msg off = { .type = LED_COMMAND_OFF, .led_number = 0 };
zbus_chan_pub(&LED_CMD_CHAN, &on,  K_NO_WAIT);
zbus_chan_pub(&LED_CMD_CHAN, &off, K_NO_WAIT);

/* Blink LED 0 at 1 Hz (500 ms on, 500 ms off) */
struct led_msg blink = { .type = LED_COMMAND_BLINK, .led_number = 0, .period_ms = 500 };
zbus_chan_pub(&LED_CMD_CHAN, &blink, K_NO_WAIT);

/* Breathe LED 1 with default period */
struct led_msg breathe = { .type = LED_COMMAND_BREATHE, .led_number = 1 };
zbus_chan_pub(&LED_CMD_CHAN, &breathe, K_NO_WAIT);

/* Marquee all LEDs at 200 ms per step */
struct led_msg marquee = { .type = LED_COMMAND_MARQUEE, .period_ms = 200 };
zbus_chan_pub(&LED_CMD_CHAN, &marquee, K_NO_WAIT);

/* Read current state */
bool state;
led_get_state(0, &state);
```

---

## Migrating from app-inline LED modules

Apps that have `src/modules/led/led.c` with their own `ZBUS_CHAN_DEFINE(LED_CMD_CHAN, ...)`:

1. Remove `src/modules/led/` from the app.
2. Remove `rsource "src/modules/led/Kconfig.led"` from app Kconfig.
3. Remove `add_subdirectory(src/modules/led)` from app CMakeLists.txt.
4. Add the integration steps above.
5. Replace `#include "../led/led.h"` with `#include "led.h"`.
6. The new `struct led_msg` has an extra `period_ms` field (default 0 = OK for existing
   ON/OFF/TOGGLE code using named-field initializers).

> `led_get_all_states_json()` is not in this module.  If needed, add it as a
> thin app-level helper that calls `led_get_state()` in a loop.

---

## Testing

```bash
# Automated — no hardware required
west twister -T zego/led/test -p native_sim/native/64 --inline-logs

# Manual build + run
west build -b native_sim/native/64 zego/led/test
./build/zephyr/zephyr.exe
```

Tests in `test/src/test_led.c`:

| Test                            | Verifies                                            |
|---------------------------------|-----------------------------------------------------|
| `test_led_on`                   | ON command → state=true, LED_STATE_CHAN published   |
| `test_led_off`                  | OFF command → state=false                          |
| `test_led_toggle_off_to_on`     | TOGGLE from OFF → ON                               |
| `test_led_toggle_on_to_off`     | TOGGLE from ON → OFF                               |
| `test_led_on_idempotent`        | Two ON commands → LED remains ON                   |
| `test_multiple_leds_independent`| LED 0 and LED 1 are independent                    |
| `test_get_state_invalid_led`    | Out-of-range index → -EINVAL                       |
| `test_get_state_null_ptr`       | NULL pointer → -EINVAL                             |
