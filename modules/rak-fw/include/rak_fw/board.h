/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RAK_FW_BOARD_H_
#define RAK_FW_BOARD_H_

#include <rak_at/rak_at_cfg.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Board hooks for rak-fw (RUI3 Core equivalent of platform glue).
 *
 * Standard AT including SN/VER/SLEEP lives in rak-fw; boards inject hardware
 * behavior here (same idea as RUI3 Core owning AT while BSP supplies HW).
 */
struct rak_fw_board_ops {
	void (*rf_window_enter)(void);
	void (*rf_window_exit)(void);
	void (*indicate_joined)(void);
	void (*indicate_tx)(void);
	/** Firmware version string for AT+VER (e.g. "V_1.0.0"). */
	const char *(*fw_version)(void);
	/**
	 * Arm RTC/GRTC wakeup for the next System OFF.
	 * @param seconds 0 disables / no arm.
	 */
	int (*arm_rtc_wakeup_s)(uint32_t seconds);
	/**
	 * Arm RTC/GRTC wakeup with millisecond resolution.
	 * @param milliseconds 0 disables / no arm.
	 */
	int (*arm_rtc_wakeup_ms)(uint32_t milliseconds);
	/** Prepare peripherals before System OFF (LEDs, buses, …). */
	void (*prepare_poweroff)(void);
	/**
	 * Delay @p delay_ms then enter System OFF (may not return).
	 */
	void (*enter_system_off)(uint32_t delay_ms);
};

struct rak_fw_cfg_ops {
	void (*get_active)(struct rak_at_runtime_cfg *cfg);
};

void rak_fw_set_board_ops(const struct rak_fw_board_ops *ops);
const struct rak_fw_board_ops *rak_fw_board_ops(void);

void rak_fw_set_cfg_ops(const struct rak_fw_cfg_ops *ops);
const struct rak_fw_cfg_ops *rak_fw_cfg_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* RAK_FW_BOARD_H_ */
