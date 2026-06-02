/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file button.h
 * @brief Zego button module — public API and message types.
 *
 * Enable with CONFIG_ZEGO_BUTTON=y.  The module auto-initializes via SYS_INIT,
 * registers a DK button callback, and classifies physical presses into three
 * gesture events published on BUTTON_CHAN (zbus):
 *
 *  - BUTTON_SINGLE_CLICK  — confirmed single press (no 2nd press within
 *                            CONFIG_ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS).
 *  - BUTTON_DOUBLE_CLICK  — two presses within the double-click window.
 *  - BUTTON_LONG_PRESS    — press held >= CONFIG_ZEGO_BUTTON_LONG_PRESS_MS.
 *                            Published while the button is still held.
 *
 * Subscribing from an application:
 * @code
 *   #include "button.h"   // path added by CMakeLists.txt include
 *
 *   static void my_listener(const struct zbus_channel *chan)
 *   {
 *       const struct button_msg *msg = zbus_chan_const_msg(chan);
 *       if (msg->type == BUTTON_SINGLE_CLICK && msg->button_number == 0) { ... }
 *       if (msg->type == BUTTON_DOUBLE_CLICK && msg->button_number == 0) { ... }
 *       if (msg->type == BUTTON_LONG_PRESS   && msg->button_number == 0) { ... }
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
	/** Raw press event. Published immediately when the button is pressed.
	 *  duration_ms = 0. */
	BUTTON_PRESSED,
	/** Raw release event. Published immediately when the button is released.
	 *  duration_ms = hold time in ms. */
	BUTTON_RELEASED,
	/** Confirmed single click (after double-click window expired).
	 *  duration_ms = hold time of the press in ms. */
	BUTTON_SINGLE_CLICK,
	/** Double click: two presses within DOUBLE_CLICK_WINDOW_MS.
	 *  duration_ms = hold time of the 2nd press in ms.
	 *
	 *  Note: holding the 2nd press does not trigger BUTTON_LONG_PRESS.
	 *  The PRESSED2 state only waits for release; it does not start a
	 *  long-press timer.  This is intentional — double-click-then-hold
	 *  is not a distinct gesture in this module's UX model. */
	BUTTON_DOUBLE_CLICK,
	/** Long press: button held >= LONG_PRESS_MS. Published while still held.
	 *  duration_ms = CONFIG_ZEGO_BUTTON_LONG_PRESS_MS. */
	BUTTON_LONG_PRESS,
};

/**
 * @brief Button gesture message published on BUTTON_CHAN.
 *
 * One message per detected gesture. See button_msg_type for duration_ms
 * semantics per event type.
 */
struct button_msg {
	enum button_msg_type type;
	uint8_t button_number; /**< 0-based button index. */
	uint32_t duration_ms;  /**< Hold time; semantics depend on type. */
	uint32_t press_count;  /**< Cumulative physical-press count for this button. */
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
 * @brief Inject a synthetic button press/release (test builds only).
 *
 * Simulates a raw DK callback. The full SMF and zbus publish path runs.
 *
 * @param btn_num  0-based button index (silently ignored if out of range).
 * @param pressed  true = press, false = release.
 */
void zego_button_inject(uint8_t btn_num, bool pressed);

/**
 * @brief Force the long-press timer to fire immediately (test builds only).
 *
 * Cancels the pending delayable work and invokes the callback synchronously.
 * Call while the button is still "pressed" to trigger BUTTON_LONG_PRESS
 * without waiting for the real timer.
 *
 * @param btn_num  0-based button index (silently ignored if out of range).
 */
void zego_button_inject_long_press_timer(uint8_t btn_num);

/**
 * @brief Force the double-click window timer to fire immediately (test builds only).
 *
 * Cancels the pending delayable work and invokes the callback synchronously.
 * Call after a press+release (in CLICK_WAIT state) to confirm a
 * BUTTON_SINGLE_CLICK without waiting for the real timer.
 *
 * @param btn_num  0-based button index (silently ignored if out of range).
 */
void zego_button_inject_double_click_timer(uint8_t btn_num);

#if CONFIG_ZEGO_BUTTON_LONG_PRESS_REPEAT_MS > 0
/**
 * @brief Force the long-press repeat timer to fire immediately (test builds only).
 *
 * Only available when CONFIG_ZEGO_BUTTON_LONG_PRESS_REPEAT_MS > 0.
 * Cancels the pending repeat work and invokes the callback synchronously.
 * Call while the button is still "held" (in LONG_PRESS state).
 *
 * @param btn_num  0-based button index (silently ignored if out of range).
 */
void zego_button_inject_long_press_repeat_timer(uint8_t btn_num);
#endif /* CONFIG_ZEGO_BUTTON_LONG_PRESS_REPEAT_MS > 0 */

#endif /* CONFIG_ZTEST */

#endif /* ZEGO_BUTTON_H */
