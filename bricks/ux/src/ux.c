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
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/net_if.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#include <string.h>

#include "button.h"
#include "console.h"
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

/* ── Startup banner ────────────────────────────────────────────────────
 *
 * zego_ux_print_banner() is called once from the application's main()
 * after SYS_INIT has run.  It is split into four sections, each owning
 * its own leading "====" separator (except banner_fw_info(), whose
 * separator opens the whole banner):
 *
 *   banner_fw_info()               - app name, version, PRD, specs,
 *                                     build date, board, MAC.
 *   banner_compiled_zego_modules() - zego bricks compiled into this image.
 *   banner_compiled_app_modules()  - __weak; override in the application
 *                                     to list its own (non-zego) modules.
 *   banner_wifi_modes_instructions() - Wi-Fi mode + connection instructions
 *                                       (moved from zego/wifi's
 *                                       zego_banner_wifi_info()); ends the
 *                                       banner with the final separator.
 * ──────────────────────────────────────────────────────────────────────
 */

#ifndef ZEGO_BANNER_APP_NAME
#define ZEGO_BANNER_APP_NAME "unknown"
#endif

#ifndef ZEGO_MODULES_VERSION
#define ZEGO_MODULES_VERSION "unknown"
#endif

static const char *get_board_name(void)
{
	if (strstr(CONFIG_BOARD, "nrf7002dk") != NULL) {
		return "nRF7002DK";
	} else if (strstr(CONFIG_BOARD, "nrf54lm20dk") != NULL) {
		return "nRF54LM20DK + nRF7002EBII";
	} else if (strstr(CONFIG_BOARD, "nrf5340_audio_dk") != NULL) {
		return "nRF5340 Audio DK + nRF7002EK";
	}
	return CONFIG_BOARD;
}

static void banner_fw_info(void)
{
	struct net_if *iface = net_if_get_default();
	struct net_linkaddr *mac = NULL;

	/* Wi-Fi driver populates the link address after SYS_INIT completes.
	 * Poll briefly (up to 200 ms) so main() can still print the MAC. */
	for (int i = 0; i < 20; i++) {
		struct net_linkaddr *candidate = net_if_get_link_addr(iface);

		if (candidate && candidate->len == 6) {
			mac = candidate;
			break;
		}
		k_sleep(K_MSEC(10));
	}

	LOG_INF("==============================================");
	LOG_INF(" " CLR_BLU "%s" CLR_RST, ZEGO_BANNER_APP_NAME);
	LOG_INF("==============================================");
	LOG_INF("Version: " CLR_GRN "%s" CLR_RST, APP_VERSION_STRING);
	LOG_INF("PRD:     " CLR_GRN "%s" CLR_RST, CONFIG_ZEGO_APP_PRD_VERSION);
	LOG_INF("Specs:   " CLR_GRN "%s" CLR_RST, CONFIG_ZEGO_APP_SPECS_VERSION);
	LOG_INF("Build:   " CLR_GRN "%s %s" CLR_RST, __DATE__, __TIME__);
	LOG_INF("Board:   " CLR_GRN "%s" CLR_RST, get_board_name());

	if (mac) {
		LOG_INF("MAC:     " CLR_GRN "%02X:%02X:%02X:%02X:%02X:%02X" CLR_RST, mac->addr[0],
			mac->addr[1], mac->addr[2], mac->addr[3], mac->addr[4], mac->addr[5]);
	}
}

static void banner_compiled_zego_modules(void)
{
	LOG_INF("==============================================");
	LOG_INF("ZEGO(" CLR_GRN "%s" CLR_RST "):", ZEGO_MODULES_VERSION);
#if CONFIG_ZEGO_BUTTON
	LOG_INF("  " CLR_BLU "button" CLR_RST);
#endif
#if CONFIG_ZEGO_LED
	LOG_INF("  " CLR_BLU "led" CLR_RST);
#endif
	LOG_INF("  " CLR_BLU "ux" CLR_RST);
	LOG_INF("  " CLR_BLU "wifi" CLR_RST);
#if CONFIG_ZEGO_NETWORK
	LOG_INF("  " CLR_BLU "network" CLR_RST);
#endif
#if CONFIG_ZEGO_WIFI_BLE_PROV
	LOG_INF("  " CLR_BLU "wifi_ble_prov" CLR_RST);
#endif
#if CONFIG_ZEGO_MEMONITOR
	LOG_INF("  " CLR_BLU "memonitor" CLR_RST);
#endif
}

/**
 * @brief Print the application's own (non-zego) compiled-module list.
 *
 * No-op default — override with a strong definition in the application to
 * list its own modules (e.g. app_http, app_mqtt, app_memfault). Must open
 * with a "====" separator line when overridden, matching the other banner
 * sections.
 */
void __weak banner_compiled_app_modules(void)
{
}

static void banner_wifi_modes_instructions(void)
{
	enum zego_wifi_mode mode = zego_wifi_get_mode();
	const char *mode_str;

	LOG_INF("==============================================");

	switch (mode) {
	case ZEGO_WIFI_MODE_STA:
		mode_str = "STA";
		break;
	case ZEGO_WIFI_MODE_SOFTAP:
		mode_str = "SoftAP";
		break;
	case ZEGO_WIFI_MODE_P2P_GO:
		mode_str = "P2P_GO";
		break;
	case ZEGO_WIFI_MODE_P2P_GC:
		mode_str = "P2P_GC";
		break;
	default:
		mode_str = "Unknown";
		break;
	}

	LOG_INF("Current Wi-Fi Mode:  " CLR_CYNB "%s" CLR_RST, mode_str);

	/* Only advertise the mode-switch command when more than one mode is
	 * compiled in — a single-mode build has nothing to switch to. */
#define _ZEGO_WIFI_MODE_COUNT                                                                      \
	(IS_ENABLED(CONFIG_ZEGO_WIFI_MODE_STA_ENABLED) +                                           \
	 IS_ENABLED(CONFIG_ZEGO_WIFI_MODE_SOFTAP_ENABLED) +                                        \
	 IS_ENABLED(CONFIG_ZEGO_WIFI_MODE_P2P_GO_ENABLED) +                                        \
	 IS_ENABLED(CONFIG_ZEGO_WIFI_MODE_P2P_GC_ENABLED))

#if _ZEGO_WIFI_MODE_COUNT > 1
	LOG_INF(CLR_PRP "Type 'zego_wifi_mode ["
#if CONFIG_ZEGO_WIFI_MODE_STA_ENABLED
			"sta"
#endif
#if CONFIG_ZEGO_WIFI_MODE_SOFTAP_ENABLED
			"|softap"
#endif
#if CONFIG_ZEGO_WIFI_MODE_P2P_GO_ENABLED
			"|p2p_go"
#endif
#if CONFIG_ZEGO_WIFI_MODE_P2P_GC_ENABLED
			"|p2p_gc"
#endif
			"]' to change mode." CLR_RST);
#endif

	LOG_INF("----------------------------------------------");

	switch (mode) {
	case ZEGO_WIFI_MODE_STA:
		LOG_INF("Connect using any available option for the current Wi-Fi Mode:");

#if CONFIG_NET_L2_WIFI_SHELL
		LOG_INF("----------------------------------------------");
		LOG_INF(CLR_PRP "[ Shell: one-time connect ]" CLR_RST);
		LOG_INF(CLR_PRP "  wifi connect -s <SSID> -p <pass> -k 1" CLR_RST);
		LOG_INF(CLR_PRP "  wifi connect --help     (more options)" CLR_RST);
#if CONFIG_WIFI_CREDENTIALS
		LOG_INF("----------------------------------------------");
		LOG_INF(CLR_PRP "[ Shell: saved credentials (auto-connect on reboot) ]" CLR_RST);
		LOG_INF(CLR_PRP "  wifi cred add <SSID> WPA2-PSK <pass> -k 1" CLR_RST);
		LOG_INF(CLR_PRP "  wifi cred auto_connect (trigger auto-connect attempt without "
				"reboot)" CLR_RST);
		LOG_INF(CLR_PRP "  wifi cred list          (show stored networks)" CLR_RST);
		LOG_INF(CLR_PRP "  wifi cred delete <SSID> (remove a network)" CLR_RST);
#endif
#endif

#if CONFIG_ZEGO_WIFI_BLE_PROV
		{
			struct net_if *_iface = net_if_get_first_wifi();
			struct net_linkaddr *_mac = _iface ? net_if_get_link_addr(_iface) : NULL;
			char ble_name[9];

			if (_mac && _mac->len >= 6) {
				snprintf(ble_name, sizeof(ble_name), "PV%02X%02X%02X",
					 _mac->addr[3], _mac->addr[4], _mac->addr[5]);
			} else {
				snprintf(ble_name, sizeof(ble_name), "PVxxxxxx");
			}
			LOG_INF("----------------------------------------------");
			LOG_INF(CLR_PRP "[ BLE provisioning (saves credentials) ]" CLR_RST);
			LOG_INF(CLR_PRP "  Device name : %s" CLR_RST, ble_name);
			LOG_INF(CLR_PRP
				"  1. Install 'nRF Wi-Fi Provisioner' on your phone" CLR_RST);
			LOG_INF(CLR_PRP "  2. Open the app and connect to '%s'" CLR_RST, ble_name);
			LOG_INF(CLR_PRP "  3. Select your AP and enter the password" CLR_RST);
			LOG_INF(CLR_PRP
				"  4. Device connects and saves credentials for reboot" CLR_RST);
		}
#endif
		break;

#if CONFIG_NRF70_AP_MODE
	case ZEGO_WIFI_MODE_SOFTAP:
#if defined(CONFIG_APP_WIFI_SSID)
		LOG_INF(CLR_PRP "Connect to AP SSID='%s' Password='%s'" CLR_RST,
			CONFIG_APP_WIFI_SSID, CONFIG_APP_WIFI_PASSWORD);
#else
		LOG_INF("SoftAP: connect to the configured SSID.");
#endif
		break;
#endif /* CONFIG_NRF70_AP_MODE */

#if CONFIG_ZEGO_WIFI_MODE_P2P_GO_ENABLED
	case ZEGO_WIFI_MODE_P2P_GO: {
		struct net_if *_iface = net_if_get_first_wifi();
		struct net_linkaddr *_mac = _iface ? net_if_get_link_addr(_iface) : NULL;
		char mac_str[18] = "XX:XX:XX:XX:XX:XX";

		if (_mac && _mac->len >= 6) {
			snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
				 _mac->addr[0], _mac->addr[1], _mac->addr[2], _mac->addr[3],
				 _mac->addr[4], _mac->addr[5]);
		}
		LOG_INF(CLR_PRP "P2P_GO: GO MAC %s" CLR_RST, mac_str);
		LOG_INF(CLR_PRP "  [ DK as P2P_GC ]" CLR_RST);
		LOG_INF(CLR_PRP
			"  Double-click Button 0 here to open the PBC pairing window," CLR_RST);
		LOG_INF(CLR_PRP "  then double-click Button 0 on the P2P_GC DK." CLR_RST);
		LOG_INF(CLR_PRP "  Manual (on the GC): wifi p2p connect %s pbc --join" CLR_RST,
			mac_str);
		LOG_INF(CLR_PRP "  [ Phone as P2P_GC ]" CLR_RST);
		LOG_INF(CLR_PRP
			"  Double-click Button 0 here to open the PBC pairing window," CLR_RST);
		LOG_INF(CLR_PRP "  then on the phone: Wi-Fi -> Wi-Fi Direct -> select this DK -> "
				"Connect" CLR_RST);
		break;
	}
#endif /* CONFIG_ZEGO_WIFI_MODE_P2P_GO_ENABLED */

#if CONFIG_ZEGO_WIFI_MODE_P2P_GC_ENABLED
	case ZEGO_WIFI_MODE_P2P_GC:
		LOG_INF(CLR_PRP
			"P2P_GC mode: double-click Button 0 to pair with a P2P_GO DK" CLR_RST);
		LOG_INF(CLR_PRP "  (GO's pairing window must be open - double-click Button 0 on GO "
				"first)." CLR_RST);
		LOG_INF(CLR_PRP
			"  Once paired, the GO MAC is saved to NVS and the GC reconnects" CLR_RST);
		LOG_INF(CLR_PRP "  automatically after reboot." CLR_RST);
		LOG_INF(CLR_PRP "  Manual (DK GO): wifi p2p find -> wifi p2p peer -> wifi p2p "
				"connect <GO MAC> pbc --join" CLR_RST);
		LOG_INF(CLR_PRP "  Manual (Phone GO): wifi p2p find -> wifi p2p peer (find phone "
				"MAC) ->" CLR_RST);
		LOG_INF(CLR_PRP
			"    wifi p2p connect <phone MAC> pbc -g 0 -> accept on phone" CLR_RST);
		break;
#endif /* CONFIG_ZEGO_WIFI_MODE_P2P_GC_ENABLED */

	default:
		break;
	}

	LOG_INF("==============================================");
}

void zego_ux_print_banner(void)
{
	/* Flush all pending log messages generated by SYS_INIT modules before
	 * printing the banner.  Without this, the burst of audio/codec/USB init
	 * logs that arrive just after the MAC poll can overflow-evict individual
	 * banner lines (CONFIG_LOG_MODE_OVERFLOW=y drops the oldest pending
	 * message when the ring buffer fills). */
	log_process();
	k_sleep(K_MSEC(50));
	log_process();

	banner_fw_info();
	banner_compiled_zego_modules();
	banner_compiled_app_modules();
	banner_wifi_modes_instructions();
}

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
