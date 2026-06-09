# Wi-Fi BLE Provisioning Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | `zego/wifi_ble_prov` |
| Version | 2026-06-05-09-31 |
| PRD Version | N/A (standalone library module) |
| NCS Version | v3.3.0 |
| Status | Stable |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-04-17-10 | Initial spec — reverse-designed from source |
| 2026-06-05-09-31 | Added Supported Hardware section; documented nRF5340 Audio DK + nRF7002EK + BLE network-core constraint |

---

## Overview

`zego/wifi_ble_prov` provides BLE-based Wi-Fi credential provisioning compatible with the
**nRF Wi-Fi Provisioner** phone app (Android / iOS). It starts a BLE GATT provisioning service
at boot, advertises the device by name (`PVxxxxxx` where `xxxxxx` is the last 3 MAC bytes), and
manages a rotating credential reconnect loop so the device automatically recovers from
disconnections by cycling through all stored networks.

The module owns the `WIFI_CHAN` zbus channel and subscribes to it via a `ZBUS_LISTENER`. The
application's `net_event_app.c` must publish `WIFI_STA_CONNECTED` / `WIFI_STA_DISCONNECTED`
messages to `WIFI_CHAN` to keep the BLE advertisement status flags up to date.

---

## Location

- **Path**: `zego/wifi_ble_prov/`
- **Files**: `src/wifi_ble_prov.c`, `src/wifi_ble_prov.h`, `Kconfig`, `Kconfig.defaults`,
  `CMakeLists.txt`, `zephyr/module.yml`

---

## Supported Hardware

BLE provisioning requires the BT host stack. On dual-core boards (nRF7002DK, nRF5340 Audio DK),
BLE runs on the network core via `hci_ipc` — set `SB_CONFIG_NETCORE_HCI_IPC=y` in
`sysbuild.conf`. On single-core nRF54LM20DK, the BT stack runs on the same core as the app.

| Board | Build target | BLE core | Notes |
|-------|-------------|----------|-------|
| nRF54LM20DK + nRF7002EB2 | `nrf54lm20dk/nrf54lm20a/cpuapp` + `-DSHIELD=nrf7002eb2` | Same app core | Single-core; ~2× flash/RAM — comfortable margin |
| nRF7002DK | `nrf7002dk/nrf5340/cpuapp` | Network core (`hci_ipc`) | `SB_CONFIG_NETCORE_HCI_IPC=y` in `sysbuild.conf`; ~1 MB app flash — BT stack (~150 KB) fits but leaves little headroom alongside a heavy app |
| nRF5340 Audio DK + nRF7002EK | `nrf5340_audio_dk/nrf5340/cpuapp` + `-DSHIELD=nrf7002ek` | Network core (`hci_ipc`) | `SB_CONFIG_NETCORE_HCI_IPC=y` in `sysbuild.conf`; same ~1 MB app-core flash budget as nRF7002DK |

---

## Module Type

- [x] **Library wrapper module** — wraps `BT_WIFI_PROV` (NCS `bluetooth/services/wifi_provisioning`)
  and `WIFI_PROV_CORE`. Runs an ADV daemon work queue thread (4096 B stack, priority 5).
  Does **not** use the SMF pattern; driven by net_mgmt callbacks and a zbus listener.

---

## External Library Interface

| Library | NCS Kconfig | Internal threads |
|---|---|---|
| `BT_WIFI_PROV` (GATT provisioning service) | `CONFIG_BT_WIFI_PROV=y` (selected automatically) | None; uses BT host stack thread |
| `WIFI_PROV_CORE` | `CONFIG_WIFI_PROV_CORE=y` (selected automatically) | None |
| BT host stack | `CONFIG_BT=y` (selected automatically) | `rx_thread`, `tx_thread`, host-side threads |

**APIs called by this wrapper:**

| API | Purpose |
|---|---|
| `bt_enable(NULL)` | Initialize BT host stack |
| `wifi_prov_init()` | Start GATT provisioning service |
| `wifi_prov_state_get()` | Query whether provisioning session is active |
| `bt_le_adv_start()` / `bt_le_adv_stop()` / `bt_le_adv_update_data()` | Manage BLE advertisement |
| `bt_conn_auth_cb_register()` | Register pairing callbacks |
| `bt_set_name()` | Set BLE device name |
| `wifi_credentials_is_empty()` | Check if any credentials are stored |
| `wifi_credentials_for_each_ssid()` | Iterate stored SSIDs for rotating reconnect |
| `wifi_credentials_get_by_ssid_personal_struct()` | Load credential for connection |
| `net_mgmt(NET_REQUEST_WIFI_CONNECT, ...)` | Issue Wi-Fi connect request |
| `net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, ...)` | Query connection state |

**Callbacks implemented:**

| Callback | Registered via | Purpose |
|---|---|---|
| `connected()` / `disconnected()` | `BT_CONN_CB_DEFINE(conn_callbacks)` | Track active BT connection; trigger adv param update on disconnect |
| `auth_cancel()` | `bt_conn_auth_cb_register(&auth_cb_display)` | Log BT pairing cancel |
| `pairing_complete()` / `pairing_failed()` | `bt_conn_auth_info_cb_register(...)` | Log pairing result; disconnect on failure |
| `wifi_mgmt_event_handler()` | `net_mgmt_init_event_callback(...)` | Trigger reconnect on `WIFI_DISCONNECT_RESULT`; clear retry on `WIFI_CONNECT_RESULT` success |

---

## Zbus Integration

**Owns (defines)**: `WIFI_CHAN` — defined in `wifi_ble_prov.c`.

```c
/* wifi_ble_prov.h */
enum wifi_msg_type {
    WIFI_STA_CONNECTED,
    WIFI_STA_DISCONNECTED,
    WIFI_DNS_READY,
    WIFI_ERROR,
};

struct wifi_msg {
    enum wifi_msg_type type;
    int32_t rssi;
    int error_code;
};

ZBUS_CHAN_DECLARE(WIFI_CHAN);   /* use in app files that publish or subscribe */
```

**Subscribes to**: `WIFI_CHAN` via `ZBUS_LISTENER_DEFINE(wifi_ble_prov_listener, ...)`.

**Listener is a `ZBUS_LISTENER`** (synchronous, runs in publisher's context — must be fast):
- `WIFI_STA_CONNECTED` → schedules `update_adv_data_work` (K_NO_WAIT)
- `WIFI_STA_DISCONNECTED` → schedules `update_adv_data_work` (K_NO_WAIT)

**Application responsibility**: `net_event_app.c` must publish to `WIFI_CHAN` after calling
`zbus_chan_pub(&WIFI_CHAN, &msg, K_NO_WAIT)` inside `zego_network_on_wifi_connected()` /
`zego_network_on_wifi_disconnected()`. Without this publish, the BLE advertisement status
flags will not update.

---

## BLE Advertisement

| Parameter | Value |
|---|---|
| Device name format | `PVxxxxxx` (P, V + upper-case hex of MAC bytes 3–5) |
| UUID advertised | `BT_UUID_PROV_VAL` (128-bit, from `wifi_provisioning.h`) |
| Fast advertising | `BT_GAP_ADV_FAST_INT_MIN_2` / `MAX_2` — used when device is not yet provisioned |
| Slow advertising | `BT_GAP_ADV_SLOW_INT_MIN` / `MAX` — used after provisioning |
| Service data flags | Bit 0: provisioning active; Bit 1: Wi-Fi connected; Byte 3: RSSI |

Advertisement data is refreshed on the `adv_daemon_work_q` via `update_adv_data_work`
(deferred work, not in the BT callback context).

---

## Credential Reconnect Loop

On `WIFI_DISCONNECT_RESULT` (when provisioner is not the intentional cause):

1. Schedule `wifi_connect_work` after `WIFI_RECONNECT_DELAY_SEC` (5 s).
2. On work handler: attempt `connect_stored_rotating()` — cycles through all stored SSIDs
   in round-robin order using `wifi_credentials_for_each_ssid()`.
3. If still disconnected: reschedule after `WIFI_RECONNECT_RETRY_SEC` (180 s).
4. Log the full retry schedule at reconnect start (shows T+Ns for each SSID in rotation order).

On `WIFI_CONNECT_RESULT` success: cancel `wifi_connect_work`; clear `wifi_reconnect_pending`.

**At boot with existing credentials**: `connection_requested_after_provisioning = true` is set
to prevent the auto-connect loop from duplicating the startup connect attempt already made by
`zego/network`.

---

## ADV Daemon Thread

| Property | Value |
|---|---|
| Thread name | `ble_adv_daemon_wq` |
| Stack size | `ADV_DAEMON_STACK_SIZE` = 4096 B |
| Priority | `ADV_DAEMON_PRIORITY` = 5 |
| Work items | `update_adv_param_work`, `update_adv_data_work`, `wifi_connect_work` |
| Justification | Advertisement updates and credential-based reconnect must not run in BT callback context; dedicated work queue avoids blocking BT host stack threads |

---

## Kconfig Symbols

| Symbol | Type | Default | Description |
|---|---|---|---|
| `CONFIG_ZEGO_WIFI_BLE_PROV` | bool | n | Enable the module; selects BT, BLE peripheral, SMP, `BT_WIFI_PROV`, `WIFI_PROV_CORE`, `NANOPB` |
| `CONFIG_ZEGO_WIFI_BLE_PROV_LOG_LEVEL` | choice | INF | Module log level |

Dependencies: `ZEGO_NETWORK && ZBUS && WIFI_CREDENTIALS`

---

## SYS_INIT

`SYS_INIT(wifi_ble_prov_init, APPLICATION, 95)` — runs after `zego/network` (priority 5) and after
the Wi-Fi stack is operational.

`wifi_ble_prov_init()` sequence:
1. Check `wifi_credentials_is_empty()` — set boot flag to skip duplicate auto-connect.
2. Start ADV daemon work queue.
3. Init `k_work_delayable` items: `wifi_connect_work`, `update_adv_param_work`, `update_adv_data_work`.
4. Register BT auth callbacks.
5. `bt_enable(NULL)` — blocks until BT stack ready.
6. `wifi_prov_init()` — start GATT provisioning service.
7. Derive device name from MAC and call `bt_set_name()`.
8. `bt_le_adv_start()` — begin advertising.
9. Register `wifi_mgmt_cb` for connect/disconnect events.

---

## Error Handling

| Condition | Behaviour |
|---|---|
| `bt_enable()` fails | Log error; `wifi_ble_prov_init` returns error code |
| `wifi_prov_init()` fails | Log error; `wifi_ble_prov_init` returns error code |
| `bt_le_adv_start()` fails | Log error; `wifi_ble_prov_init` returns error code |
| BT pairing failed | Log error; `bt_conn_disconnect()` with `BT_HCI_ERR_AUTH_FAIL` |
| No stored credentials on reconnect | Log warning; clear `wifi_reconnect_pending` |
| `connect_stored_rotating()` fails | Log warning; retry loop continues |

---

## Memory Estimate

| Item | Flash | RAM |
|---|---|---|
| `wifi_ble_prov.c` | ~12 KB | ~6 KB (ADV stack 4096 + state + buffers) |
| BT host stack (selected) | ~150 KB | ~30 KB |
| `WIFI_PROV_CORE` + nanopb | ~20 KB | ~4 KB |
| **Module total (excl. BT stack)** | **~12 KB** | **~6 KB** |

> The BT host stack is the dominant cost (~150 KB Flash). On boards with ~1 MB app-core flash
> (nRF7002DK, nRF5340 Audio DK), disable `CONFIG_ZEGO_WIFI_BLE_PROV` when a heavy application
> (Memfault, large audio stack, etc.) is present. nRF54LM20DK has sufficient headroom.

---

## Test Points (UART log)

| Condition | Expected log |
|---|---|
| BT init | `[zego_wifi_ble_prov] Bluetooth initialized` |
| Provisioning service started | `[zego_wifi_ble_prov] Wi-Fi provisioning service started` |
| Advertising started | `[zego_wifi_ble_prov] BT Advertising started (device name: PVxxxxxx)` |
| BT client connected | `[zego_wifi_ble_prov] BT Connected` |
| Wi-Fi connected (from listener) | `[zego_wifi_ble_prov] WiFi connected — BLE advertisement updated` |
| Disconnect + reconnect | `[zego_wifi_ble_prov] WiFi disconnected, scheduling reconnect in 5 s` |
| Retry schedule logged | `[zego_wifi_ble_prov] --- Retry schedule (N stored network(s)) ---` |
