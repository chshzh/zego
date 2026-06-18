# memonitor Brick Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | `zego/bricks/memonitor` |
| Version | 2026-06-18-00-00 |
| PRD Version | N/A (standalone library brick) |
| NCS Version | v3.3.0 |
| Status | Validated (migrated from nordic-wifi-webdash) |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-18-00-00 | Initial spec — migrated from `nordic-wifi-webdash/src/modules/memonitor` after in-app validation. Renamed Kconfig prefix `APP_MEMONITOR_*` → `ZEGO_MEMONITOR_*`. |

---

## Overview

`zego/memonitor` is a **periodic memory and thread watermark sampler** brick.
It fires every `CONFIG_ZEGO_MEMONITOR_INTERVAL_MS` on the Zephyr system work queue,
captures a snapshot of all runtime heap and thread metrics, caches the result in a
spinlock-protected static buffer, and publishes `MEMONITOR_CHAN` so any number of
subscribers can consume the data without polling.

Consumers call `memonitor_get_heaps()` or `memonitor_get_threads()` to obtain a
thread-safe, consistent point-in-time copy of the latest snapshot. This is safe to
call from any thread context including HTTP handler threads.

---

## Location

- **Path**: `zego/bricks/memonitor/`
- **Files**: `src/memonitor.c`, `src/memonitor.h`, `Kconfig`, `CMakeLists.txt`,
  `zephyr/module.yml`, `docs/memonitor-spec.md`

---

## Supported Hardware

Board-agnostic. Requires only the Zephyr kernel (no hardware peripherals).
Tested on:

| Board | Build target |
|-------|-------------|
| nRF7002DK | `nrf7002dk/nrf5340/cpuapp` |
| nRF54LM20DK + nRF7002EB2 | `nrf54lm20dk/nrf54lm20a/cpuapp` |

---

## Module Type

- [x] **Library brick** — driven by `SYS_INIT(APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY)`.
  No dedicated thread. All sampling runs on the system work queue via `k_work_delayable`.

---

## Dependencies

| Dependency | Kconfig | Notes |
|-----------|---------|-------|
| Zbus | `CONFIG_ZBUS` | Required for `MEMONITOR_CHAN` |
| Heap runtime stats | `CONFIG_SYS_HEAP_RUNTIME_STATS` | Enables `sys_heap_runtime_stats_get()` |
| Thread stack info | `CONFIG_THREAD_STACK_INFO` | Required for `thread->stack_info.size` |
| Stack init fill | `CONFIG_INIT_STACKS` | Fills stacks with `0xaa`; required for `k_thread_stack_space_get()` |
| Stack sentinel OFF | `CONFIG_STACK_SENTINEL=n` | Must be `n`; `=y` overwrites `0xaa` fill with `0xf0f0f0f0`, causing all watermarks to report 100% |
| mbedTLS heap (optional) | `CONFIG_MBEDTLS_ENABLE_HEAP` | When `=y`, mbedTLS internal heap is auto-included in snapshot as `"mbedtls_heap"` |

---

## Zbus Integration

### Channel

```c
/* Defined in memonitor.c — single owner. */
ZBUS_CHAN_DEFINE(MEMONITOR_CHAN, struct memonitor_event, ...);
```

### Message type

```c
struct memonitor_event {
    uint32_t timestamp_ms;   /* k_uptime_get_32() at sample time */
    uint32_t interval_ms;    /* CONFIG_ZEGO_MEMONITOR_INTERVAL_MS */
    uint8_t  heap_count;     /* number of valid heap entries in cache */
    uint8_t  thread_count;   /* number of valid thread entries in cache */
};
```

The message is intentionally **small** (12 bytes). The actual snapshot data (heap
entries + thread entries, ~2 KB total) lives in static BSS and is accessed via the
accessor API. This avoids pushing 2 KB onto every subscriber's callback stack.

### Subscriber pattern

```c
ZBUS_SUBSCRIBER_DEFINE(my_sub, 4);

static void my_task(void)
{
    const struct zbus_channel *chan;
    while (!zbus_sub_wait(&my_sub, &chan, K_FOREVER)) {
        struct memonitor_heap_entry heaps[MEMONITOR_MAX_HEAPS];
        uint8_t count = 0;
        memonitor_get_heaps(heaps, ARRAY_SIZE(heaps), &count);
        /* process heaps[] */
    }
}
```

---

## Public API

```c
/* memonitor.h */

int memonitor_get_heaps(struct memonitor_heap_entry *out, uint8_t max, uint8_t *count);
int memonitor_get_threads(struct memonitor_thread_entry *out, uint8_t max, uint8_t *count);

static inline uint32_t memonitor_get_interval_ms(void);
```

Both `get_*` functions are thread-safe (spinlock-protected memcpy). They return `0`
on success, `-EINVAL` on bad arguments.

---

## Data Structures

```c
#define MEMONITOR_MAX_HEAPS    8
#define MEMONITOR_MAX_THREADS  32

struct memonitor_heap_entry {
    char   name[32];      /* heap symbol name */
    size_t free;          /* free bytes */
    size_t used;          /* allocated bytes */
    size_t watermark;     /* peak allocated bytes (HWM) */
    size_t total;         /* free + used */
};

struct memonitor_thread_entry {
    char   name[32];      /* k_thread_name_get() */
    char   state[12];     /* "ready" | "pending" | "suspended" | "dead" */
    size_t stack_hwm;     /* stack_size - unused (from k_thread_stack_space_get) */
    size_t stack_size;    /* thread->stack_info.size */
};
```

---

## Heap Enumeration

The sampler uses `STRUCT_SECTION_FOREACH(k_heap, heap)` to iterate all heaps
registered in the `_k_heap_list` linker section. All heaps defined with
`K_HEAP_DEFINE` are included automatically. Known heap names are resolved by
pointer comparison against externally-declared symbols:

| Symbol | Source |
|--------|--------|
| `_system_heap` | Zephyr kernel (`mempool.c`) |
| `net_buf_mem_pool_rx_bufs` | Wi-Fi RX VAR pool |
| `net_buf_mem_pool_tx_bufs` | Wi-Fi TX VAR pool |
| `shell_uart_history_heap` | Shell UART history |
| `wifi_drv_ctrl_mem_pool` | nRF Wi-Fi driver control path |
| `wifi_drv_data_mem_pool` | nRF Wi-Fi driver data path |

All are declared `extern __weak` so the brick links cleanly in any Kconfig
combination. Unrecognized heaps fall back to `"k_heap"`.

When `CONFIG_MBEDTLS_ENABLE_HEAP=y`, the mbedTLS internal heap is appended to the
snapshot using `mbedtls_memory_buffer_alloc_cur_get()` and `_max_get()`.

---

## Stack Watermark Implementation

```
CONFIG_INIT_STACKS=y  →  kernel fills stack memory with 0xaa at thread creation
CONFIG_STACK_SENTINEL=n  →  sentinel does NOT overwrite 0xaa
k_thread_stack_space_get(thread, &unused)  →  counts trailing 0xaa bytes
stack_hwm = stack_size - unused
```

`CONFIG_STACK_SENTINEL=y` is **incompatible**: it overwrites the `0xaa` fill with
`0xf0f0f0f0`, causing `k_thread_stack_space_get()` to report 0 unused bytes → 100%
watermark on every thread.

---

## Memory Budget

| Item | Size | Notes |
|------|------|-------|
| `heap_cache[]` | 8 × 48 = 384 B | static BSS |
| `thread_cache[]` | 32 × 52 = 1664 B | static BSS |
| `tmp[]` (heap sampler) | 384 B | `static` local — NOT on stack |
| `tmp[]` (thread sampler) | 1664 B | `static` local — NOT on stack |
| `k_spinlock` | 0–4 B | 0 on UP (uniprocessor) |
| **Total BSS** | **~4096 B** | Constant regardless of sample count |
| Work queue stack pressure | ~200 B | Callback frames only; tmp[] are static |

The `tmp[]` arrays are declared `static` inside their functions. They are only
ever accessed from the system work queue (single work item, never re-entrant), so
static allocation is safe and avoids overflowing the work queue stack.

---

## Kconfig Reference

| Symbol | Default | Description |
|--------|---------|-------------|
| `CONFIG_ZEGO_MEMONITOR` | `n` | Enable the brick |
| `CONFIG_ZEGO_MEMONITOR_INTERVAL_MS` | `2000` | Sampling period (ms) |
| `CONFIG_ZEGO_MEMONITOR_LOG_LEVEL` | `3` (INF) | Log verbosity 0–4 |

---

## Integration Example (webdash)

```cmake
# CMakeLists.txt
get_filename_component(ZEGO_MEMONITOR_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/../zego/bricks/memonitor REALPATH)
list(APPEND EXTRA_ZEPHYR_MODULES ${ZEGO_MEMONITOR_DIR})
```

```kconfig
# Kconfig
rsource "path/to/zego/bricks/memonitor/Kconfig"  # or via module system
```

```conf
# prj.conf
CONFIG_ZEGO_MEMONITOR=y
CONFIG_ZEGO_MEMONITOR_INTERVAL_MS=2000
```

```c
/* consumer (e.g. webserver.c) */
#include <memonitor.h>

struct memonitor_heap_entry heaps[MEMONITOR_MAX_HEAPS];
uint8_t count = 0;
memonitor_get_heaps(heaps, ARRAY_SIZE(heaps), &count);
```

---

## Test Points

| # | Scenario | Pass Criteria |
|---|----------|---------------|
| TP-1 | Boot with `CONFIG_ZEGO_MEMONITOR=y` | Log: `memonitor started (interval=2000 ms)` |
| TP-2 | `/api/heaps` in browser | JSON contains all 6 k_heap entries with correct names |
| TP-3 | `/api/threads` in browser | All threads listed; no entry shows 100% watermark |
| TP-4 | `CONFIG_ZEGO_MEMONITOR=n` | `/api/heaps` and `/api/threads` return 404; sections hidden |
| TP-5 | `CONFIG_MBEDTLS_ENABLE_HEAP=y` | `mbedtls_heap` row appears in `/api/heaps` |
| TP-6 | ZView heap view | Matches `/api/heaps` values within one sample interval |
