/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Zego LED sample — manual hardware test harness.
 *
 * Walks through every LED command type so you can verify behaviour on real
 * hardware.  Subscribes to LED_STATE_CHAN and logs every hardware state change
 * at DEBUG level.  Step announcements are at INFO level.
 *
 * LED index-to-silkscreen mapping:
 *   nRF7002DK    — idx 0 = LED1,  idx 1 = LED2
 *   nRF54LM20DK  — idx 0 = LED0,  idx 1 = LED1,  idx 2 = LED2,  idx 3 = LED3
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "led.h"

LOG_MODULE_REGISTER(led_sample, LOG_LEVEL_DBG);

/* --------------------------------------------------------------------------
 * LED_STATE_CHAN subscriber — echoes every hardware state change
 * -------------------------------------------------------------------------- */

static void on_led_state(const struct zbus_channel *chan)
{
	const struct led_state_msg *s = zbus_chan_const_msg(chan);

	LOG_DBG("  state: LED%d -> %s", s->led_number, s->is_on ? "ON" : "OFF");
}

ZBUS_LISTENER_DEFINE(led_sample_listener, on_led_state);
ZBUS_CHAN_ADD_OBS(LED_STATE_CHAN, led_sample_listener, 0);

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static void led_cmd(uint8_t n, enum led_msg_type type, uint16_t period_ms)
{
	struct led_msg msg = {.type = type, .led_number = n, .period_ms = period_ms};
	int ret = zbus_chan_pub(&LED_CMD_CHAN, &msg, K_NO_WAIT);

	if (ret < 0) {
		LOG_ERR("LED_CMD_CHAN pub failed: %d", ret);
	}
}

static void all_off(void)
{
	for (uint8_t i = 0; i < CONFIG_ZEGO_LED_NUM_LEDS; i++) {
		led_cmd(i, LED_COMMAND_OFF, 0);
	}
}

/* --------------------------------------------------------------------------
 * Test steps
 * -------------------------------------------------------------------------- */

static void step_static_on_off(void)
{
	LOG_INF("=== T1: Static ON/OFF (each LED) ===");
	for (uint8_t i = 0; i < CONFIG_ZEGO_LED_NUM_LEDS; i++) {
		LOG_INF("  LED%d ON", i);
		led_cmd(i, LED_COMMAND_ON, 0);
		k_sleep(K_MSEC(600));
		led_cmd(i, LED_COMMAND_OFF, 0);
		k_sleep(K_MSEC(300));
	}
}

static void step_toggle(void)
{
	LOG_INF("=== T2: TOGGLE (LED 0, 4x) ===");
	for (int i = 0; i < 4; i++) {
		led_cmd(0, LED_COMMAND_TOGGLE, 0);
		k_sleep(K_MSEC(400));
	}
	led_cmd(0, LED_COMMAND_OFF, 0);
}

static void step_blink(void)
{
	LOG_INF("=== T3: BLINK -- LED 0, half-period=%d ms ===", CONFIG_ZEGO_LED_BLINK_PERIOD_MS);
	led_cmd(0, LED_COMMAND_BLINK, 0);
	k_sleep(K_SECONDS(4));
	led_cmd(0, LED_COMMAND_OFF, 0);
	k_sleep(K_MSEC(300));
}

static void step_breathe(void)
{
	LOG_INF("=== T4: BREATHE -- LED 0, ramp=%d ms/dir, pwm=%d ms/step ===",
		CONFIG_ZEGO_LED_BREATHE_PERIOD_MS, CONFIG_ZEGO_LED_BREATHE_PWM_PERIOD_MS);
	led_cmd(0, LED_COMMAND_BREATHE, 0);
	/* One full up+down cycle = 2 * ramp period */
	k_sleep(K_MSEC((uint32_t)CONFIG_ZEGO_LED_BREATHE_PERIOD_MS * 2));
	led_cmd(0, LED_COMMAND_OFF, 0);
	k_sleep(K_MSEC(300));
}

static void step_marquee(void)
{
	LOG_INF("=== T5: MARQUEE -- all LEDs, %d ms/step ===", CONFIG_ZEGO_LED_MARQUEE_PERIOD_MS);
	led_cmd(0, LED_COMMAND_MARQUEE, 0);
	k_sleep(K_MSEC((uint32_t)CONFIG_ZEGO_LED_MARQUEE_PERIOD_MS * CONFIG_ZEGO_LED_NUM_LEDS * 3));
	all_off();
	k_sleep(K_MSEC(300));
}

/* --------------------------------------------------------------------------
 * Entry point
 * -------------------------------------------------------------------------- */

int main(void)
{
	LOG_INF("==========================================");
	LOG_INF("  Zego LED Sample -- Hardware Test");
	LOG_INF("==========================================");
	LOG_INF("  LEDs              : %d  (NUM_LEDS)", CONFIG_ZEGO_LED_NUM_LEDS);
	LOG_INF("  Blink half-period : %d ms", CONFIG_ZEGO_LED_BLINK_PERIOD_MS);
	LOG_INF("  Breathe period    : %d ms/dir  pwm=%d ms/step",
		CONFIG_ZEGO_LED_BREATHE_PERIOD_MS, CONFIG_ZEGO_LED_BREATHE_PWM_PERIOD_MS);
	LOG_INF("  Marquee step      : %d ms  (MARQUEE_PERIOD_MS)",
		CONFIG_ZEGO_LED_MARQUEE_PERIOD_MS);
	LOG_INF("------------------------------------------");
	LOG_INF("  nRF7002DK  : LED1=idx0  LED2=idx1");
	LOG_INF("  nRF54LM20DK: LED0=idx0 ... LED3=idx3");
	LOG_INF("==========================================");

	while (true) {
		step_static_on_off();
		step_toggle();
		step_blink();
		step_breathe();
		step_marquee();
		LOG_INF("--- sequence complete, restarting in 2 s ---");
		all_off();
		k_sleep(K_SECONDS(2));
	}

	return 0;
}
