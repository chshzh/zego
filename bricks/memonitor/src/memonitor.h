/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file memonitor.h
 * @brief Public API for the memory and thread monitor module.
 *
 * memonitor periodically samples all k_heap instances and Zephyr thread
 * stack watermarks, caches the results, and publishes MEMONITOR_CHAN so
 * subscribers know when fresh data is available.
 *
 * Consumers call memonitor_get_heaps() / memonitor_get_threads() to obtain
 * a point-in-time copy of the latest snapshot — safe to call from any
 * context including HTTP handler threads.
 */

#ifndef MEMONITOR_H
#define MEMONITOR_H

#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <zephyr/zbus/zbus.h>

/** Maximum number of k_heap entries tracked. */
#define MEMONITOR_MAX_HEAPS   8
/** Maximum number of threads tracked. */
#define MEMONITOR_MAX_THREADS 32

#define MEMONITOR_HEAP_NAME_LEN   32
#define MEMONITOR_THREAD_NAME_LEN 32
#define MEMONITOR_STATE_LEN       12

/** Runtime snapshot of a single k_heap. */
struct memonitor_heap_entry {
	char   name[MEMONITOR_HEAP_NAME_LEN];
	size_t free;
	size_t used;
	size_t watermark;
	size_t total;
};

/** Runtime snapshot of a single Zephyr thread. */
struct memonitor_thread_entry {
	char   name[MEMONITOR_THREAD_NAME_LEN];
	char   state[MEMONITOR_STATE_LEN]; /**< "ready", "pending", "suspended", "dead" */
	size_t stack_hwm;
	size_t stack_size;
};

/** Published on MEMONITOR_CHAN after each sampling cycle. */
struct memonitor_event {
	uint32_t timestamp_ms;   /**< k_uptime_get_32() at sample time */
	uint32_t interval_ms;    /**< CONFIG_ZEGO_MEMONITOR_INTERVAL_MS */
	uint8_t  heap_count;     /**< Number of valid heap entries in cache */
	uint8_t  thread_count;   /**< Number of valid thread entries in cache */
};

/**
 * @brief Zbus channel published every CONFIG_ZEGO_MEMONITOR_INTERVAL_MS.
 *
 * Subscribe to this channel to be notified when a new snapshot is ready.
 * Retrieve the actual data via memonitor_get_heaps() / memonitor_get_threads().
 */
ZBUS_CHAN_DECLARE(MEMONITOR_CHAN);

/**
 * @brief Copy the latest heap snapshot into a caller-provided array.
 *
 * Thread-safe. Returns a consistent point-in-time copy; the data will not
 * change under the caller after this function returns.
 * Returns -ENOTSUP if CONFIG_ZEGO_MEMONITOR_HEAP_MONITOR is not enabled.
 *
 * @param out   Destination array with at least @p max entries.
 * @param max   Maximum number of entries to copy.
 * @param count Set to the actual number of entries written (≤ max).
 * @return 0 on success, -EINVAL on bad arguments, -ENOTSUP if feature disabled.
 */
#if defined(CONFIG_ZEGO_MEMONITOR_HEAP_MONITOR)
int memonitor_get_heaps(struct memonitor_heap_entry *out, uint8_t max, uint8_t *count);
#else
static inline int memonitor_get_heaps(struct memonitor_heap_entry *out, uint8_t max,
				      uint8_t *count)
{
	ARG_UNUSED(out);
	ARG_UNUSED(max);
	if (count) {
		*count = 0;
	}
	return -ENOTSUP;
}
#endif

/**
 * @brief Return the configured sampling interval in milliseconds.
 */
static inline uint32_t memonitor_get_interval_ms(void)
{
	return CONFIG_ZEGO_MEMONITOR_INTERVAL_MS;
}

/**
 * @brief Copy the latest thread snapshot into a caller-provided array.
 *
 * Thread-safe. Same semantics as memonitor_get_heaps().
 * Returns -ENOTSUP if CONFIG_ZEGO_MEMONITOR_THREAD_MONITOR is not enabled.
 *
 * @param out   Destination array with at least @p max entries.
 * @param max   Maximum number of entries to copy.
 * @param count Set to the actual number of entries written (≤ max).
 * @return 0 on success, -EINVAL on bad arguments, -ENOTSUP if feature disabled.
 */
#if defined(CONFIG_ZEGO_MEMONITOR_THREAD_MONITOR)
int memonitor_get_threads(struct memonitor_thread_entry *out, uint8_t max, uint8_t *count);
#else
static inline int memonitor_get_threads(struct memonitor_thread_entry *out, uint8_t max,
					uint8_t *count)
{
	ARG_UNUSED(out);
	ARG_UNUSED(max);
	if (count) {
		*count = 0;
	}
	return -ENOTSUP;
}
#endif

#endif /* MEMONITOR_H */
