/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Zego button sample — manual hardware test harness.
 *
 * Subscribes to BUTTON_CHAN and logs every gesture event with a
 * k_uptime timestamp and a one-line tuning hint so you can judge
 * whether to adjust CONFIG_ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS or
 * CONFIG_ZEGO_BUTTON_LONG_PRESS_MS without rebuilding.
 *
 * Raw BUTTON_PRESSED / BUTTON_RELEASED events are logged at DEBUG level
 * (visible when CONFIG_ZEGO_BUTTON_LOG_LEVEL_DBG=y).  Gesture events are
 * at INFO level and always visible.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "button.h"

LOG_MODULE_REGISTER(btn_sample, LOG_LEVEL_DBG);

/* --------------------------------------------------------------------------
 * Zbus listener
 * --------------------------------------------------------------------------
 */

static void on_button(const struct zbus_channel *chan)
{
	const struct button_msg *msg = zbus_chan_const_msg(chan);
	uint32_t ts = msg->timestamp;
	uint8_t btn = msg->button_number;

	switch (msg->type) {
	case BUTTON_PRESSED:
		LOG_DBG("[%7u ms] BTN%u PRESSED   (press #%u)", ts, btn, msg->press_count);
		break;

	case BUTTON_RELEASED:
		LOG_DBG("[%7u ms] BTN%u RELEASED  held=%u ms", ts, btn, msg->duration_ms);
		break;

	case BUTTON_SINGLE_CLICK:
		LOG_INF("[%7u ms] BTN%u SINGLE_CLICK  held=%u ms  count=%u", ts, btn,
			msg->duration_ms, msg->press_count);
		LOG_INF("           hint: single-click wait = %d ms — "
			"shorten with CONFIG_ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS",
			CONFIG_ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS);
		break;

	case BUTTON_DOUBLE_CLICK:
		LOG_INF("[%7u ms] BTN%u DOUBLE_CLICK  2nd-held=%u ms  count=%u", ts, btn,
			msg->duration_ms, msg->press_count);
		LOG_INF("           hint: detection window = %d ms — "
			"increase if fast double-taps are missed: "
			"CONFIG_ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS",
			CONFIG_ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS);
		break;

	case BUTTON_LONG_PRESS:
		LOG_INF("[%7u ms] BTN%u LONG_PRESS  threshold=%d ms  count=%u", ts, btn,
			CONFIG_ZEGO_BUTTON_LONG_PRESS_MS, msg->press_count);
		LOG_INF("           hint: fired after %d ms hold — "
			"adjust with CONFIG_ZEGO_BUTTON_LONG_PRESS_MS",
			CONFIG_ZEGO_BUTTON_LONG_PRESS_MS);
		break;
	}
}

ZBUS_LISTENER_DEFINE(btn_sample_listener, on_button);
ZBUS_CHAN_ADD_OBS(BUTTON_CHAN, btn_sample_listener, 0);

/* --------------------------------------------------------------------------
 * Boot banner
 * --------------------------------------------------------------------------
 */

int main(void)
{
	LOG_INF("===========================================");
	LOG_INF("  Zego Button Sample — Hardware Test");
	LOG_INF("===========================================");
	LOG_INF("  Buttons monitored    : %d  (NUM_BUTTONS)", CONFIG_ZEGO_BUTTON_NUM_BUTTONS);
	LOG_INF("  Double-click window  : %d ms  (DOUBLE_CLICK_WINDOW_MS)",
		CONFIG_ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS);
	LOG_INF("  Long-press threshold : %d ms  (LONG_PRESS_MS)",
		CONFIG_ZEGO_BUTTON_LONG_PRESS_MS);
	LOG_INF("-------------------------------------------");
	LOG_INF("  SINGLE_CLICK : press + release, then wait");
	LOG_INF("  DOUBLE_CLICK : two presses within %d ms",
		CONFIG_ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS);
	LOG_INF("  LONG_PRESS   : hold > %d ms", CONFIG_ZEGO_BUTTON_LONG_PRESS_MS);
	LOG_INF("===========================================");
	return 0;
}
