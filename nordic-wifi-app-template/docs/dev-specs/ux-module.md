# UX Module Engineering Spec

## Document Information

| Field | Value |
|---|---|
| Module | app_ux |
| Version | 2026-07-01-10-54 |
| PRD Version | 2026-07-01-10-50 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF54LM20DK + nRF7002EB2, nRF7002DK, nRF5340 Audio DK + nRF7002EK |
| Status | Current |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-07-01-10-54 | Updated to PRD v2026-07-01-10-50: (1) `APP_WIFI_STATE_ERROR` (fast BLINK) is now driven by `zego_on_net_event_wifi_disconnect(false)` specifically — the STA-zero-stored-credentials case — instead of firing on every disconnect; a disconnect that will auto-retry now publishes `APP_WIFI_STATE_CONNECTING` (`zego_on_net_event_wifi_disconnect(true)`) so LED 0 ROTATEs instead. P2P_GC never reaches `ERROR` since it always retries. (2) P2P pairing BREATHE now also covers auto-started pairing (no saved GO at boot), not just the double-click-triggered case — no state-machine change, since both paths already published the same `APP_WIFI_STATE_PAIRING`. (3) Restructured §4 from a single state-keyed table into a state→effect table plus a new effect→per-board-LED-index table (with exact Kconfig symbols), replacing the prose board-difference notes, for easier scanning given how differently the nRF5340 Audio DK maps effects to physical LEDs. |
| 2026-06-04-18-00 | Initial spec — Button 0 gestures, LED 0 Wi-Fi state feedback |
| 2026-06-04-22-00 | SoftAP LED behavior: ROTATE when no clients (was slow BLINK); solid ON when client connected; ROTATE again when last client disconnects. Added `zego_on_net_event_softap_sta_disconnected()` hook. Updated APP_WIFI_STATE_CHAN table and state machine diagram. |
| 2026-06-05-09-38 | Added nRF5340 Audio DK + nRF7002EK: Button 0 = VOL-; ROTATE chases RGB1 only (3 LEDs); BLE prov double-click disabled (1 MB flash); board differences note added |
| 2026-06-09-16-03 | nRF5340 Audio DK: ROTATE extends to RGB1+RGB2 (`CONFIG_ZEGO_LED_ROTATE_NUM_LEDS=6`); connected state drives LED 4 (green channel of RGB2) ON + LEDs 3 and 5 OFF, replacing plain LED_COMMAND_ON; new Kconfig `CONFIG_APP_UX_CONNECTED_LED` and `CONFIG_APP_UX_CONNECTED_LED_GREEN_ONLY` |
| 2026-06-09-16-23 | Corrected nRF5340 Audio DK ROTATE to RGB2 only (indices 3–5); added `CONFIG_APP_UX_ROTATE_FIRST_LED` and `CONFIG_APP_UX_ROTATE_COUNT` Kconfig symbols; `ux.c` now uses `led_rotate()` + `led_connected()` helpers; long-press ack uses first rotate LED; removed `CONFIG_ZEGO_LED_ROTATE_NUM_LEDS=3` from board conf |
| 2026-06-09-17-25 | PRD Version sync to v2026-06-09-17-25 (FR-107 P2P_CLIENT added to PRD; no UX spec change required — P2P_CLIENT uses same ROTATE→solid-ON LED transitions as STA mode) |
| 2026-06-29-23-06 | Mode-cycle gesture text corrected to STA → SoftAP → P2P_GO → P2P_GC → STA (P2P_GC was missing); validation-found doc fix. |
| 2026-06-29-21-44 | Updated to PRD v2026-06-29-21-44: Button 0 double-click is now mode-aware — BLE-prov toggle in STA/SoftAP, P2P pairing trigger in P2P_GO/P2P_GC (calls `wifi_p2p_start_pairing()` in zego/network). P2P_CLIENT→P2P_GC naming aligned. |
| 2026-06-30-15-11 | Add `CONFIG_APP_UX_PAIRING_LED_IDX`: nRF5340 Audio DK BREATHE (BLE prov + P2P pairing) targets index 5 (blue channel of RGB2) only. PRD Version bumped to 2026-06-30-15-11. |
| 2026-06-30-13-04 | Updated to PRD v2026-06-30-13-00: (1) UX gesture button is now board-configurable via `CONFIG_APP_UX_BUTTON_IDX` (default 0; **=4 / BTN5 on nRF5340 Audio DK** instead of VOL-). (2) LED 0 BREATHEs during P2P pairing as well as BLE prov — new `APP_WIFI_STATE_PAIRING` state, driven by the network brick's `zego_on_net_event_p2p_pairing(bool)` weak hook (both roles, while pairing active). Reconciled WPS PIN → **PBC** wording to match the implemented code. |

---

## 1. Purpose

The UX module (`src/modules/ux/ux.c`) provides out-of-box hardware UX for the app template using `BUTTON_CHAN` (input) and `LED_CMD_CHAN` (output). No application code changes are needed for basic connectivity feedback.

Enable with `CONFIG_APP_UX_MODULE=y`.

---

## 2. Kconfig Symbols

| Symbol | Default | Description |
|--------|---------|-------------|
| `CONFIG_APP_UX_MODULE` | `n` | Enable the UX module |
| `CONFIG_APP_UX_INIT_PRIORITY` | `95` | `SYS_INIT` priority; must be > `ZEGO_LED_INIT_PRIORITY` (91) |
| `CONFIG_APP_UX_BUTTON_IDX` | `0` | Index of the button that carries the UX gestures (single/double/long). Set to `4` on the nRF5340 Audio DK board conf so gestures use **BTN5** instead of VOL- (idx 0). The handler ignores `BUTTON_CHAN` events whose `button_number` != this. |
| `CONFIG_APP_UX_ROTATE_FIRST_LED` | `0` | First LED index in the Wi-Fi ROTATE sweep; consecutive indices used up to `ROTATE_COUNT`. |
| `CONFIG_APP_UX_ROTATE_COUNT` | `0` | Number of LEDs in the ROTATE sweep. `0` = LED module default (`ROTATE_NUM_LEDS` from idx 0). |
| `CONFIG_APP_UX_CONNECTED_LED` | `0` | LED index turned ON for the CONNECTED state. Set to `4` on nRF5340 Audio DK (green channel of RGB2). |
| `CONFIG_APP_UX_CONNECTED_LED_GREEN_ONLY` | `n` | When `y`, also send OFF to `CONNECTED_LED-1` and `CONNECTED_LED+1` for a pure-colour indicator. Set `y` on nRF5340 Audio DK. |
| `CONFIG_APP_UX_PAIRING_LED_IDX` | `0` | LED index that BREATHEs during BLE provisioning and P2P pairing. Set to `5` on nRF5340 Audio DK (blue channel of RGB2). `0` = same LED as `ROTATE_FIRST_LED`. |

---

## 3. UX Gesture Button & Gesture Map

All gestures are carried by a single **UX gesture button**, selected by
`CONFIG_APP_UX_BUTTON_IDX` (default `0`; `4` / BTN5 on the nRF5340 Audio DK). The handler
ignores `BUTTON_CHAN` events whose `button_number != CONFIG_APP_UX_BUTTON_IDX`.

| Gesture | Threshold | Action |
|---------|-----------|--------|
| Single click | — | Print current Wi-Fi mode to UART log |
| Double-click (STA / SoftAP modes) | `ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS` | Toggle BLE provisioning (BREATHE ↔ last Wi-Fi state LED) + `zego_wifi_ble_prov_advertise()` — nRF54LM20DK only (`CONFIG_ZEGO_WIFI_BLE_PROV=y`) |
| Double-click (P2P_GO / P2P_GC modes) | `ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS` | Trigger P2P pairing: call `wifi_p2p_start_pairing()` (zego/network) — GO refreshes its WPS PBC window, GC discovers + joins the pairing GO and saves its MAC. On P2P_GC this is optional — the same pairing sequence already auto-starts at boot when no GO is saved (see network-spec.md); the double-click lets the user (re-)pair with a different GO at any time. All boards with P2P enabled |
| Long press | `ZEGO_BUTTON_LONG_PRESS_MS` (default 3000 ms) | Cycle mode STA → SoftAP → P2P_GO → P2P_GC → STA; save via `settings_save_one("app/zego_wifi_mode")`; `sys_reboot(SYS_REBOOT_COLD)` |

> **Double-click dispatch is mode-aware.** The `BUTTON_DOUBLE_CLICK` handler reads
> `zego_wifi_get_mode()` and branches: in `P2P_GO`/`P2P_GC` it calls `wifi_p2p_start_pairing()`;
> otherwise it falls through to the BLE-prov toggle (compiled in only when
> `CONFIG_ZEGO_WIFI_BLE_PROV=y`). BLE provisioning is disabled in P2P modes, so there is no
> gesture conflict.

> **Long-press acknowledgement**: LED 0 is turned OFF for 300 ms before reboot so the user gets visual confirmation the gesture was registered.

> **BLE prov double-click**: in STA/SoftAP modes the BREATHE effect is driven locally by toggling `ble_prov_led_active`, and `zego_wifi_ble_prov_advertise(ble_prov_led_active)` starts/stops BLE advertising.

> **P2P pairing double-click — non-blocking requirement**: `btn_listener_cb` is a `ZBUS_LISTENER` running synchronously in the button publisher's context, so it must return in microseconds. `wifi_p2p_start_pairing()` must therefore only submit work to the network brick's dedicated P2P work queue (it does not perform the `WIFI_P2P_FIND`/`WIFI_P2P_CONNECT` net_mgmt calls inline). See the network spec [P2P_GC Pairing Sequence](../../../bricks/network/docs/network-spec.md).

> **nRF5340 Audio DK**: the UX gesture button is **BTN5 (index 4)** — set `CONFIG_APP_UX_BUTTON_IDX=4` in the board conf (VOL- / idx 0 is left free for the application). All gestures work identically on BTN5. The double-click BLE-prov toggle is compiled out (`CONFIG_ZEGO_WIFI_BLE_PROV=n`), but the P2P pairing double-click works in P2P_GO/P2P_GC modes.

---

## 4. LED 0 State Machine

LED 0 is driven by `APP_WIFI_STATE_CHAN` (published by `net_event_app.c`) and by the double-click toggle in this module.

```
Boot
 │
 ▼
[ROTATE] ◄── APP_WIFI_STATE_CONNECTING published (at SYS_INIT, and on any disconnect
 │            that will auto-retry: zego_on_net_event_wifi_disconnect(true))
 │
 ├──► APP_WIFI_STATE_CONNECTED  ──► [Solid ON]
 │       │
 │       └── disconnect, will_retry=true ──► APP_WIFI_STATE_CONNECTING ──► [ROTATE]  (loops
 │           back here on the next successful reconnect)
 │
 ├──► APP_WIFI_STATE_SOFTAP     ──► [ROTATE]  (AP up, no clients)
 │       │
 │       └── APP_WIFI_STATE_CONNECTED ──► [Solid ON]  (client joined)
 │               │
 │               └── APP_WIFI_STATE_SOFTAP ──► [ROTATE]  (last client left)
 │
 ├──► APP_WIFI_STATE_ERROR      ──► [Fast BLINK 100 ms]
 │       (only reachable via zego_on_net_event_wifi_disconnect(false): STA mode start or
 │        disconnect with zero stored Wi-Fi credentials. P2P_GC never reaches this state —
 │        it always retries, so will_retry is unconditionally true for P2P_GC.)
 │
 └──► APP_WIFI_STATE_PAIRING    ──► [BREATHE]   (P2P pairing in progress — started
         │                          automatically at boot with no saved GO, or by double-click)
         └── pairing ends → next state event (CONNECTED → Solid ON, else SOFTAP/CONNECTING)

Double-click (nRF54LM20DK, CONFIG_ZEGO_WIFI_BLE_PROV=y):
  ble_prov_led_active = false → true   ──► [BREATHE]
  ble_prov_led_active = true  → false  ──► restore last Wi-Fi state LED
```

| `app_wifi_state` | LED 0 effect | `period_ms` | Published when |
|------------------|-------------|-------------|-----------------|
| `CONNECTING` | ROTATE | Kconfig default | SYS_INIT (boot); or `zego_on_net_event_wifi_disconnect(true)` — a retry will happen automatically |
| `CONNECTED` | Solid ON | — | `zego_on_net_event_dhcp_bound()` — STA/P2P link up, or first SoftAP/P2P_GO client joins |
| `SOFTAP` | ROTATE | Kconfig default | AP enabled with no clients, or last client just left |
| `PAIRING` | BREATHE | Kconfig default | `zego_on_net_event_p2p_pairing(true)` — pairing active (automatic or double-click-triggered) |
| `ERROR` | Fast BLINK | 100 ms | `zego_on_net_event_wifi_disconnect(false)` — **only** case: STA with zero stored credentials |

> **BLINK is not a generic "disconnected" indicator.** It fires only when reconnection is
> structurally impossible (STA, nothing stored to retry with). Any disconnect where a retry
> will occur — STA with stored credentials, or P2P_GC (always) — publishes `CONNECTING`
> (ROTATE), not `ERROR`. This keeps the LED meaning "trying" during transient AP reboots,
> out-of-range periods, etc., reserving BLINK for "you need to configure something."

> **Pairing BREATHE**: while a P2P pairing attempt is active the LED breathes (same visual as
> BLE prov). It applies on **both roles** — the GO while its pairing window is open, the GC
> while discovering/joining, whether that pairing was started automatically (no saved GO at
> boot) or via double-click. When pairing ends, the network brick drives the next state event
> (`CONNECTED` → solid ON on success; otherwise `SOFTAP`/`CONNECTING`), so the LED reverts
> automatically. The breathe is gated on `APP_WIFI_STATE_PAIRING`, not on a local UX timer.

### Effect → per-board LED index

Which physical LED(s) light up for a given effect differs by board — the nRF5340 Audio DK
dedicates RGB2 to Wi-Fi state and uses different channels per effect, while the other boards
drive all their LEDs together:

| Effect | nRF54LM20DK + nRF7002EB2 | nRF7002DK | nRF5340 Audio DK + nRF7002EK | Kconfig |
|--------|--------------------------|-----------|-------------------------------|---------|
| ROTATE | idx 0–3 (all 4 chase) | idx 0–1 (both chase) | idx 3–5, RGB2 all 3 channels chase | `CONFIG_APP_UX_ROTATE_FIRST_LED` / `_COUNT` (default 0/0 = LED module's `ZEGO_LED_ROTATE_NUM_LEDS` from idx 0; Audio DK sets `3`/`3`) |
| Solid ON | idx 0 | idx 0 | idx 4 Green only; idx 3, 5 held OFF | `CONFIG_APP_UX_CONNECTED_LED` (default 0; Audio DK sets `4` + `CONFIG_APP_UX_CONNECTED_LED_GREEN_ONLY=y`) |
| BREATHE | idx 0 | idx 0 | idx 5 Blue only | `CONFIG_APP_UX_PAIRING_LED_IDX` (default 0; Audio DK sets `5`) |
| Fast BLINK | idx 0 | idx 0 | idx 3 Red only | `CONFIG_APP_UX_ERROR_LED_IDX` (default 0; Audio DK sets `3`) |

> RGB1 (idx 0–2) and the mono LEDs (idx 6–8) on the nRF5340 Audio DK are never touched by this
> state machine — they remain free for application use. Values above are verified against the
> Kconfig defaults in `src/modules/ux/Kconfig` and the overrides in
> `boards/nrf5340_audio_dk_nrf5340_cpuapp.conf`.

---

## 5. APP_WIFI_STATE_CHAN

Defined in `src/modules/network/net_event_app.c`. Declared in `src/modules/messages.h`.

| Event | Published when |
|-------|----------------|
| `APP_WIFI_STATE_CONNECTED` | `zego_on_net_event_dhcp_bound()` — any mode (STA, P2P_GC, SoftAP client joined) |
| `APP_WIFI_STATE_CONNECTING` | `zego_on_net_event_wifi_disconnect(true)` — disconnected but a retry will happen automatically |
| `APP_WIFI_STATE_SOFTAP` | `zego_on_net_event_wifi_ap_enabled()` (AP enabled) or `zego_on_net_event_wifi_ap_sta_disconnected()` with `remaining_clients == 0` |
| `APP_WIFI_STATE_PAIRING` | `zego_on_net_event_p2p_pairing(true)` — P2P pairing started (GO window opened; or GC discovery began, automatically at boot or via double-click) |
| `APP_WIFI_STATE_ERROR` | `zego_on_net_event_wifi_disconnect(false)` — reconnection is not possible (STA, zero stored credentials) |

> **`zego_on_net_event_p2p_pairing(bool active)`** is a weak hook in `zego/network`,
> called by the P2P engine on pairing start (`active=true`) and end (`active=false`). On
> `true`, `net_event_app.c` publishes `APP_WIFI_STATE_PAIRING`. On `false`, it re-publishes
> the resolved current state (`CONNECTED` if the link is up, else `SOFTAP`/`CONNECTING`) so the
> LED leaves BREATHE. See the network spec for exactly when the engine calls it.

> **`zego_on_net_event_wifi_disconnect(bool will_retry)`**: `net_event_app.c` maps
> `will_retry=true` to `APP_WIFI_STATE_CONNECTING` (ROTATE resumes) and `will_retry=false` to
> `APP_WIFI_STATE_ERROR` (BLINK). `APP_WIFI_STATE_CONNECTING` is also entered implicitly at
> boot — the UX module's `SYS_INIT` starts ROTATE unconditionally before any state is published.

> In SoftAP mode the state machine follows: ROTATE (AP enabled, no clients) → Solid ON (client joins) → ROTATE (last client leaves). The `zego_on_net_event_wifi_ap_sta_disconnected(remaining_clients)` hook triggers the revert to SOFTAP state when `remaining_clients == 0`.

---

## 6. Zbus Channel Summary

| Channel | Role | Direction |
|---------|------|-----------|
| `BUTTON_CHAN` | Button gestures (from `zego/button`) | Input |
| `APP_WIFI_STATE_CHAN` | Wi-Fi state events (from `net_event_app.c`) | Input |
| `LED_CMD_CHAN` | LED commands (to `zego/led`) | Output |

---

## 7. File Map

| File | Role |
|------|------|
| `src/modules/ux/ux.c` | Module implementation |
| `src/modules/ux/Kconfig` | `CONFIG_APP_UX_MODULE`, `CONFIG_APP_UX_INIT_PRIORITY` |
| `src/modules/ux/CMakeLists.txt` | Adds `ux.c` when module is enabled |
| `src/modules/messages.h` | `APP_WIFI_STATE_CHAN` declaration + `app_wifi_state_msg` type |
| `src/modules/network/net_event_app.c` | Defines `APP_WIFI_STATE_CHAN`; publishes on connectivity events |

> **External dependency**: the P2P pairing double-click calls `wifi_p2p_start_pairing()` and reads `zego_wifi_get_mode()`, both provided by the `zego/network` and `zego/wifi` bricks (declared in `wifi_utils.h` / `wifi.h`). No new Zbus channel is introduced.

---

## 8. Known Limitations

No open issues.

> In SoftAP mode with multiple clients, disconnecting one client does not change the LED (still solid) until the **last** client disconnects. This is intentional — the AP is still serving traffic.

> **Undetected client loss (SoftAP / P2P_GO)**: if the last remaining client disappears without a clean disconnect (power cut, crash — no deauth frame sent), the LED can stay solid ON for up to ~5 minutes before reverting to ROTATE, since the revert depends on the AP's inactivity-timeout eviction (`ap_max_inactivity`, default 300 s). See `network-spec.md` for the detection mechanism and how to lower the timeout at runtime.
