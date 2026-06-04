/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/**
 * @file led_hw.h
 * @brief Hardware abstraction layer for the zego LED module.
 *
 * Implement one of the two backends:
 *   - led_hw_dk.c      (CONFIG_ZEGO_LED_BACKEND_DK)      — dk_buttons_and_leds
 *   - led_hw_zephyr.c  (CONFIG_ZEGO_LED_BACKEND_ZEPHYR)  — Zephyr LED driver API
 *
 * The Zephyr backend optionally supports hardware-PWM brightness control
 * (CONFIG_ZEGO_LED_USE_PWM=y).  Per-LED PWM availability is declared via
 * CONFIG_ZEGO_LED_n_PWM_INDEX (n = 0..3): set to the pwm-leds device child
 * index for that LED, or -1 if the LED has no PWM channel.
 */

#ifndef ZEGO_LED_HW_H
#define ZEGO_LED_HW_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialize the LED hardware backend.
 * @return 0 on success, negative errno on failure.
 */
int led_hw_init(void);

/**
 * @brief Turn a LED on or off.
 *
 * @param led_number  0-based LED index.
 * @param on          true = on, false = off.
 */
void led_hw_set(uint8_t led_number, bool on);

/**
 * @brief Query whether a LED supports hardware brightness control.
 *
 * When true, led_hw_set_brightness() drives the LED via a hardware PWM
 * channel and produces smooth fades without CPU wake-ups.
 * When false, the LED only supports on/off; the breathe effect falls back
 * to software PWM implemented in led.c.
 *
 * @param led_number  0-based LED index.
 * @return true if hardware brightness is available, false otherwise.
 */
bool led_hw_has_brightness(uint8_t led_number);

/**
 * @brief Set LED brightness via hardware PWM (0 – 100 %).
 *
 * Only valid when led_hw_has_brightness(led_number) returns true.
 * Behaviour is undefined for LEDs that report no brightness support.
 *
 * @param led_number  0-based LED index.
 * @param percent     Brightness level, 0 (off) to 100 (full on).
 */
void led_hw_set_brightness(uint8_t led_number, uint8_t percent);

#endif /* ZEGO_LED_HW_H */
