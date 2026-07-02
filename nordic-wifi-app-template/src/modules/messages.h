/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file messages.h
 * @brief Application-level Zbus channel and message type definitions.
 *
 * Define all app-level channels here so every module sees the same
 * declarations.  Each channel must be DEFINED (ZBUS_CHAN_DEFINE) in exactly
 * one .c file.
 *
 * The Wi-Fi connectivity state channel used to drive LED 0
 * (previously APP_WIFI_STATE_CHAN) is now owned by the zego/ux brick as
 * ZEGO_UX_WIFI_STATE_CHAN — see zego/bricks/ux/src/ux.h. net_event_mgmt_app.c
 * publishes to it directly; no app-level declaration is needed for it.
 *
 * Add your own app-specific channels below following the same pattern.
 */

#ifndef APP_MESSAGES_H
#define APP_MESSAGES_H

#include <zephyr/zbus/zbus.h>

#endif /* APP_MESSAGES_H */
