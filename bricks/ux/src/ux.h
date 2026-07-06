/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef ZEGO_UX_H
#define ZEGO_UX_H

/**
 * @file ux.h
 * @brief Zego UX brick — Button 0 gestures and LED 0 Wi-Fi feedback.
 *
 * Subscribes to BUTTON_CHAN and ZEGO_UX_WIFI_STATE_CHAN, publishes to
 * LED_CMD_CHAN.  The application (or any zego brick that tracks Wi-Fi
 * connectivity, e.g. net_event_app.c overriding zego/network's weak hooks)
 * publishes the current state to ZEGO_UX_WIFI_STATE_CHAN to drive LED 0.
 *
 * Example — publish a state transition:
 *
 *   struct zego_ux_wifi_state_msg msg = {
 *           .state = ZEGO_UX_WIFI_STATE_CONNECTED,
 *           .mode = ZEGO_WIFI_MODE_STA,
 *   };
 *   zbus_chan_pub(&ZEGO_UX_WIFI_STATE_CHAN, &msg, K_NO_WAIT);
 *
 * Weak-hook API — each Button 0 gesture action below is a __weak function
 * with a sensible default implementation in ux.c.  Override any of them
 * with a strong definition elsewhere in the application to fully replace
 * that gesture's behaviour; the LED state machine and button-index
 * filtering (CONFIG_ZEGO_UX_BUTTON_IDX) are unaffected.
 */

#include <zephyr/zbus/zbus.h>
#include <wifi.h> /* enum zego_wifi_mode */

#ifdef __cplusplus
extern "C" {
#endif

/* ── ZEGO_UX_WIFI_STATE_CHAN — input channel ─────────────────────────────── */

/** @brief Application-level Wi-Fi state used to drive LED 0. */
enum zego_ux_wifi_state {
	/** Boot / connecting — ROTATE on LED 0. */
	ZEGO_UX_WIFI_STATE_CONNECTING = 0,
	/** STA or P2P link established (IP assigned / peer joined). */
	ZEGO_UX_WIFI_STATE_CONNECTED,
	/** SoftAP active — first client connected. */
	ZEGO_UX_WIFI_STATE_SOFTAP,
	/** P2P pairing in progress (GO window open or GC discovering/joining) — BREATHE. */
	ZEGO_UX_WIFI_STATE_PAIRING,
	/** Link lost or fatal error. */
	ZEGO_UX_WIFI_STATE_ERROR,
};

/** @brief Message published on ZEGO_UX_WIFI_STATE_CHAN. */
struct zego_ux_wifi_state_msg {
	enum zego_ux_wifi_state state;
	enum zego_wifi_mode mode;
};

/**
 * @brief ZEGO_UX_WIFI_STATE_CHAN — input channel; publish the current Wi-Fi
 * connectivity state here to drive LED 0's ROTATE/ON/BREATHE/BLINK effects.
 */
ZBUS_CHAN_DECLARE(ZEGO_UX_WIFI_STATE_CHAN);

/* ── Weak gesture hooks — override to rewrite an action ─────────────────── */

/**
 * @brief Button 0 single-click action.
 *
 * Default (ux.c): log the current Wi-Fi mode via zego_wifi_get_mode().
 */
void zego_ux_on_single_click(void);

/**
 * @brief Button 0 double-click action.
 *
 * Default (ux.c): mode-aware — in P2P_GO/P2P_GC, triggers P2P pairing via
 * wifi_p2p_start_pairing(); otherwise toggles BLE-prov advertising (BREATHE
 * on LED 0) when CONFIG_ZEGO_WIFI_BLE_PROV=y.
 */
void zego_ux_on_double_click(void);

/**
 * @brief Button 0 long-press action.
 *
 * Default (ux.c): cycle the Wi-Fi mode (STA → SoftAP → P2P_GO → P2P_GC →
 * STA), persist to NVS via settings_save_one(), and sys_reboot().
 */
void zego_ux_on_long_press(void);

/* ── Startup banner ───────────────────────────────────────────────────── */

/**
 * @brief Print the startup banner (version, board, MAC, compiled zego
 *        modules, Wi-Fi mode, connection instructions).
 *
 * Call once from the application's main() after SYS_INIT has run.
 */
void zego_ux_print_banner(void);

/**
 * @brief Application-specific compiled-module list — override point.
 *
 * Default (ux.c): no-op. Override with a strong definition in the
 * application to print its own (non-zego) compiled modules as part of the
 * banner. When overridden, start with a "====" separator line to match the
 * other banner sections.
 */
void banner_compiled_app_modules(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEGO_UX_H */
