/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file button_hw.h
 * @brief Hardware abstraction layer for the zego button module.
 *
 * Implement one of the two backends:
 *   - button_hw_dk.c   (CONFIG_ZEGO_BUTTON_BACKEND_DK)   — dk_buttons_and_leds
 *   - button_hw_gpio.c (CONFIG_ZEGO_BUTTON_BACKEND_GPIO)  — Zephyr Input subsystem
 *
 * The DTS gpio-keys backend requires each button node to use consecutive
 * linux,code values starting from 0 (0 = button 0, 1 = button 1, ...).
 * Provide board DTS overlays that set these codes if the board defaults differ.
 */

#ifndef ZEGO_BUTTON_HW_H
#define ZEGO_BUTTON_HW_H

#include <stdint.h>

/**
 * @brief Callback fired on button state change.
 *
 * Matches the dk_buttons_and_leds callback signature so button.c is
 * backend-agnostic.
 *
 * @param button_state  Bitmask of current button states (BIT(n) = pressed).
 * @param has_changed   Bitmask of buttons that changed since last call.
 */
typedef void (*button_hw_callback_t)(uint32_t button_state, uint32_t has_changed);

/**
 * @brief Initialize the button hardware backend.
 *
 * @param cb  Callback to invoke on every button state change.
 * @return 0 on success, negative errno on failure.
 */
int button_hw_init(button_hw_callback_t cb);

#endif /* ZEGO_BUTTON_HW_H */
