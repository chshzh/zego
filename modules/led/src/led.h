/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file led.h
 * @brief Zego LED module — public API and message types.
 *
 * Enable with CONFIG_ZEGO_LED=y.  The module auto-initializes via SYS_INIT,
 * subscribes to LED_CMD_CHAN, and drives DK LEDs using an SMF state machine.
 * LED_STATE_CHAN is published after every hardware state change.
 *
 * Commanding an LED from an application:
 * @code
 *   #include "led.h"   // path added by CMakeLists.txt include
 *
 *   struct led_msg cmd = { .type = LED_COMMAND_ON, .led_number = 0 };
 *   zbus_chan_pub(&LED_CMD_CHAN, &cmd, K_NO_WAIT);
 * @endcode
 *
 * Observing state changes:
 * @code
 *   static void my_state_listener(const struct zbus_channel *chan)
 *   {
 *       const struct led_state_msg *s = zbus_chan_const_msg(chan);
 *       // s->command tells you what caused the change:
 *       //   LED_COMMAND_ON / OFF / TOGGLE  — static state change; s->is_on is the new state.
 *       //   LED_COMMAND_BLINK / BREATHE    — effect started; no per-toggle updates follow.
 *       //   LED_COMMAND_MARQUEE            — marquee started; s->led_number = first lit LED.
 *   }
 *   ZBUS_LISTENER_DEFINE(my_state_lst, my_state_listener);
 *   ZBUS_CHAN_ADD_OBS(LED_STATE_CHAN, my_state_lst, 0);
 * @endcode
 *
 * State-channel publish policy:
 *  - Static commands (ON / OFF / TOGGLE): one publish per command.
 *  - Effect commands (BLINK / BREATHE / MARQUEE): one publish when the effect
 *    starts; no per-toggle publishes during execution.  When an effect is
 *    replaced by a static command, the static command generates its own publish.
 */

#ifndef ZEGO_LED_H
#define ZEGO_LED_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/zbus/zbus.h>

/* ==========================================================================
 * MESSAGE TYPES
 * ==========================================================================
 */

/** @brief LED command type, sent on LED_CMD_CHAN. */
enum led_msg_type {
	LED_COMMAND_ON,      /**< Turn LED on. */
	LED_COMMAND_OFF,     /**< Turn LED off. */
	LED_COMMAND_TOGGLE,  /**< Toggle LED state. */
	LED_COMMAND_BLINK,   /**< Blink: equal on/off at period_ms each. */
	LED_COMMAND_BREATHE, /**< Linear fade: ramps from 0% to 100% brightness over period_ms, then 100% back to 0% over period_ms. Full cycle = 2 × period_ms. */
	LED_COMMAND_MARQUEE, /**< Cycle all LEDs in sequence at period_ms per step. led_number ignored. */
};

/**
 * @brief LED command message, published on LED_CMD_CHAN.
 *
 * Any module may publish here to control an LED.
 */
struct led_msg {
	enum led_msg_type type;
	uint8_t led_number;  /**< 0-based LED index. Ignored for LED_COMMAND_MARQUEE. */
	uint16_t period_ms;  /**< Effect period in ms. 0 = use Kconfig default. */
};

/**
 * @brief LED state notification, published on LED_STATE_CHAN.
 *
 * Published once when a command takes effect.  During blink, breathe, or
 * marquee effects the channel is silent — only the effect-start publish is
 * sent (see module-level docstring for the full policy).
 */
struct led_state_msg {
	uint8_t led_number;        /**< 0-based LED index (for MARQUEE: first lit LED). */
	bool is_on;                /**< New state: true = on/effect running, false = off. */
	enum led_msg_type command; /**< Command that triggered this notification. */
};

/* ==========================================================================
 * ZBUS CHANNEL DECLARATIONS
 * ==========================================================================
 */

/**
 * @brief LED_CMD_CHAN — input channel; publish here to command an LED.
 *
 * Defined in led.c.
 */
ZBUS_CHAN_DECLARE(LED_CMD_CHAN);

/**
 * @brief LED_STATE_CHAN — output channel; subscribe to observe LED state changes.
 *
 * Defined in led.c.
 */
ZBUS_CHAN_DECLARE(LED_STATE_CHAN);

/* ==========================================================================
 * PUBLIC API
 * ==========================================================================
 */

/**
 * @brief Get the current state of an LED.
 *
 * Reads from the SMF context — does not query hardware.
 *
 * @param led_number  0-based LED index (must be < CONFIG_ZEGO_LED_NUM_LEDS).
 * @param[out] state  Pointer to store the LED state (true = on, false = off).
 *
 * @retval 0       Success.
 * @retval -EINVAL Invalid led_number or NULL state pointer.
 */
int led_get_state(uint8_t led_number, bool *state);

#endif /* ZEGO_LED_H */
