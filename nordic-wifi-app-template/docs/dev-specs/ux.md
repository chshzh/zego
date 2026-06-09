# UX Module Engineering Spec

## Document Information

| Field | Value |
|---|---|
| Module | app_ux |
| Version | 2026-06-05-09-38 |
| PRD Version | 2026-06-05-09-38 |
| NCS Version | v3.3.0 |
| Target Board(s) | nRF54LM20DK + nRF7002EB2, nRF7002DK, nRF5340 Audio DK + nRF7002EK |
| Status | Current |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-04-18-00 | Initial spec — Button 0 gestures, LED 0 Wi-Fi state feedback |
| 2026-06-04-22-00 | SoftAP LED behavior: ROTATE when no clients (was slow BLINK); solid ON when client connected; ROTATE again when last client disconnects. Added `zego_network_on_softap_sta_disconnected()` hook. Updated APP_WIFI_STATE_CHAN table and state machine diagram. |
| 2026-06-05-09-38 | Added nRF5340 Audio DK + nRF7002EK: Button 0 = VOL-; ROTATE chases RGB1 only (3 LEDs); BLE prov double-click disabled (1 MB flash); board differences note added |

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

---

## 3. Button 0 Gesture Map

| Gesture | Threshold | Action | Both boards |
|---------|-----------|--------|-------------|
| Single click | — | Print current Wi-Fi mode to UART log | Yes |
| Double-click | `ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS` | Toggle BLE provisioning LED (BREATHE ↔ last Wi-Fi state LED) | nRF54LM20DK only (`CONFIG_ZEGO_WIFI_BLE_PROV=y`) |
| Long press | `ZEGO_BUTTON_LONG_PRESS_MS` (default 3000 ms) | Cycle mode STA → SoftAP → P2P_GO → STA; save via `settings_save_one("app/app_wifi_mode")`; `sys_reboot(SYS_REBOOT_COLD)` | Yes |

> **Long-press acknowledgement**: LED 0 is turned OFF for 300 ms before reboot so the user gets visual confirmation the gesture was registered.

> **BLE prov double-click**: The BREATHE effect is driven locally within the UX module by toggling `ble_prov_led_active`. It does **not** start or stop BLE advertising — that requires a future `zego_wifi_ble_prov_advertise(bool)` API in `zego/modules/wifi_ble_prov`.

> **nRF5340 Audio DK**: Button 0 maps to **VOL-** (index 0). All three gestures work identically. The double-click BLE prov toggle is compiled in but has no visual effect because `CONFIG_ZEGO_WIFI_BLE_PROV=n` on this board.

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
 └──► APP_WIFI_STATE_ERROR      ──► [Fast BLINK 100 ms]

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
| `ERROR` (disconnected) | BLINK | 100 ms |
| BLE prov active (local toggle) | BREATHE | Kconfig default |

> **ROTATE LED count**: On nRF54LM20DK (4 LEDs) ROTATE chases across all four. On nRF7002DK (2 LEDs) it chases across both LEDs. On nRF5340 Audio DK (9 LEDs total) ROTATE chases across **RGB1 only (indices 0–2)**, controlled by `CONFIG_ZEGO_LED_ROTATE_NUM_LEDS=3`; RGB2 and mono LEDs remain off during the effect.

---

## 5. APP_WIFI_STATE_CHAN

Defined in `src/modules/network/net_event_app.c`. Declared in `src/modules/messages.h`.

| Event | Published when |
|-------|----------------|
| `APP_WIFI_STATE_CONNECTED` | `zego_network_on_wifi_connected()` — any mode (STA, P2P_CLIENT, SoftAP client joined) |
| `APP_WIFI_STATE_SOFTAP` | `zego_network_on_softap_ready()` (AP enabled) or `zego_network_on_softap_sta_disconnected()` with `remaining_clients == 0` |
| `APP_WIFI_STATE_ERROR` | `zego_network_on_wifi_disconnected()` |

> `APP_WIFI_STATE_CONNECTING` is not published explicitly — the UX module's `SYS_INIT` starts ROTATE at boot unconditionally.

> In SoftAP mode the state machine follows: ROTATE (AP enabled, no clients) → Solid ON (client joins) → ROTATE (last client leaves). The `zego_network_on_softap_sta_disconnected(remaining_clients)` hook triggers the revert to SOFTAP state when `remaining_clients == 0`.

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

---

## 8. Known Limitations

No open issues.

> In SoftAP mode with multiple clients, disconnecting one client does not change the LED (still solid) until the **last** client disconnects. This is intentional — the AP is still serving traffic.
