#include "at/hw_at.h"
#include "storage/hw_storage.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

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

static int save_lora_cfg(const struct hw_runtime_cfg *cfg)
{
	if (hw_storage_set_pending_cfg(cfg) != 0) {
		return -EIO;
	}

	return hw_storage_apply_pending_cfg();
}

int hw_at_cmd_deveui(const struct hw_at_request *req)
{
	struct hw_runtime_cfg cfg;

	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("Description: Device EUI (8 bytes hex)");
		hw_at_resp_line("Example: AT+DEVEUI=?  AT+DEVEUI=<16 hex chars>");
		hw_at_resp_ok();
		return 0;
	}

	hw_storage_get_active_cfg(&cfg);

	if (req->form == HW_AT_FORM_GET) {
		char deveui_hex[HW_EUI_BIN_LEN * 2 + 1];

		if ((cfg.valid_mask & HW_RUNTIME_CFG_VALID_DEVEUI) == 0U) {
			hw_at_resp_line("AT+DEVEUI=");
			hw_at_resp_ok();
			return 0;
		}

		if (format_hex_bytes(cfg.deveui, sizeof(cfg.deveui), deveui_hex, sizeof(deveui_hex)) != 0) {
			hw_at_resp_error(NULL);
			return -EINVAL;
		}

		hw_at_resp_line("AT+DEVEUI=%s", deveui_hex);
		hw_at_resp_ok();
		return 0;
	}

	if (req->form == HW_AT_FORM_SET) {
		if (parse_hex_bytes(req->args, HW_EUI_BIN_LEN * 2, cfg.deveui, sizeof(cfg.deveui)) != 0) {
			param_error();
			return -EINVAL;
		}

		cfg.valid_mask |= HW_RUNTIME_CFG_VALID_DEVEUI;
		if (save_lora_cfg(&cfg) != 0) {
			hw_at_resp_error(NULL);
			return -EIO;
		}

		hw_at_resp_ok();
		return 0;
	}

	param_error();
	return -EINVAL;
}

int hw_at_cmd_appeui(const struct hw_at_request *req)
{
	struct hw_runtime_cfg cfg;

	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("Description: JoinEUI / AppEUI (8 bytes hex)");
		hw_at_resp_line("Example: AT+APPEUI=?  AT+APPEUI=<16 hex chars>");
		hw_at_resp_ok();
		return 0;
	}

	hw_storage_get_active_cfg(&cfg);

	if (req->form == HW_AT_FORM_GET) {
		char appeui_hex[HW_EUI_BIN_LEN * 2 + 1];

		if ((cfg.valid_mask & HW_RUNTIME_CFG_VALID_APPEUI) == 0U) {
			hw_at_resp_line("AT+APPEUI=");
			hw_at_resp_ok();
			return 0;
		}

		if (format_hex_bytes(cfg.appeui, sizeof(cfg.appeui), appeui_hex, sizeof(appeui_hex)) != 0) {
			hw_at_resp_error(NULL);
			return -EINVAL;
		}

		hw_at_resp_line("AT+APPEUI=%s", appeui_hex);
		hw_at_resp_ok();
		return 0;
	}

	if (req->form == HW_AT_FORM_SET) {
		if (parse_hex_bytes(req->args, HW_EUI_BIN_LEN * 2, cfg.appeui, sizeof(cfg.appeui)) != 0) {
			param_error();
			return -EINVAL;
		}

		cfg.valid_mask |= HW_RUNTIME_CFG_VALID_APPEUI;
		if (save_lora_cfg(&cfg) != 0) {
			hw_at_resp_error(NULL);
			return -EIO;
		}

		hw_at_resp_ok();
		return 0;
	}

	param_error();
	return -EINVAL;
}
