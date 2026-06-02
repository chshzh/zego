/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "led_hw.h"

#include <dk_buttons_and_leds.h>

int led_hw_init(void)
{
	return dk_leds_init();
}

void led_hw_set(uint8_t led_number, bool on)
{
	if (on) {
		dk_set_led_on(led_number);
	} else {
		dk_set_led_off(led_number);
	}
}

bool led_hw_has_brightness(uint8_t led_number)
{
	ARG_UNUSED(led_number);
	return false;
}

void led_hw_set_brightness(uint8_t led_number, uint8_t percent)
{
	ARG_UNUSED(led_number);
	ARG_UNUSED(percent);
}
