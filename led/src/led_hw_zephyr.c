/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * Zephyr LED driver backend for the zego LED module.
 *
 * On/off control uses the generic gpio-leds device (led_on / led_off from
 * <zephyr/drivers/led.h>).
 *
 * When CONFIG_ZEGO_LED_USE_PWM=y, brightness control is available for LEDs
 * whose pwm-leds device index is configured via CONFIG_ZEGO_LED_n_PWM_INDEX.
 * Set the value to the child index within the board's pwm-leds node, or -1
 * for LEDs that have no PWM channel.  LEDs with index == -1 fall back to
 * software PWM in led.c.
 *
 * Board examples (nrf7002dk: LED0 has pwm index 0):
 *   CONFIG_ZEGO_LED_USE_PWM=y
 *   CONFIG_ZEGO_LED_0_PWM_INDEX=0    # pwm_led_0 in pwm-leds
 *   CONFIG_ZEGO_LED_1_PWM_INDEX=-1   # GPIO only
 *
 * Board examples (nrf54lm20dk: only LED1 has PWM, at pwm-leds index 0):
 *   CONFIG_ZEGO_LED_USE_PWM=y
 *   CONFIG_ZEGO_LED_1_PWM_INDEX=0    # pwm_led_1 in pwm-leds (index 0)
 */

#include "led_hw.h"

#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zego_led, CONFIG_ZEGO_LED_LOG_LEVEL);

/* GPIO LED device — provides led_on() / led_off() */
static const struct device *const gpio_leds_dev = DEVICE_DT_GET_ANY(gpio_leds);

#if defined(CONFIG_ZEGO_LED_USE_PWM)
/* PWM LED device — provides led_set_brightness() */
static const struct device *const pwm_leds_dev = DEVICE_DT_GET_ANY(pwm_leds);

/* Per-LED index into the pwm-leds device (-1 = no PWM). */
static const int8_t pwm_idx_map[4] = {
	[0] = CONFIG_ZEGO_LED_0_PWM_INDEX,
	[1] = CONFIG_ZEGO_LED_1_PWM_INDEX,
	[2] = CONFIG_ZEGO_LED_2_PWM_INDEX,
	[3] = CONFIG_ZEGO_LED_3_PWM_INDEX,
};
#endif /* CONFIG_ZEGO_LED_USE_PWM */

/* ============================================================================
 * PUBLIC API
 * ============================================================================
 */

int led_hw_init(void)
{
	if (!device_is_ready(gpio_leds_dev)) {
		LOG_ERR("gpio-leds device not ready");
		return -ENODEV;
	}

#if defined(CONFIG_ZEGO_LED_USE_PWM)
	if (!device_is_ready(pwm_leds_dev)) {
		LOG_ERR("pwm-leds device not ready");
		return -ENODEV;
	}
#endif

	return 0;
}

void led_hw_set(uint8_t led_number, bool on)
{
	int ret = on ? led_on(gpio_leds_dev, led_number) : led_off(gpio_leds_dev, led_number);

	if (ret < 0) {
		LOG_ERR("led_hw_set(%u, %d) failed: %d", (unsigned)led_number, (int)on, ret);
	}
}

bool led_hw_has_brightness(uint8_t led_number)
{
#if defined(CONFIG_ZEGO_LED_USE_PWM)
	if (led_number >= 4) {
		return false;
	}
	return pwm_idx_map[led_number] >= 0;
#else
	ARG_UNUSED(led_number);
	return false;
#endif
}

void led_hw_set_brightness(uint8_t led_number, uint8_t percent)
{
#if defined(CONFIG_ZEGO_LED_USE_PWM)
	if (led_number >= 4 || pwm_idx_map[led_number] < 0) {
		return;
	}
	int ret = led_set_brightness(pwm_leds_dev, (uint32_t)pwm_idx_map[led_number], percent);

	if (ret < 0) {
		LOG_ERR("led_set_brightness(led=%u, %u%%) failed: %d", (unsigned)led_number,
			(unsigned)percent, ret);
	}
#else
	ARG_UNUSED(led_number);
	ARG_UNUSED(percent);
#endif
}
