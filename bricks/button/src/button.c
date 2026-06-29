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
 * Per-button FSM with five states:
 *
 *   IDLE → PRESSED (on press)
 *   PRESSED → CLICK_WAIT (on release, before long-press timer fires)
 *   PRESSED → LONG_PRESS_HELD (on long-press timer expiry)
 *   CLICK_WAIT → PRESSED2 (on 2nd press within double-click window)
 *   CLICK_WAIT → IDLE (double-click timer fires → publish BUTTON_SINGLE_CLICK)
 *   PRESSED2 → IDLE (on release → publish BUTTON_DOUBLE_CLICK)
 *   LONG_PRESS_HELD → IDLE (on release)
 *
 * Timer events are delivered by k_work_delayable callbacks that set a flag
 * and call smf_run_state(); all execution is on the system work queue so no
 * additional locking is needed.
 * ============================================================================
 */

#define BTN_S_IDLE       0
#define BTN_S_PRESSED    1
#define BTN_S_CLICK_WAIT 2
#define BTN_S_PRESSED2   3
#define BTN_S_LONG_PRESS 4

static enum smf_state_result idle_run(void *obj);
static void pressed_entry(void *obj);
static enum smf_state_result pressed_run(void *obj);
static void pressed_exit(void *obj);
static void click_wait_entry(void *obj);
static enum smf_state_result click_wait_run(void *obj);
static void pressed2_entry(void *obj);
static enum smf_state_result pressed2_run(void *obj);
static void long_press_entry(void *obj);
static enum smf_state_result long_press_run(void *obj);

static const struct smf_state button_states[] = {
	[BTN_S_IDLE] = SMF_CREATE_STATE(NULL, idle_run, NULL, NULL, NULL),
	[BTN_S_PRESSED] = SMF_CREATE_STATE(pressed_entry, pressed_run, pressed_exit, NULL, NULL),
	[BTN_S_CLICK_WAIT] = SMF_CREATE_STATE(click_wait_entry, click_wait_run, NULL, NULL, NULL),
	[BTN_S_PRESSED2] = SMF_CREATE_STATE(pressed2_entry, pressed2_run, NULL, NULL, NULL),
	[BTN_S_LONG_PRESS] = SMF_CREATE_STATE(long_press_entry, long_press_run, NULL, NULL, NULL),
};

struct button_sm_object {
	struct smf_ctx ctx;
	uint8_t button_number;
	uint32_t press_count;
	int64_t press_timestamp_ms;   /**< Time of most recent press. */
	int64_t release_timestamp_ms; /**< Time of most recent release (set in pressed_exit). */
	bool current_state;
	bool previous_state;
	bool long_press_fired; /**< Set by long_press_work, checked in pressed_run. */
	bool click_timeout;    /**< Set by double_click_work, checked in click_wait_run. */
	struct k_work_delayable long_press_work;
	struct k_work_delayable double_click_work;
};

static struct button_sm_object button_sm[NUM_BUTTONS];

static const char *const button_names[] = {
	CONFIG_ZEGO_BUTTON_NAME_0, CONFIG_ZEGO_BUTTON_NAME_1, CONFIG_ZEGO_BUTTON_NAME_2,
	CONFIG_ZEGO_BUTTON_NAME_3, CONFIG_ZEGO_BUTTON_NAME_4,
};

BUILD_ASSERT(ARRAY_SIZE(button_names) >= NUM_BUTTONS,
	     "button_names array must cover all configured buttons");

/* ============================================================================
 * HELPER
 * ============================================================================
 */

static void publish_event(struct button_sm_object *sm, enum button_msg_type type,
			  uint32_t duration_ms)
{
	struct button_msg msg = {
		.type = type,
		.button_number = sm->button_number,
		.duration_ms = duration_ms,
		.press_count = sm->press_count,
		.timestamp = k_uptime_get_32(),
	};

	int ret = zbus_chan_pub(&BUTTON_CHAN, &msg, K_MSEC(100));

	if (ret < 0) {
		LOG_ERR("Failed to publish btn event %d (btn %d): %d", (int)type, sm->button_number,
			ret);
	}
}

/* ============================================================================
 * TIMER CALLBACKS
 * ============================================================================
 */

static void long_press_work_fn(struct k_work *work)
{
	struct button_sm_object *sm = CONTAINER_OF(k_work_delayable_from_work(work),
						   struct button_sm_object, long_press_work);

	sm->long_press_fired = true;
	int ret = smf_run_state(SMF_CTX(sm));

	if (ret < 0) {
		LOG_ERR("Button %d long-press timer SM error: %d", sm->button_number, ret);
	}
}

static void double_click_work_fn(struct k_work *work)
{
	struct button_sm_object *sm = CONTAINER_OF(k_work_delayable_from_work(work),
						   struct button_sm_object, double_click_work);

	sm->click_timeout = true;
	int ret = smf_run_state(SMF_CTX(sm));

	if (ret < 0) {
		LOG_ERR("Button %d double-click timer SM error: %d", sm->button_number, ret);
	}
}

/* ============================================================================
 * STATE IMPLEMENTATIONS
 * ============================================================================
 */

static enum smf_state_result idle_run(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	if (sm->current_state && !sm->previous_state) {
		smf_set_state(SMF_CTX(sm), &button_states[BTN_S_PRESSED]);
	}
	sm->previous_state = sm->current_state;
	return SMF_EVENT_HANDLED;
}

static void pressed_entry(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	sm->press_count++;
	sm->press_timestamp_ms = k_uptime_get();
	k_work_schedule(&sm->long_press_work, K_MSEC(CONFIG_ZEGO_BUTTON_LONG_PRESS_MS));
	publish_event(sm, BUTTON_PRESSED, 0);
	LOG_DBG("Button %d press #%u", sm->button_number, sm->press_count);
}

static enum smf_state_result pressed_run(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	if (sm->long_press_fired) {
		sm->long_press_fired = false;
		smf_set_state(SMF_CTX(sm), &button_states[BTN_S_LONG_PRESS]);
	} else if (!sm->current_state && sm->previous_state) {
		uint32_t duration_ms = (uint32_t)(k_uptime_get() - sm->press_timestamp_ms);

		publish_event(sm, BUTTON_RELEASED, duration_ms);
		smf_set_state(SMF_CTX(sm), &button_states[BTN_S_CLICK_WAIT]);
	}
	sm->previous_state = sm->current_state;
	return SMF_EVENT_HANDLED;
}

static void pressed_exit(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	k_work_cancel_delayable(&sm->long_press_work);
	sm->long_press_fired = false;
	sm->release_timestamp_ms = k_uptime_get();
}

static void click_wait_entry(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	k_work_schedule(&sm->double_click_work, K_MSEC(CONFIG_ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS));
}

static enum smf_state_result click_wait_run(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	if (sm->click_timeout) {
		sm->click_timeout = false;
		uint32_t duration_ms =
			(uint32_t)(sm->release_timestamp_ms - sm->press_timestamp_ms);
		publish_event(sm, BUTTON_SINGLE_CLICK, duration_ms);
		LOG_INF("%s single click (%u ms)", button_names[sm->button_number], duration_ms);
		smf_set_state(SMF_CTX(sm), &button_states[BTN_S_IDLE]);
	} else if (sm->current_state && !sm->previous_state) {
		k_work_cancel_delayable(&sm->double_click_work);
		smf_set_state(SMF_CTX(sm), &button_states[BTN_S_PRESSED2]);
	}
	sm->previous_state = sm->current_state;
	return SMF_EVENT_HANDLED;
}

static void pressed2_entry(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	sm->press_count++;
	sm->press_timestamp_ms = k_uptime_get();
	publish_event(sm, BUTTON_PRESSED, 0);
	LOG_DBG("Button %d 2nd press #%u", sm->button_number, sm->press_count);
}

static enum smf_state_result pressed2_run(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	if (!sm->current_state && sm->previous_state) {
		uint32_t duration_ms = (uint32_t)(k_uptime_get() - sm->press_timestamp_ms);

		publish_event(sm, BUTTON_RELEASED, duration_ms);
		publish_event(sm, BUTTON_DOUBLE_CLICK, duration_ms);
		LOG_INF("%s double click", button_names[sm->button_number]);
		smf_set_state(SMF_CTX(sm), &button_states[BTN_S_IDLE]);
	}
	sm->previous_state = sm->current_state;
	return SMF_EVENT_HANDLED;
}

static void long_press_entry(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	publish_event(sm, BUTTON_LONG_PRESS, CONFIG_ZEGO_BUTTON_LONG_PRESS_MS);
	LOG_INF("%s long press", button_names[sm->button_number]);
}

static enum smf_state_result long_press_run(void *obj)
{
	struct button_sm_object *sm = (struct button_sm_object *)obj;

	if (!sm->current_state && sm->previous_state) {
		publish_event(sm, BUTTON_RELEASED,
			      (uint32_t)(k_uptime_get() - sm->press_timestamp_ms));
		smf_set_state(SMF_CTX(sm), &button_states[BTN_S_IDLE]);
	}
	sm->previous_state = sm->current_state;
	return SMF_EVENT_HANDLED;
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

void zego_button_inject_long_press_timer(uint8_t btn_num)
{
	if (btn_num >= NUM_BUTTONS) {
		LOG_WRN("zego_button_inject_long_press_timer: btn_num %d out of range", btn_num);
		return;
	}
	k_work_cancel_delayable(&button_sm[btn_num].long_press_work);
	long_press_work_fn(&button_sm[btn_num].long_press_work.work);
}

void zego_button_inject_double_click_timer(uint8_t btn_num)
{
	if (btn_num >= NUM_BUTTONS) {
		LOG_WRN("zego_button_inject_double_click_timer: btn_num %d out of range", btn_num);
		return;
	}
	k_work_cancel_delayable(&button_sm[btn_num].double_click_work);
	double_click_work_fn(&button_sm[btn_num].double_click_work.work);
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
		button_sm[i].release_timestamp_ms = 0;
		button_sm[i].current_state = false;
		button_sm[i].previous_state = false;
		button_sm[i].long_press_fired = false;
		button_sm[i].click_timeout = false;
		k_work_init_delayable(&button_sm[i].long_press_work, long_press_work_fn);
		k_work_init_delayable(&button_sm[i].double_click_work, double_click_work_fn);
		smf_set_initial(SMF_CTX(&button_sm[i]), &button_states[BTN_S_IDLE]);
	}

	LOG_INF("zego_button initialized");
	return 0;
}

SYS_INIT(button_module_init, APPLICATION, CONFIG_ZEGO_BUTTON_INIT_PRIORITY);
