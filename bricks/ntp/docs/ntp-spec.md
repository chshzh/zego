# NTP Brick Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | `zego/bricks/ntp` |
| Version | 2026-07-08-00-00 |
| PRD Version | N/A (standalone library brick) |
| NCS Version | v3.4.0 |
| Status | Migrated from `nordic-wifi-memfault` |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-07-08-00-00 | Initial spec — migrated from `nordic-wifi-memfault/src/modules/ntp` as a first-class zego brick. Kconfig prefix `NTP_MODULE`/`NTP_*` → `ZEGO_NTP`/`ZEGO_NTP_*`. The module no longer depends on the app-local `NETWORK_CHAN`/`network_msg` type; it now declares its own `ZEGO_NTP_NET_CHAN` (`struct zego_ntp_net_msg { bool connected; }`), following the same decoupling pattern as `zego/ux`'s `ZEGO_UX_WIFI_STATE_CHAN` — the application publishes to it from its `zego/network` weak-hook overrides. Public API renamed `ntp_sync_init()` → `zego_ntp_init()`. |

---

## Overview

`zego/ntp` synchronizes the system clock via SNTP once network connectivity is
established. It queries `CONFIG_ZEGO_NTP_SERVER` and sets `CLOCK_REALTIME`, so
Zephyr log output shows real-world wall-clock timestamps when
`CONFIG_LOG_TIMESTAMP_USE_REALTIME=y`, and any application code that reads
`CLOCK_REALTIME` (e.g. Memfault event timestamps) gets real time instead of
uptime.

The module has no application-specific logic and contains no dependency on
app-level zbus types. Instead it declares `ZEGO_NTP_NET_CHAN` (see below),
which the application publishes to — typically from its `zego/network`
weak-hook overrides (`net_event_app.c`) — following the same decoupling
pattern used by `zego/ux`.

Failed queries are retried via a `k_work_delayable` item on the system work
queue — no dedicated thread required. A successful sync is periodically
refreshed to compensate for crystal oscillator drift. Disconnecting resets
sync state so a fresh sync is performed after each reconnect.

---

## Location

- **Path**: `zego/bricks/ntp/`
- **Files**: `src/ntp.c`, `src/ntp.h`, `Kconfig`, `CMakeLists.txt`,
  `zephyr/module.yml`, `docs/ntp-spec.md`

---

## Supported Hardware

Board-agnostic. Requires only Zephyr networking + `CONFIG_SNTP` (auto-selected).
Tested on:

| Board | Build target |
|-------|-------------|
| nRF7002DK | `nrf7002dk/nrf5340/cpuapp` |
| nRF54LM20DK + nRF7002EB2 | `nrf54lm20dk/nrf54lm20a/cpuapp` |

---

## Module Type

- [x] **Library brick** — driven by `SYS_INIT(APPLICATION, 3)`. No dedicated
  thread; SNTP queries and retries run on the system work queue via
  `k_work_delayable`.

---

## Dependencies

| Dependency | Kconfig | Notes |
|-----------|---------|-------|
| Zbus | `CONFIG_ZBUS` | Required for `ZEGO_NTP_NET_CHAN` |
| Networking | `CONFIG_NETWORKING` | Required to reach the SNTP server |
| SNTP | `CONFIG_SNTP` | Auto-selected by `CONFIG_ZEGO_NTP` |

---

## Zbus Integration

**Owns (declares)**: `ZEGO_NTP_NET_CHAN` — defined in `ntp.c`.

```c
/* ntp.h */
struct zego_ntp_net_msg {
	bool connected; /* true when the network is up, false when it is lost */
};

ZBUS_CHAN_DECLARE(ZEGO_NTP_NET_CHAN);
```

**Subscribes to**: `ZEGO_NTP_NET_CHAN` via `ZBUS_LISTENER_DEFINE(ntp_net_listener, ...)`.

**Application responsibility**: publish to `ZEGO_NTP_NET_CHAN` from the
`zego/network` weak-hook overrides in `net_event_app.c`:

```c
#include <ntp.h>

void zego_on_net_event_dhcp_bound(enum zego_wifi_mode mode, const char *ip_addr,
				  const char *mac_addr, const char *ssid)
{
	struct zego_ntp_net_msg msg = { .connected = true };

	zbus_chan_pub(&ZEGO_NTP_NET_CHAN, &msg, K_NO_WAIT);
}

void zego_on_net_event_wifi_disconnect(bool will_retry)
{
	struct zego_ntp_net_msg msg = { .connected = false };

	zbus_chan_pub(&ZEGO_NTP_NET_CHAN, &msg, K_NO_WAIT);
}
```

Without this publish, `zego/ntp` never learns the network is up and will
never query the SNTP server.

---

## Behavior

1. On `connected = true`: if not already synced, schedule an immediate SNTP
   query (`K_NO_WAIT`).
2. On successful query: call `sys_clock_settime(SYS_CLOCK_REALTIME, ...)`,
   mark synced, and reschedule the next query after
   `CONFIG_ZEGO_NTP_RESYNC_INTERVAL_SEC`.
3. On failed query: reschedule after `CONFIG_ZEGO_NTP_RETRY_INTERVAL_SEC`.
4. On `connected = false`: cancel any pending work and clear the synced flag,
   so the next `connected = true` triggers an immediate re-sync.

---

## Kconfig Symbols

| Symbol | Default | Description |
|--------|---------|--------------|
| `CONFIG_ZEGO_NTP` | `n` | Enable the brick |
| `CONFIG_ZEGO_NTP_SERVER` | `"pool.ntp.org"` | SNTP server hostname |
| `CONFIG_ZEGO_NTP_TIMEOUT_MS` | `5000` | SNTP query timeout (1000–30000 ms) |
| `CONFIG_ZEGO_NTP_RETRY_INTERVAL_SEC` | `30` | Retry interval on failure (5–3600 s) |
| `CONFIG_ZEGO_NTP_RESYNC_INTERVAL_SEC` | `10800` | Periodic re-sync interval after success (60–86400 s); `0` disables periodic re-sync |
| `CONFIG_ZEGO_NTP_LOG_LEVEL` | `3` (INF) | Log verbosity (standard Zephyr log level template) |

---

## Test Points

| Test | Expected result |
|------|------------------|
| Boot with Wi-Fi connected | Log shows `Querying pool.ntp.org ...` then `Time synced, epoch ...` within `ZEGO_NTP_TIMEOUT_MS` |
| `CONFIG_LOG_TIMESTAMP_USE_REALTIME=y` | Log timestamps switch from uptime (`[00:00:12.345,000]`) to real-world UTC after sync |
| Disconnect then reconnect | A fresh SNTP query fires shortly after `zego_on_net_event_dhcp_bound()` is called again |
| SNTP server unreachable | `SNTP query failed (...)` logged, retried every `ZEGO_NTP_RETRY_INTERVAL_SEC` |
| Long uptime (> resync interval) | A new `Querying ...` log line appears every `ZEGO_NTP_RESYNC_INTERVAL_SEC` |
