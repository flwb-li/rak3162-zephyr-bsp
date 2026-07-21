#include "at/hw_at.h"
#include "lora/hw_lorawan.h"
#include "storage/hw_storage.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <zephyr/sys/util.h>

static void param_error(void)
{
	hw_at_resp_line("AT_PARAM_ERROR");
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

static int save_cfg(const struct hw_runtime_cfg *cfg)
{
	if (hw_storage_set_pending_cfg(cfg) != 0) {
		return -EIO;
	}

	return hw_storage_apply_pending_cfg();
}

static int cmd_key_common(const struct hw_at_request *req, const char *name, uint32_t valid_bit, bool is_appkey)
{
	struct hw_runtime_cfg cfg;
	char key_hex[HW_KEY_BIN_LEN * 2 + 1];
	uint8_t *key;

	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("Description: %s (16 bytes hex)", name);
		hw_at_resp_line("Example: AT+%s=?  AT+%s=<32 hex chars>", name, name);
		hw_at_resp_ok();
		return 0;
	}

	hw_storage_get_active_cfg(&cfg);
	key = is_appkey ? cfg.appkey : cfg.nwkkey;

	if (req->form == HW_AT_FORM_GET) {
		if ((cfg.valid_mask & valid_bit) == 0U) {
			hw_at_resp_line("AT+%s=", name);
			hw_at_resp_ok();
			return 0;
		}

		if (format_hex_bytes(key, HW_KEY_BIN_LEN, key_hex, sizeof(key_hex)) != 0) {
			hw_at_resp_error(NULL);
			return -EINVAL;
		}

		hw_at_resp_line("AT+%s=%s", name, key_hex);
		hw_at_resp_ok();
		return 0;
	}

	if (req->form == HW_AT_FORM_SET) {
		if (parse_hex_bytes(req->args, HW_KEY_BIN_LEN * 2, key, HW_KEY_BIN_LEN) != 0) {
			param_error();
			return -EINVAL;
		}

		cfg.valid_mask |= valid_bit;
		if (save_cfg(&cfg) != 0) {
			hw_at_resp_error(NULL);
			return -EIO;
		}

		hw_at_resp_ok();
		return 0;
	}

	param_error();
	return -EINVAL;
}

int hw_at_cmd_appkey(const struct hw_at_request *req)
{
	return cmd_key_common(req, "APPKEY", HW_RUNTIME_CFG_VALID_APPKEY, true);
}

int hw_at_cmd_nwkkey(const struct hw_at_request *req)
{
	return cmd_key_common(req, "NWKKEY", HW_RUNTIME_CFG_VALID_NWKKEY, false);
}

int hw_at_cmd_join(const struct hw_at_request *req)
{
	struct hw_runtime_cfg cfg;
	const uint8_t *nwkkey;
	int ret;

	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("Description: OTAA join using stored DEVEUI/APPEUI/APPKEY[/NWKKEY]");
		hw_at_resp_line("Example: AT+JOIN  AT+JOIN=?");
		hw_at_resp_ok();
		return 0;
	}

	if (req->form == HW_AT_FORM_GET) {
		hw_at_resp_line("AT+JOIN=%s", hw_lorawan_is_joined() ? "joined" : "idle");
		hw_at_resp_ok();
		return 0;
	}

	if (req->form != HW_AT_FORM_EXEC) {
		param_error();
		return -EINVAL;
	}

	hw_storage_get_active_cfg(&cfg);

	if (((cfg.valid_mask & HW_RUNTIME_CFG_VALID_DEVEUI) == 0U) ||
	    ((cfg.valid_mask & HW_RUNTIME_CFG_VALID_APPEUI) == 0U) ||
	    ((cfg.valid_mask & HW_RUNTIME_CFG_VALID_APPKEY) == 0U)) {
		hw_at_resp_line("AT_ERROR: missing DEVEUI/APPEUI/APPKEY");
		return -EINVAL;
	}

	nwkkey = ((cfg.valid_mask & HW_RUNTIME_CFG_VALID_NWKKEY) != 0U) ? cfg.nwkkey : cfg.appkey;

	hw_at_resp_line("+EVT:JOIN_START");
	ret = hw_lorawan_join_otaa(cfg.deveui, cfg.appeui, cfg.appkey, nwkkey);
	if (ret != 0) {
		hw_at_resp_line("+EVT:JOIN_FAILED,%d", ret);
		hw_at_resp_error(NULL);
		return ret;
	}

	hw_at_resp_line("+EVT:JOINED");
	hw_at_resp_ok();
	return 0;
}

int hw_at_cmd_send(const struct hw_at_request *req)
{
	char args_buf[260];
	char *port_str;
	char *hex_str;
	char *type_str;
	char *saveptr = NULL;
	uint8_t payload[128];
	size_t hex_len;
	size_t bin_len;
	unsigned long port_ul;
	enum lorawan_message_type msg_type = LORAWAN_MSG_CONFIRMED;
	int ret;

	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("Description: LoRaWAN uplink");
		hw_at_resp_line("Syntax: AT+SEND=<port>,<hex>[,c|u]");
		hw_at_resp_line("  c=confirmed (default), u=unconfirmed");
		hw_at_resp_line("Example: AT+SEND=2,010203,c");
		hw_at_resp_ok();
		return 0;
	}

	if (req->form != HW_AT_FORM_SET) {
		param_error();
		return -EINVAL;
	}

	if (!hw_lorawan_is_joined()) {
		hw_at_resp_line("AT_ERROR: not joined");
		return -ENOTCONN;
	}

	if ((req->args == NULL) || (strlen(req->args) >= sizeof(args_buf))) {
		param_error();
		return -EINVAL;
	}

	strncpy(args_buf, req->args, sizeof(args_buf) - 1U);
	args_buf[sizeof(args_buf) - 1U] = '\0';

	port_str = strtok_r(args_buf, ",", &saveptr);
	hex_str = strtok_r(NULL, ",", &saveptr);
	type_str = strtok_r(NULL, ",", &saveptr);

	if ((port_str == NULL) || (hex_str == NULL)) {
		param_error();
		return -EINVAL;
	}

	port_ul = strtoul(port_str, NULL, 10);
	if ((port_ul == 0UL) || (port_ul > 223UL)) {
		param_error();
		return -EINVAL;
	}

	hex_len = strlen(hex_str);
	if (((hex_len % 2U) != 0U) || (hex_len == 0U) || (hex_len > (sizeof(payload) * 2U))) {
		param_error();
		return -EINVAL;
	}

	if (!hex_valid(hex_str, hex_len)) {
		param_error();
		return -EINVAL;
	}

	bin_len = hex_len / 2U;
	if (hex2bin(hex_str, hex_len, payload, sizeof(payload)) != bin_len) {
		param_error();
		return -EINVAL;
	}

	if (type_str != NULL) {
		if ((strcasecmp(type_str, "u") == 0) || (strcasecmp(type_str, "unconfirmed") == 0)) {
			msg_type = LORAWAN_MSG_UNCONFIRMED;
		} else if ((strcasecmp(type_str, "c") == 0) || (strcasecmp(type_str, "confirmed") == 0)) {
			msg_type = LORAWAN_MSG_CONFIRMED;
		} else {
			param_error();
			return -EINVAL;
		}
	}

	ret = hw_lorawan_send((uint8_t)port_ul, payload, (uint8_t)bin_len, msg_type);
	if (ret == -EAGAIN) {
		hw_at_resp_line("AT_ERROR: payload too large for current DR");
		return ret;
	}
	if (ret < 0) {
		hw_at_resp_line("AT_ERROR: send failed %d", ret);
		return ret;
	}

	hw_at_resp_line("+EVT:SEND_OK");
	hw_at_resp_ok();
	return 0;
}

int hw_at_cmd_class(const struct hw_at_request *req)
{
	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("Description: Query LoRaWAN class (Class A only in this sample)");
		hw_at_resp_line("Example: AT+CLASS=?");
		hw_at_resp_ok();
		return 0;
	}

	if (req->form != HW_AT_FORM_GET) {
		param_error();
		return -EINVAL;
	}

	hw_at_resp_line("AT+CLASS=%c",
			(hw_lorawan_get_class() == LORAWAN_CLASS_A) ? 'A' : '?');
	hw_at_resp_ok();
	return 0;
}
