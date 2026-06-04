/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * wifi_mode_selector.c — Wi-Fi mode persistence and selection.
 *
 * Loads the persisted Wi-Fi mode from NVS at SYS_INIT APPLICATION priority 0
 * and publishes it on WIFI_MODE_CHAN so the network module can start in the
 * correct mode.
 *
 * The 'app_wifi_mode [softap|sta|p2p_go|p2p_client]' shell command saves the
 * new mode to NVS and performs a cold reboot to apply it.
 *
 * NVS note: mode values are stored as uint8_t.  The enum order in wifi.h
 * must not change without erasing NVS (--erase / --recover) on all devices.
 */

#include "wifi.h"

#include <string.h>
#include <strings.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(wifi_mode_selector, CONFIG_ZEGO_WIFI_LOG_LEVEL);

/* ============================================================================
 * ZBUS CHANNEL DEFINITION
 * ============================================================================
 */

ZBUS_CHAN_DEFINE(WIFI_MODE_CHAN, struct wifi_mode_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

/* ============================================================================
 * MODULE STATE
 * ============================================================================
 */

static enum zego_wifi_mode s_mode;

/* ============================================================================
 * NVS / SETTINGS PERSISTENCE
 * ============================================================================
 */

static int settings_set_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (strcmp(key, "app_wifi_mode") == 0 && len == sizeof(uint8_t)) {
		uint8_t val;
		ssize_t rc = read_cb(cb_arg, &val, sizeof(val));

		if (rc >= 0) {
			s_mode = (enum zego_wifi_mode)val;
		}
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(wifi_mode_selector_settings, "app", NULL, settings_set_cb, NULL,
			       NULL);

static int nvs_save_mode(enum zego_wifi_mode mode)
{
	uint8_t val = (uint8_t)mode;
	int ret = settings_save_one("app/app_wifi_mode", &val, sizeof(val));

	if (ret) {
		LOG_ERR("Failed to save mode to NVS: %d", ret);
	}
	return ret;
}

/* ============================================================================
 * HELPER UTILITIES
 * ============================================================================
 */

static const char *mode_to_str(enum zego_wifi_mode mode)
{
	switch (mode) {
	case ZEGO_WIFI_MODE_STA:
		return "STA";
	case ZEGO_WIFI_MODE_SOFTAP:
		return "SoftAP";
	case ZEGO_WIFI_MODE_P2P_GO:
		return "P2P_GO";
	case ZEGO_WIFI_MODE_P2P_CLIENT:
		return "P2P_CLIENT";
	default:
		return "Unknown";
	}
}

static void publish_mode(enum zego_wifi_mode mode)
{
	struct wifi_mode_msg msg = {.mode = mode};
	int ret = zbus_chan_pub(&WIFI_MODE_CHAN, &msg, K_NO_WAIT);

	if (ret) {
		LOG_ERR("Failed to publish WIFI_MODE_CHAN: %d", ret);
	}
}

/* ============================================================================
 * SHELL COMMAND: app_wifi_mode [softap|sta|p2p_go|p2p_client]
 *
 * Can be run at any time. Saves the new mode to NVS and performs a cold
 * reboot so the system starts cleanly in that mode.
 * ============================================================================
 */

#if CONFIG_SHELL
static int cmd_wifi_mode(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh,
			    "Current mode: %s\r\n"
			    "Usage: app_wifi_mode [softap|sta|p2p_go|p2p_client]\r\n"
			    "  sta        (join existing Wi-Fi)\r\n"
#if CONFIG_NRF70_AP_MODE
			    "  softap     (create own SoftAP, IP 192.168.7.1)\r\n"
#endif
#if CONFIG_NRF70_P2P_MODE
			    "  p2p_go     (Wi-Fi Direct, device is Group Owner)\r\n"
			    "  p2p_client (Wi-Fi Direct, device joins phone group)\r\n"
#endif
			    "Board reboots automatically after mode change.",
			    mode_to_str(s_mode));
		return 0;
	}

	const char *arg = argv[1];
	enum zego_wifi_mode new_mode;

	if (strcasecmp(arg, "STA") == 0) {
		new_mode = ZEGO_WIFI_MODE_STA;
	} else if (strcasecmp(arg, "SoftAP") == 0) {
#if CONFIG_NRF70_AP_MODE
		new_mode = ZEGO_WIFI_MODE_SOFTAP;
#else
		shell_error(sh, "SoftAP not available — CONFIG_NRF70_AP_MODE not set");
		return -EINVAL;
#endif
	} else if (strcasecmp(arg, "P2P_GO") == 0) {
#if CONFIG_NRF70_P2P_MODE
		new_mode = ZEGO_WIFI_MODE_P2P_GO;
#else
		shell_error(sh, "P2P_GO not available — build with -DSNIPPET=wifi-p2p");
		return -EINVAL;
#endif
	} else if (strcasecmp(arg, "P2P_CLIENT") == 0) {
#if CONFIG_NRF70_P2P_MODE
		new_mode = ZEGO_WIFI_MODE_P2P_CLIENT;
#else
		shell_error(sh, "P2P_CLIENT not available — build with -DSNIPPET=wifi-p2p");
		return -EINVAL;
#endif
	} else {
		shell_error(sh, "Invalid mode '%s'. Use softap, sta, p2p_go, or p2p_client.", arg);
		return -EINVAL;
	}

	if (new_mode == s_mode) {
		shell_print(sh, "Already in %s mode, no change.", mode_to_str(s_mode));
		return 0;
	}

	shell_print(sh, "Switching to %s mode -- rebooting...", mode_to_str(new_mode));

	int ret = nvs_save_mode(new_mode);

	if (ret == 0) {
		LOG_INF("Mode saved: %s -> rebooting", mode_to_str(new_mode));
	}

	/* Brief delay so shell output flushes before the reboot */
	k_sleep(K_MSEC(200));
	sys_reboot(SYS_REBOOT_COLD);

	return 0;
}

SHELL_CMD_ARG_REGISTER(app_wifi_mode, NULL,
		       "Set Wi-Fi mode and reboot: app_wifi_mode [softap|sta|p2p_go|p2p_client]",
		       cmd_wifi_mode, 1, 1);
#endif /* CONFIG_SHELL */

/* ============================================================================
 * SYS_INIT FUNCTION (APPLICATION priority 0)
 * ============================================================================
 */

static int mode_selector_init(void)
{
	int ret;

	/* Compile-time default if NVS has no stored value (e.g. fresh --erase flash) */
#if CONFIG_ZEGO_WIFI_DEFAULT_WIFI_MODE_SOFTAP
	s_mode = ZEGO_WIFI_MODE_SOFTAP;
#elif CONFIG_ZEGO_WIFI_DEFAULT_WIFI_MODE_P2P_GO
	s_mode = ZEGO_WIFI_MODE_P2P_GO;
#elif CONFIG_ZEGO_WIFI_DEFAULT_WIFI_MODE_P2P_CLIENT
	s_mode = ZEGO_WIFI_MODE_P2P_CLIENT;
#else
	s_mode = ZEGO_WIFI_MODE_STA;
#endif

	ret = settings_subsys_init();
	if (ret) {
		LOG_WRN("settings_subsys_init failed (%d), using default mode: %s", ret,
			mode_to_str(s_mode));
	} else {
		settings_load_subtree("app");

		/* Validate loaded mode against what this build actually supports.
		 * If NVS holds e.g. SOFTAP but CONFIG_NRF70_AP_MODE is not set in
		 * this build (e.g. firmware downgrade), fall back to STA and
		 * persist the correction so the next boot is clean. */
		bool valid = true;

#if !CONFIG_NRF70_AP_MODE
		if (s_mode == ZEGO_WIFI_MODE_SOFTAP) {
			valid = false;
		}
#endif
#if !CONFIG_NRF70_P2P_MODE
		if (s_mode == ZEGO_WIFI_MODE_P2P_GO || s_mode == ZEGO_WIFI_MODE_P2P_CLIENT) {
			valid = false;
		}
#endif
		if (!valid) {
			LOG_WRN("Stored mode '%s' unsupported by this build — falling back to STA",
				mode_to_str(s_mode));
			s_mode = ZEGO_WIFI_MODE_STA;
			nvs_save_mode(s_mode);
		}

		LOG_INF("Active wifi mode: %s", mode_to_str(s_mode));
	}

	publish_mode(s_mode);

	return 0;
}

SYS_INIT(mode_selector_init, APPLICATION, 0);

/* ============================================================================
 * PUBLIC API
 * ============================================================================
 */

enum zego_wifi_mode zego_wifi_get_mode(void)
{
	return s_mode;
}
