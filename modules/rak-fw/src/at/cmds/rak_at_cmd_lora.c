/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rak_at/rak_at.h>
#include <rak_at/rak_at_cfg.h>
#include <rak_at/rak_at_lorawan_svc.h>
#include <rak_at/rak_at_p2p_svc.h>
#include <rak_at/rak_at_util.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/sys/util.h>

static const struct rak_at_p2p_ops *p2p(void)
{
	return rak_at_p2p_ops();
}

static int cfg_save(const struct rak_at_runtime_cfg *cfg)
{
	return rak_at_cfg_set_and_apply(cfg);
}


static void param_error(void)
{
	rak_at_resp_line("AT_PARAM_ERROR");
}

static void busy_error(void)
{
	rak_at_resp_line("AT_BUSY_ERROR");
}

static void resp_line_at_value(const char *name, const char *value)
{
	rak_at_resp_line("AT+%s=%s", name, value);
}

static bool hex_valid(const char *s, size_t exact_len)
{
	if ((s == NULL) || (strlen(s) != exact_len)) {
		return false;
	}

	for (size_t i = 0; i < exact_len; i++) {
		if (isxdigit((unsigned char)s[i]) == 0) {
			return false;
		}
	}

	return true;
}

static bool hex_valid_n(const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (isxdigit((unsigned char)s[i]) == 0) {
			return false;
		}
	}

	return true;
}

static void str_to_upper(char *s)
{
	while (*s != '\0') {
		*s = (char)toupper((unsigned char)*s);
		s++;
	}
}

static int parse_hex_bytes(const char *hex, size_t hex_len, uint8_t *buf, size_t buf_len)
{
	if (!hex_valid(hex, hex_len)) {
		return -EINVAL;
	}

	if (hex2bin(hex, hex_len, buf, buf_len) != buf_len) {
		return -EINVAL;
	}

	return 0;
}

static int format_hex_bytes(const uint8_t *buf, size_t buf_len, char *hex, size_t hex_len)
{
	if (bin2hex(buf, buf_len, hex, hex_len) != (buf_len * 2U)) {
		return -EINVAL;
	}

	str_to_upper(hex);
	return 0;
}

static int save_lora_cfg(const struct rak_at_runtime_cfg *cfg)
{
	return cfg_save(cfg);
}

static int require_p2p_mode(void)
{
	struct rak_at_runtime_cfg cfg;

	rak_at_cfg_get_active(&cfg);
	if (cfg.nwm != RAK_AT_NWM_P2P_LORA) {
		rak_at_resp_error(NULL);
		return -EINVAL;
	}

	return 0;
}





int rak_at_cmd_deveui(const struct rak_at_request *req)
{
	struct rak_at_runtime_cfg cfg;

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("Description: Device EUI (8 bytes hex)");
		rak_at_resp_line("Example: AT+DEVEUI=?  AT+DEVEUI=<16 hex chars>");
		rak_at_resp_ok();
		return 0;
	}

	rak_at_cfg_get_active(&cfg);

	if (req->form == RAK_AT_FORM_GET) {
		char deveui_hex[RAK_AT_EUI_BIN_LEN * 2 + 1];

		if ((cfg.valid_mask & RAK_AT_CFG_VALID_DEVEUI) == 0U) {
			rak_at_resp_line("AT+DEVEUI=");
			rak_at_resp_ok();
			return 0;
		}

		if (format_hex_bytes(cfg.deveui, sizeof(cfg.deveui), deveui_hex, sizeof(deveui_hex)) != 0) {
			rak_at_resp_error(NULL);
			return -EINVAL;
		}

		rak_at_resp_line("AT+DEVEUI=%s", deveui_hex);
		rak_at_resp_ok();
		return 0;
	}

	if (req->form == RAK_AT_FORM_SET) {
		if (parse_hex_bytes(req->args, RAK_AT_EUI_BIN_LEN * 2, cfg.deveui, sizeof(cfg.deveui)) != 0) {
			param_error();
			return -EINVAL;
		}

		cfg.valid_mask |= RAK_AT_CFG_VALID_DEVEUI;
		if (save_lora_cfg(&cfg) != 0) {
			rak_at_resp_error(NULL);
			return -EIO;
		}

		rak_at_resp_ok();
		return 0;
	}

	param_error();
	return -EINVAL;
}

int rak_at_cmd_appeui(const struct rak_at_request *req)
{
	struct rak_at_runtime_cfg cfg;

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("Description: JoinEUI / AppEUI (8 bytes hex)");
		rak_at_resp_line("Example: AT+APPEUI=?  AT+APPEUI=<16 hex chars>");
		rak_at_resp_ok();
		return 0;
	}

	rak_at_cfg_get_active(&cfg);

	if (req->form == RAK_AT_FORM_GET) {
		char appeui_hex[RAK_AT_EUI_BIN_LEN * 2 + 1];

		if ((cfg.valid_mask & RAK_AT_CFG_VALID_APPEUI) == 0U) {
			rak_at_resp_line("AT+APPEUI=");
			rak_at_resp_ok();
			return 0;
		}

		if (format_hex_bytes(cfg.appeui, sizeof(cfg.appeui), appeui_hex, sizeof(appeui_hex)) != 0) {
			rak_at_resp_error(NULL);
			return -EINVAL;
		}

		rak_at_resp_line("AT+APPEUI=%s", appeui_hex);
		rak_at_resp_ok();
		return 0;
	}

	if (req->form == RAK_AT_FORM_SET) {
		if (parse_hex_bytes(req->args, RAK_AT_EUI_BIN_LEN * 2, cfg.appeui, sizeof(cfg.appeui)) != 0) {
			param_error();
			return -EINVAL;
		}

		cfg.valid_mask |= RAK_AT_CFG_VALID_APPEUI;
		if (save_lora_cfg(&cfg) != 0) {
			rak_at_resp_error(NULL);
			return -EIO;
		}

		rak_at_resp_ok();
		return 0;
	}

	param_error();
	return -EINVAL;
}

int rak_at_cmd_p2p(const struct rak_at_request *req)
{
	char buf[128];

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("AT+P2P: get or set all P2P parameters");
		rak_at_resp_ok();
		return 0;
	}

	if ((req->form == RAK_AT_FORM_GET) || (req->form == RAK_AT_FORM_EXEC)) {
		if (p2p()->params_format(buf, sizeof(buf)) != 0) {
			rak_at_resp_error(NULL);
			return -EIO;
		}
		resp_line_at_value("P2P", buf);
		rak_at_resp_ok();
		return 0;
	}

	if (req->form == RAK_AT_FORM_SET) {
		int s;

		if (require_p2p_mode() != 0) {
			return -EINVAL;
		}

		if ((req->args == NULL) || (req->args[0] == '\0')) {
			param_error();
			return -EINVAL;
		}

		s = p2p()->params_set(req->args);
		if (s == -EBUSY) {
			busy_error();
			return -EBUSY;
		}
		if (s != 0) {
			param_error();
			return -EINVAL;
		}
		rak_at_resp_ok();
		return 0;
	}

	param_error();
	return -EINVAL;
}

int rak_at_cmd_precv(const struct rak_at_request *req)
{
	char buf[32];

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("Description: P2P receive mode");
		rak_at_resp_line("AT+PRECV=? : get current PRECV value");
		rak_at_resp_line("AT+PRECV=<time>");
		rak_at_resp_line("0 stop; 1..65532 timed ms; 65533 cont+TX;");
		rak_at_resp_line("65534 cont locked; 65535 until one packet");
		rak_at_resp_ok();
		return 0;
	}

	if ((req->form == RAK_AT_FORM_GET) || (req->form == RAK_AT_FORM_EXEC)) {
		(void)snprintf(buf, sizeof(buf), "%u", (unsigned int)p2p()->recv_get());
		resp_line_at_value("PRECV", buf);
		rak_at_resp_ok();
		return 0;
	}

	if (req->form != RAK_AT_FORM_SET) {
		param_error();
		return -EINVAL;
	}

	if (require_p2p_mode() != 0) {
		return -EINVAL;
	}

	if ((req->args == NULL) || (req->args[0] == '\0')) {
		param_error();
		return -EINVAL;
	}

	{
		char *end = NULL;
		unsigned long value;
		int s;

		errno = 0;
		value = strtoul(req->args, &end, 10);
		if ((errno != 0) || (end == req->args) || (*end != '\0') || (value > 65535UL)) {
			param_error();
			return -EINVAL;
		}

		s = p2p()->recv_set((uint16_t)value);
		if (s == -EBUSY) {
			busy_error();
			return -EBUSY;
		}
		if (s == -EINVAL) {
			param_error();
			return -EINVAL;
		}
		if (s != 0) {
			rak_at_resp_error(NULL);
			return s;
		}
	}

	rak_at_resp_ok();
	return 0;
}

int rak_at_cmd_psend(const struct rak_at_request *req)
{
	uint8_t out[256];
	size_t olen;
	size_t hexlen;

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("Description: P2P send hex payload");
		rak_at_resp_line("AT+PSEND=<hex>");
		rak_at_resp_line("Payload: 2..500 hex chars, even length, 1..256 bytes");
		rak_at_resp_ok();
		return 0;
	}

	if (req->form != RAK_AT_FORM_SET) {
		param_error();
		return -EINVAL;
	}

	if (require_p2p_mode() != 0) {
		return -EINVAL;
	}

	if (req->args == NULL) {
		param_error();
		return -EINVAL;
	}

	hexlen = strlen(req->args);
	if ((hexlen < 2U) || (hexlen > 500U) || ((hexlen & 1U) != 0U)) {
		param_error();
		return -EINVAL;
	}

	if (!hex_valid_n(req->args, hexlen)) {
		param_error();
		return -EINVAL;
	}

	olen = hexlen / 2U;
	if ((olen < 1U) || (olen > 256U)) {
		param_error();
		return -EINVAL;
	}

	if (hex2bin(req->args, hexlen, out, sizeof(out)) != olen) {
		param_error();
		return -EINVAL;
	}

	{
		int s = p2p()->send_payload(out, olen);

		if (s == -EBUSY) {
			busy_error();
			return -EBUSY;
		}
		if (s == -EINVAL) {
			param_error();
			return -EINVAL;
		}
		if (s != 0) {
			rak_at_resp_error(NULL);
			return s;
		}
	}

	rak_at_resp_ok();
	return 0;
}

void rak_at_register_lora_p2p_commands(void)
{
	(void)rak_at_register_command("DEVEUI", rak_at_cmd_deveui, "AT+DEVEUI");
	(void)rak_at_register_command("APPEUI", rak_at_cmd_appeui, "AT+APPEUI");
	(void)rak_at_register_command("P2P", rak_at_cmd_p2p, "AT+P2P");
	(void)rak_at_register_command("PRECV", rak_at_cmd_precv, "AT+PRECV");
	(void)rak_at_register_command("PSEND", rak_at_cmd_psend, "AT+PSEND");
}
