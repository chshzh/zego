/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file ux.c
 * @brief Application UX module — Button 0 gestures and LED 0 Wi-Fi feedback.
 *
 * Button 0 gesture → action mapping:
 *   SINGLE_CLICK  Print current Wi-Fi mode to UART log.
 *   DOUBLE_CLICK  Toggle BLE provisioning LED (BREATHE ↔ last Wi-Fi state).
 *                 (Full BLE adv toggle requires CONFIG_ZEGO_WIFI_BLE_PROV=y
 *                  and a future zego_wifi_ble_prov_advertise() API.)
 *   LONG_PRESS    Cycle Wi-Fi mode STA → SoftAP → P2P_GO → STA,
 *                 save to NVS via settings, reboot.
 *
 * LED 0 state machine driven by APP_WIFI_STATE_CHAN:
 *   CONNECTING  →  MARQUEE  (starts at boot via SYS_INIT)
 *   CONNECTED   →  Solid ON
 *   SOFTAP      →  Slow BLINK  (500 ms half-period)
 *   ERROR       →  Fast BLINK  (100 ms half-period)
 *   BLE_PROV    →  BREATHE     (double-click toggle, local to this module)
 *
 * Inputs:  BUTTON_CHAN, APP_WIFI_STATE_CHAN
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
#include "../messages.h"
#if defined(CONFIG_ZEGO_WIFI_BLE_PROV)
#include "wifi_ble_prov.h"
#endif

LOG_MODULE_REGISTER(app_ux, LOG_LEVEL_INF);

/* ── Wi-Fi mode cycle ──────────────────────────────────────────────────── */

static const enum zego_wifi_mode mode_cycle[] = {
	ZEGO_WIFI_MODE_STA,
	ZEGO_WIFI_MODE_SOFTAP,
	ZEGO_WIFI_MODE_P2P_GO,
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
	case ZEGO_WIFI_MODE_P2P_CLIENT:
		return "p2p_client";
	default:
		return "unknown";
	}
}

static void do_mode_cycle(void)
{
	enum zego_wifi_mode cur = zego_wifi_get_mode();
	enum zego_wifi_mode next = ZEGO_WIFI_MODE_STA;

	for (int i = 0; i < ARRAY_SIZE(mode_cycle); i++) {
		if (mode_cycle[i] == cur) {
			next = mode_cycle[(i + 1) % ARRAY_SIZE(mode_cycle)];
			break;
		}
	}

	LOG_INF("Mode cycle: %s → %s — saving and rebooting",
		mode_name(cur), mode_name(next));

	/* Acknowledge the long press with a brief LED-off before reboot. */
	struct led_msg ack = {.type = LED_COMMAND_OFF, .led_number = 0};

	zbus_chan_pub(&LED_CMD_CHAN, &ack, K_NO_WAIT);
	k_sleep(K_MSEC(300));

	uint8_t val = (uint8_t)next;
	int ret = settings_save_one("app/app_wifi_mode", &val, sizeof(val));

	if (ret) {
		LOG_ERR("settings_save_one failed (%d) — mode not saved", ret);
	}

	sys_reboot(SYS_REBOOT_COLD);
}

/* ── LED helpers ───────────────────────────────────────────────────────── */

static void led_set(enum led_msg_type type, uint16_t period_ms)
{
	struct led_msg msg = {
		.type = type,
		.led_number = 0,
		.period_ms = period_ms,
	};

	zbus_chan_pub(&LED_CMD_CHAN, &msg, K_NO_WAIT);
}

static void apply_wifi_state_led(enum app_wifi_state state)
{
	switch (state) {
	case APP_WIFI_STATE_CONNECTING:
		led_set(LED_COMMAND_MARQUEE, 0);
		break;
	case APP_WIFI_STATE_CONNECTED:
		led_set(LED_COMMAND_ON, 0);
		break;
	case APP_WIFI_STATE_SOFTAP:
		led_set(LED_COMMAND_BLINK, 500);
		break;
	case APP_WIFI_STATE_ERROR:
		led_set(LED_COMMAND_BLINK, 100);
		break;
	}
}

/* ── BLE prov toggle state ─────────────────────────────────────────────── */

static enum app_wifi_state last_wifi_state = APP_WIFI_STATE_CONNECTING;
static bool ble_prov_led_active;

/* ── Button 0 listener ─────────────────────────────────────────────────── */

static void btn_listener_cb(const struct zbus_channel *chan)
{
	const struct button_msg *msg = zbus_chan_const_msg(chan);

	if (msg->button_number != 0) {
		return;
	}

	switch (msg->type) {
	case BUTTON_SINGLE_CLICK:
		LOG_INF("Wi-Fi mode: %s  (run 'wifi status' for full details)",
			mode_name(zego_wifi_get_mode()));
		break;

	case BUTTON_DOUBLE_CLICK:
#if defined(CONFIG_ZEGO_WIFI_BLE_PROV)
		ble_prov_led_active = !ble_prov_led_active;
		zego_wifi_ble_prov_advertise(ble_prov_led_active);
		if (ble_prov_led_active) {
			LOG_INF("BLE provisioning: enabled");
			led_set(LED_COMMAND_BREATHE, 0);
		} else {
			LOG_INF("BLE provisioning: disabled");
			apply_wifi_state_led(last_wifi_state);
		}
#else
		LOG_INF("BLE provisioning not enabled on this board");
#endif
		break;

	case BUTTON_LONG_PRESS:
		do_mode_cycle();
		break;

	default:
		break;
	}
}

ZBUS_LISTENER_DEFINE(app_btn_listener, btn_listener_cb);
ZBUS_CHAN_ADD_OBS(BUTTON_CHAN, app_btn_listener, 0);

/* ── APP_WIFI_STATE_CHAN listener → LED 0 ──────────────────────────────── */

static void wifi_state_listener_cb(const struct zbus_channel *chan)
{
	const struct app_wifi_state_msg *msg = zbus_chan_const_msg(chan);

	last_wifi_state = msg->state;

	/* Don't override BREATHE while BLE provisioning LED is active. */
	if (ble_prov_led_active) {
		return;
	}

	apply_wifi_state_led(msg->state);
}

ZBUS_LISTENER_DEFINE(app_wifi_state_listener, wifi_state_listener_cb);
ZBUS_CHAN_ADD_OBS(APP_WIFI_STATE_CHAN, app_wifi_state_listener, 0);

/* ── SYS_INIT: start MARQUEE at boot ───────────────────────────────────── */

static int app_ux_init(void)
{
	led_set(LED_COMMAND_MARQUEE, 0);
	return 0;
}

SYS_INIT(app_ux_init, APPLICATION, CONFIG_APP_UX_INIT_PRIORITY);
