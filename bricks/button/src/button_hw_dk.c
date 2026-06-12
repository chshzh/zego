/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "button_hw.h"

#include <dk_buttons_and_leds.h>

static button_hw_callback_t user_cb;

static void dk_button_handler(uint32_t button_state, uint32_t has_changed)
{
	if (user_cb) {
		user_cb(button_state, has_changed);
	}
}

int button_hw_init(button_hw_callback_t cb)
{
	user_cb = cb;
	return dk_buttons_init(dk_button_handler);
}
