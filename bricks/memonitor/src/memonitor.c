/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * memonitor.c — periodic k_heap and thread watermark sampler.
 *
 * Fires every CONFIG_ZEGO_MEMONITOR_INTERVAL_MS via a k_work_delayable on the
 * system work queue.  Each cycle:
 *   1. Iterates the _k_heap_list section (STRUCT_SECTION_FOREACH) to capture
 *      all k_heap free / used / watermark / total byte counts.
 *   2. Iterates all Zephyr threads (k_thread_foreach) to capture name, state,
 *      and stack high-water mark.
 *   3. Stores both snapshots in static caches protected by a spinlock.
 *   4. Publishes MEMONITOR_CHAN so subscribers know fresh data is available.
 *
 * Consumers (webserver, future logger, Memfault reporter) call
 * memonitor_get_heaps() or memonitor_get_threads() to obtain a thread-safe
 * point-in-time copy of the latest snapshot.
 *
 * Designed to migrate to zego/bricks/memonitor once validated in-app.
 */

#include "memonitor.h"

#include <string.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/mem_stats.h>
#include <zephyr/zbus/zbus.h>
#if defined(CONFIG_MBEDTLS_MEMORY_DEBUG)
#include <mbedtls/memory_buffer_alloc.h>

/* Read by ZView orchestrator as a flat 3×u32: total, used (cur), max_used.
 * Two attributes are required together:
 *   __attribute__((used))   — tells GCC/LTO "emit this symbol even if no code
 *                             references it"; prevents LTO tree-shaking before
 *                             the linker stage.
 *   __attribute__((retain)) — marks the ELF section SHF_GNU_RETAIN so the
 *                             linker --gc-sections pass keeps it even when no
 *                             live code reference reaches this symbol.
 * Both are necessary: retain alone is ignored by LTO, used alone is ignored
 * by --gc-sections. */
struct {
	uint32_t total_bytes;
	uint32_t used_bytes;
	uint32_t max_used_bytes;
} zview_mbedtls_stats __attribute__((used, retain));
#endif

LOG_MODULE_REGISTER(memonitor, CONFIG_ZEGO_MEMONITOR_LOG_LEVEL);

/* ── Zbus channel ───────────────────────────────────────────────────────────
 * Single owner: this file.  webserver.c is a subscriber.
 * Future subscribers: log reporter, Memfault metrics uploader.
 */
ZBUS_CHAN_DEFINE(MEMONITOR_CHAN, struct memonitor_event, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.timestamp_ms = 0, .interval_ms = 0, .heap_count = 0,
			       .thread_count = 0));

/* ── Static snapshot cache ──────────────────────────────────────────────────
 * Protected by a spinlock so webserver HTTP handlers can read from any thread.
 * Arrays are only allocated when the respective sub-feature is enabled.
 */
#if defined(CONFIG_ZEGO_MEMONITOR_HEAP_MONITOR)
static struct memonitor_heap_entry heap_cache[MEMONITOR_MAX_HEAPS];
static uint8_t heap_count_cached;
#endif
#if defined(CONFIG_ZEGO_MEMONITOR_THREAD_MONITOR)
static struct memonitor_thread_entry thread_cache[MEMONITOR_MAX_THREADS];
static uint8_t thread_count_cached;
#endif
static struct k_spinlock cache_lock;

/* ── Known k_heap symbol declarations ──────────────────────────────────────
 * All 6 heaps in the nRF7002DK / nRF54LM20DK build are globally visible
 * (uppercase D in nm).  __weak prevents linker errors for any Kconfig
 * combination that lacks a particular heap.
 */
extern struct k_heap _system_heap;
extern __weak struct k_heap net_buf_mem_pool_rx_bufs;
extern __weak struct k_heap net_buf_mem_pool_tx_bufs;
extern __weak struct k_heap shell_uart_history_heap;
extern __weak struct k_heap wifi_drv_ctrl_mem_pool;
extern __weak struct k_heap wifi_drv_data_mem_pool;

static const char *k_heap_name_lookup(const struct k_heap *heap)
{
	if (heap == &_system_heap) {
		return "_system_heap";
	}
	if (&net_buf_mem_pool_rx_bufs && heap == &net_buf_mem_pool_rx_bufs) {
		return "net_buf_mem_pool_rx_bufs";
	}
	if (&net_buf_mem_pool_tx_bufs && heap == &net_buf_mem_pool_tx_bufs) {
		return "net_buf_mem_pool_tx_bufs";
	}
	if (&shell_uart_history_heap && heap == &shell_uart_history_heap) {
		return "shell_uart_history_heap";
	}
	if (&wifi_drv_ctrl_mem_pool && heap == &wifi_drv_ctrl_mem_pool) {
		return "wifi_drv_ctrl_mem_pool";
	}
	if (&wifi_drv_data_mem_pool && heap == &wifi_drv_data_mem_pool) {
		return "wifi_drv_data_mem_pool";
	}
	return "k_heap";
}

/* ── Heap sampler ───────────────────────────────────────────────────────────*/

static void sample_heaps(void)
{
	/* Static: these run only from the system work queue (never re-entrant).
	 * Keeps 384 B off the work-queue stack and prevents overflow. */
	static struct memonitor_heap_entry tmp[MEMONITOR_MAX_HEAPS];
	uint8_t cnt = 0;

	STRUCT_SECTION_FOREACH(k_heap, heap) {
		if (cnt >= MEMONITOR_MAX_HEAPS) {
			break;
		}
		struct sys_memory_stats st = {0};

		if (sys_heap_runtime_stats_get(&heap->heap, &st) != 0) {
			continue;
		}
		strncpy(tmp[cnt].name, k_heap_name_lookup(heap), sizeof(tmp[cnt].name) - 1);
		tmp[cnt].name[sizeof(tmp[cnt].name) - 1] = '\0';
		tmp[cnt].free = st.free_bytes;
		tmp[cnt].used = st.allocated_bytes;
		tmp[cnt].watermark = st.max_allocated_bytes;
		tmp[cnt].total = st.free_bytes + st.allocated_bytes;
		cnt++;
	}

#if defined(CONFIG_MBEDTLS_MEMORY_DEBUG)
	/* mbedTLS internal heap — not a k_heap, needs its own API */
	if (cnt < MEMONITOR_MAX_HEAPS) {
		size_t cur_used = 0, cur_blocks = 0;
		size_t max_used = 0, max_blocks = 0;

		mbedtls_memory_buffer_alloc_cur_get(&cur_used, &cur_blocks);
		mbedtls_memory_buffer_alloc_max_get(&max_used, &max_blocks);

		ARG_UNUSED(cur_blocks);
		ARG_UNUSED(max_blocks);

		strncpy(tmp[cnt].name, "mbedtls_heap", sizeof(tmp[cnt].name) - 1);
		tmp[cnt].name[sizeof(tmp[cnt].name) - 1] = '\0';
		tmp[cnt].used = cur_used;
		tmp[cnt].watermark = max_used;
		tmp[cnt].total = CONFIG_MBEDTLS_HEAP_SIZE;
		tmp[cnt].free = CONFIG_MBEDTLS_HEAP_SIZE > cur_used
					? CONFIG_MBEDTLS_HEAP_SIZE - cur_used
					: 0;
		cnt++;

		/* Keep ZView-visible struct in sync */
		zview_mbedtls_stats.total_bytes = CONFIG_MBEDTLS_HEAP_SIZE;
		zview_mbedtls_stats.used_bytes = (uint32_t)cur_used;
		zview_mbedtls_stats.max_used_bytes = (uint32_t)max_used;
	}
#endif /* CONFIG_MBEDTLS_ENABLE_HEAP */

	K_SPINLOCK(&cache_lock) {
		memcpy(heap_cache, tmp, cnt * sizeof(tmp[0]));
		heap_count_cached = cnt;
	}
}

/* ── Thread sampler ─────────────────────────────────────────────────────────*/

struct thread_sample_ctx {
	struct memonitor_thread_entry *buf;
	uint8_t max;
	uint8_t cnt;
};

static void thread_sample_cb(const struct k_thread *thread, void *user_data)
{
	struct thread_sample_ctx *ctx = user_data;

	if (ctx->cnt >= ctx->max) {
		return;
	}

	struct memonitor_thread_entry *e = &ctx->buf[ctx->cnt];

	/* Name */
	const char *name = k_thread_name_get((k_tid_t)thread);

	strncpy(e->name, (name && name[0]) ? name : "?", sizeof(e->name) - 1);
	e->name[sizeof(e->name) - 1] = '\0';

	/* State */
	uint32_t ts = thread->base.thread_state;
	const char *state_str;

	if (ts & _THREAD_DEAD) {
		state_str = "dead";
	} else if (ts & _THREAD_SUSPENDED) {
		state_str = "suspended";
	} else if (ts & _THREAD_PENDING) {
		state_str = "pending";
	} else {
		state_str = "ready";
	}
	strncpy(e->state, state_str, sizeof(e->state) - 1);
	e->state[sizeof(e->state) - 1] = '\0';

	/* Stack high-water mark */
	e->stack_hwm = 0;
	e->stack_size = 0;

#if defined(CONFIG_THREAD_STACK_INFO)
	e->stack_size = thread->stack_info.size;
#if defined(CONFIG_INIT_STACKS)
	size_t unused = 0;

	if (k_thread_stack_space_get(thread, &unused) == 0) {
		e->stack_hwm = e->stack_size - unused;
	}
#endif
#endif

	ctx->cnt++;
}

static void sample_threads(void)
{
	/* Static: same reasoning as sample_heaps — saves 1664 B of stack. */
	static struct memonitor_thread_entry tmp[MEMONITOR_MAX_THREADS];
	struct thread_sample_ctx ctx = {
		.buf = tmp,
		.max = MEMONITOR_MAX_THREADS,
		.cnt = 0,
	};

	k_thread_foreach(thread_sample_cb, &ctx);

	K_SPINLOCK(&cache_lock) {
		memcpy(thread_cache, tmp, ctx.cnt * sizeof(tmp[0]));
		thread_count_cached = ctx.cnt;
	}
}

/* ── Periodic work ──────────────────────────────────────────────────────────*/

static void memonitor_work_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(memonitor_work, memonitor_work_fn);

static void memonitor_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

#if defined(CONFIG_ZEGO_MEMONITOR_HEAP_MONITOR)
	sample_heaps();
#endif
#if defined(CONFIG_ZEGO_MEMONITOR_THREAD_MONITOR)
	sample_threads();
#endif

	struct memonitor_event ev = {
		.timestamp_ms = k_uptime_get_32(),
		.interval_ms = CONFIG_ZEGO_MEMONITOR_INTERVAL_MS,
#if defined(CONFIG_ZEGO_MEMONITOR_HEAP_MONITOR)
		.heap_count = heap_count_cached,
#else
		.heap_count = 0,
#endif
#if defined(CONFIG_ZEGO_MEMONITOR_THREAD_MONITOR)
		.thread_count = thread_count_cached,
#else
		.thread_count = 0,
#endif
	};

	int ret = zbus_chan_pub(&MEMONITOR_CHAN, &ev, K_NO_WAIT);

	if (ret < 0) {
		LOG_WRN("MEMONITOR_CHAN publish failed: %d", ret);
	}

#if defined(CONFIG_ZEGO_MEMONITOR_LOG_PERIODIC)
	/* Read directly from static caches — safe here: caches were just written,
	 * next write is after k_work_reschedule below, concurrent access is read-only. */
#if defined(CONFIG_ZEGO_MEMONITOR_HEAP_MONITOR)
	for (uint8_t i = 0; i < heap_count_cached; i++) {
		uint32_t pct =
			heap_cache[i].total
				? (uint32_t)(heap_cache[i].watermark * 100u / heap_cache[i].total)
				: 0u;
		LOG_INF("heap %-24s hwm=%5zu/%5zu (%u%%)", heap_cache[i].name,
			heap_cache[i].watermark, heap_cache[i].total, pct);
	}
#endif /* ZEGO_MEMONITOR_HEAP_MONITOR */
#if defined(CONFIG_ZEGO_MEMONITOR_THREAD_MONITOR)
	for (uint8_t i = 0; i < thread_count_cached; i++) {
		if (thread_cache[i].stack_size == 0) {
			continue;
		}
		uint32_t pct =
			(uint32_t)(thread_cache[i].stack_hwm * 100u / thread_cache[i].stack_size);
		LOG_INF("thrd %-24s hwm=%5zu/%5zu (%u%%)", thread_cache[i].name,
			thread_cache[i].stack_hwm, thread_cache[i].stack_size, pct);
	}
#endif /* ZEGO_MEMONITOR_THREAD_MONITOR */
#endif /* ZEGO_MEMONITOR_LOG_PERIODIC */

	k_work_reschedule(&memonitor_work, K_MSEC(CONFIG_ZEGO_MEMONITOR_INTERVAL_MS));
}

static int memonitor_init(void)
{
	/* First sample fires immediately; subsequent ones every INTERVAL_MS. */
	k_work_schedule(&memonitor_work, K_NO_WAIT);
	LOG_INF("memonitor started (interval=%d ms)", CONFIG_ZEGO_MEMONITOR_INTERVAL_MS);
	return 0;
}

SYS_INIT(memonitor_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

/* ── Public accessor API ────────────────────────────────────────────────────*/

#if defined(CONFIG_ZEGO_MEMONITOR_HEAP_MONITOR)
int memonitor_get_heaps(struct memonitor_heap_entry *out, uint8_t max, uint8_t *count)
{
	if (!out || !count || max == 0) {
		return -EINVAL;
	}

	K_SPINLOCK(&cache_lock) {
		uint8_t n = MIN(heap_count_cached, max);

		memcpy(out, heap_cache, n * sizeof(heap_cache[0]));
		*count = n;
	}

	return 0;
}
#endif /* CONFIG_ZEGO_MEMONITOR_HEAP_MONITOR */

#if defined(CONFIG_ZEGO_MEMONITOR_THREAD_MONITOR)
int memonitor_get_threads(struct memonitor_thread_entry *out, uint8_t max, uint8_t *count)
{
	if (!out || !count || max == 0) {
		return -EINVAL;
	}

	K_SPINLOCK(&cache_lock) {
		uint8_t n = MIN(thread_count_cached, max);

		memcpy(out, thread_cache, n * sizeof(thread_cache[0]));
		*count = n;
	}

	return 0;
}
#endif /* CONFIG_ZEGO_MEMONITOR_THREAD_MONITOR */
