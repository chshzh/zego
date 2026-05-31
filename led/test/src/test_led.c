/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Ztest unit tests for the zego LED module.
 *
 * Strategy
 * --------
 * The LED module subscribes to LED_CMD_CHAN and publishes LED_STATE_CHAN.
 * Tests drive the module by publishing to LED_CMD_CHAN, then verify through:
 *   1. led_get_state() — reads the SMF internal state (not hardware).
 *   2. LED_STATE_CHAN  — captured by a test listener.
 *
 * The LED module uses zbus LISTENERS (synchronous callbacks), so by the time
 * zbus_chan_pub() returns, the SMF has already run and LED_STATE_CHAN has been
 * published.  No waits or polling needed.
 *
 * dk_set_led_on/off are no-op stubs on native_sim — that is intentional.
 * We verify behavior through the module's exported state, not hardware pins.
 */

#include <zephyr/ztest.h>
#include "led.h"
#include <zephyr/zbus/zbus.h>

/* -----------------------------------------------------------------------
 * Test subscriber for LED_STATE_CHAN
 * ----------------------------------------------------------------------- */

static struct led_state_msg g_last_state;
static bool g_state_received;

static void capture_state_listener(const struct zbus_channel *chan)
{
	g_last_state = *(const struct led_state_msg *)zbus_chan_const_msg(chan);
	g_state_received = true;
}

ZBUS_LISTENER_DEFINE(test_led_state_capture, capture_state_listener);
ZBUS_CHAN_ADD_OBS(LED_STATE_CHAN, test_led_state_capture, 0);

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

static void reset_state_capture(void)
{
	g_state_received = false;
	g_last_state.is_on = false;
	g_last_state.led_number = 0xFF;
}

static void cmd(uint8_t led_number, enum led_msg_type type)
{
	struct led_msg msg = {
		.type = type,
		.led_number = led_number,
	};
	int ret = zbus_chan_pub(&LED_CMD_CHAN, &msg, K_NO_WAIT);

	zassert_ok(ret, "zbus_chan_pub(LED_CMD_CHAN) failed: %d", ret);
}

/* -----------------------------------------------------------------------
 * Tests
 * ----------------------------------------------------------------------- */

/** LED_COMMAND_ON must turn the LED on. */
ZTEST(led_suite, test_led_on)
{
	/* Ensure LED starts off */
	cmd(0, LED_COMMAND_OFF);

	reset_state_capture();
	cmd(0, LED_COMMAND_ON);

	bool state;
	int ret = led_get_state(0, &state);

	zassert_ok(ret, "led_get_state failed");
	zassert_true(state, "LED 0 should be ON after LED_COMMAND_ON");

	zassert_true(g_state_received, "Expected notification on LED_STATE_CHAN");
	zassert_equal(g_last_state.led_number, 0);
	zassert_true(g_last_state.is_on);
}

/** LED_COMMAND_OFF must turn the LED off. */
ZTEST(led_suite, test_led_off)
{
	cmd(0, LED_COMMAND_ON); /* known on */

	reset_state_capture();
	cmd(0, LED_COMMAND_OFF);

	bool state;
	int ret = led_get_state(0, &state);

	zassert_ok(ret, "led_get_state failed");
	zassert_false(state, "LED 0 should be OFF after LED_COMMAND_OFF");

	zassert_true(g_state_received, "Expected notification on LED_STATE_CHAN");
	zassert_false(g_last_state.is_on);
}

/** LED_COMMAND_TOGGLE must invert the current state. */
ZTEST(led_suite, test_led_toggle_off_to_on)
{
	cmd(0, LED_COMMAND_OFF); /* start off */

	cmd(0, LED_COMMAND_TOGGLE);

	bool state;

	led_get_state(0, &state);
	zassert_true(state, "Toggle from OFF should result in ON");
}

/** LED_COMMAND_TOGGLE must also toggle from on to off. */
ZTEST(led_suite, test_led_toggle_on_to_off)
{
	cmd(0, LED_COMMAND_ON); /* start on */

	cmd(0, LED_COMMAND_TOGGLE);

	bool state;

	led_get_state(0, &state);
	zassert_false(state, "Toggle from ON should result in OFF");
}

/** LED_COMMAND_ON when already on should leave LED on (idempotent). */
ZTEST(led_suite, test_led_on_idempotent)
{
	cmd(0, LED_COMMAND_ON);
	cmd(0, LED_COMMAND_ON); /* second ON */

	bool state;

	led_get_state(0, &state);
	zassert_true(state, "LED should remain ON after two consecutive ON commands");
}

/** Multiple LEDs must be independent. */
ZTEST(led_suite, test_multiple_leds_independent)
{
	cmd(0, LED_COMMAND_OFF);
	cmd(1, LED_COMMAND_ON);

	bool state0, state1;

	led_get_state(0, &state0);
	led_get_state(1, &state1);

	zassert_false(state0, "LED 0 should be OFF");
	zassert_true(state1, "LED 1 should be ON");

	/* Clean up */
	cmd(1, LED_COMMAND_OFF);
}

/** led_get_state with invalid LED number must return -EINVAL. */
ZTEST(led_suite, test_get_state_invalid_led)
{
	bool state;
	int ret = led_get_state(CONFIG_ZEGO_LED_NUM_LEDS, &state);

	zassert_equal(ret, -EINVAL, "Expected -EINVAL for out-of-range LED index");
}

/** led_get_state with NULL pointer must return -EINVAL. */
ZTEST(led_suite, test_get_state_null_ptr)
{
	int ret = led_get_state(0, NULL);

	zassert_equal(ret, -EINVAL, "Expected -EINVAL for NULL state pointer");
}

ZTEST_SUITE(led_suite, NULL, NULL, NULL, NULL, NULL);
