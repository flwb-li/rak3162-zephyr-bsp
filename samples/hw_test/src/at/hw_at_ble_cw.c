/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_radio.h>

#include "at/hw_at.h"

LOG_MODULE_REGISTER(hw_at_ble_cw, LOG_LEVEL_INF);

struct ble_cw_state {
	bool cw_running;
	uint8_t channel;
	int8_t cw_power_dbm;
};

static struct ble_cw_state ble_cw;

#define ANT_SW_NODE DT_ALIAS(lora_ant_sw)

static int ble_cw_clock_init(void)
{
	int err;
	int res;
	struct onoff_manager *clk_mgr;
	struct onoff_client clk_cli;

	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (clk_mgr == NULL) {
		return -ENODEV;
	}

	sys_notify_init_spinwait(&clk_cli.notify);
	err = onoff_request(clk_mgr, &clk_cli);
	if (err < 0) {
		return err;
	}

	do {
		err = sys_notify_fetch_result(&clk_cli.notify, &res);
		if (!err && res) {
			return -EIO;
		}
	} while (err != 0);

	return 0;
}

static nrf_radio_txpower_t cw_dbm_to_txpower(int8_t tx_power)
{
	switch (tx_power) {
	case -40:
		return RADIO_TXPOWER_TXPOWER_Neg40dBm;
	case -20:
		return RADIO_TXPOWER_TXPOWER_Neg20dBm;
	case -16:
		return RADIO_TXPOWER_TXPOWER_Neg16dBm;
	case -12:
		return RADIO_TXPOWER_TXPOWER_Neg12dBm;
	case -8:
		return RADIO_TXPOWER_TXPOWER_Neg8dBm;
	case -4:
		return RADIO_TXPOWER_TXPOWER_Neg4dBm;
	case 0:
		return RADIO_TXPOWER_TXPOWER_0dBm;
#if defined(RADIO_TXPOWER_TXPOWER_Pos2dBm)
	case 2:
		return RADIO_TXPOWER_TXPOWER_Pos2dBm;
#endif
#if defined(RADIO_TXPOWER_TXPOWER_Pos3dBm)
	case 3:
		return RADIO_TXPOWER_TXPOWER_Pos3dBm;
#endif
#if defined(RADIO_TXPOWER_TXPOWER_Pos4dBm)
	case 4:
		return RADIO_TXPOWER_TXPOWER_Pos4dBm;
#endif
#if defined(RADIO_TXPOWER_TXPOWER_Pos5dBm)
	case 5:
		return RADIO_TXPOWER_TXPOWER_Pos5dBm;
#endif
#if defined(RADIO_TXPOWER_TXPOWER_Pos6dBm)
	case 6:
		return RADIO_TXPOWER_TXPOWER_Pos6dBm;
#endif
#if defined(RADIO_TXPOWER_TXPOWER_Pos7dBm)
	case 7:
		return RADIO_TXPOWER_TXPOWER_Pos7dBm;
#endif
#if defined(RADIO_TXPOWER_TXPOWER_Pos8dBm)
	case 8:
		return RADIO_TXPOWER_TXPOWER_Pos8dBm;
#endif
	default:
		LOG_WRN("BLECW unsupported TX power %d dBm, fallback 0 dBm", tx_power);
		return RADIO_TXPOWER_TXPOWER_0dBm;
	}
}

static void ble_cw_radio_disable(void)
{
	nrf_radio_shorts_set(NRF_RADIO, 0);
	nrf_radio_int_disable(NRF_RADIO, UINT32_MAX);
	nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_DISABLED);
	nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_DISABLE);
	while (!nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_DISABLED)) {
	}
	nrf_radio_event_clear(NRF_RADIO, NRF_RADIO_EVENT_DISABLED);
}

static int ble_cw_rf_path_enable(void)
{
#if DT_NODE_EXISTS(ANT_SW_NODE)
	const struct device *ant_sw = DEVICE_DT_GET(ANT_SW_NODE);
	int err;

	if (!device_is_ready(ant_sw)) {
		return -ENODEV;
	}

	err = regulator_enable(ant_sw);
	if ((err != 0) && (err != -EALREADY)) {
		return err;
	}
#endif
	return 0;
}

static int ble_cw_start(uint8_t channel, int8_t power_dbm)
{
	int err;

	if (channel > 100U) {
		return -EINVAL;
	}
	if (ble_cw.cw_running) {
		return -EALREADY;
	}

	err = ble_cw_clock_init();
	if (err != 0) {
		return err;
	}

	err = ble_cw_rf_path_enable();
	if (err != 0) {
		return err;
	}

	ble_cw_radio_disable();
	nrf_radio_mode_set(NRF_RADIO, NRF_RADIO_MODE_BLE_1MBIT);
	nrf_radio_shorts_enable(NRF_RADIO, NRF_RADIO_SHORT_READY_START_MASK);
	nrf_radio_txpower_set(NRF_RADIO, cw_dbm_to_txpower(power_dbm));
	nrf_radio_frequency_set(NRF_RADIO, 2400U + channel);
	nrf_radio_task_trigger(NRF_RADIO, NRF_RADIO_TASK_TXEN);

	ble_cw.cw_running = true;
	ble_cw.channel = channel;
	ble_cw.cw_power_dbm = power_dbm;
	return 0;
}

static int ble_cw_stop(void)
{
	if (!ble_cw.cw_running) {
		return 0;
	}
	ble_cw_radio_disable();
	ble_cw.cw_running = false;
	return 0;
}

static int parse_blecw_csv(const char *args, uint8_t *ch, int8_t *pwr_dbm)
{
	unsigned long a, b;
	char *end = NULL;

	if ((args == NULL) || (args[0] == '\0')) {
		return -EINVAL;
	}

	a = strtoul(args, &end, 10);
	if ((end == NULL) || (*end == '\0')) {
		if (a > 100UL) {
			return -EINVAL;
		}
		*ch = (uint8_t)a;
		*pwr_dbm = 8;
		return 0;
	}

	if (*end != ',') {
		return -EINVAL;
	}

	b = strtoul(end + 1, &end, 10);
	if ((end == NULL) || (*end != '\0')) {
		return -EINVAL;
	}
	if ((a > 100UL) || (b > 127UL)) {
		return -EINVAL;
	}

	*ch = (uint8_t)a;
	*pwr_dbm = (int8_t)b;
	return 0;
}

int hw_at_cmd_blecw(const struct hw_at_request *req)
{
	uint8_t ch;
	int8_t pwr;
	int err;

	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("Start Nordic native BLE CW (unmodulated carrier)");
		hw_at_resp_line("AT+BLECW=?            : status");
		hw_at_resp_line("AT+BLECW=<ch>[,<pwr>] : start CW, ch=0-100 (freq=2400+ch MHz), default pwr=8 dBm");
		hw_at_resp_line("Stop: AT+BLECWSTOP");
		hw_at_resp_ok();
		return 0;
	}

	if (req->form == HW_AT_FORM_GET) {
		if (!ble_cw.cw_running) {
			hw_at_resp_line("BLECW: idle");
		} else {
			hw_at_resp_line("BLECW: running freq=%u MHz ch=%u pwr=%d dBm",
					2400U + ble_cw.channel, ble_cw.channel, (int)ble_cw.cw_power_dbm);
		}
		hw_at_resp_ok();
		return 0;
	}

	if (req->form != HW_AT_FORM_SET) {
		hw_at_resp_line("AT_PARAM_ERROR");
		return -EINVAL;
	}

	if (parse_blecw_csv(req->args, &ch, &pwr) != 0) {
		hw_at_resp_line("AT_PARAM_ERROR");
		return -EINVAL;
	}

	err = ble_cw_start(ch, pwr);
	if (err == -EALREADY) {
		hw_at_resp_line("BLECW: already running, use AT+BLECWSTOP");
		hw_at_resp_line("AT_ERROR");
		return err;
	}
	if (err != 0) {
		hw_at_resp_line("BLECW start failed: %d", err);
		hw_at_resp_line("AT_ERROR");
		return err;
	}

	hw_at_resp_line("BLECW: freq=%u MHz ch=%u pwr=%d dBm", 2400U + ch, ch, (int)pwr);
	hw_at_resp_ok();
	return 0;
}

int hw_at_cmd_blecwstop(const struct hw_at_request *req)
{
	int err;

	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("Stop Nordic native BLE CW");
		hw_at_resp_ok();
		return 0;
	}

	if (req->form != HW_AT_FORM_EXEC) {
		hw_at_resp_line("AT_PARAM_ERROR");
		return -EINVAL;
	}

	err = ble_cw_stop();
	if (err != 0) {
		hw_at_resp_line("BLECWSTOP failed: %d", err);
		hw_at_resp_line("AT_ERROR");
		return err;
	}

	hw_at_resp_line("BLECWSTOP: ok");
	hw_at_resp_ok();
	return 0;
}
