/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bind board adapters and rak-fw LoRa services into the AT framework.
 */

#include "config.h"
#include "core/bus_pm.h"
#include "core/led.h"
#include "storage/storage.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/poweroff.h>

#include <rak_at/rak_at.h>
#include <rak_at/rak_at_lorawan_svc.h>
#include <rak_at/rak_at_p2p_svc.h>
#include <rak_at/rak_at_port.h>
#include <rak_fw/board.h>
#include <rak_fw/lora_p2p.h>
#include <rak_fw/lorawan.h>

LOG_MODULE_REGISTER(radio_bind, LOG_LEVEL_INF);

static const struct device *const ant_sw_dev = DEVICE_DT_GET(DT_ALIAS(lora_ant_sw));
static const struct device *const lora_spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi22));

static void ant_sw_set(bool enable)
{
	int ret;

	if (!device_is_ready(ant_sw_dev)) {
		return;
	}

	ret = enable ? regulator_enable(ant_sw_dev) : regulator_disable(ant_sw_dev);
	if ((ret < 0) && (ret != -EALREADY)) {
		LOG_WRN("ANT_SW %s failed: %d", enable ? "enable" : "disable", ret);
	}
}

static void lora_spi_pm_action(enum pm_device_action action)
{
	int ret;

	if (!device_is_ready(lora_spi_dev)) {
		return;
	}

	ret = pm_device_action_run(lora_spi_dev, action);
	if ((ret < 0) && (ret != -EALREADY) && (ret != -ENOTSUP)) {
		LOG_WRN("LoRa SPI %s failed: %d",
			(action == PM_DEVICE_ACTION_RESUME) ? "resume" : "suspend", ret);
	}
}

static void radio_event_handler(const char *line)
{
	rak_at_evt("%s", line);
}

static void lorawan_rf_state_handler(bool active)
{
	if (active) {
		rak_at_port_lp_exit();
	} else {
		rak_at_port_lp_enter();
	}
}

static void board_rf_window_enter(void)
{
	/* SPI must be awake before the SX1262 cold-start reinitialization. */
	lora_spi_pm_action(PM_DEVICE_ACTION_RESUME);
	rak3162_bus_pm_resume();
	ant_sw_set(true);
}

static void board_rf_window_exit(void)
{
	rak3162_bus_pm_suspend();
	ant_sw_set(false);
}

static const char *board_fw_version(void)
{
	return SOFTWARE_VERSION;
}

static int board_arm_rtc_wakeup_ms(uint32_t milliseconds)
{
	if (milliseconds == 0U) {
		return 0;
	}

	return z_nrf_grtc_wakeup_prepare((uint64_t)milliseconds * 1000ULL);
}

static int board_arm_rtc_wakeup_s(uint32_t seconds)
{
	return board_arm_rtc_wakeup_ms(seconds * 1000U);
}

static void board_prepare_poweroff(void)
{
	/* Warm sleep retains SX1262 configuration across MCU System OFF prep. */
	rak_fw_lorawan_radio_cold_sleep();
	lora_spi_pm_action(PM_DEVICE_ACTION_SUSPEND);
	ant_sw_set(false);

	rak3162_led_prepare_poweroff();
	rak3162_bus_pm_suspend();
}

static void board_enter_system_off(uint32_t delay_ms)
{
	k_msleep(delay_ms);

	const struct device *cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

	if (device_is_ready(cons)) {
		int ret = pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);

		if (ret < 0) {
			LOG_WRN("console suspend failed: %d", ret);
		}
	}
	(void)hwinfo_clear_reset_cause();
	LOG_INF("Entering system off");
	sys_poweroff();
}

void radio_bind_prepare_system_on_idle(void)
{
	/*
	 * RF-window exit normally performs these actions first. Keep this
	 * function idempotent so join failures take the same low-power path.
	 */
	rak_fw_lorawan_radio_cold_sleep();
	lora_spi_pm_action(PM_DEVICE_ACTION_SUSPEND);
	ant_sw_set(false);
	rak3162_led_prepare_poweroff();
	rak3162_bus_pm_suspend();
	rak_at_port_lp_enter();
}

static const struct rak_fw_board_ops board_ops = {
	.rf_window_enter = board_rf_window_enter,
	.rf_window_exit = board_rf_window_exit,
	.indicate_joined = rak3162_led_indicate_joined,
	.indicate_tx = rak3162_led_indicate_tx,
	.fw_version = board_fw_version,
	.arm_rtc_wakeup_s = board_arm_rtc_wakeup_s,
	.arm_rtc_wakeup_ms = board_arm_rtc_wakeup_ms,
	.prepare_poweroff = board_prepare_poweroff,
	.enter_system_off = board_enter_system_off,
};

static const struct rak_fw_cfg_ops cfg_ops = {
	.get_active = rak3162_storage_get_active_cfg,
};

static const struct rak_at_lorawan_ops lorawan_ops = {
	.ensure_started = rak_fw_lorawan_ensure_started,
	.join_otaa_async = rak_fw_lorawan_join_otaa_async,
	.join_stop = rak_fw_lorawan_join_stop,
	.send_async = rak_fw_lorawan_send_async,
	.is_joined = rak_fw_lorawan_is_joined,
	.is_started = rak_fw_lorawan_is_started,
	.is_busy = rak_fw_lorawan_is_busy,
	.is_joining = rak_fw_lorawan_is_joining,
	.band_supported = rak_fw_lorawan_band_supported,
	.set_cfm = rak_fw_lorawan_set_cfm,
	.get_cfm = rak_fw_lorawan_get_cfm,
	.get_cfs = rak_fw_lorawan_get_cfs,
	.set_adr = rak_fw_lorawan_set_adr,
	.get_adr = rak_fw_lorawan_get_adr,
	.recv_format_and_clear = rak_fw_lorawan_recv_format_and_clear,
};

static const struct rak_at_p2p_ops p2p_ops = {
	.is_busy = rak_fw_lora_p2p_is_busy,
	.is_active = rak_fw_lora_p2p_is_active,
	.params_set = rak_fw_lora_p2p_params_set,
	.params_format = rak_fw_lora_p2p_params_format,
	.send_payload = rak_fw_lora_p2p_send_payload,
	.recv_set = rak_fw_lora_p2p_recv_set,
	.recv_get = rak_fw_lora_p2p_recv_get,
};

void radio_bind_at(void)
{
	rak_fw_set_board_ops(&board_ops);
	rak_fw_set_cfg_ops(&cfg_ops);
	rak_at_lorawan_set_ops(&lorawan_ops);
	rak_at_p2p_set_ops(&p2p_ops);
	rak_fw_lorawan_set_handlers(radio_event_handler, lorawan_rf_state_handler);
	rak_fw_lora_p2p_set_event_handler(radio_event_handler);
}
