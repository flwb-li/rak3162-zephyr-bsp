/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RAK3162_LED_H_
#define RAK3162_LED_H_

/**
 * @brief Configure LEDs inactive (no continuous blink thread).
 */
int rak3162_led_start(void);

/** Brief LED pulse after OTAA join success. */
void rak3162_led_indicate_joined(void);

/** Brief LED pulse after a successful uplink. */
void rak3162_led_indicate_tx(void);

/** Drive both LEDs inactive (e.g. before sys_poweroff). */
void rak3162_led_prepare_poweroff(void);

#endif /* RAK3162_LED_H_ */
