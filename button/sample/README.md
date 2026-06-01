# Zego Button Sample — Hardware Test Guide

Manual hardware verification for the `zego/button` module.
Run this sample to validate all gesture types and calibrate the timing parameters.

---

## Supported Boards

| Board | Build target | Available buttons |
|-------|-------------|-------------------|
| nRF7002DK | `nrf7002dk/nrf5340/cpuapp` | Button 1 (idx 0), Button 2 (idx 1) |
| nRF54LM20DK (standalone) | `nrf54lm20dk/nrf54lm20a/cpuapp` | BUTTON0 (idx 0)–BUTTON3 (idx 3) |
| nRF54LM20DK + nRF7002EB2 | `nrf54lm20dk/nrf54lm20a/cpuapp` + `-DSHIELD=nrf7002eb2` | BUTTON0–BUTTON2 (BUTTON3 unavailable — shield pin conflict) |

---

## Build

```bash
# nRF7002DK (2 buttons: Button 1, Button 2)
nrfutil sdk-manager toolchain launch --ncs-version=v3.3.0 -- \
  west build -b nrf7002dk/nrf5340/cpuapp -p \
  -d zego/button/sample/build_7002dk \
  zego/button/sample

# nRF54LM20DK (4 buttons: BUTTON0–BUTTON3)
nrfutil sdk-manager toolchain launch --ncs-version=v3.3.0 -- \
  west build -b nrf54lm20dk/nrf54lm20a/cpuapp -p \
  -d zego/button/sample/build_54lm20dk \
  zego/button/sample
```

## Flash

```bash
nrfutil device list   # note the serial number(s)

nrfutil sdk-manager toolchain launch --ncs-version=v3.3.0 -- \
  west flash -d zego/button/sample/build_7002dk --dev-id <SN>
```

---

## Boot Banner (Expected)

On a successful boot you should see:

```
*** Booting nRF Connect SDK vX.Y.Z ***
[00:00:00.251] <inf> zego_button: Initializing zego_button (N buttons)
[00:00:00.251] <inf> zego_button: zego_button initialized
[00:00:00.251] <inf> btn_sample: ===========================================
[00:00:00.251] <inf> btn_sample:   Zego Button Sample — Hardware Test
[00:00:00.251] <inf> btn_sample: ===========================================
[00:00:00.251] <inf> btn_sample:   Buttons monitored    : N  (NUM_BUTTONS)
[00:00:00.251] <inf> btn_sample:   Double-click window  : 300 ms  (DOUBLE_CLICK_WINDOW_MS)
[00:00:00.251] <inf> btn_sample:   Long-press threshold : 3000 ms  (LONG_PRESS_MS)
```

Verify `N` matches the expected button count for the board before proceeding.

---

## Test Sequence

Button names to use per board:

| Step | nRF7002DK | nRF54LM20DK |
|------|-----------|-------------|
| T1, T2, T3 | **Button 1** | **BUTTON0** |
| T4 | **Button 2** | **BUTTON1, BUTTON2, BUTTON3** |

---

### T1 — Single Click × 3

**Action:** Press and release **Button 1** (nRF7002DK) / **BUTTON0** (nRF54LM20DK) three
times. Hold each press for ~150 ms and wait at least 600 ms between presses.

**Expected log (one block per click):**

```
<dbg> btn_sample: [  XXXXX ms] BTN0 PRESSED   (press #N)
<dbg> btn_sample: [  XXXXX ms] BTN0 RELEASED  held=~150 ms
<inf> btn_sample: [  XXXXX ms] BTN0 SINGLE_CLICK  held=~150 ms  count=N
<inf> btn_sample:            hint: single-click wait = 300 ms — shorten with CONFIG_ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS
<inf> zego_button: Button 0 single click (~150 ms)
```

**Pass condition:** Three `SINGLE_CLICK` events, `button_number=0`, `press_count` increments by 1 each time.

**Timing tuning:**

The `SINGLE_CLICK` event is delayed by `DOUBLE_CLICK_WINDOW_MS` after the release —
the module waits that long to confirm no second press is coming.
Observed hold times from actual hardware: ~141–161 ms.

| `DOUBLE_CLICK_WINDOW_MS` | Single-click total latency | Risk for double-click detection |
|--------------------------|---------------------------|--------------------------------|
| 300 ms (default) | ~550 ms | Comfortable margin |
| **300 ms (default)** | **~450 ms** | **Still catches natural double-taps (observed gap: ~150 ms)** |
| 200 ms | ~350 ms | Tight — fast double-taps may be missed |

**Note:** The default 300 ms window was chosen based on observed inter-press gaps of ~150 ms. Increase to 400 ms if double-clicks are missed; decrease to 200 ms for a snappier single-click (risky — see table above).

---

### T2 — Double Click × 3

**Action:** Press and release **Button 1** / **BUTTON0** twice in quick succession,
three times total. Keep the gap between the two presses under 300 ms.

**Expected log (one block per double-click):**

```
<dbg> btn_sample: [  XXXXX ms] BTN0 PRESSED   (press #N)
<dbg> zego_button: pressed_entry: Button 0 press #N
<dbg> btn_sample: [  XXXXX ms] BTN0 RELEASED  held=~130 ms
<dbg> btn_sample: [  XXXXX ms] BTN0 PRESSED   (press #N+1)
<dbg> zego_button: pressed2_entry: Button 0 2nd press #N+1
<dbg> btn_sample: [  XXXXX ms] BTN0 RELEASED  held=~130 ms
<inf> btn_sample: [  XXXXX ms] BTN0 DOUBLE_CLICK  2nd-held=~130 ms  count=N+1
<inf> btn_sample:            hint: detection window = 300 ms — increase if fast double-taps are missed
<inf> zego_button: Button 0 double click
```

**Pass condition:** Three `DOUBLE_CLICK` events, `button_number=0`, `press_count`
increments by 2 per double-click (two physical presses each).

**Timing tuning:**

The window that matters is the gap between the **1st release** and the **2nd press**.
Observed values from actual hardware: ~142–167 ms.

| `DOUBLE_CLICK_WINDOW_MS` | Catches 167 ms gap? | Catches 200 ms gap? |
|--------------------------|---------------------|---------------------|
| 300 ms (default) | Yes (233 ms margin) | Yes |
| **300 ms (default)** | Yes (133 ms margin) | Yes |
| 200 ms | Yes (33 ms margin — risky) | No |

**Note:** The default 300 ms window captures natural double-taps (observed gap: ~150 ms) with a comfortable 133 ms margin. Do not go below 250 ms without re-testing.

---

### T3 — Long Press × 1

**Action:** Press and **hold** **Button 1** / **BUTTON0** for more than 3 seconds,
then release.

**Expected log:**

```
<dbg> btn_sample: [  XXXXX ms] BTN0 PRESSED   (press #N)
<dbg> zego_button: pressed_entry: Button 0 press #N
--- (3000 ms later) ---
<inf> btn_sample: [  XXXXX ms] BTN0 LONG_PRESS  threshold=3000 ms  count=N
<inf> btn_sample:            hint: fired after 3000 ms hold — adjust with CONFIG_ZEGO_BUTTON_LONG_PRESS_MS
<inf> zego_button: Button 0 long press
--- (on release) ---
<dbg> btn_sample: [  XXXXX ms] BTN0 RELEASED  held=XXXX ms
```

**Pass condition:** `LONG_PRESS` fires exactly `LONG_PRESS_MS` after the press
timestamp. A `RELEASED` event follows when you let go. **No** `SINGLE_CLICK` is
published after a long press.

**Important — near-miss behaviour:** If you release the button before the
`LONG_PRESS_MS` threshold (e.g., at 2629 ms with a 3000 ms threshold), you will
get a `SINGLE_CLICK` with `held=2629 ms` instead. This is expected: the FSM
classifies any hold < `LONG_PRESS_MS` as a click sequence, not a long press.

**Timing tuning:**

| `LONG_PRESS_MS` | Feel | Recommended use case |
|-----------------|------|----------------------|
| 1000 ms | Fast, easy to trigger | Simple confirm / cancel |
| 2000 ms | Moderate | Mode switch |
| **3000 ms (default)** | **Deliberate** | **Factory reset, pairing clear** |
| 5000 ms | Very deliberate | Critical / destructive action |

---

### T4 — Single Click on All Remaining Buttons (Independence Check)

Single-click each remaining button once to confirm that all per-button FSMs are
functional and independent. Buttons to test per board:

| Board | Buttons to test in T4 |
|-------|-----------------------|
| nRF7002DK | **Button 2** (idx 1) |
| nRF54LM20DK standalone | **BUTTON1** (idx 1), **BUTTON2** (idx 2), **BUTTON3** (idx 3) |
| nRF54LM20DK + nRF7002EB2 | **BUTTON1** (idx 1), **BUTTON2** (idx 2) |

**Action per button:** Press and release once, then wait 600 ms.

**Expected log (pattern repeats for each button, N = 1 / 2 / 3):**

```
<dbg> btn_sample: [  XXXXX ms] BTN<N> PRESSED   (press #1)
<dbg> btn_sample: [  XXXXX ms] BTN<N> RELEASED  held=~150 ms
<inf> btn_sample: [  XXXXX ms] BTN<N> SINGLE_CLICK  held=~150 ms  count=1
<inf> zego_button: Button <N> single click (~150 ms)
```

**Pass condition per button:**
- `button_number` matches the physical button index (1, 2, or 3).
- `press_count=1` — each button has its own independent counter.
- No events fire on other buttons while pressing this one.

---

## Summary Table

| Step | Action | nRF7002DK button | nRF54LM20DK button | Expected event | Key check |
|------|--------|------------------|--------------------|----------------|-----------|
| T1 × 3 | Single click, wait 600 ms | Button 1 | BUTTON0 | `SINGLE_CLICK` | `button_number=0`, count+1 each |
| T2 × 3 | Two rapid presses | Button 1 | BUTTON0 | `DOUBLE_CLICK` | `button_number=0`, count+2 each |
| T3 × 1 | Hold > 3 s, release | Button 1 | BUTTON0 | `LONG_PRESS` then `RELEASED` | No click after release |
| T4 × 1 each | Single click, wait 600 ms | **Button 2** | **BUTTON1, BUTTON2, BUTTON3**¹ | `SINGLE_CLICK` per button | `button_number`=idx, count=1 |

¹ With nRF7002EB2 shield: BUTTON1 and BUTTON2 only (BUTTON3 unavailable — shield pin conflict).

---

## Quick Tune Reference

```
# Increase if fast double-taps are missed (default is 300):
# CONFIG_ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS=400

# Faster long-press trigger:
CONFIG_ZEGO_BUTTON_LONG_PRESS_MS=1500           # default 3000
```

Edit `prj.conf`, rebuild (`-p` for pristine), reflash. The boot banner shows
the active values on every boot so you can confirm the change took effect.
