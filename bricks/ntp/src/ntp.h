/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef ZEGO_NTP_H
#define ZEGO_NTP_H

/**
 * @file ntp.h
 * @brief Zego NTP brick — SNTP time synchronization.
 *
 * Queries CONFIG_ZEGO_NTP_SERVER once the network is up and sets
 * CLOCK_REALTIME, so log output and application code can rely on
 * real-world wall-clock time. Re-syncs periodically to compensate for
 * crystal drift and retries automatically on failure.
 *
 * The module has no application-specific logic; instead it declares
 * ZEGO_NTP_NET_CHAN (see below), which the application publishes to -
 * typically from its zego/network weak-hook overrides (net_event_app.c) -
 * following the same decoupling pattern used by zego/ux's
 * ZEGO_UX_WIFI_STATE_CHAN.
 *
 * Example — publish network state from net_event_app.c:
 *
 *   #include <ntp.h>
 *
 *   void zego_on_net_event_dhcp_bound(...)
 *   {
 *           struct zego_ntp_net_msg msg = { .connected = true };
 *           zbus_chan_pub(&ZEGO_NTP_NET_CHAN, &msg, K_NO_WAIT);
 *   }
 *
 *   void zego_on_net_event_wifi_disconnect(bool will_retry)
 *   {
 *           struct zego_ntp_net_msg msg = { .connected = false };
 *           zbus_chan_pub(&ZEGO_NTP_NET_CHAN, &msg, K_NO_WAIT);
 *   }
 */

#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── ZEGO_NTP_NET_CHAN — input channel ───────────────────────────────────
 * Owned by this module (defined in ntp.c). The application publishes here
 * whenever network connectivity changes; ZEGO_NTP subscribes and drives its
 * SNTP query / retry / resync state machine accordingly.
 */

/**
 * @brief Network connectivity message published on ZEGO_NTP_NET_CHAN.
 */
struct zego_ntp_net_msg {
	bool connected; /**< true when the network is up, false when it is lost. */
};

ZBUS_CHAN_DECLARE(ZEGO_NTP_NET_CHAN);

/**
 * @brief Initialize the NTP sync module.
 *
 * Called automatically by SYS_INIT at APPLICATION priority.
 *
 * @return 0 on success, negative errno on failure.
 */
int zego_ntp_init(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEGO_NTP_H */
