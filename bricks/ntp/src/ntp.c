/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * NTP time synchronization module.
 *
 * Subscribes to ZEGO_NTP_NET_CHAN (published by the application, typically
 * from its zego/network weak-hook overrides). On connect, queries the
 * configured SNTP server and sets CLOCK_REALTIME so Zephyr log output shows
 * real-world wall-clock timestamps when CONFIG_LOG_TIMESTAMP_USE_REALTIME=y.
 *
 * Retries failed queries via a k_work_delayable item on the system work
 * queue — no dedicated thread required. Resets on disconnect so a fresh
 * sync is performed after each reconnect.
 */

#include "ntp.h"

#include <zephyr/kernel.h>
#include <zephyr/net/sntp.h>
#include <zephyr/sys/clock.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/init.h>

LOG_MODULE_REGISTER(zego_ntp, CONFIG_ZEGO_NTP_LOG_LEVEL);

/* Definition of ZEGO_NTP_NET_CHAN — declared in ntp.h. Publisher is
 * typically the application's zego/network weak-hook overrides. */
ZBUS_CHAN_DEFINE(ZEGO_NTP_NET_CHAN, struct zego_ntp_net_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.connected = false));

static bool ntp_synced;
static bool ntp_network_ready;

static void ntp_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(ntp_work, ntp_work_handler);

static void ntp_work_handler(struct k_work *work)
{
	struct sntp_time ts;
	struct timespec tspec;
	int ret;

	if (!ntp_network_ready) {
		return;
	}

	LOG_INF("Querying %s ...", CONFIG_ZEGO_NTP_SERVER);

	ret = sntp_simple(CONFIG_ZEGO_NTP_SERVER, CONFIG_ZEGO_NTP_TIMEOUT_MS, &ts);
	if (ret < 0) {
		LOG_WRN("SNTP query failed (%d) - retry in %ds", ret,
			CONFIG_ZEGO_NTP_RETRY_INTERVAL_SEC);
		k_work_reschedule(&ntp_work, K_SECONDS(CONFIG_ZEGO_NTP_RETRY_INTERVAL_SEC));
		return;
	}

	tspec.tv_sec = (time_t)ts.seconds;
	tspec.tv_nsec = ((uint64_t)ts.fraction * NSEC_PER_SEC) >> 32;

	ret = sys_clock_settime(SYS_CLOCK_REALTIME, &tspec);
	if (ret < 0) {
		LOG_ERR("sys_clock_settime failed (%d)", ret);
		return;
	}

	ntp_synced = true;
	LOG_INF("Time synced, epoch %llu (next resync in %d s)", ts.seconds,
		CONFIG_ZEGO_NTP_RESYNC_INTERVAL_SEC);
	k_work_reschedule(&ntp_work, K_SECONDS(CONFIG_ZEGO_NTP_RESYNC_INTERVAL_SEC));
}

static void ntp_notify_connected(void)
{
	ntp_network_ready = true;
	if (!ntp_synced) {
		k_work_reschedule(&ntp_work, K_NO_WAIT);
	}
}

static void ntp_notify_disconnected(void)
{
	ntp_network_ready = false;
	ntp_synced = false;
	k_work_cancel_delayable(&ntp_work);
}

static void ntp_net_cb(const struct zbus_channel *chan)
{
	const struct zego_ntp_net_msg *msg = zbus_chan_const_msg(chan);

	if (msg->connected) {
		ntp_notify_connected();
	} else {
		ntp_notify_disconnected();
	}
}

ZBUS_LISTENER_DEFINE(ntp_net_listener, ntp_net_cb);
ZBUS_CHAN_ADD_OBS(ZEGO_NTP_NET_CHAN, ntp_net_listener, 0);

int zego_ntp_init(void)
{
	LOG_INF("NTP sync initialized (server: %s)", CONFIG_ZEGO_NTP_SERVER);
	return 0;
}

static int zego_ntp_module_init(void)
{
	return zego_ntp_init();
}
SYS_INIT(zego_ntp_module_init, APPLICATION, 3);
