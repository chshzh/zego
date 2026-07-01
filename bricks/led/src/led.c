/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "led.h"
#include "led_hw.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zego_led, CONFIG_ZEGO_LED_LOG_LEVEL);

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/zbus/zbus.h>

#define NUM_LEDS        CONFIG_ZEGO_LED_NUM_LEDS
#define ROTATE_NUM_LEDS CONFIG_ZEGO_LED_ROTATE_NUM_LEDS

/* ============================================================================
 * ZBUS CHANNEL DEFINITIONS
 * ============================================================================
 */

/* Input:  publish here to command an LED */
ZBUS_CHAN_DEFINE(LED_CMD_CHAN, struct led_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));

/* Output: subscribe here to observe LED state changes */
ZBUS_CHAN_DEFINE(LED_STATE_CHAN, struct led_state_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

/* ============================================================================
 * PER-LED STATE MACHINE — four proper SMF states: OFF, ON, BLINK, BREATHE
 *
 * All four states are Zephyr SMF states with entry/run/exit actions.
 * BLINK and BREATHE no longer bypass the SMF — they use it.
 *
 * Events delivered to the run handler via sm->event:
 *   LED_EVENT_CMD   a new command arrived (sm->cmd holds the message)
 *   LED_EVENT_TICK  the per-LED effect_work timer fired
 *
 * Thread safety:
 *   All smf_run_state() calls happen on the system workqueue:
 *   - led_cmd_work_fn (CMD events) runs on sysworkq
 *   - effect_work_fn  (TICK events) runs on sysworkq
 *   Therefore k_work_cancel_delayable (in exit actions) and
 *   k_work_schedule (in entry/run actions) are always on the same thread —
 *   no timeout dlist race, no BUS FAULTs.
 * ============================================================================
 */

enum led_event {
	LED_EVENT_NONE,
	LED_EVENT_CMD,
	LED_EVENT_TICK,
};

static void s_off_entry(void *obj);
static enum smf_state_result s_off_run(void *obj);
static void s_on_entry(void *obj);
static enum smf_state_result s_on_run(void *obj);
static void s_blink_entry(void *obj);
static enum smf_state_result s_blink_run(void *obj);
static void s_blink_exit(void *obj);
static void s_breathe_entry(void *obj);
static enum smf_state_result s_breathe_run(void *obj);
static void s_breathe_exit(void *obj);

enum led_state_idx {
	LED_S_OFF = 0,
	LED_S_ON,
	LED_S_BLINK,
	LED_S_BREATHE,
};

static const struct smf_state led_states[] = {
	[LED_S_OFF] = SMF_CREATE_STATE(s_off_entry, s_off_run, NULL, NULL, NULL),
	[LED_S_ON] = SMF_CREATE_STATE(s_on_entry, s_on_run, NULL, NULL, NULL),
	[LED_S_BLINK] = SMF_CREATE_STATE(s_blink_entry, s_blink_run, s_blink_exit, NULL, NULL),
	[LED_S_BREATHE] =
		SMF_CREATE_STATE(s_breathe_entry, s_breathe_run, s_breathe_exit, NULL, NULL),
};

struct led_sm_object {
	struct smf_ctx ctx;
	uint8_t led_number;
	bool is_on;
	enum led_event event;
	struct led_msg cmd; /* valid when event == LED_EVENT_CMD */
	/* Effect parameters — written in entry, read in run / effect_work_fn */
	uint16_t effect_period_ms;
	/* Breathe state */
	uint16_t breathe_step;
	uint16_t breathe_total_steps;
	bool breathe_ramp_up;
	bool breathe_in_on_phase;
	/* Per-LED effect timer */
	struct k_work_delayable effect_work;
};

static struct led_sm_object led_sm[NUM_LEDS];

/* ============================================================================
 * ROTATE — module-level global effect
 * ============================================================================
 */

static struct {
	struct k_work_delayable work;
	uint16_t period_ms;
	uint8_t current; /* index into indices[], not a raw LED number */
	bool active;
	uint8_t count;
	uint8_t indices[NUM_LEDS];
} rotate;

/* ============================================================================
 * HELPERS
 * ============================================================================
 */

static void publish_state(uint8_t n, bool on, enum led_msg_type cmd)
{
	struct led_state_msg msg = {.led_number = n, .is_on = on, .command = cmd};
	int ret = zbus_chan_pub(&LED_STATE_CHAN, &msg, K_NO_WAIT);

	if (ret < 0) {
		LOG_ERR("Failed to publish LED_STATE_CHAN (led %d): %d", n, ret);
	}
}

static void breathe_advance_step(struct led_sm_object *sm)
{
	bool was_up = sm->breathe_ramp_up;

	if (sm->breathe_ramp_up) {
		sm->breathe_step++;
		if (sm->breathe_step >= sm->breathe_total_steps) {
			sm->breathe_step = sm->breathe_total_steps - 1;
			sm->breathe_ramp_up = false;
		}
	} else {
		if (sm->breathe_step == 0) {
			sm->breathe_ramp_up = true;
		} else {
			sm->breathe_step--;
		}
	}

	if (was_up != sm->breathe_ramp_up) {
		LOG_DBG("LED %d BREATHE direction -> %s (step %u/%u)", sm->led_number,
			sm->breathe_ramp_up ? "UP" : "DOWN", sm->breathe_step,
			sm->breathe_total_steps);
	}
}

/* ============================================================================
 * EFFECT WORK HANDLER — blink / breathe (runs on system workqueue)
 *
 * Sets LED_EVENT_TICK and calls smf_run_state(), which invokes the current
 * state's run handler.  The run handler toggles/steps the LED and reschedules.
 * ============================================================================
 */

static void effect_work_fn(struct k_work *work)
{
	struct led_sm_object *sm =
		CONTAINER_OF(k_work_delayable_from_work(work), struct led_sm_object, effect_work);

	sm->event = LED_EVENT_TICK;
	int ret = smf_run_state(SMF_CTX(sm));

	if (ret < 0) {
		LOG_ERR("LED %d effect tick SM error: %d", sm->led_number, ret);
	}
}

/* ============================================================================
 * ROTATE WORK HANDLER (runs on system workqueue)
 * ============================================================================
 */

static void rotate_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!rotate.active) {
		return;
	}

	uint8_t old_led = rotate.indices[rotate.current];

	led_hw_set(old_led, false);
	led_sm[old_led].is_on = false;

	rotate.current = (uint8_t)((rotate.current + 1) % rotate.count);

	uint8_t new_led = rotate.indices[rotate.current];

	led_hw_set(new_led, true);
	led_sm[new_led].is_on = true;

	k_work_schedule(&rotate.work, K_MSEC(rotate.period_ms));
}

/* ============================================================================
 * STATE IMPLEMENTATIONS — OFF
 * ============================================================================
 */

static void s_off_entry(void *obj)
{
	struct led_sm_object *sm = obj;

	led_hw_set(sm->led_number, false);
	sm->is_on = false;
	enum led_msg_type cause = (sm->event == LED_EVENT_CMD) ? sm->cmd.type : LED_COMMAND_OFF;

	publish_state(sm->led_number, false, cause);
	LOG_DBG("LED %d OFF", sm->led_number);
}

static enum smf_state_result s_off_run(void *obj)
{
	struct led_sm_object *sm = obj;

	if (sm->event != LED_EVENT_CMD) {
		return SMF_EVENT_HANDLED;
	}
	switch (sm->cmd.type) {
	case LED_COMMAND_ON:
	case LED_COMMAND_TOGGLE:
		smf_set_state(SMF_CTX(sm), &led_states[LED_S_ON]);
		break;
	case LED_COMMAND_BLINK:
		smf_set_state(SMF_CTX(sm), &led_states[LED_S_BLINK]);
		break;
	case LED_COMMAND_BREATHE:
		smf_set_state(SMF_CTX(sm), &led_states[LED_S_BREATHE]);
		break;
	default:
		break;
	}
	return SMF_EVENT_HANDLED;
}

/* ============================================================================
 * STATE IMPLEMENTATIONS — ON
 * ============================================================================
 */

static void s_on_entry(void *obj)
{
	struct led_sm_object *sm = obj;

	led_hw_set(sm->led_number, true);
	sm->is_on = true;
	enum led_msg_type cause = (sm->event == LED_EVENT_CMD) ? sm->cmd.type : LED_COMMAND_ON;

	publish_state(sm->led_number, true, cause);
	LOG_INF("SOLID-ON effect started (LED %d)", sm->led_number);
}

static enum smf_state_result s_on_run(void *obj)
{
	struct led_sm_object *sm = obj;

	if (sm->event != LED_EVENT_CMD) {
		return SMF_EVENT_HANDLED;
	}
	switch (sm->cmd.type) {
	case LED_COMMAND_OFF:
	case LED_COMMAND_TOGGLE:
		smf_set_state(SMF_CTX(sm), &led_states[LED_S_OFF]);
		break;
	case LED_COMMAND_BLINK:
		smf_set_state(SMF_CTX(sm), &led_states[LED_S_BLINK]);
		break;
	case LED_COMMAND_BREATHE:
		smf_set_state(SMF_CTX(sm), &led_states[LED_S_BREATHE]);
		break;
	default:
		break;
	}
	return SMF_EVENT_HANDLED;
}

/* ============================================================================
 * STATE IMPLEMENTATIONS — BLINK
 * ============================================================================
 */

static void s_blink_entry(void *obj)
{
	struct led_sm_object *sm = obj;

	sm->effect_period_ms =
		(sm->cmd.period_ms > 0) ? sm->cmd.period_ms : CONFIG_ZEGO_LED_BLINK_PERIOD_MS;
	sm->is_on = false;
	led_hw_set(sm->led_number, false);
	publish_state(sm->led_number, false, LED_COMMAND_BLINK);
	k_work_schedule(&sm->effect_work, K_MSEC(sm->effect_period_ms));
	LOG_INF("BLINK effect started (LED %d, period %u ms)", sm->led_number,
		(unsigned)sm->effect_period_ms);
}

static enum smf_state_result s_blink_run(void *obj)
{
	struct led_sm_object *sm = obj;

	if (sm->event == LED_EVENT_TICK) {
		sm->is_on = !sm->is_on;
		led_hw_set(sm->led_number, sm->is_on);
		k_work_schedule(&sm->effect_work, K_MSEC(sm->effect_period_ms));
		return SMF_EVENT_HANDLED;
	}

	if (sm->event != LED_EVENT_CMD) {
		return SMF_EVENT_HANDLED;
	}

	switch (sm->cmd.type) {
	case LED_COMMAND_OFF:
		smf_set_state(SMF_CTX(sm), &led_states[LED_S_OFF]);
		break;
	case LED_COMMAND_ON:
		smf_set_state(SMF_CTX(sm), &led_states[LED_S_ON]);
		break;
	case LED_COMMAND_TOGGLE:
		smf_set_state(SMF_CTX(sm),
			      sm->is_on ? &led_states[LED_S_OFF] : &led_states[LED_S_ON]);
		break;
	case LED_COMMAND_BLINK:
		/* Restart blink with (possibly new) period — cancel and reschedule */
		k_work_cancel_delayable(&sm->effect_work);
		sm->effect_period_ms = (sm->cmd.period_ms > 0) ? sm->cmd.period_ms
							       : CONFIG_ZEGO_LED_BLINK_PERIOD_MS;
		k_work_schedule(&sm->effect_work, K_MSEC(sm->effect_period_ms));
		break;
	case LED_COMMAND_BREATHE:
		smf_set_state(SMF_CTX(sm), &led_states[LED_S_BREATHE]);
		break;
	default:
		break;
	}
	return SMF_EVENT_HANDLED;
}

static void s_blink_exit(void *obj)
{
	struct led_sm_object *sm = obj;

	k_work_cancel_delayable(&sm->effect_work);
	LOG_DBG("LED %d BLINK exit", sm->led_number);
}

/* ============================================================================
 * STATE IMPLEMENTATIONS — BREATHE
 * ============================================================================
 */

static void s_breathe_entry(void *obj)
{
	struct led_sm_object *sm = obj;

	sm->effect_period_ms =
		(sm->cmd.period_ms > 0) ? sm->cmd.period_ms : CONFIG_ZEGO_LED_BREATHE_PERIOD_MS;
	uint32_t pwm_ms = CONFIG_ZEGO_LED_BREATHE_PWM_PERIOD_MS;
	uint16_t steps = (uint16_t)(sm->effect_period_ms / pwm_ms);

	if (steps < 2) {
		steps = 2;
	}
	sm->breathe_step = 0;
	sm->breathe_total_steps = steps;
	sm->breathe_ramp_up = true;
	sm->breathe_in_on_phase = false;
	sm->is_on = false;
	led_hw_set(sm->led_number, false);
	publish_state(sm->led_number, false, LED_COMMAND_BREATHE);

	if (led_hw_has_brightness(sm->led_number)) {
		k_work_schedule(&sm->effect_work, K_MSEC(CONFIG_ZEGO_LED_BREATHE_PWM_PERIOD_MS));
		LOG_INF("BREATHE effect started (LED %d, HW PWM, ramp %u ms, %u steps x %u "
			"ms/step)",
			sm->led_number, (unsigned)sm->effect_period_ms, steps, (unsigned)pwm_ms);
	} else {
		k_work_schedule(&sm->effect_work, K_MSEC(CONFIG_ZEGO_LED_BREATHE_PWM_PERIOD_MS));
		LOG_INF("BREATHE effect started (LED %d, SW PWM, ramp %u ms, %u steps x %u "
			"ms/step)",
			sm->led_number, (unsigned)sm->effect_period_ms, steps, (unsigned)pwm_ms);
	}
}

static enum smf_state_result s_breathe_run(void *obj)
{
	struct led_sm_object *sm = obj;

	if (sm->event == LED_EVENT_TICK) {
		if (led_hw_has_brightness(sm->led_number)) {
			/* Hardware-PWM breathe path */
			uint16_t denom =
				sm->breathe_total_steps > 1 ? sm->breathe_total_steps - 1 : 1;
			uint8_t brightness = (uint8_t)((uint32_t)sm->breathe_step * 100U / denom);

			led_hw_set_brightness(sm->led_number, brightness);
			sm->is_on = (brightness > 0);
			breathe_advance_step(sm);
			k_work_schedule(&sm->effect_work,
					K_MSEC(CONFIG_ZEGO_LED_BREATHE_PWM_PERIOD_MS));
		} else {
			/* Software-PWM breathe path */
			uint32_t pwm_ms = CONFIG_ZEGO_LED_BREATHE_PWM_PERIOD_MS;
			uint16_t steps = sm->breathe_total_steps;
			uint32_t on_ms = (uint32_t)sm->breathe_step * pwm_ms / steps;
			uint32_t off_ms = (uint32_t)(steps - sm->breathe_step) * pwm_ms / steps;

			if (sm->breathe_in_on_phase) {
				sm->is_on = false;
				led_hw_set(sm->led_number, false);
				sm->breathe_in_on_phase = false;
				k_work_schedule(&sm->effect_work, K_MSEC(off_ms > 0 ? off_ms : 1));
			} else {
				breathe_advance_step(sm);
				on_ms = (uint32_t)sm->breathe_step * pwm_ms / steps;
				off_ms = (uint32_t)(steps - sm->breathe_step) * pwm_ms / steps;

				if (on_ms > 0) {
					sm->is_on = true;
					led_hw_set(sm->led_number, true);
					sm->breathe_in_on_phase = true;
					k_work_schedule(&sm->effect_work, K_MSEC(on_ms));
				} else {
					k_work_schedule(&sm->effect_work,
							K_MSEC(off_ms > 0 ? off_ms : pwm_ms));
				}
			}
		}
		return SMF_EVENT_HANDLED;
	}

	if (sm->event != LED_EVENT_CMD) {
		return SMF_EVENT_HANDLED;
	}

	switch (sm->cmd.type) {
	case LED_COMMAND_OFF:
		smf_set_state(SMF_CTX(sm), &led_states[LED_S_OFF]);
		break;
	case LED_COMMAND_ON:
		smf_set_state(SMF_CTX(sm), &led_states[LED_S_ON]);
		break;
	case LED_COMMAND_TOGGLE:
		smf_set_state(SMF_CTX(sm),
			      sm->is_on ? &led_states[LED_S_OFF] : &led_states[LED_S_ON]);
		break;
	case LED_COMMAND_BLINK:
		smf_set_state(SMF_CTX(sm), &led_states[LED_S_BLINK]);
		break;
	case LED_COMMAND_BREATHE:
		/* Restart breathe with (possibly new) period */
		k_work_cancel_delayable(&sm->effect_work);
		sm->effect_period_ms = (sm->cmd.period_ms > 0) ? sm->cmd.period_ms
							       : CONFIG_ZEGO_LED_BREATHE_PERIOD_MS;
		{
			uint32_t pwm_ms = CONFIG_ZEGO_LED_BREATHE_PWM_PERIOD_MS;
			uint16_t steps = (uint16_t)(sm->effect_period_ms / pwm_ms);

			if (steps < 2) {
				steps = 2;
			}
			sm->breathe_step = 0;
			sm->breathe_total_steps = steps;
			sm->breathe_ramp_up = true;
			sm->breathe_in_on_phase = false;
		}
		k_work_schedule(&sm->effect_work, K_MSEC(CONFIG_ZEGO_LED_BREATHE_PWM_PERIOD_MS));
		break;
	default:
		break;
	}
	return SMF_EVENT_HANDLED;
}

static void s_breathe_exit(void *obj)
{
	struct led_sm_object *sm = obj;

	k_work_cancel_delayable(&sm->effect_work);
	LOG_DBG("LED %d BREATHE exit", sm->led_number);
}

/* ============================================================================
 * ROTATE HELPERS
 * ============================================================================
 */

static void rotate_stop(void)
{
	if (!rotate.active) {
		return;
	}
	rotate.active = false;
	k_work_cancel_delayable(&rotate.work);
	uint8_t led = rotate.indices[rotate.current];

	led_hw_set(led, false);
	led_sm[led].is_on = false;
}

/* Cancel all per-LED effect timers without changing SMF state. */
static void cancel_all_effect_timers(void)
{
	for (int i = 0; i < NUM_LEDS; i++) {
		k_work_cancel_delayable(&led_sm[i].effect_work);
		led_hw_set(i, false);
		led_sm[i].is_on = false;
	}
}

/* ============================================================================
 * COMMAND PROCESSING — runs on system workqueue via led_cmd_work_fn
 * ============================================================================
 */

static void process_led_command(const struct led_msg *msg)
{
	if (msg->type == LED_COMMAND_ROTATE) {
		cancel_all_effect_timers();
		rotate.period_ms =
			msg->period_ms > 0 ? msg->period_ms : CONFIG_ZEGO_LED_ROTATE_PERIOD_MS;
		rotate.current = 0;
		rotate.active = true;

		if (msg->rotate_count > 0) {
			rotate.count = MIN(msg->rotate_count, (uint8_t)NUM_LEDS);
			memcpy(rotate.indices, msg->rotate_indices, rotate.count);
		} else {
			rotate.count = (uint8_t)ROTATE_NUM_LEDS;
			for (uint8_t i = 0; i < rotate.count; i++) {
				rotate.indices[i] = i;
			}
		}

		uint8_t first = rotate.indices[0];

		led_hw_set(first, true);
		led_sm[first].is_on = true;
		publish_state(first, true, LED_COMMAND_ROTATE);
		k_work_schedule(&rotate.work, K_MSEC(rotate.period_ms));
		LOG_INF("ROTATE effect started (period %u ms, %d LEDs, first=%d)",
			(unsigned)rotate.period_ms, rotate.count, first);
		return;
	}

	if (msg->led_number >= NUM_LEDS) {
		LOG_WRN("Invalid LED number: %d (max %d)", msg->led_number, NUM_LEDS - 1);
		return;
	}

	if (rotate.active) {
		rotate_stop();
	}

	struct led_sm_object *sm = &led_sm[msg->led_number];

	sm->event = LED_EVENT_CMD;
	sm->cmd = *msg;

	int ret = smf_run_state(SMF_CTX(sm));

	if (ret < 0) {
		LOG_ERR("LED %d command SM error: %d", msg->led_number, ret);
	}
}

/* ============================================================================
 * COMMAND QUEUE — decouples zbus listener from system workqueue
 *
 * The zbus listener runs synchronously in the publisher's thread (which may
 * be the net_mgmt thread or any other Zephyr thread).  If it called
 * k_work_cancel_delayable directly, it would race with the effect_work /
 * rotate.work callbacks on the system workqueue, corrupting the kernel
 * timeout dlist.
 *
 * Instead: listener enqueues to led_cmd_queue (non-blocking, drops if full)
 * and submits led_cmd_work.  led_cmd_work_fn drains the queue on the system
 * workqueue, serialised with all effect timers.
 * ============================================================================
 */

K_MSGQ_DEFINE(led_cmd_queue, sizeof(struct led_msg), CONFIG_ZEGO_LED_CMD_QUEUE_DEPTH, 4);

static void led_cmd_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	struct led_msg msg;

	while (k_msgq_get(&led_cmd_queue, &msg, K_NO_WAIT) == 0) {
		process_led_command(&msg);
	}
}

static K_WORK_DEFINE(led_cmd_work, led_cmd_work_fn);

static void led_cmd_listener(const struct zbus_channel *chan)
{
	const struct led_msg *msg = zbus_chan_const_msg(chan);
	int ret = k_msgq_put(&led_cmd_queue, msg, K_NO_WAIT);

	if (ret < 0) {
		LOG_WRN("LED command queue full — command dropped (type %d, led %d)",
			(int)msg->type, msg->led_number);
	}
	k_work_submit(&led_cmd_work);
}

ZBUS_LISTENER_DEFINE(led_cmd_listener_def, led_cmd_listener);
ZBUS_CHAN_ADD_OBS(LED_CMD_CHAN, led_cmd_listener_def, 0);

/* ============================================================================
 * PUBLIC API
 * ============================================================================
 */

int led_get_state(uint8_t led_number, bool *state)
{
	if (led_number >= NUM_LEDS || !state) {
		return -EINVAL;
	}
	*state = led_sm[led_number].is_on;
	return 0;
}

/* ============================================================================
 * MODULE INITIALIZATION
 * ============================================================================
 */

static int led_module_init(void)
{
	int ret;

	LOG_INF("Initializing zego_led (%d LEDs)", NUM_LEDS);

	ret = led_hw_init();
	if (ret) {
		LOG_ERR("led_hw_init failed: %d", ret);
		return ret;
	}

	for (int i = 0; i < NUM_LEDS; i++) {
		led_sm[i].led_number = (uint8_t)i;
		led_sm[i].is_on = false;
		led_sm[i].event = LED_EVENT_NONE;
		led_sm[i].cmd = (struct led_msg){0};
		led_sm[i].effect_period_ms = 0;
		led_sm[i].breathe_step = 0;
		led_sm[i].breathe_total_steps = 0;
		led_sm[i].breathe_ramp_up = true;
		led_sm[i].breathe_in_on_phase = false;
		k_work_init_delayable(&led_sm[i].effect_work, effect_work_fn);
		/* smf_set_initial calls s_off_entry which turns the LED off */
		smf_set_initial(SMF_CTX(&led_sm[i]), &led_states[LED_S_OFF]);
	}

	k_work_init_delayable(&rotate.work, rotate_work_fn);
	rotate.active = false;
	rotate.current = 0;
	rotate.count = (uint8_t)ROTATE_NUM_LEDS;
	for (uint8_t i = 0; i < rotate.count; i++) {
		rotate.indices[i] = i;
	}

	LOG_INF("zego_led initialized");
	return 0;
}

SYS_INIT(led_module_init, APPLICATION, CONFIG_ZEGO_LED_INIT_PRIORITY);
