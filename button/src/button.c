/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "button.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zego_button, CONFIG_ZEGO_BUTTON_LOG_LEVEL);

#include <dk_buttons_and_leds.h>
#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/zbus/zbus.h>

#define NUM_BUTTONS CONFIG_ZEGO_BUTTON_NUM_BUTTONS

/* ============================================================================
 * ZBUS CHANNEL DEFINITION
 * ============================================================================
 */

ZBUS_CHAN_DEFINE(BUTTON_CHAN, struct button_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

/* ============================================================================
 * STATE MACHINE
 *
 * Per-button FSM with three states:
 *   IDLE → PRESSED (on press) → RELEASED (on release) → IDLE
 *
 * BUTTON_PRESSED is published in button_pressed_entry.
 * BUTTON_RELEASED is published in button_released_entry (with duration_ms).
 * ============================================================================
 */

static enum smf_state_result button_idle_run(void *obj);
static void button_pressed_entry(void *obj);
static enum smf_state_result button_pressed_run(void *obj);
static void button_released_entry(void *obj);

static const struct smf_state button_states[] = {
	[0] = SMF_CREATE_STATE(NULL, button_idle_run, NULL, NULL, NULL),
	[1] = SMF_CREATE_STATE(button_pressed_entry, button_pressed_run, NULL, NULL, NULL),
	[2] = SMF_CREATE_STATE(button_released_entry, NULL, NULL, NULL, NULL),
};

struct button_sm_object {
	struct smf_ctx ctx;
	uint8_t button_number;
	uint32_t press_count;
	int64_t press_timestamp_ms;
	bool current_state;
	bool previous_state;
};

static struct button_sm_object button_sm[NUM_BUTTONS];

/* ============================================================================
 * STATE IMPLEMENTATIONS
 * ============================================================================
 */

static enum smf_state_result button_idle_run(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	if (sm->current_state && !sm->previous_state) {
		smf_set_state(SMF_CTX(sm), &button_states[1]);
	}
	sm->previous_state = sm->current_state;
	return SMF_EVENT_HANDLED;
}

static void button_pressed_entry(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	sm->press_count++;
	sm->press_timestamp_ms = k_uptime_get();

	struct button_msg msg = {
		.type = BUTTON_PRESSED,
		.button_number = sm->button_number,
		.duration_ms = 0,
		.press_count = sm->press_count,
		.timestamp = k_uptime_get_32(),
	};

	int ret = zbus_chan_pub(&BUTTON_CHAN, &msg, K_MSEC(100));

	if (ret < 0) {
		LOG_ERR("Failed to publish BUTTON_PRESSED (btn %d): %d", sm->button_number, ret);
	} else {
		LOG_INF("Button %d pressed (count: %u)", sm->button_number, sm->press_count);
	}
}

static enum smf_state_result button_pressed_run(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	if (!sm->current_state && sm->previous_state) {
		smf_set_state(SMF_CTX(sm), &button_states[2]);
	}
	sm->previous_state = sm->current_state;
	return SMF_EVENT_HANDLED;
}

static void button_released_entry(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;
	uint32_t duration_ms = (uint32_t)(k_uptime_get() - sm->press_timestamp_ms);

	struct button_msg msg = {
		.type = BUTTON_RELEASED,
		.button_number = sm->button_number,
		.duration_ms = duration_ms,
		.press_count = sm->press_count,
		.timestamp = k_uptime_get_32(),
	};

	int ret = zbus_chan_pub(&BUTTON_CHAN, &msg, K_MSEC(100));

	if (ret < 0) {
		LOG_ERR("Failed to publish BUTTON_RELEASED (btn %d): %d", sm->button_number,
			ret);
	} else {
		LOG_INF("Button %d released (held %u ms)", sm->button_number, duration_ms);
	}

	smf_set_state(SMF_CTX(sm), &button_states[0]);
}

/* ============================================================================
 * DK BUTTON CALLBACK
 * ============================================================================
 */

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	for (int i = 0; i < NUM_BUTTONS; i++) {
		if (!(has_changed & BIT(i))) {
			continue;
		}

		button_sm[i].current_state = (button_state & BIT(i)) != 0;

		int ret = smf_run_state(SMF_CTX(&button_sm[i]));

		if (ret < 0) {
			LOG_ERR("Button %d SM error: %d", i, ret);
		}
	}
}

/* ============================================================================
 * TEST SHIM (ztest / native_sim builds only)
 * ============================================================================
 */

#ifdef CONFIG_ZTEST
void zego_button_inject(uint8_t btn_num, bool pressed)
{
	if (btn_num >= NUM_BUTTONS) {
		LOG_WRN("zego_button_inject: btn_num %d out of range (max %d)", btn_num,
			NUM_BUTTONS - 1);
		return;
	}

	uint32_t button_state = pressed ? BIT(btn_num) : 0U;
	uint32_t has_changed = BIT(btn_num);

	button_handler(button_state, has_changed);
}
#endif /* CONFIG_ZTEST */

/* ============================================================================
 * MODULE INITIALIZATION
 * ============================================================================
 */

static int button_module_init(void)
{
	int ret;

	LOG_INF("Initializing zego_button (%d buttons)", NUM_BUTTONS);

	ret = dk_buttons_init(button_handler);
	if (ret) {
		LOG_ERR("dk_buttons_init failed: %d", ret);
		return ret;
	}

	for (int i = 0; i < NUM_BUTTONS; i++) {
		button_sm[i].button_number = (uint8_t)i;
		button_sm[i].press_count = 0;
		button_sm[i].press_timestamp_ms = 0;
		button_sm[i].current_state = false;
		button_sm[i].previous_state = false;
		smf_set_initial(SMF_CTX(&button_sm[i]), &button_states[0]);
	}

	LOG_INF("zego_button initialized");
	return 0;
}

SYS_INIT(button_module_init, APPLICATION, CONFIG_ZEGO_BUTTON_INIT_PRIORITY);
