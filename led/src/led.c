/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "led.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zego_led, CONFIG_ZEGO_LED_LOG_LEVEL);

#include <dk_buttons_and_leds.h>
#include <zephyr/kernel.h>
#include <zephyr/smf.h>
#include <zephyr/zbus/zbus.h>

#define NUM_LEDS CONFIG_ZEGO_LED_NUM_LEDS

/* ============================================================================
 * ZBUS CHANNEL DEFINITIONS
 * ============================================================================
 */

/* Input:  publish here to command an LED */
ZBUS_CHAN_DEFINE(LED_CMD_CHAN, struct led_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

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
	[1] = SMF_CREATE_STATE(led_on_entry,  led_on_run,  NULL, NULL, NULL), /* LED_ON  */
};

/* Active effect mode for a single LED */
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
	/* effect state — accessed from both listener thread and work queue;
	 * single-core ARM guarantees bool/uint16 reads/writes are atomic. */
	enum led_effect effect;
	uint16_t effect_period_ms;
	bool breathe_high; /* current breathe phase: true = on */
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

static void set_led_hw(uint8_t n, bool on)
{
	if (on) {
		dk_set_led_on(n);
	} else {
		dk_set_led_off(n);
	}
}

static void publish_state(uint8_t n, bool on)
{
	struct led_state_msg msg = {.led_number = n, .is_on = on};
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

	set_led_hw(sm->led_number, false);
	sm->is_on = false;
	LOG_DBG("LED %d OFF", sm->led_number);
	publish_state(sm->led_number, false);
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

	set_led_hw(sm->led_number, true);
	sm->is_on = true;
	LOG_DBG("LED %d ON", sm->led_number);
	publish_state(sm->led_number, true);
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
 * EFFECT WORK HANDLER  (blink / breathe — runs on system work queue)
 * ============================================================================
 */

static void led_effect_work_fn(struct k_work *work)
{
	struct led_sm_object *sm =
		CONTAINER_OF(k_work_delayable_from_work(work),
			     struct led_sm_object, effect_work);

	if (sm->effect == LED_EFFECT_BLINK) {
		sm->is_on = !sm->is_on;
		set_led_hw(sm->led_number, sm->is_on);
		publish_state(sm->led_number, sm->is_on);
		k_work_schedule(&sm->effect_work, K_MSEC(sm->effect_period_ms));

	} else if (sm->effect == LED_EFFECT_BREATHE) {
		sm->breathe_high = !sm->breathe_high;
		sm->is_on = sm->breathe_high;
		set_led_hw(sm->led_number, sm->is_on);
		publish_state(sm->led_number, sm->is_on);

		uint32_t on_ms  = (uint32_t)sm->effect_period_ms *
				  CONFIG_ZEGO_LED_BREATHE_ON_PCT / 100U;
		uint32_t off_ms = sm->effect_period_ms - on_ms;

		if (on_ms  == 0) { on_ms  = 1; }
		if (off_ms == 0) { off_ms = 1; }

		k_work_schedule(&sm->effect_work,
				K_MSEC(sm->breathe_high ? on_ms : off_ms));
	}
	/* If effect was set to STATIC before this fire, do nothing — the
	 * mode was changed before cancel so this fire is a benign straggler. */
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

	/* Turn off current LED, advance, turn on next */
	set_led_hw(marquee.current, false);
	publish_state(marquee.current, false);
	led_sm[marquee.current].is_on = false;

	marquee.current = (uint8_t)((marquee.current + 1) % NUM_LEDS);

	set_led_hw(marquee.current, true);
	publish_state(marquee.current, true);
	led_sm[marquee.current].is_on = true;

	k_work_schedule(&marquee.work, K_MSEC(marquee.period_ms));
}

/* ============================================================================
 * EFFECT MANAGEMENT HELPERS
 * ============================================================================
 */

/* Cancel any active blink/breathe effect and reset to STATIC. */
static void cancel_led_effect(struct led_sm_object *sm)
{
	if (sm->effect != LED_EFFECT_STATIC) {
		sm->effect = LED_EFFECT_STATIC; /* set before cancel so straggler fires exit early */
		k_work_cancel_delayable(&sm->effect_work);
	}
}

/* Cancel all per-LED effects; leave all LEDs off. */
static void cancel_all_effects(void)
{
	for (int i = 0; i < NUM_LEDS; i++) {
		led_sm[i].effect = LED_EFFECT_STATIC;
		k_work_cancel_delayable(&led_sm[i].effect_work);
		set_led_hw(i, false);
		led_sm[i].is_on = false;
		smf_set_initial(SMF_CTX(&led_sm[i]), &led_states[0]);
	}
}

/* Stop marquee and turn off the currently lit LED. */
static void marquee_stop(void)
{
	if (!marquee.active) {
		return;
	}
	marquee.active = false;
	k_work_cancel_delayable(&marquee.work);
	set_led_hw(marquee.current, false);
	publish_state(marquee.current, false);
	led_sm[marquee.current].is_on = false;
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
		marquee.period_ms = msg->period_ms > 0 ? msg->period_ms
						        : CONFIG_ZEGO_LED_MARQUEE_PERIOD_MS;
		marquee.current   = 0;
		marquee.active    = true;
		set_led_hw(0, true);
		publish_state(0, true);
		led_sm[0].is_on = true;
		k_work_schedule(&marquee.work, K_MSEC(marquee.period_ms));
		LOG_INF("Marquee started (period %u ms)", (unsigned)marquee.period_ms);
		return;
	}

	if (msg->led_number >= NUM_LEDS) {
		LOG_WRN("Invalid LED number: %d (max %d)", msg->led_number, NUM_LEDS - 1);
		return;
	}

	/* Any per-LED command stops marquee first */
	if (marquee.active) {
		marquee_stop();
	}

	struct led_sm_object *sm = &led_sm[msg->led_number];

	switch (msg->type) {
	case LED_COMMAND_BLINK: {
		cancel_led_effect(sm);
		sm->effect           = LED_EFFECT_BLINK;
		sm->effect_period_ms = msg->period_ms > 0 ? msg->period_ms
							   : CONFIG_ZEGO_LED_BLINK_PERIOD_MS;
		sm->is_on = false;
		set_led_hw(sm->led_number, false);
		smf_set_initial(SMF_CTX(sm), &led_states[0]);
		k_work_schedule(&sm->effect_work, K_MSEC(sm->effect_period_ms));
		LOG_INF("LED %d BLINK period=%u ms",
			sm->led_number, (unsigned)sm->effect_period_ms);
		break;
	}
	case LED_COMMAND_BREATHE: {
		cancel_led_effect(sm);
		sm->effect           = LED_EFFECT_BREATHE;
		sm->effect_period_ms = msg->period_ms > 0 ? msg->period_ms
							   : CONFIG_ZEGO_LED_BREATHE_PERIOD_MS;
		sm->breathe_high = false; /* start from off */
		sm->is_on        = false;
		set_led_hw(sm->led_number, false);
		smf_set_initial(SMF_CTX(sm), &led_states[0]);
		uint32_t off_ms = (uint32_t)sm->effect_period_ms *
				  (100U - CONFIG_ZEGO_LED_BREATHE_ON_PCT) / 100U;

		if (off_ms == 0) { off_ms = 1; }
		k_work_schedule(&sm->effect_work, K_MSEC(off_ms));
		LOG_INF("LED %d BREATHE period=%u ms",
			sm->led_number, (unsigned)sm->effect_period_ms);
		break;
	}
	default: {
		/* ON / OFF / TOGGLE — cancel any active effect and run SMF */
		cancel_led_effect(sm);
		sm->pending_command     = msg->type;
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

	ret = dk_leds_init();
	if (ret) {
		LOG_ERR("dk_leds_init failed: %d", ret);
		return ret;
	}

	for (int i = 0; i < NUM_LEDS; i++) {
		led_sm[i].led_number         = (uint8_t)i;
		led_sm[i].is_on              = false;
		led_sm[i].has_pending_command = false;
		led_sm[i].effect             = LED_EFFECT_STATIC;
		k_work_init_delayable(&led_sm[i].effect_work, led_effect_work_fn);
		smf_set_initial(SMF_CTX(&led_sm[i]), &led_states[0]);
		smf_run_state(SMF_CTX(&led_sm[i])); /* run entry to init hardware */
	}

	k_work_init_delayable(&marquee.work, marquee_work_fn);
	marquee.active  = false;
	marquee.current = 0;

	LOG_INF("zego_led initialized");
	return 0;
}

SYS_INIT(led_module_init, APPLICATION, CONFIG_ZEGO_LED_INIT_PRIORITY);
