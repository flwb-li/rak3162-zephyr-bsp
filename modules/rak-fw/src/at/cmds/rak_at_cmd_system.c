/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Standard system AT commands (RUI3-style: part of the framework, not the app).
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <rak_at/rak_at.h>
#include <rak_at/rak_at_cfg.h>
#include <rak_at/rak_at_util.h>
#include <rak_fw/board.h>
#if defined(CONFIG_RAK_AT_LORAWAN)
#include <rak_fw/lorawan.h>
#endif
#if defined(CONFIG_RAK_AT_LORA_P2P)
#include <rak_fw/lora_p2p.h>
#endif

LOG_MODULE_REGISTER(rak_at_sys, LOG_LEVEL_INF);

static uint32_t rtc_wakeup_delay_s;

static bool is_printable_ascii_string(const char *s, size_t exact_len)
{
	if ((s == NULL) || (strlen(s) != exact_len)) {
		return false;
	}

	for (size_t i = 0; i < exact_len; i++) {
		if (((unsigned char)s[i] < 0x20U) || ((unsigned char)s[i] > 0x7EU)) {
			return false;
		}
	}

	return true;
}

static int cmd_atsn(const struct rak_at_request *req)
{
	struct rak_at_runtime_cfg cfg;

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("Description: Serial number");
		rak_at_resp_line("Example: AT+SN?   AT+SN=?  AT+SN=<18 char>");
		rak_at_resp_ok();
		return 0;
	}

	rak_at_cfg_get_active(&cfg);

	if (req->form == RAK_AT_FORM_GET) {
		if ((cfg.valid_mask & RAK_AT_CFG_VALID_SN) == 0U) {
			rak_at_resp_line("AT+SN=");
		} else {
			rak_at_resp_line("AT+SN=%s", cfg.sn);
		}
		rak_at_resp_ok();
		return 0;
	}

	if (req->form == RAK_AT_FORM_SET) {
		if (!is_printable_ascii_string(req->args, RAK_AT_SN_LEN)) {
			rak_at_resp_line("AT_PARAM_ERROR");
			return -EINVAL;
		}

		snprintf(cfg.sn, sizeof(cfg.sn), "%s", req->args);
		cfg.valid_mask |= RAK_AT_CFG_VALID_SN;
		if (rak_at_cfg_set_and_apply(&cfg) != 0) {
			rak_at_resp_error(NULL);
			return -EIO;
		}
		rak_at_resp_ok();
		return 0;
	}

	rak_at_resp_line("AT_PARAM_ERROR");
	return -EINVAL;
}

static int cmd_atver(const struct rak_at_request *req)
{
	const struct rak_fw_board_ops *bops = rak_fw_board_ops();
	const char *ver = "V_0.0.0";

	if ((bops != NULL) && (bops->fw_version != NULL)) {
		const char *v = bops->fw_version();

		if ((v != NULL) && (v[0] != '\0')) {
			ver = v;
		}
	}

	if ((req->form == RAK_AT_FORM_GET) || (req->form == RAK_AT_FORM_HELP)) {
		rak_at_resp_line("AT+%s=%s", req->name, ver);
		rak_at_resp_ok();
		return 0;
	}

	rak_at_resp_error(NULL);
	return -EINVAL;
}

static int cmd_sleep(const struct rak_at_request *req)
{
	const struct rak_fw_board_ops *bops = rak_fw_board_ops();

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("AT+SLEEP=<delay_ms>");
		rak_at_resp_line("Delay then enter System OFF (stops SENDINT; optional RTC via AT+RTC)");
		rak_at_resp_ok();
		return 0;
	}

	if (req->form == RAK_AT_FORM_SET) {
		unsigned long delay_ms = 0;

		if (rak_at_parse_ulong(req->args, &delay_ms) != 0) {
			rak_at_resp_line("AT_PARAM_ERROR");
			return -EINVAL;
		}

		if ((bops == NULL) || (bops->enter_system_off == NULL)) {
			rak_at_resp_error(NULL);
			return -ENOTSUP;
		}

		if ((rtc_wakeup_delay_s > 0U) && (bops->arm_rtc_wakeup_s == NULL)) {
			rak_at_resp_line("RTC_WAKEUP_ERROR");
			return -ENOTSUP;
		}

#if defined(CONFIG_RAK_AT_LORAWAN)
		if (rak_fw_lorawan_is_busy()) {
			rak_at_resp_line("AT_BUSY_ERROR");
			return -EBUSY;
		}
#endif
#if defined(CONFIG_RAK_AT_LORA_P2P)
		if (rak_fw_lora_p2p_is_busy()) {
			rak_at_resp_line("AT_BUSY_ERROR");
			return -EBUSY;
		}
#endif

		rak_at_resp_ok();

		/* Stop auto uplink / join retry before radio poweroff. */
		if (bops->prepare_poweroff != NULL) {
			bops->prepare_poweroff();
		}

		/*
		 * Delay first, then arm RTC immediately before System OFF so the
		 * wake interval is measured from poweroff entry (not from OK).
		 */
		if (delay_ms > 0UL) {
			k_msleep((uint32_t)delay_ms);
		}

		if (rtc_wakeup_delay_s > 0U) {
			int ret = bops->arm_rtc_wakeup_s(rtc_wakeup_delay_s);

			if (ret != 0) {
				LOG_WRN("RTC wakeup prepare failed: %d", ret);
				/* Already replied OK; still enter System OFF. */
			} else {
				LOG_INF("RTC wakeup armed: %u s", rtc_wakeup_delay_s);
			}
		}

		bops->enter_system_off(0U);
		return 0;
	}

	rak_at_resp_error(NULL);
	return -EINVAL;
}

static int cmd_rtc(const struct rak_at_request *req)
{
	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("AT+RTC=?           Get wakeup delay (s)");
		rak_at_resp_line("AT+RTC=<s>         Set wakeup delay for next AT+SLEEP");
		rak_at_resp_line("AT+RTC=0           Disable RTC wakeup");
		rak_at_resp_ok();
		return 0;
	}

	if (req->form == RAK_AT_FORM_GET) {
		rak_at_resp_line("AT+RTC=%u", rtc_wakeup_delay_s);
		rak_at_resp_ok();
		return 0;
	}

	if (req->form == RAK_AT_FORM_SET) {
		unsigned long v = 0U;

		if ((rak_at_parse_ulong(req->args, &v) != 0) || (v > UINT32_MAX)) {
			rak_at_resp_line("AT_PARAM_ERROR");
			return -EINVAL;
		}

		rtc_wakeup_delay_s = (uint32_t)v;
		rak_at_resp_ok();
		return 0;
	}

	rak_at_resp_line("AT_PARAM_ERROR");
	return -EINVAL;
}

void rak_at_register_system_commands(void)
{
	(void)rak_at_register_command("SN", cmd_atsn, "AT+SN");
	(void)rak_at_register_command("VER", cmd_atver, "AT+VER");
	(void)rak_at_register_command("RTC", cmd_rtc, "AT+RTC");
	(void)rak_at_register_command("SLEEP", cmd_sleep, "AT+SLEEP");
}

void rak_at_register_standard_commands(void)
{
	rak_at_register_system_commands();
#if defined(CONFIG_RAK_AT_LORA_P2P)
	rak_at_register_lora_p2p_commands();
#endif
#if defined(CONFIG_RAK_AT_LORAWAN)
	rak_at_register_lorawan_commands();
#endif
}
