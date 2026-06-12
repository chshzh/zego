/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * Zephyr Input subsystem backend for the zego button module.
 *
 * Translates INPUT_EV_KEY events from the gpio-keys driver into the
 * (button_state, has_changed) bitmask format expected by button.c.
 *
 * REQUIREMENT: The DTS gpio-keys button nodes must assign consecutive
 * linux,code values starting from 0:
 *
 *   button0: button_0 { linux,code = <0>; ... };
 *   button1: button_1 { linux,code = <1>; ... };
 *   ...
 *
 * Key codes >= CONFIG_ZEGO_BUTTON_NUM_BUTTONS are silently ignored.
 * Provide a board DTS overlay in zego/button/boards/<board>.overlay to
 * set the correct codes if the upstream board DTS uses different values.
 */

#include "button_hw.h"

#include <zephyr/input/input.h>
#include <zephyr/kernel.h>

static button_hw_callback_t user_cb;
static uint32_t button_state;

static void input_event_handler(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type != INPUT_EV_KEY) {
		return;
	}
	if (evt->code >= CONFIG_ZEGO_BUTTON_NUM_BUTTONS) {
		return;
	}

	uint32_t mask = BIT(evt->code);
	uint32_t prev = button_state;

	if (evt->value) {
		button_state |= mask;
	} else {
		button_state &= ~mask;
	}

	uint32_t has_changed = prev ^ button_state;

	if (has_changed && user_cb) {
		user_cb(button_state, has_changed);
	}
}

/* Listen to all input devices (NULL = wildcard). */
INPUT_CALLBACK_DEFINE(NULL, input_event_handler, NULL);

int button_hw_init(button_hw_callback_t cb)
{
	user_cb = cb;
	button_state = 0;
	return 0;
}
