/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file button.h
 * @brief Zego button module — public API and message types.
 *
 * Enable with CONFIG_ZEGO_BUTTON=y.  The module auto-initializes via SYS_INIT,
 * registers a DK button callback, and publishes BUTTON_PRESSED / BUTTON_RELEASED
 * events on BUTTON_CHAN using an SMF state machine.
 *
 * Subscribing from an application:
 * @code
 *   #include "button.h"   // path added by CMakeLists.txt include
 *
 *   static void my_listener(const struct zbus_channel *chan)
 *   {
 *       const struct button_msg *msg = zbus_chan_const_msg(chan);
 *       if (msg->type == BUTTON_PRESSED && msg->button_number == 0) { ... }
 *   }
 *   ZBUS_LISTENER_DEFINE(my_btn_listener, my_listener);
 *   ZBUS_CHAN_ADD_OBS(BUTTON_CHAN, my_btn_listener, 0);
 * @endcode
 */

#ifndef ZEGO_BUTTON_H
#define ZEGO_BUTTON_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/zbus/zbus.h>

/* ==========================================================================
 * MESSAGE TYPES
 * ==========================================================================
 */

/** @brief Button event type. */
enum button_msg_type {
	BUTTON_PRESSED,  /**< Initial press detected (after debounce). duration_ms = 0. */
	BUTTON_RELEASED, /**< Release detected. duration_ms = hold time in ms. */
};

/**
 * @brief Button event message published on BUTTON_CHAN.
 *
 * Published on every press and every release.
 * For BUTTON_PRESSED, duration_ms is 0.
 * For BUTTON_RELEASED, duration_ms is the hold time in milliseconds.
 */
struct button_msg {
	enum button_msg_type type;
	uint8_t button_number; /**< 0-based button index. */
	uint32_t duration_ms;  /**< Hold duration; 0 for BUTTON_PRESSED. */
	uint32_t press_count;  /**< Cumulative press count for this button (1-based). */
	uint32_t timestamp;    /**< k_uptime_get_32() at event time. */
};

/* ==========================================================================
 * ZBUS CHANNEL DECLARATION
 * ==========================================================================
 */

/**
 * @brief BUTTON_CHAN — output channel; subscribe to receive button events.
 *
 * The channel is defined in button.c.  External subscribers declare it here.
 */
ZBUS_CHAN_DECLARE(BUTTON_CHAN);

/* ==========================================================================
 * TEST SHIM
 * ==========================================================================
 */

#ifdef CONFIG_ZTEST
/**
 * @brief Inject a synthetic button event (test builds only).
 *
 * Simulates a raw DK callback for button @p btn_num.  Available when
 * CONFIG_ZTEST=y.  Calls the internal button handler directly; the full SMF
 * and zbus publish path still runs.
 *
 * @param btn_num  0-based button index (must be < CONFIG_ZEGO_BUTTON_NUM_BUTTONS).
 * @param pressed  true = button pressed, false = button released.
 */
void zego_button_inject(uint8_t btn_num, bool pressed);
#endif /* CONFIG_ZTEST */

#endif /* ZEGO_BUTTON_H */
