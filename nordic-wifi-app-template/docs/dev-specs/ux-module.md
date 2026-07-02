# UX Module Engineering Spec — MOVED

## Document Information

| Field | Value |
|---|---|
| Module | `zego/ux` (formerly `app_ux`) |
| Version | 2026-07-02-00-00 |
| Status | Superseded — moved |

---

## This spec has moved

The UX module was moved from an in-tree application module
(`nordic-wifi-app-template/src/modules/ux/`, `CONFIG_APP_UX_MODULE`) to a
first-class zego brick:

**→ [`zego/bricks/ux/docs/ux-spec.md`](../../../bricks/ux/docs/ux-spec.md)**

Summary of what changed:

- Location: `src/modules/ux/` → `zego/bricks/ux/`
- Kconfig: `CONFIG_APP_UX_MODULE` → `CONFIG_ZEGO_UX` (all `CONFIG_APP_UX_*` sub-symbols
  renamed to `CONFIG_ZEGO_UX_*`)
- Zbus channel: `APP_WIFI_STATE_CHAN` (declared in the app's `messages.h`) →
  `ZEGO_UX_WIFI_STATE_CHAN` (owned by the brick, declared in `ux.h`)
- Button 0 gesture actions (single click, double-click, long press) are now
  `__weak` functions (`zego_ux_on_single_click()`, `zego_ux_on_double_click()`,
  `zego_ux_on_long_press()`) that applications may override individually,
  following the same weak-hook pattern as `zego/network`'s
  `zego_on_net_event_*()` callbacks.

No runtime behaviour changed for existing consumers of
`nordic-wifi-app-template` — this was a structural move plus the weak-hook
refactor described above.

This file is kept only so existing links do not 404; see the changelog in
[`0-overview.md`](0-overview.md) for the full history preceding the move.

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
