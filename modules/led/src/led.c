/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "led.h"
#include "led_hw.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zego_led, CONFIG_ZEGO_LED_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/zbus/zbus.h>

#define NUM_LEDS CONFIG_ZEGO_LED_NUM_LEDS

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
 * PER-LED STATE MACHINE  (static modes: OFF / ON)
 *
 * Dynamic effects (BLINK / BREATHE) bypass the SMF and run via k_work_delayable.
 * The SMF is used for ON/OFF/TOGGLE; its state is reset to LED_OFF when an
 * effect starts so that the next static command starts from a known state.
 * ============================================================================
 */

static void led_off_entry(void *obj);
static enum smf_state_result led_off_run(void *obj);
static void led_on_entry(void *obj);
static enum smf_state_result led_on_run(void *obj);

static const struct smf_state led_states[] = {
	[0] = SMF_CREATE_STATE(led_off_entry, led_off_run, NULL, NULL, NULL), /* LED_OFF */
	[1] = SMF_CREATE_STATE(led_on_entry, led_on_run, NULL, NULL, NULL),   /* LED_ON  */
};

/* Active effect mode for a single LED.
 *
 * Thread-safety: `effect` is written by the zbus listener (publisher's thread)
 * and read by the work queue handler.  It is stored as atomic_t so that the
 * "set STATIC before cancel" pattern is visible across both execution contexts
 * without relying on architecture-specific atomicity guarantees.
 *
 * Other per-LED fields (effect_period_ms, breathe_*) are only written in the
 * listener BEFORE k_work_schedule(), and only read in the work handler AFTER
 * the first fire.  On single-core targets the work-queue scheduling point
 * provides the required memory ordering; no additional locking is needed.
 */
enum led_effect {
	LED_EFFECT_STATIC,  /* ON or OFF — driven by SMF */
	LED_EFFECT_BLINK,   /* periodic toggle via work item */
	LED_EFFECT_BREATHE, /* asymmetric duty-cycle pulse via work item */
};

struct led_sm_object {
	struct smf_ctx ctx;
	uint8_t led_number;
	bool is_on;
	enum led_msg_type pending_command;
	bool has_pending_command;
	atomic_t effect; /* enum led_effect — see thread-safety note above */
	uint16_t effect_period_ms;
	/* breathe linear-fade state — valid only while effect == LED_EFFECT_BREATHE */
	uint16_t breathe_step;        /* current step: 0 = fully off, total_steps-1 = full on */
	uint16_t breathe_total_steps; /* steps per ramp direction = period_ms / PWM_PERIOD_MS */
	bool breathe_ramp_up;         /* true = ramping up, false = ramping down */
	bool breathe_in_on_phase;     /* SW-PWM only: currently in the on portion of the frame */
	struct k_work_delayable effect_work;
};

static struct led_sm_object led_sm[NUM_LEDS];

/* ============================================================================
 * MARQUEE — global mode that cycles one LED at a time across all LEDs
 * ============================================================================
 */

static struct {
	struct k_work_delayable work;
	uint16_t period_ms;
	uint8_t current;
	bool active;
} marquee;

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

/* ============================================================================
 * STATIC STATE MACHINE IMPLEMENTATIONS
 * ============================================================================
 */

static void led_off_entry(void *obj)
{
	struct led_sm_object *sm = obj;

	led_hw_set(sm->led_number, false);
	sm->is_on = false;
	LOG_DBG("LED %d OFF", sm->led_number);
	publish_state(sm->led_number, false, sm->pending_command);
}

static enum smf_state_result led_off_run(void *obj)
{
	struct led_sm_object *sm = obj;

	if (sm->has_pending_command) {
		sm->has_pending_command = false;
		if (sm->pending_command == LED_COMMAND_ON ||
		    sm->pending_command == LED_COMMAND_TOGGLE) {
			smf_set_state(SMF_CTX(sm), &led_states[1]);
		}
	}
	return SMF_EVENT_HANDLED;
}

static void led_on_entry(void *obj)
{
	struct led_sm_object *sm = obj;

	led_hw_set(sm->led_number, true);
	sm->is_on = true;
	LOG_DBG("LED %d ON", sm->led_number);
	publish_state(sm->led_number, true, sm->pending_command);
}

static enum smf_state_result led_on_run(void *obj)
{
	struct led_sm_object *sm = obj;

	if (sm->has_pending_command) {
		sm->has_pending_command = false;
		if (sm->pending_command == LED_COMMAND_OFF ||
		    sm->pending_command == LED_COMMAND_TOGGLE) {
			smf_set_state(SMF_CTX(sm), &led_states[0]);
		}
	}
	return SMF_EVENT_HANDLED;
}

/* ============================================================================
 * BREATHE STEP ADVANCE
 * ============================================================================ */

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
 * EFFECT WORK HANDLER  (blink / breathe — runs on system work queue)
 * ============================================================================ */

static void led_effect_work_fn(struct k_work *work)
{
	struct led_sm_object *sm =
		CONTAINER_OF(k_work_delayable_from_work(work), struct led_sm_object, effect_work);

	enum led_effect eff = (enum led_effect)atomic_get(&sm->effect);

	/* If the effect was cancelled before this fire, exit without touching HW. */
	if (eff == LED_EFFECT_STATIC) {
		return;
	}

	if (eff == LED_EFFECT_BLINK) {
		sm->is_on = !sm->is_on;
		led_hw_set(sm->led_number, sm->is_on);
		/* No LED_STATE_CHAN publish — blink is an active effect. */
		k_work_schedule(&sm->effect_work, K_MSEC(sm->effect_period_ms));

	} else if (eff == LED_EFFECT_BREATHE) {

		if (led_hw_has_brightness(sm->led_number)) {
			/*
			 * Hardware-PWM breathe path.
			 *
			 * One work-queue wake per ramp step; hardware handles the
			 * actual PWM signal.  Brightness = step/max * 100 %.
			 */
			uint16_t denom =
				sm->breathe_total_steps > 1 ? sm->breathe_total_steps - 1 : 1;
			uint8_t brightness = (uint8_t)((uint32_t)sm->breathe_step * 100U / denom);

			led_hw_set_brightness(sm->led_number, brightness);
			sm->is_on = (brightness > 0);
			breathe_advance_step(sm);
			k_work_schedule(&sm->effect_work,
					K_MSEC(CONFIG_ZEGO_LED_BREATHE_PWM_PERIOD_MS));

		} else {
			/*
			 * Software-PWM breathe path.
			 *
			 * Each step is one PWM frame of CONFIG_ZEGO_LED_BREATHE_PWM_PERIOD_MS.
			 * Duty cycle = step / total_steps (0% at step 0, ~100% at step max).
			 * The work item alternates between the on-phase and off-phase of the
			 * current frame, then advances to the next step at off-phase end.
			 *
			 *   on_ms  = step * pwm_ms / total_steps
			 *   off_ms = (total_steps - step) * pwm_ms / total_steps
			 */
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
		/* No LED_STATE_CHAN publish during breathe — effect-start already published. */
	}
}

/* ============================================================================
 * MARQUEE WORK HANDLER  (runs on system work queue)
 * ============================================================================
 */

static void marquee_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!marquee.active) {
		return;
	}

	led_hw_set(marquee.current, false);
	led_sm[marquee.current].is_on = false;

	marquee.current = (uint8_t)((marquee.current + 1) % NUM_LEDS);

	led_hw_set(marquee.current, true);
	led_sm[marquee.current].is_on = true;

	/* No LED_STATE_CHAN publish per step — marquee-start already published. */
	k_work_schedule(&marquee.work, K_MSEC(marquee.period_ms));
}

/* ============================================================================
 * EFFECT MANAGEMENT HELPERS
 * ============================================================================
 */

static void cancel_led_effect(struct led_sm_object *sm)
{
	/* Set STATIC before cancel so any in-flight work-queue fire exits early. */
	if ((enum led_effect)atomic_get(&sm->effect) != LED_EFFECT_STATIC) {
		atomic_set(&sm->effect, LED_EFFECT_STATIC);
		k_work_cancel_delayable(&sm->effect_work);
	}
}

static void cancel_all_effects(void)
{
	for (int i = 0; i < NUM_LEDS; i++) {
		atomic_set(&led_sm[i].effect, LED_EFFECT_STATIC);
		k_work_cancel_delayable(&led_sm[i].effect_work);
		led_hw_set(i, false);
		led_sm[i].is_on = false;
		smf_set_initial(SMF_CTX(&led_sm[i]), &led_states[0]);
	}
}

static void marquee_stop(void)
{
	if (!marquee.active) {
		return;
	}
	marquee.active = false;
	k_work_cancel_delayable(&marquee.work);
	led_hw_set(marquee.current, false);
	led_sm[marquee.current].is_on = false;
	/* No LED_STATE_CHAN publish — the incoming command will publish its own state. */
}

/* ============================================================================
 * ZBUS LISTENER  (runs synchronously in the publisher's thread)
 * ============================================================================
 */

static void led_cmd_listener(const struct zbus_channel *chan)
{
	const struct led_msg *msg = zbus_chan_const_msg(chan);

	/* ---- MARQUEE: global effect — takes over all LEDs ---- */
	if (msg->type == LED_COMMAND_MARQUEE) {
		cancel_all_effects();
		marquee.period_ms =
			msg->period_ms > 0 ? msg->period_ms : CONFIG_ZEGO_LED_MARQUEE_PERIOD_MS;
		marquee.current = 0;
		marquee.active = true;
		led_hw_set(0, true);
		led_sm[0].is_on = true;
		/* One publish for the whole marquee start. */
		publish_state(0, true, LED_COMMAND_MARQUEE);
		k_work_schedule(&marquee.work, K_MSEC(marquee.period_ms));
		LOG_INF("Marquee started (period %u ms)", (unsigned)marquee.period_ms);
		return;
	}

	if (msg->led_number >= NUM_LEDS) {
		LOG_WRN("Invalid LED number: %d (max %d)", msg->led_number, NUM_LEDS - 1);
		return;
	}

	if (marquee.active) {
		marquee_stop();
	}

	struct led_sm_object *sm = &led_sm[msg->led_number];

	switch (msg->type) {
	case LED_COMMAND_BLINK: {
		cancel_led_effect(sm);
		atomic_set(&sm->effect, LED_EFFECT_BLINK);
		sm->effect_period_ms =
			msg->period_ms > 0 ? msg->period_ms : CONFIG_ZEGO_LED_BLINK_PERIOD_MS;
		sm->is_on = false;
		led_hw_set(sm->led_number, false);
		smf_set_initial(SMF_CTX(sm), &led_states[0]);
		/* Publish effect start — no per-toggle publishes will follow. */
		publish_state(sm->led_number, false, LED_COMMAND_BLINK);
		k_work_schedule(&sm->effect_work, K_MSEC(sm->effect_period_ms));
		LOG_INF("LED %d BLINK period=%u ms", sm->led_number,
			(unsigned)sm->effect_period_ms);
		break;
	}
	case LED_COMMAND_BREATHE: {
		cancel_led_effect(sm);
		atomic_set(&sm->effect, LED_EFFECT_BREATHE);
		sm->effect_period_ms =
			msg->period_ms > 0 ? msg->period_ms : CONFIG_ZEGO_LED_BREATHE_PERIOD_MS;
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
		smf_set_initial(SMF_CTX(sm), &led_states[0]);
		/* Publish effect start — no per-step publishes will follow. */
		publish_state(sm->led_number, false, LED_COMMAND_BREATHE);
		if (led_hw_has_brightness(sm->led_number)) {
			/* HW PWM: first tick fires after one step period. */
			k_work_schedule(&sm->effect_work,
					K_MSEC(CONFIG_ZEGO_LED_BREATHE_PWM_PERIOD_MS));
			LOG_INF("LED %d BREATHE (HW PWM) ramp=%u ms (%u steps x %u ms/step)",
				sm->led_number, (unsigned)sm->effect_period_ms, steps,
				(unsigned)pwm_ms);
		} else {
			/* SW PWM: step 0 = 0% duty; wait one full PWM frame first. */
			k_work_schedule(&sm->effect_work, K_MSEC(pwm_ms));
			LOG_INF("LED %d BREATHE (SW PWM) ramp=%u ms (%u steps x %u ms/step)",
				sm->led_number, (unsigned)sm->effect_period_ms, steps,
				(unsigned)pwm_ms);
		}
		break;
	}
	default: {
		/* ON / OFF / TOGGLE — cancel any active effect and run SMF.
		 * led_on_entry / led_off_entry will publish via pending_command. */
		cancel_led_effect(sm);
		sm->pending_command = msg->type;
		sm->has_pending_command = true;
		int ret = smf_run_state(SMF_CTX(sm));

		if (ret < 0) {
			LOG_ERR("LED %d SM error: %d", sm->led_number, ret);
		}
		break;
	}
	}
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
		led_sm[i].has_pending_command = false;
		led_sm[i].pending_command = LED_COMMAND_OFF;
		atomic_set(&led_sm[i].effect, LED_EFFECT_STATIC);
		k_work_init_delayable(&led_sm[i].effect_work, led_effect_work_fn);
		smf_set_initial(SMF_CTX(&led_sm[i]), &led_states[0]);
		smf_run_state(SMF_CTX(&led_sm[i])); /* run entry to init hardware */
	}

	k_work_init_delayable(&marquee.work, marquee_work_fn);
	marquee.active = false;
	marquee.current = 0;

	LOG_INF("zego_led initialized");
	return 0;
}

SYS_INIT(led_module_init, APPLICATION, CONFIG_ZEGO_LED_INIT_PRIORITY);
