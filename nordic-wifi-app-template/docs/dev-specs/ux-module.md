# UX Module Engineering Spec

## Document Information

| Field | Value |
|---|---|
| Module | app_ux |
| Version | 2026-06-30-13-04 |
| PRD Version | 2026-06-30-13-00 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF54LM20DK + nRF7002EB2, nRF7002DK, nRF5340 Audio DK + nRF7002EK |
| Status | Current |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-04-18-00 | Initial spec — Button 0 gestures, LED 0 Wi-Fi state feedback |
| 2026-06-04-22-00 | SoftAP LED behavior: ROTATE when no clients (was slow BLINK); solid ON when client connected; ROTATE again when last client disconnects. Added `zego_on_net_event_softap_sta_disconnected()` hook. Updated APP_WIFI_STATE_CHAN table and state machine diagram. |
| 2026-06-05-09-38 | Added nRF5340 Audio DK + nRF7002EK: Button 0 = VOL-; ROTATE chases RGB1 only (3 LEDs); BLE prov double-click disabled (1 MB flash); board differences note added |
| 2026-06-09-16-03 | nRF5340 Audio DK: ROTATE extends to RGB1+RGB2 (`CONFIG_ZEGO_LED_ROTATE_NUM_LEDS=6`); connected state drives LED 4 (green channel of RGB2) ON + LEDs 3 and 5 OFF, replacing plain LED_COMMAND_ON; new Kconfig `CONFIG_APP_UX_CONNECTED_LED` and `CONFIG_APP_UX_CONNECTED_LED_GREEN_ONLY` |
| 2026-06-09-16-23 | Corrected nRF5340 Audio DK ROTATE to RGB2 only (indices 3–5); added `CONFIG_APP_UX_ROTATE_FIRST_LED` and `CONFIG_APP_UX_ROTATE_COUNT` Kconfig symbols; `ux.c` now uses `led_rotate()` + `led_connected()` helpers; long-press ack uses first rotate LED; removed `CONFIG_ZEGO_LED_ROTATE_NUM_LEDS=3` from board conf |
| 2026-06-09-17-25 | PRD Version sync to v2026-06-09-17-25 (FR-107 P2P_CLIENT added to PRD; no UX spec change required — P2P_CLIENT uses same ROTATE→solid-ON LED transitions as STA mode) |
| 2026-06-29-23-06 | Mode-cycle gesture text corrected to STA → SoftAP → P2P_GO → P2P_GC → STA (P2P_GC was missing); validation-found doc fix. |
| 2026-06-29-21-44 | Updated to PRD v2026-06-29-21-44: Button 0 double-click is now mode-aware — BLE-prov toggle in STA/SoftAP, P2P pairing trigger in P2P_GO/P2P_GC (calls `wifi_p2p_start_pairing()` in zego/network). P2P_CLIENT→P2P_GC naming aligned. |
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

---

## 3. UX Gesture Button & Gesture Map

All gestures are carried by a single **UX gesture button**, selected by
`CONFIG_APP_UX_BUTTON_IDX` (default `0`; `4` / BTN5 on the nRF5340 Audio DK). The handler
ignores `BUTTON_CHAN` events whose `button_number != CONFIG_APP_UX_BUTTON_IDX`.

| Gesture | Threshold | Action |
|---------|-----------|--------|
| Single click | — | Print current Wi-Fi mode to UART log |
| Double-click (STA / SoftAP modes) | `ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS` | Toggle BLE provisioning (BREATHE ↔ last Wi-Fi state LED) + `zego_wifi_ble_prov_advertise()` — nRF54LM20DK only (`CONFIG_ZEGO_WIFI_BLE_PROV=y`) |
| Double-click (P2P_GO / P2P_GC modes) | `ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS` | Trigger P2P pairing: call `wifi_p2p_start_pairing()` (zego/network) — GO refreshes its WPS PBC window, GC discovers + joins the pairing GO and saves its MAC. All boards with P2P enabled |
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
[ROTATE] ◄── APP_WIFI_STATE_CONNECTING published (or at SYS_INIT)
 │
 ├──► APP_WIFI_STATE_CONNECTED  ──► [Solid ON]
 │
 ├──► APP_WIFI_STATE_SOFTAP     ──► [ROTATE]  (AP up, no clients)
 │       │
 │       └── APP_WIFI_STATE_CONNECTED ──► [Solid ON]  (client joined)
 │               │
 │               └── APP_WIFI_STATE_SOFTAP ──► [ROTATE]  (last client left)
 │
 ├──► APP_WIFI_STATE_ERROR      ──► [Fast BLINK 100 ms]
 │
 └──► APP_WIFI_STATE_PAIRING    ──► [BREATHE]   (P2P pairing in progress)
         │
         └── pairing ends → next state event (CONNECTED → Solid ON, else SOFTAP/ERROR)

Double-click (nRF54LM20DK, CONFIG_ZEGO_WIFI_BLE_PROV=y):
  ble_prov_led_active = false → true   ──► [BREATHE]
  ble_prov_led_active = true  → false  ──► restore last Wi-Fi state LED
```

| `app_wifi_state` | LED 0 effect | `period_ms` |
|------------------|-------------|-------------|
| `CONNECTING` (boot) | ROTATE | Kconfig default |
| `CONNECTED` (STA/P2P) | Solid ON | — |
| `SOFTAP` (AP up, no clients) | ROTATE | Kconfig default |
| `CONNECTED` (SoftAP client joined) | Solid ON | — |
| `PAIRING` (P2P pairing active) | BREATHE | Kconfig default |
| `ERROR` (disconnected) | BLINK | 100 ms |
| BLE prov active (local toggle) | BREATHE | Kconfig default |

> **Pairing BREATHE**: while a P2P pairing attempt is active the LED breathes (same visual as
> BLE prov). It applies on **both roles** — the GO while its pairing window is open, the GC
> while discovering/joining. When pairing ends, the network brick drives the next state event
> (`CONNECTED` → solid ON on success; otherwise `SOFTAP`/`ERROR`), so the LED reverts
> automatically. The breathe is gated on `APP_WIFI_STATE_PAIRING`, not on a local UX timer.

> **ROTATE LED count**: On nRF54LM20DK (4 LEDs) ROTATE chases across all four. On nRF7002DK (2 LEDs) it chases across both. On nRF5340 Audio DK (9 LEDs total) ROTATE chases across **RGB2 only (indices 3–5)**, controlled by `CONFIG_APP_UX_ROTATE_FIRST_LED=3` and `CONFIG_APP_UX_ROTATE_COUNT=3`. RGB1 and mono LEDs remain off during the effect.
>
> **Connected state on nRF5340 Audio DK**: the UX module sends `LED_COMMAND_ON` to LED 4 (green channel of RGB2) and `LED_COMMAND_OFF` to LEDs 3 and 5 (red and blue channels), producing a solid-green indicator. Controlled by `CONFIG_APP_UX_CONNECTED_LED=4` and `CONFIG_APP_UX_CONNECTED_LED_GREEN_ONLY=y`.

---

## 5. APP_WIFI_STATE_CHAN

Defined in `src/modules/network/net_event_app.c`. Declared in `src/modules/messages.h`.

| Event | Published when |
|-------|----------------|
| `APP_WIFI_STATE_CONNECTED` | `zego_on_net_event_dhcp_bound()` — any mode (STA, P2P_GC, SoftAP client joined) |
| `APP_WIFI_STATE_SOFTAP` | `zego_on_net_event_wifi_ap_enabled()` (AP enabled) or `zego_on_net_event_wifi_ap_sta_disconnected()` with `remaining_clients == 0` |
| `APP_WIFI_STATE_PAIRING` | `zego_on_net_event_p2p_pairing(true)` — P2P pairing started (GO window opened, or GC discovery began) |
| `APP_WIFI_STATE_ERROR` | `zego_on_net_event_wifi_disconnect()` |

> **`zego_on_net_event_p2p_pairing(bool active)`** is a new weak hook in `zego/network`,
> called by the P2P engine on pairing start (`active=true`) and end (`active=false`). On
> `true`, `net_event_app.c` publishes `APP_WIFI_STATE_PAIRING`. On `false`, it re-publishes
> the resolved current state (`CONNECTED` if the link is up, else `SOFTAP`/`ERROR`) so the LED
> leaves BREATHE. See the network spec for exactly when the engine calls it.

> `APP_WIFI_STATE_CONNECTING` is not published explicitly — the UX module's `SYS_INIT` starts ROTATE at boot unconditionally.

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
