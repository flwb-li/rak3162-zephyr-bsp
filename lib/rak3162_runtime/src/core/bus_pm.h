/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RAK3162_BUS_PM_H_
#define RAK3162_BUS_PM_H_

/**
 * @brief Suspend secondary buses for shallow sleep: uart21 / i2c30 / spi00.
 *
 * AT console uart20 is not touched (must stay available for AT commands).
 * Safe to call repeatedly.
 */
void rak3162_bus_pm_suspend(void);

/**
 * @brief Resume secondary buses before RF / sensor activity.
 * Safe to call repeatedly.
 */
void rak3162_bus_pm_resume(void);

#endif /* RAK3162_BUS_PM_H_ */
