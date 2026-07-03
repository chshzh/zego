/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file ux.c
 * @brief Zego UX brick — Button 0 gestures and LED 0 Wi-Fi feedback.
 *
 * Button 0 gesture → action mapping (default implementations; each is a
 * __weak function in this file — see ux.h for the override contract):
 *   SINGLE_CLICK  Print current Wi-Fi mode to UART log.
 *   DOUBLE_CLICK  Toggle BLE provisioning LED (BREATHE ↔ last Wi-Fi state),
 *                 or trigger P2P pairing in P2P_GO/P2P_GC modes.
 *   LONG_PRESS    Cycle Wi-Fi mode among this build's enabled modes (see
 *                 ZEGO_WIFI_MODE_*_ENABLED in zego/bricks/wifi/Kconfig;
 *                 default STA → SoftAP → P2P_GO → P2P_GC → STA), save to
 *                 NVS via settings, reboot.
 *
 * LED 0 state machine driven by ZEGO_UX_WIFI_STATE_CHAN:
 *   CONNECTING  →  ROTATE  (starts at boot via SYS_INIT)
 *   CONNECTED   →  Solid ON
 *   SOFTAP      →  ROTATE  (same as CONNECTING; AP up, no clients)
 *   ERROR       →  Fast BLINK  (100 ms half-period)
 *   BLE_PROV    →  BREATHE     (double-click toggle, local to this module)
 *
 * Inputs:  BUTTON_CHAN, ZEGO_UX_WIFI_STATE_CHAN
 * Outputs: LED_CMD_CHAN
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#include "button.h"
#include "led.h"
#include "wifi.h"
#include "wifi_utils.h"
#include "ux.h"
#if defined(CONFIG_ZEGO_WIFI_BLE_PROV)
#include "wifi_ble_prov.h"
#endif

LOG_MODULE_REGISTER(zego_ux, CONFIG_ZEGO_UX_LOG_LEVEL);

/* Definition of ZEGO_UX_WIFI_STATE_CHAN — declared in ux.h. Publishers are
 * typically the application's zego/network weak-hook overrides. */
ZBUS_CHAN_DEFINE(ZEGO_UX_WIFI_STATE_CHAN, struct zego_ux_wifi_state_msg, NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.state = ZEGO_UX_WIFI_STATE_CONNECTING, .mode = ZEGO_WIFI_MODE_STA));

/* ── Wi-Fi mode cycle ──────────────────────────────────────────────────── */

/* Only cycle through modes this build actually exposes (mirrors the
 * ZEGO_WIFI_MODE_*_ENABLED symbols in zego/bricks/wifi/Kconfig) — e.g. a
 * STA+P2P_GO-only build must not long-press its way into a SoftAP that was
 * never compiled in. */
static const enum zego_wifi_mode mode_cycle[] = {
#if defined(CONFIG_ZEGO_WIFI_MODE_STA_ENABLED)
	ZEGO_WIFI_MODE_STA,
#endif
#if defined(CONFIG_ZEGO_WIFI_MODE_SOFTAP_ENABLED)
	ZEGO_WIFI_MODE_SOFTAP,
#endif
#if defined(CONFIG_ZEGO_WIFI_MODE_P2P_GO_ENABLED)
	ZEGO_WIFI_MODE_P2P_GO,
#endif
#if defined(CONFIG_ZEGO_WIFI_MODE_P2P_GC_ENABLED)
	ZEGO_WIFI_MODE_P2P_GC,
#endif
};

static const char *mode_name(enum zego_wifi_mode m)
{
	switch (m) {
	case ZEGO_WIFI_MODE_STA:
		return "sta";
	case ZEGO_WIFI_MODE_SOFTAP:
		return "softap";
	case ZEGO_WIFI_MODE_P2P_GO:
		return "p2p_go";
	case ZEGO_WIFI_MODE_P2P_GC:
		return "p2p_gc";
	default:
		return "unknown";
	}
}

/* ── LED helpers ───────────────────────────────────────────────────────── */

/*
 * Send ROTATE, optionally constraining to a board-specific LED subset.
 *
 * When CONFIG_ZEGO_UX_ROTATE_COUNT > 0 the command carries an explicit index
 * array (FIRST_LED, FIRST_LED+1, …, FIRST_LED+COUNT-1), overriding the LED
 * module's default 0..ROTATE_NUM_LEDS-1 sweep.
 *
 * Example — nRF5340 Audio DK, RGB2 only (indices 3, 4, 5):
 *   CONFIG_ZEGO_UX_ROTATE_FIRST_LED=3  CONFIG_ZEGO_UX_ROTATE_COUNT=3
 */
static void led_rotate(void)
{
	struct led_msg msg = {
		.type = LED_COMMAND_ROTATE,
		.period_ms = 0,
	};

#if CONFIG_ZEGO_UX_ROTATE_COUNT > 0
	msg.rotate_count = CONFIG_ZEGO_UX_ROTATE_COUNT;
	for (uint8_t i = 0; i < (uint8_t)CONFIG_ZEGO_UX_ROTATE_COUNT; i++) {
		msg.rotate_indices[i] = (uint8_t)(CONFIG_ZEGO_UX_ROTATE_FIRST_LED + i);
	}
#endif

	zbus_chan_pub(&LED_CMD_CHAN, &msg, K_NO_WAIT);
}

/*
 * Drive the connected-state LED solid ON.
 *
 * CONFIG_ZEGO_UX_CONNECTED_LED selects the LED index (default 0).
 * CONFIG_ZEGO_UX_CONNECTED_LED_GREEN_ONLY also turns the adjacent LEDs OFF,
 * isolating a single colour channel (e.g. green channel of an RGB LED).
 *
 * Example — nRF5340 Audio DK, green channel of RGB2 (LED 4):
 *   CONFIG_ZEGO_UX_CONNECTED_LED=4  CONFIG_ZEGO_UX_CONNECTED_LED_GREEN_ONLY=y
 *   → LED 4 ON, LED 3 OFF, LED 5 OFF
 */
static void led_connected(void)
{
	struct led_msg on = {
		.type = LED_COMMAND_ON,
		.led_number = CONFIG_ZEGO_UX_CONNECTED_LED,
	};

	zbus_chan_pub(&LED_CMD_CHAN, &on, K_NO_WAIT);

	if (IS_ENABLED(CONFIG_ZEGO_UX_CONNECTED_LED_GREEN_ONLY)) {
		if (CONFIG_ZEGO_UX_CONNECTED_LED > 0) {
			struct led_msg off_r = {
				.type = LED_COMMAND_OFF,
				.led_number = CONFIG_ZEGO_UX_CONNECTED_LED - 1,
			};

			zbus_chan_pub(&LED_CMD_CHAN, &off_r, K_NO_WAIT);
		}
		struct led_msg off_b = {
			.type = LED_COMMAND_OFF,
			.led_number = CONFIG_ZEGO_UX_CONNECTED_LED + 1,
		};

		zbus_chan_pub(&LED_CMD_CHAN, &off_b, K_NO_WAIT);
	}
}

/*
 * Drive the pairing/BLE-provisioning BREATHE effect.
 *
 * CONFIG_ZEGO_UX_PAIRING_LED_IDX selects the LED index (default 0).
 * Set to 5 on nRF5340 Audio DK to breathe the blue channel of RGB2,
 * keeping the green (connected) and red channels distinct.
 */
static void led_breathe_pairing(void)
{
	struct led_msg msg = {
		.type = LED_COMMAND_BREATHE,
		.led_number = CONFIG_ZEGO_UX_PAIRING_LED_IDX,
	};

	zbus_chan_pub(&LED_CMD_CHAN, &msg, K_NO_WAIT);
}

static void apply_wifi_state_led(enum zego_ux_wifi_state state)
{
	switch (state) {
	case ZEGO_UX_WIFI_STATE_CONNECTING:
		led_rotate();
		break;
	case ZEGO_UX_WIFI_STATE_CONNECTED:
		led_connected();
		break;
	case ZEGO_UX_WIFI_STATE_SOFTAP:
		led_rotate();
		break;
	case ZEGO_UX_WIFI_STATE_PAIRING:
		led_breathe_pairing();
		break;
	case ZEGO_UX_WIFI_STATE_ERROR: {
		struct led_msg err = {
			.type = LED_COMMAND_BLINK,
			.led_number = CONFIG_ZEGO_UX_ERROR_LED_IDX,
			.period_ms = 100,
		};

		zbus_chan_pub(&LED_CMD_CHAN, &err, K_NO_WAIT);
		break;
	}
	}
}

/* ── BLE prov toggle state ─────────────────────────────────────────────── */

static enum zego_ux_wifi_state last_wifi_state = ZEGO_UX_WIFI_STATE_CONNECTING;
static bool ble_prov_led_active;
/*
 * Set while a P2P pairing or BLE-prov BREATHE is in progress.
 * Suppresses CONNECTING / ERROR / SOFTAP LED overrides that arrive during
 * the re-pairing disconnect, until the final CONNECTED state clears it.
 */
static bool pairing_led_active;

/*
 * Deferred LED work — runs on the system workqueue.
 *
 * led_cmd_listener() (called via zbus_chan_pub) runs synchronously in the
 * caller's thread.  If it runs in the net_mgmt thread it can race with
 * rotate_work_fn (system workqueue) on the kernel timeout dlist, causing a
 * BUS FAULT in sys_clock_announce.  Submitting the LED command through this
 * work item ensures led_cmd_listener runs on the system workqueue, where it
 * is serialised with rotate_work_fn.
 */
static enum zego_ux_wifi_state pending_wifi_state;

/*
 * Ready flag: set to 1 in zego_ux_init() once the LED module (SYS_INIT
 * priority 91) is fully initialised.  The net_mgmt thread can submit
 * zego_ux_led_work before SYS_INIT reaches the LED module; if the work runs
 * on the sysworkq before k_work_init_delayable() has been called for
 * led_sm[n].effect_work, k_work_schedule() inserts a garbage timeout node
 * into the kernel timeout list and the RTC ISR later crashes with a NULL
 * dereference in sys_dlist_remove.  Gating on this flag prevents that race.
 */
static atomic_t zego_ux_ready = ATOMIC_INIT(0);

static void zego_ux_led_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!atomic_get(&zego_ux_ready)) {
		/* LED module not yet initialised — will be replayed in zego_ux_init */
		return;
	}
	if (ble_prov_led_active) {
		led_breathe_pairing();
	} else {
		apply_wifi_state_led(pending_wifi_state);
	}
}

static K_WORK_DEFINE(zego_ux_led_work, zego_ux_led_work_fn);

/* ── Default (__weak) gesture actions — see ux.h for the override contract ── */

void __weak zego_ux_on_single_click(void)
{
	LOG_INF("Wi-Fi mode: %s  (run 'wifi status' for full details)",
		mode_name(zego_wifi_get_mode()));
}

void __weak zego_ux_on_double_click(void)
{
	enum zego_wifi_mode mode = zego_wifi_get_mode();

	/* In P2P modes the double-click triggers pairing; BLE provisioning is
	 * disabled in those modes so there is no gesture conflict. */
	if (mode == ZEGO_WIFI_MODE_P2P_GO || mode == ZEGO_WIFI_MODE_P2P_GC) {
		LOG_INF("Double-click: triggering P2P pairing");
		wifi_p2p_start_pairing();
		return;
	}
#if defined(CONFIG_ZEGO_WIFI_BLE_PROV)
	{
		static bool adv_enabled = true; /* BLE prov auto-starts at boot */

		adv_enabled = !adv_enabled;
		zego_wifi_ble_prov_advertise(adv_enabled);
		LOG_INF("BLE provisioning advertising: %s", adv_enabled ? "enabled" : "disabled");
	}
#else
	LOG_INF("BLE provisioning not enabled on this board");
#endif
}

void __weak zego_ux_on_long_press(void)
{
	enum zego_wifi_mode cur = zego_wifi_get_mode();
	enum zego_wifi_mode next = ZEGO_WIFI_MODE_STA;

	for (int i = 0; i < ARRAY_SIZE(mode_cycle); i++) {
		if (mode_cycle[i] == cur) {
			next = mode_cycle[(i + 1) % ARRAY_SIZE(mode_cycle)];
			break;
		}
	}

	LOG_INF("Mode cycle: %s → %s — saving and rebooting", mode_name(cur), mode_name(next));

	/* Acknowledge the long press: briefly turn off the first rotate LED
	 * (stops ROTATE and gives the user a visible blink before reboot). */
	uint8_t ack_led =
		(CONFIG_ZEGO_UX_ROTATE_COUNT > 0) ? (uint8_t)CONFIG_ZEGO_UX_ROTATE_FIRST_LED : 0;
	struct led_msg ack = {.type = LED_COMMAND_OFF, .led_number = ack_led};

	zbus_chan_pub(&LED_CMD_CHAN, &ack, K_NO_WAIT);
	k_sleep(K_MSEC(300));

	uint8_t val = (uint8_t)next;
	int ret = settings_save_one("app/zego_wifi_mode", &val, sizeof(val));

	if (ret) {
		LOG_ERR("settings_save_one failed (%d) — mode not saved", ret);
	}

	sys_reboot(SYS_REBOOT_COLD);
}

/* ── UX gesture button listener ────────────────────────────────────────── */

static void btn_listener_cb(const struct zbus_channel *chan)
{
	const struct button_msg *msg = zbus_chan_const_msg(chan);

	/* Only the configured UX gesture button (idx 0 default; BTN5/idx 4 on the
	 * nRF5340 Audio DK via CONFIG_ZEGO_UX_BUTTON_IDX) carries the gestures. */
	if (msg->button_number != CONFIG_ZEGO_UX_BUTTON_IDX) {
		return;
	}

	switch (msg->type) {
	case BUTTON_SINGLE_CLICK:
		zego_ux_on_single_click();
		break;
	case BUTTON_DOUBLE_CLICK:
		zego_ux_on_double_click();
		break;
	case BUTTON_LONG_PRESS:
		zego_ux_on_long_press();
		break;
	default:
		break;
	}
}

ZBUS_LISTENER_DEFINE(app_btn_listener, btn_listener_cb);
ZBUS_CHAN_ADD_OBS(BUTTON_CHAN, app_btn_listener, 0);

/* ── ZEGO_UX_WIFI_STATE_CHAN listener → LED 0 ──────────────────────────── */

static void wifi_state_listener_cb(const struct zbus_channel *chan)
{
	const struct zego_ux_wifi_state_msg *msg = zbus_chan_const_msg(chan);

	last_wifi_state = msg->state;

	/* Don't override BREATHE while BLE provisioning LED is active. */
	if (ble_prov_led_active) {
		return;
	}

	/* Track pairing state; suppress LED overrides until pairing completes. */
	if (msg->state == ZEGO_UX_WIFI_STATE_PAIRING) {
		pairing_led_active = true;
	} else if (msg->state == ZEGO_UX_WIFI_STATE_CONNECTED) {
		pairing_led_active = false;
	} else if (pairing_led_active) {
		/* CONNECTING / ERROR during re-pairing — keep BREATHE active. */
		return;
	}

	/*
	 * Defer the LED command to the system workqueue (see zego_ux_led_work_fn).
	 * Do not call apply_wifi_state_led() directly here — it would invoke
	 * led_cmd_listener synchronously in this thread (net_mgmt event thread),
	 * which races with rotate_work_fn on the kernel timeout dlist.
	 */
	pending_wifi_state = msg->state;
	k_work_submit(&zego_ux_led_work);
}

ZBUS_LISTENER_DEFINE(app_wifi_state_listener, wifi_state_listener_cb);
ZBUS_CHAN_ADD_OBS(ZEGO_UX_WIFI_STATE_CHAN, app_wifi_state_listener, 0);

/* ── BLE_PROV_CONN_CHAN listener → LED BREATHE on phone connect ─────────── */

#if defined(CONFIG_ZEGO_WIFI_BLE_PROV)
static void ble_prov_conn_listener_cb(const struct zbus_channel *chan)
{
	const struct ble_prov_msg *msg = zbus_chan_const_msg(chan);

	ble_prov_led_active = msg->connected;
	pending_wifi_state = last_wifi_state;
	k_work_submit(&zego_ux_led_work);
}

ZBUS_LISTENER_DEFINE(app_ble_prov_conn_listener, ble_prov_conn_listener_cb);
ZBUS_CHAN_ADD_OBS(BLE_PROV_CONN_CHAN, app_ble_prov_conn_listener, 0);
#endif

/* ── SYS_INIT: start ROTATE at boot ───────────────────────────────────── */

static int zego_ux_init(void)
{
	/* Start boot animation.  led_module_init (priority 91) has already run
	 * because this function runs at a higher priority number (>91). */
	led_rotate();

	/* Unblock zego_ux_led_work_fn — LED module is now fully initialised. */
	atomic_set(&zego_ux_ready, 1);

	/* Replay any wifi-state transition that arrived before init completed.
	 * k_work_submit is a no-op if zego_ux_led_work is still pending. */
	if (last_wifi_state != ZEGO_UX_WIFI_STATE_CONNECTING) {
		pending_wifi_state = last_wifi_state;
		k_work_submit(&zego_ux_led_work);
	}

	return 0;
}

SYS_INIT(zego_ux_init, APPLICATION, CONFIG_ZEGO_UX_INIT_PRIORITY);
