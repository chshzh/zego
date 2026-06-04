# LED Module Sample — `zego/led`

Manual hardware verification for the `zego/led` module.
Run this sample to validate all LED command types and calibrate effect timing parameters.

---

## Supported Boards

| Board | Build target | LEDs available |
|-------|-------------|----------------|
| nRF7002DK | `nrf7002dk/nrf5340/cpuapp` | LED1 (idx 0), LED2 (idx 1) |
| nRF54LM20DK | `nrf54lm20dk/nrf54lm20a/cpuapp` | LED0 (idx 0) – LED3 (idx 3) |

---

## Build

```bash
# nRF7002DK (2 LEDs: LED1, LED2)
nrfutil sdk-manager toolchain launch --ncs-version=v3.3.0 -- \
  west build -b nrf7002dk/nrf5340/cpuapp -p \
  -d zego/led/sample/build_7002dk \
  zego/led/sample

# nRF54LM20DK (4 LEDs: LED0–LED3)
nrfutil sdk-manager toolchain launch --ncs-version=v3.3.0 -- \
  west build -b nrf54lm20dk/nrf54lm20a/cpuapp -p \
  -d zego/led/sample/build_54lm20dk \
  zego/led/sample
```

## Flash

```bash
nrfutil device list   # note the serial number(s)

nrfutil sdk-manager toolchain launch --ncs-version=v3.3.0 -- \
  west flash -d zego/led/sample/build_7002dk --dev-id <SN>
```

---

## Boot Banner (Expected)

On a successful boot you should see:

```
*** Booting nRF Connect SDK vX.Y.Z ***
[00:00:00.XXX] <inf> zego_led: Initializing zego_led (N LEDs)
[00:00:00.XXX] <inf> zego_led: zego_led initialized
[00:00:00.XXX] <inf> led_sample: ==========================================
[00:00:00.XXX] <inf> led_sample:   Zego LED Sample -- Hardware Test
[00:00:00.XXX] <inf> led_sample: ==========================================
[00:00:00.XXX] <inf> led_sample:   LEDs              : N  (NUM_LEDS)
[00:00:00.XXX] <inf> led_sample:   Blink half-period : 250 ms
[00:00:00.XXX] <inf> led_sample:   Breathe period    : 3000 ms/dir  pwm=20 ms/step
[00:00:00.XXX] <inf> led_sample:   Marquee step      : 300 ms  (MARQUEE_PERIOD_MS)
[00:00:00.XXX] <inf> led_sample: ------------------------------------------
[00:00:00.XXX] <inf> led_sample:   nRF7002DK  : LED1=idx0  LED2=idx1
[00:00:00.XXX] <inf> led_sample:   nRF54LM20DK: LED0=idx0 ... LED3=idx3
[00:00:00.XXX] <inf> led_sample: ==========================================
[00:00:00.XXX] <inf> led_sample: === T1: Static ON/OFF (each LED) ===
```

Verify `N` matches the expected LED count for the board before continuing.

---

## Test Sequence

The sample loops through five steps automatically.

LED names to use per board:

| Step | nRF7002DK | nRF54LM20DK |
|------|-----------|-------------|
| T1 | LED1, LED2 | LED0, LED1, LED2, LED3 |
| T2–T4 | **LED1** (idx 0) | **LED0** (idx 0) |
| T5 | LED1 + LED2 | LED0 + LED1 + LED2 + LED3 |

---

### T1 — Static ON/OFF (all LEDs)

**Action:** Automatic — the sample lights each LED for 600 ms then turns it off, cycling through all LEDs.

**Expected log (per LED `i`):**

```
<inf> led_sample:   LED<i> ON
<dbg> led_sample:   state: LED<i> -> ON  (cmd=ON)
--- (600 ms) ---
<dbg> led_sample:   state: LED<i> -> OFF  (cmd=OFF)
```

**Pass condition:** Each LED (LED1/LED0 first) lights in sequence. No LED stays on longer than ~600 ms. All LEDs off when the step ends.

---

### T2 — TOGGLE (LED 0, 4×)

**Action:** Automatic — LED 0 (LED1 on nRF7002DK / LED0 on nRF54LM20DK) toggles four times at ~400 ms intervals.

**Expected log:**

```
<inf> led_sample: === T2: TOGGLE (LED 0, 4x) ===
<dbg> led_sample:   state: LED0 -> ON  (cmd=TOGGLE)
<dbg> led_sample:   state: LED0 -> OFF  (cmd=TOGGLE)
<dbg> led_sample:   state: LED0 -> ON  (cmd=TOGGLE)
<dbg> led_sample:   state: LED0 -> OFF  (cmd=TOGGLE)
```

**Pass condition:** LED 0 blinks four times, ending off. Other LEDs stay off throughout.

---

### T3 — BLINK (LED 0)

**Action:** Automatic — LED 0 blinks for 4 seconds using the configured `BLINK_PERIOD_MS` half-period.

**Expected log:**

```
<inf> led_sample: === T3: BLINK -- LED 0, half-period=250 ms ===
<inf> zego_led: LED 0 BLINK period=250 ms
<dbg> led_sample:   state: LED0 -> OFF  (cmd=BLINK)
```

> BLINK publishes to `LED_STATE_CHAN` **once** when the effect starts (`is_on=false` — LED
> starts off before the first toggle).  The hardware LED still blinks at 2 Hz — verify visually
> or with an oscilloscope.

**Pass condition:** LED 0 blinks at a steady 2 Hz (with default 250 ms half-period, full cycle = 500 ms). All LEDs off when the step ends.

**Timing tuning:**

| `CONFIG_ZEGO_LED_BLINK_PERIOD_MS` | Full cycle | Visual effect |
|-----------------------------------|-----------|---------------|
| 100 ms (fast) | 200 ms | Rapid flicker |
| **250 ms (default)** | **500 ms** | **2 Hz blink** |
| 500 ms (slow) | 1000 ms | 1 Hz blink |

---

### T4 — BREATHE (LED 0)

**Action:** Automatic — LED 0 runs one full breathe cycle (ramp up then ramp down) using the default period.

**Expected log (default DK backend = SW PWM):**

```
<inf> led_sample: === T4: BREATHE -- LED 0, ramp=3000 ms/dir, pwm=20 ms/step ===
<inf> zego_led: LED 0 BREATHE (SW PWM) ramp=3000 ms (150 steps x 20 ms/step)
<dbg> led_sample:   state: LED0 -> OFF  (cmd=BREATHE)
<dbg> zego_led: LED 0 BREATHE direction -> DOWN (step 149/150)
```

> BREATHE publishes to `LED_STATE_CHAN` **once** when the effect starts (`is_on=false` — LED
> begins at 0% and ramps up).  Individual hardware toggles are not reported via the state channel.
> Verify the smooth fade visually.
>
> With `CONFIG_ZEGO_LED_USE_PWM=y` the log says `(HW PWM)` for LEDs that have a PWM channel
> and `(SW PWM)` for those that don't — see T6 below.

**Pass condition:** LED 0 starts dark, gradually brightens over ~3 s, holds at near-full brightness briefly, then dims back to off over ~3 s. The ON intervals visibly lengthen during ramp-up and shorten during ramp-down. All LEDs off when step ends.

> DK LEDs are GPIO-only (no PWM). The breathe effect uses software PWM (50 Hz frames by default). It looks like a smooth fade, not a true analogue dimming.

**Timing tuning:**

| Symbol | Default | Effect |
|--------|---------|--------|
| `CONFIG_ZEGO_LED_BREATHE_PERIOD_MS` | 3000 | Ramp duration per direction (ms); full cycle = 2× |
| `CONFIG_ZEGO_LED_BREATHE_PWM_PERIOD_MS` | 20 | PWM frame size (ms); steps per ramp = PERIOD/PWM |

Examples:
- `BREATHE_PERIOD_MS=1000` → fast 1 s up + 1 s down breathe
- `BREATHE_PWM_PERIOD_MS=10` → 300 steps per ramp (finer but more CPU)

---

### T5 — MARQUEE (all LEDs)

**Action:** Automatic — one LED at a time chases through all LEDs for three sweeps.

**Expected log:**

```
<inf> led_sample: === T5: MARQUEE -- all LEDs, 300 ms/step ===
<inf> zego_led: Marquee started (period 300 ms)
<dbg> led_sample:   state: LED0 -> ON  (cmd=MARQUEE)
```

> MARQUEE publishes to `LED_STATE_CHAN` **once** when the effect starts.
> The per-step LED transitions are not reported via the state channel.
> Verify the chasing pattern visually — exactly one LED lit at a time.

**Pass condition:** Exactly one LED is lit at any time, cycling left-to-right. No two LEDs on simultaneously. All LEDs off when the step ends.

**Tuning:** Change `CONFIG_ZEGO_LED_MARQUEE_PERIOD_MS` in `prj.conf`. Lower = faster chase.

---

### T6 — BREATHE with Hardware PWM (optional, needs Zephyr backend)

T6 only runs when `CONFIG_ZEGO_LED_USE_PWM=y` is set.  With the default DK backend T6 is
skipped — the boot banner reports `HW PWM breathe: disabled`.

**Action:** Build with the Zephyr LED backend and per-LED PWM index settings (see below),
then reboot.  The sample automatically runs T6 after T5.

**Build configuration for HW PWM test:**

```bash
# nRF7002DK: LED1 (idx 0) has a PWM channel on pwm-leds child 0
#            LED2 (idx 1) has no PWM → SW PWM fallback
nrfutil sdk-manager toolchain launch --ncs-version=v3.3.0 -- \
  west build -b nrf7002dk/nrf5340/cpuapp -p \
  -d zego/led/sample/build_7002dk_pwm \
  zego/led/sample \
  -- -DCONFIG_ZEGO_LED_BACKEND_ZEPHYR=y \
     -DCONFIG_ZEGO_LED_USE_PWM=y \
     -DCONFIG_ZEGO_LED_0_PWM_INDEX=0 \
     -DCONFIG_ZEGO_LED_1_PWM_INDEX=-1

# nRF54LM20DK: LED1 (idx 1) has a PWM channel on pwm-leds child 0
#              LED0/2/3 have no PWM → SW PWM fallback
nrfutil sdk-manager toolchain launch --ncs-version=v3.3.0 -- \
  west build -b nrf54lm20dk/nrf54lm20a/cpuapp -p \
  -d zego/led/sample/build_54lm20dk_pwm \
  zego/led/sample \
  -- -DCONFIG_ZEGO_LED_BACKEND_ZEPHYR=y \
     -DCONFIG_ZEGO_LED_USE_PWM=y \
     -DCONFIG_ZEGO_LED_0_PWM_INDEX=-1 \
     -DCONFIG_ZEGO_LED_1_PWM_INDEX=0 \
     -DCONFIG_ZEGO_LED_2_PWM_INDEX=-1 \
     -DCONFIG_ZEGO_LED_3_PWM_INDEX=-1
```

**Expected log (nRF7002DK example):**

```
<inf> led_sample:   HW PWM breathe    : enabled  (T6 will run)
...
<inf> led_sample: === T6: BREATHE (HW PWM) -- each LED, ramp=3000 ms/dir ===
<inf> led_sample:     Module log: '(HW PWM)' = hardware PWM path
<inf> led_sample:                 '(SW PWM)' = software PWM fallback
<inf> led_sample:   LED0 BREATHE
<inf> zego_led: LED 0 BREATHE (HW PWM) ramp=3000 ms (150 steps x 20 ms/step)
<dbg> led_sample:   state: LED0 -> OFF  (cmd=BREATHE)
<dbg> zego_led: LED 0 BREATHE direction -> DOWN (step 149/150)
--- (600 ms pause) ---
<inf> led_sample:   LED1 BREATHE
<inf> zego_led: LED 1 BREATHE (SW PWM) ramp=3000 ms (150 steps x 20 ms/step)
<dbg> led_sample:   state: LED1 -> OFF  (cmd=BREATHE)
...
```

**Pass condition:**

| LED | Board | Expected path | Visual |
|-----|-------|---------------|--------|
| LED0 (LED1 silkscreen) | nRF7002DK | `(HW PWM)` | Smooth analogue fade |
| LED1 (LED2 silkscreen) | nRF7002DK | `(SW PWM)` | Rapid on/off toggling (perceived fade) |
| LED0 | nRF54LM20DK | `(SW PWM)` | Rapid toggling |
| LED1 | nRF54LM20DK | `(HW PWM)` | Smooth analogue fade |
| LED2, LED3 | nRF54LM20DK | `(SW PWM)` | Rapid toggling |

> The visual difference between HW and SW PWM is most obvious at low brightness levels:
> HW PWM produces a true dim glow while SW PWM flickers visibly.

---

## Summary Table

| Step | Effect | LED(s) | Duration | Pass condition |
|------|--------|--------|----------|----------------|
| T1 | Static ON/OFF | all | ~1 s/LED | each LED on 600 ms then off, in sequence |
| T2 | TOGGLE | LED 0 | ~2 s | 4 blinks at ~400 ms, ends off |
| T3 | BLINK | LED 0 | 4 s | steady 2 Hz at default; state log fires once (`is_on=false`, cmd=BLINK) |
| T4 | BREATHE | LED 0 | 6 s | fade up then down; state log fires once (`is_on=false`, cmd=BREATHE); module log says `(SW PWM)` |
| T5 | MARQUEE | all | 3 sweeps | one LED at a time, left-to-right; state log fires once (cmd=MARQUEE) |
| T6 | BREATHE (HW PWM) | each LED | `2×BREATHE_PERIOD_MS` per LED | Requires `CONFIG_ZEGO_LED_USE_PWM=y`; module log says `(HW PWM)` or `(SW PWM)` per LED |

---

## Quick Tune Reference

Edit `prj.conf`, rebuild with `-p`, reflash. The boot banner shows active values on every boot.

```
# Faster blink (default 250 ms):
CONFIG_ZEGO_LED_BLINK_PERIOD_MS=100

# Faster breathe (default 3000 ms/dir = 6 s full cycle):
CONFIG_ZEGO_LED_BREATHE_PERIOD_MS=1000

# Finer steps / smoother fade (default 20 ms = 150 steps per 3 s ramp):
CONFIG_ZEGO_LED_BREATHE_PWM_PERIOD_MS=10

# Faster marquee chase (default 300 ms/step):
CONFIG_ZEGO_LED_MARQUEE_PERIOD_MS=100

# Enable hardware-PWM breathe (T6) via west -D overrides — see T6 section for
# full -DCONFIG_ZEGO_LED_n_PWM_INDEX board-specific values:
# CONFIG_ZEGO_LED_BACKEND_ZEPHYR=y
# CONFIG_ZEGO_LED_USE_PWM=y
```
