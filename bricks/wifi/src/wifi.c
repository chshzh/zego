/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * wifi.c - Wi-Fi mode storage: WIFI_MODE_CHAN and zego_wifi_get_mode().
 *
 * The startup banner (previously printed from this file) has moved to
 * zego/bricks/ux — see zego_ux_print_banner() in ux.c.
 */

#include "wifi.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zego_wifi, CONFIG_ZEGO_WIFI_LOG_LEVEL);

/* WIFI_MODE_CHAN is always defined here so wifi.c is the single owner
 * regardless of whether ZEGO_WIFI_MODE_SELECTOR is enabled. */
ZBUS_CHAN_DEFINE(WIFI_MODE_CHAN, struct wifi_mode_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

#if !CONFIG_ZEGO_WIFI_MODE_SELECTOR
/* STA-only build: no NVS access needed.  Publish STA on WIFI_MODE_CHAN at the
 * same APPLICATION priority 0 that wifi_mode_selector would use, so the
 * network module (priority 5) always sees a valid channel value. */
enum zego_wifi_mode zego_wifi_get_mode(void)
{
	return ZEGO_WIFI_MODE_STA;
}

static int wifi_sta_mode_init(void)
{
	struct wifi_mode_msg msg = {.mode = ZEGO_WIFI_MODE_STA};

	zbus_chan_pub(&WIFI_MODE_CHAN, &msg, K_NO_WAIT);
	return 0;
}

SYS_INIT(wifi_sta_mode_init, APPLICATION, 0);
#endif /* !CONFIG_ZEGO_WIFI_MODE_SELECTOR */
