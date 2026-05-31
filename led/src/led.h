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
 *       // s->led_number, s->is_on
 *   }
 *   ZBUS_LISTENER_DEFINE(my_state_lst, my_state_listener);
 *   ZBUS_CHAN_ADD_OBS(LED_STATE_CHAN, my_state_lst, 0);
 * @endcode
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
	LED_COMMAND_BREATHE, /**< Breathing pulse: short on-flash, long off (duty set by CONFIG_ZEGO_LED_BREATHE_ON_PCT). */
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
 * The LED module publishes this after every hardware state change.
 */
struct led_state_msg {
	uint8_t led_number; /**< 0-based LED index. */
	bool is_on;         /**< New LED state after the change. */
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
