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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <zephyr/sys/util.h>

static const struct rak_at_lorawan_ops *lw(void)
{
	return rak_at_lorawan_ops();
}

static const struct rak_at_p2p_ops *p2p(void)
{
	return rak_at_p2p_ops();
}

static int cfg_commit(const struct rak_at_runtime_cfg *cfg)
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

static int save_cfg(const struct rak_at_runtime_cfg *cfg)
{
	return cfg_commit(cfg);
}

static bool nwm_is_lorawan(void)
{
	struct rak_at_runtime_cfg cfg;

	rak_at_cfg_get_active(&cfg);
	return cfg.nwm == RAK_AT_NWM_LORAWAN;
}

static bool nwm_is_p2p(void)
{
	struct rak_at_runtime_cfg cfg;

	rak_at_cfg_get_active(&cfg);
	return cfg.nwm == RAK_AT_NWM_P2P_LORA;
}

static int cmd_key_common(const struct rak_at_request *req, const char *name, uint32_t valid_bit,
			  bool is_appkey)
{
	struct rak_at_runtime_cfg cfg;
	char key_hex[RAK_AT_KEY_BIN_LEN * 2 + 1];
	uint8_t *key;

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("AT+%s: get or set the %s (16 bytes in hex)", name, name);
		rak_at_resp_ok();
		return 0;
	}

	rak_at_cfg_get_active(&cfg);
	key = is_appkey ? cfg.appkey : cfg.nwkkey;

	if (req->form == RAK_AT_FORM_GET) {
		if ((cfg.valid_mask & valid_bit) == 0U) {
			rak_at_resp_line("AT+%s=", name);
			rak_at_resp_ok();
			return 0;
		}

		if (format_hex_bytes(key, RAK_AT_KEY_BIN_LEN, key_hex, sizeof(key_hex)) != 0) {
			rak_at_resp_error(NULL);
			return -EINVAL;
		}

		rak_at_resp_line("AT+%s=%s", name, key_hex);
		rak_at_resp_ok();
		return 0;
	}

	if (req->form == RAK_AT_FORM_SET) {
		if (parse_hex_bytes(req->args, RAK_AT_KEY_BIN_LEN * 2, key, RAK_AT_KEY_BIN_LEN) != 0) {
			param_error();
			return -EINVAL;
		}

		cfg.valid_mask |= valid_bit;
		if (save_cfg(&cfg) != 0) {
			rak_at_resp_error(NULL);
			return -EIO;
		}

		rak_at_resp_ok();
		return 0;
	}

	param_error();
	return -EINVAL;
}





int rak_at_cmd_appkey(const struct rak_at_request *req)
{
	return cmd_key_common(req, "APPKEY", RAK_AT_CFG_VALID_APPKEY, true);
}

int rak_at_cmd_nwkkey(const struct rak_at_request *req)
{
	return cmd_key_common(req, "NWKKEY", RAK_AT_CFG_VALID_NWKKEY, false);
}

int rak_at_cmd_nwm(const struct rak_at_request *req)
{
	struct rak_at_runtime_cfg cfg;
	unsigned long v;

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("AT+NWM: get or set the network working mode (0 = P2P_LORA, 1 = LoRaWAN)");
		rak_at_resp_ok();
		return 0;
	}

	rak_at_cfg_get_active(&cfg);

	if (req->form == RAK_AT_FORM_GET) {
		rak_at_resp_line("AT+NWM=%u", cfg.nwm);
		rak_at_resp_ok();
		return 0;
	}

	if (req->form != RAK_AT_FORM_SET) {
		param_error();
		return -EINVAL;
	}

	v = strtoul(req->args, NULL, 10);
	if (v == 2UL) {
		param_error();
		return -EINVAL;
	}
	if ((v != 0UL) && (v != 1UL)) {
		param_error();
		return -EINVAL;
	}

	if ((uint8_t)v == cfg.nwm) {
		rak_at_resp_ok();
		return 0;
	}

	if (lw()->is_busy() || p2p()->is_busy()) {
		busy_error();
		return -EBUSY;
	}

	/* Stop P2P RX when leaving P2P mode. */
	if (cfg.nwm == RAK_AT_NWM_P2P_LORA) {
		(void)p2p()->recv_set(0);
	}

	cfg.nwm = (uint8_t)v;
	cfg.valid_mask |= RAK_AT_CFG_VALID_LW_OPTS;
	if (save_cfg(&cfg) != 0) {
		rak_at_resp_error(NULL);
		return -EIO;
	}

	rak_at_resp_ok();
	return 0;
}

int rak_at_cmd_band(const struct rak_at_request *req)
{
	struct rak_at_runtime_cfg cfg;
	unsigned long v;

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line(
			"AT+BAND: get or set the active region (0=EU433,1=CN470,2=RU864,3=IN865,4=EU868,5=US915,6=AU915,7=KR920,8=AS923)");
		rak_at_resp_ok();
		return 0;
	}

	rak_at_cfg_get_active(&cfg);

	if (req->form == RAK_AT_FORM_GET) {
		rak_at_resp_line("AT+BAND=%u", cfg.band);
		rak_at_resp_ok();
		return 0;
	}

	if (req->form != RAK_AT_FORM_SET) {
		param_error();
		return -EINVAL;
	}

	if (lw()->is_started() || lw()->is_busy()) {
		busy_error();
		return -EBUSY;
	}

	v = strtoul(req->args, NULL, 10);
	if ((v > 12UL) || !lw()->band_supported((uint8_t)v)) {
		param_error();
		return -EINVAL;
	}

	if ((uint8_t)v == cfg.band) {
		rak_at_resp_ok();
		return 0;
	}

	cfg.band = (uint8_t)v;
	cfg.valid_mask |= RAK_AT_CFG_VALID_LW_OPTS;
	if (save_cfg(&cfg) != 0) {
		rak_at_resp_error(NULL);
		return -EIO;
	}

	rak_at_resp_ok();
	return 0;
}

int rak_at_cmd_cfm(const struct rak_at_request *req)
{
	struct rak_at_runtime_cfg cfg;
	unsigned long v;

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("AT+CFM: get or set the confirmation mode (0 = OFF, 1 = ON)");
		rak_at_resp_ok();
		return 0;
	}

	rak_at_cfg_get_active(&cfg);

	if (req->form == RAK_AT_FORM_GET) {
		rak_at_resp_line("AT+CFM=%u", lw()->get_cfm());
		rak_at_resp_ok();
		return 0;
	}

	if (req->form != RAK_AT_FORM_SET) {
		param_error();
		return -EINVAL;
	}

	v = strtoul(req->args, NULL, 10);
	if ((v != 0UL) && (v != 1UL)) {
		param_error();
		return -EINVAL;
	}

	cfg.cfm = (uint8_t)v;
	cfg.valid_mask |= RAK_AT_CFG_VALID_LW_OPTS;
	lw()->set_cfm(cfg.cfm);
	if (save_cfg(&cfg) != 0) {
		rak_at_resp_error(NULL);
		return -EIO;
	}

	rak_at_resp_ok();
	return 0;
}

int rak_at_cmd_cfs(const struct rak_at_request *req)
{
	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line(
			"AT+CFS: get the confirmation status of the last AT+SEND (0 = failure, 1 = success)");
		rak_at_resp_ok();
		return 0;
	}

	if (req->form != RAK_AT_FORM_GET) {
		param_error();
		return -EINVAL;
	}

	rak_at_resp_line("AT+CFS=%u", lw()->get_cfs());
	rak_at_resp_ok();
	return 0;
}

int rak_at_cmd_njs(const struct rak_at_request *req)
{
	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("AT+NJS: get the join status (0 = not joined, 1 = joined)");
		rak_at_resp_ok();
		return 0;
	}

	if (req->form != RAK_AT_FORM_GET) {
		param_error();
		return -EINVAL;
	}

	rak_at_resp_line("AT+NJS=%u", lw()->is_joined() ? 1U : 0U);
	rak_at_resp_ok();
	return 0;
}

int rak_at_cmd_njm(const struct rak_at_request *req)
{
	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("AT+NJM: get or set the network join mode (0 = ABP, 1 = OTAA)");
		rak_at_resp_ok();
		return 0;
	}

	if (req->form == RAK_AT_FORM_GET) {
		rak_at_resp_line("AT+NJM=1");
		rak_at_resp_ok();
		return 0;
	}

	if (req->form == RAK_AT_FORM_SET) {
		unsigned long v = strtoul(req->args, NULL, 10);

		if (v != 1UL) {
			param_error();
			return -EINVAL;
		}
		rak_at_resp_ok();
		return 0;
	}

	param_error();
	return -EINVAL;
}

int rak_at_cmd_adr(const struct rak_at_request *req)
{
	struct rak_at_runtime_cfg cfg;
	unsigned long v;

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("AT+ADR: get or set the adaptive data rate setting (0 = OFF, 1 = ON)");
		rak_at_resp_ok();
		return 0;
	}

	rak_at_cfg_get_active(&cfg);

	if (req->form == RAK_AT_FORM_GET) {
		rak_at_resp_line("AT+ADR=%u", lw()->get_adr() ? 1U : 0U);
		rak_at_resp_ok();
		return 0;
	}

	if (req->form != RAK_AT_FORM_SET) {
		param_error();
		return -EINVAL;
	}

	v = strtoul(req->args, NULL, 10);
	if ((v != 0UL) && (v != 1UL)) {
		param_error();
		return -EINVAL;
	}

	cfg.adr = (uint8_t)v;
	cfg.valid_mask |= RAK_AT_CFG_VALID_LW_OPTS;
	lw()->set_adr(v != 0UL);
	if (save_cfg(&cfg) != 0) {
		rak_at_resp_error(NULL);
		return -EIO;
	}

	rak_at_resp_ok();
	return 0;
}

int rak_at_cmd_recv(const struct rak_at_request *req)
{
	char buf[8 + 256];

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("AT+RECV: print the last received data in hex format");
		rak_at_resp_ok();
		return 0;
	}

	if (req->form != RAK_AT_FORM_GET) {
		param_error();
		return -EINVAL;
	}

	if (lw()->recv_format_and_clear(buf, sizeof(buf)) != 0) {
		rak_at_resp_error(NULL);
		return -EINVAL;
	}

	rak_at_resp_line("AT+RECV=%s", buf);
	rak_at_resp_ok();
	return 0;
}

int rak_at_cmd_join(const struct rak_at_request *req)
{
	struct rak_at_runtime_cfg cfg;
	const uint8_t *nwkkey;
	char *saveptr = NULL;
	char args_buf[32];
	char *tok;
	unsigned long p1 = 1UL;
	unsigned long p2;
	unsigned long p3;
	unsigned long p4;
	int ret;

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("AT+JOIN: join network");
		rak_at_resp_ok();
		return 0;
	}

	rak_at_cfg_get_active(&cfg);

	/* RUI3: AT+JOIN=? returns AT_BUSY_ERROR while joining. */
	if (req->form == RAK_AT_FORM_GET) {
		if (lw()->is_joining()) {
			busy_error();
			return -EBUSY;
		}
		rak_at_resp_line("AT+JOIN=%u:%u:%u:%u", cfg.join_cmd, cfg.join_auto,
				cfg.join_interval_s, cfg.join_attempts);
		rak_at_resp_ok();
		return 0;
	}

	if ((req->form != RAK_AT_FORM_EXEC) && (req->form != RAK_AT_FORM_SET)) {
		param_error();
		return -EINVAL;
	}

	if (!nwm_is_lorawan()) {
		rak_at_resp_error(NULL);
		return -EINVAL;
	}

	if (p2p()->is_active()) {
		busy_error();
		return -EBUSY;
	}

	p2 = cfg.join_auto;
	p3 = cfg.join_interval_s;
	p4 = cfg.join_attempts;

	if (req->form == RAK_AT_FORM_SET) {
		if ((req->args == NULL) || (strlen(req->args) >= sizeof(args_buf))) {
			param_error();
			return -EINVAL;
		}

		strncpy(args_buf, req->args, sizeof(args_buf) - 1U);
		args_buf[sizeof(args_buf) - 1U] = '\0';

		tok = strtok_r(args_buf, ":", &saveptr);
		if (tok == NULL) {
			param_error();
			return -EINVAL;
		}
		p1 = strtoul(tok, NULL, 10);

		tok = strtok_r(NULL, ":", &saveptr);
		if (tok != NULL) {
			p2 = strtoul(tok, NULL, 10);
			tok = strtok_r(NULL, ":", &saveptr);
			if (tok != NULL) {
				p3 = strtoul(tok, NULL, 10);
				tok = strtok_r(NULL, ":", &saveptr);
				if (tok != NULL) {
					p4 = strtoul(tok, NULL, 10);
				}
			}
		}

		if ((p1 > 1UL) || (p2 > 1UL) || (p3 > 255UL) || (p4 > 255UL)) {
			param_error();
			return -EINVAL;
		}
		if ((p3 != 0UL) && (p3 < 7UL)) {
			param_error();
			return -EINVAL;
		}

		cfg.join_cmd = (uint8_t)p1;
		cfg.join_auto = (uint8_t)p2;
		cfg.join_interval_s = (p3 == 0UL) ? 8U : (uint8_t)p3;
		cfg.join_attempts = (uint8_t)p4;
		cfg.valid_mask |= RAK_AT_CFG_VALID_LW_OPTS;
		if (save_cfg(&cfg) != 0) {
			rak_at_resp_error(NULL);
			return -EIO;
		}

		/* Param1=0: stop joining (RUI3). */
		if (p1 == 0UL) {
			(void)lw()->join_stop();
			rak_at_resp_ok();
			return 0;
		}
	}

	/* EXEC AT+JOIN or SET with Param1=1: start join using (stored) params. */
	if (lw()->is_busy()) {
		busy_error();
		return -EBUSY;
	}

	if (((cfg.valid_mask & RAK_AT_CFG_VALID_DEVEUI) == 0U) ||
	    ((cfg.valid_mask & RAK_AT_CFG_VALID_APPEUI) == 0U) ||
	    ((cfg.valid_mask & RAK_AT_CFG_VALID_APPKEY) == 0U)) {
		param_error();
		return -EINVAL;
	}

	nwkkey = ((cfg.valid_mask & RAK_AT_CFG_VALID_NWKKEY) != 0U) ? cfg.nwkkey : cfg.appkey;

	ret = lw()->join_otaa_async(cfg.deveui, cfg.appeui, cfg.appkey, nwkkey,
					 cfg.join_interval_s, cfg.join_attempts);
	if (ret == -EBUSY) {
		busy_error();
		return ret;
	}
	if (ret != 0) {
		rak_at_resp_error(NULL);
		return ret;
	}

	/* RUI3: OK means join started; completion via +EVT */
	rak_at_resp_ok();
	return 0;
}

int rak_at_cmd_send(const struct rak_at_request *req)
{
	char args_buf[260];
	char *colon;
	char *port_str;
	char *hex_str;
	uint8_t payload[128];
	size_t hex_len;
	size_t bin_len;
	unsigned long port_ul;
	enum lorawan_message_type msg_type;
	int ret;

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("AT+SEND: send data along with the application port");
		rak_at_resp_ok();
		return 0;
	}

	if (req->form != RAK_AT_FORM_SET) {
		param_error();
		return -EINVAL;
	}

	if (!nwm_is_lorawan()) {
		rak_at_resp_error(NULL);
		return -EINVAL;
	}

	if (!lw()->is_joined()) {
		rak_at_resp_line("AT_NO_NETWORK_JOINED");
		return -ENOTCONN;
	}

	if (lw()->is_busy()) {
		busy_error();
		return -EBUSY;
	}

	if ((req->args == NULL) || (strlen(req->args) >= sizeof(args_buf))) {
		param_error();
		return -EINVAL;
	}

	strncpy(args_buf, req->args, sizeof(args_buf) - 1U);
	args_buf[sizeof(args_buf) - 1U] = '\0';

	colon = strchr(args_buf, ':');
	if (colon == NULL) {
		param_error();
		return -EINVAL;
	}

	*colon = '\0';
	port_str = args_buf;
	hex_str = colon + 1;

	if ((port_str[0] == '\0') || (hex_str[0] == '\0')) {
		param_error();
		return -EINVAL;
	}

	port_ul = strtoul(port_str, NULL, 10);
	if ((port_ul == 0UL) || (port_ul > 233UL)) {
		param_error();
		return -EINVAL;
	}

	hex_len = strlen(hex_str);
	if (((hex_len % 2U) != 0U) || (hex_len == 0U) || (hex_len > (sizeof(payload) * 2U))) {
		param_error();
		return -EINVAL;
	}

	if (!hex_valid_n(hex_str, hex_len)) {
		param_error();
		return -EINVAL;
	}

	bin_len = hex_len / 2U;
	if (hex2bin(hex_str, hex_len, payload, sizeof(payload)) != bin_len) {
		param_error();
		return -EINVAL;
	}

	msg_type = (lw()->get_cfm() != 0U) ? LORAWAN_MSG_CONFIRMED : LORAWAN_MSG_UNCONFIRMED;

	ret = lw()->send_async((uint8_t)port_ul, payload, (uint8_t)bin_len, msg_type);
	if (ret == -EBUSY) {
		busy_error();
		return ret;
	}
	if (ret == -ENOTCONN) {
		rak_at_resp_line("AT_NO_NETWORK_JOINED");
		return ret;
	}
	if (ret < 0) {
		param_error();
		return ret;
	}

	rak_at_resp_ok();
	return 0;
}

int rak_at_cmd_class(const struct rak_at_request *req)
{
	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("AT+CLASS: get or set the device class (A = class A, B = class B, C = class C)");
		rak_at_resp_ok();
		return 0;
	}

	if (req->form == RAK_AT_FORM_GET) {
		rak_at_resp_line("AT+CLASS=A");
		rak_at_resp_ok();
		return 0;
	}

	if (req->form == RAK_AT_FORM_SET) {
		if ((req->args == NULL) || (strcasecmp(req->args, "A") != 0)) {
			if ((req->args != NULL) &&
			    ((strcasecmp(req->args, "B") == 0) || (strcasecmp(req->args, "C") == 0))) {
				rak_at_resp_line("AT_NO_CLASSB_ENABLE");
				return -ENOTSUP;
			}
			param_error();
			return -EINVAL;
		}
		rak_at_resp_ok();
		return 0;
	}

	param_error();
	return -EINVAL;
}

/* Used by older call sites; prefer reading NWM from cfg. */
bool rak_at_nwm_is_p2p(void)
{
	return nwm_is_p2p();
}

void rak_at_register_lorawan_commands(void)
{
	(void)rak_at_register_command("APPKEY", rak_at_cmd_appkey, "AT+APPKEY");
	(void)rak_at_register_command("NWKKEY", rak_at_cmd_nwkkey, "AT+NWKKEY");
	(void)rak_at_register_command("NWM", rak_at_cmd_nwm, "AT+NWM");
	(void)rak_at_register_command("BAND", rak_at_cmd_band, "AT+BAND");
	(void)rak_at_register_command("CFM", rak_at_cmd_cfm, "AT+CFM");
	(void)rak_at_register_command("CFS", rak_at_cmd_cfs, "AT+CFS");
	(void)rak_at_register_command("NJS", rak_at_cmd_njs, "AT+NJS");
	(void)rak_at_register_command("NJM", rak_at_cmd_njm, "AT+NJM");
	(void)rak_at_register_command("ADR", rak_at_cmd_adr, "AT+ADR");
	(void)rak_at_register_command("RECV", rak_at_cmd_recv, "AT+RECV");
	(void)rak_at_register_command("JOIN", rak_at_cmd_join, "AT+JOIN");
	(void)rak_at_register_command("SEND", rak_at_cmd_send, "AT+SEND");
	(void)rak_at_register_command("CLASS", rak_at_cmd_class, "AT+CLASS");
}
