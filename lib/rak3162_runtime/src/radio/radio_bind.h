/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RADIO_BIND_H_
#define RADIO_BIND_H_

void radio_bind_at(void);

/** Prepare radio/front-end/UART for MCU System ON idle. */
void radio_bind_prepare_system_on_idle(void);

/**
 * Register app hook invoked from board prepare_poweroff (AT+SLEEP).
 * Used to cancel SENDINT / join-retry works before System OFF.
 */
void radio_bind_set_pre_sleep_cb(void (*cb)(void));

#endif /* RADIO_BIND_H_ */
