#include "at/hw_at.h"
#include "lora/hw_lora_p2p.h"
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

static void busy_error(void)
{
	hw_at_resp_line("AT_BUSY_ERROR");
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

static int save_cfg(const struct hw_runtime_cfg *cfg)
{
	if (hw_storage_set_pending_cfg(cfg) != 0) {
		return -EIO;
	}

	return hw_storage_apply_pending_cfg();
}

static bool nwm_is_lorawan(void)
{
	struct hw_runtime_cfg cfg;

	hw_storage_get_active_cfg(&cfg);
	return cfg.nwm == HW_NWM_LORAWAN;
}

static bool nwm_is_p2p(void)
{
	struct hw_runtime_cfg cfg;

	hw_storage_get_active_cfg(&cfg);
	return cfg.nwm == HW_NWM_P2P_LORA;
}

static int cmd_key_common(const struct hw_at_request *req, const char *name, uint32_t valid_bit,
			  bool is_appkey)
{
	struct hw_runtime_cfg cfg;
	char key_hex[HW_KEY_BIN_LEN * 2 + 1];
	uint8_t *key;

	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("AT+%s: get or set the %s (16 bytes in hex)", name, name);
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

int hw_at_cmd_nwm(const struct hw_at_request *req)
{
	struct hw_runtime_cfg cfg;
	unsigned long v;

	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("AT+NWM: get or set the network working mode (0 = P2P_LORA, 1 = LoRaWAN)");
		hw_at_resp_ok();
		return 0;
	}

	hw_storage_get_active_cfg(&cfg);

	if (req->form == HW_AT_FORM_GET) {
		hw_at_resp_line("AT+NWM=%u", cfg.nwm);
		hw_at_resp_ok();
		return 0;
	}

	if (req->form != HW_AT_FORM_SET) {
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
		hw_at_resp_ok();
		return 0;
	}

	if (hw_lorawan_is_busy() || hw_lora_p2p_is_busy()) {
		busy_error();
		return -EBUSY;
	}

	/* Stop P2P RX when leaving P2P mode. */
	if (cfg.nwm == HW_NWM_P2P_LORA) {
		(void)hw_lora_p2p_recv_set(0);
	}

	cfg.nwm = (uint8_t)v;
	cfg.valid_mask |= HW_RUNTIME_CFG_VALID_LW_OPTS;
	if (save_cfg(&cfg) != 0) {
		hw_at_resp_error(NULL);
		return -EIO;
	}

	hw_at_resp_ok();
	return 0;
}

int hw_at_cmd_band(const struct hw_at_request *req)
{
	struct hw_runtime_cfg cfg;
	unsigned long v;

	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line(
			"AT+BAND: get or set the active region (0=EU433,1=CN470,2=RU864,3=IN865,4=EU868,5=US915,6=AU915,7=KR920,8=AS923)");
		hw_at_resp_ok();
		return 0;
	}

	hw_storage_get_active_cfg(&cfg);

	if (req->form == HW_AT_FORM_GET) {
		hw_at_resp_line("AT+BAND=%u", cfg.band);
		hw_at_resp_ok();
		return 0;
	}

	if (req->form != HW_AT_FORM_SET) {
		param_error();
		return -EINVAL;
	}

	if (hw_lorawan_is_started() || hw_lorawan_is_busy()) {
		busy_error();
		return -EBUSY;
	}

	v = strtoul(req->args, NULL, 10);
	if ((v > 12UL) || !hw_lorawan_band_supported((uint8_t)v)) {
		param_error();
		return -EINVAL;
	}

	if ((uint8_t)v == cfg.band) {
		hw_at_resp_ok();
		return 0;
	}

	cfg.band = (uint8_t)v;
	cfg.valid_mask |= HW_RUNTIME_CFG_VALID_LW_OPTS;
	if (save_cfg(&cfg) != 0) {
		hw_at_resp_error(NULL);
		return -EIO;
	}

	hw_at_resp_ok();
	return 0;
}

int hw_at_cmd_cfm(const struct hw_at_request *req)
{
	struct hw_runtime_cfg cfg;
	unsigned long v;

	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("AT+CFM: get or set the confirmation mode (0 = OFF, 1 = ON)");
		hw_at_resp_ok();
		return 0;
	}

	hw_storage_get_active_cfg(&cfg);

	if (req->form == HW_AT_FORM_GET) {
		hw_at_resp_line("AT+CFM=%u", hw_lorawan_get_cfm());
		hw_at_resp_ok();
		return 0;
	}

	if (req->form != HW_AT_FORM_SET) {
		param_error();
		return -EINVAL;
	}

	v = strtoul(req->args, NULL, 10);
	if ((v != 0UL) && (v != 1UL)) {
		param_error();
		return -EINVAL;
	}

	cfg.cfm = (uint8_t)v;
	cfg.valid_mask |= HW_RUNTIME_CFG_VALID_LW_OPTS;
	hw_lorawan_set_cfm(cfg.cfm);
	if (save_cfg(&cfg) != 0) {
		hw_at_resp_error(NULL);
		return -EIO;
	}

	hw_at_resp_ok();
	return 0;
}

int hw_at_cmd_cfs(const struct hw_at_request *req)
{
	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line(
			"AT+CFS: get the confirmation status of the last AT+SEND (0 = failure, 1 = success)");
		hw_at_resp_ok();
		return 0;
	}

	if (req->form != HW_AT_FORM_GET) {
		param_error();
		return -EINVAL;
	}

	hw_at_resp_line("AT+CFS=%u", hw_lorawan_get_cfs());
	hw_at_resp_ok();
	return 0;
}

int hw_at_cmd_njs(const struct hw_at_request *req)
{
	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("AT+NJS: get the join status (0 = not joined, 1 = joined)");
		hw_at_resp_ok();
		return 0;
	}

	if (req->form != HW_AT_FORM_GET) {
		param_error();
		return -EINVAL;
	}

	hw_at_resp_line("AT+NJS=%u", hw_lorawan_is_joined() ? 1U : 0U);
	hw_at_resp_ok();
	return 0;
}

int hw_at_cmd_njm(const struct hw_at_request *req)
{
	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("AT+NJM: get or set the network join mode (0 = ABP, 1 = OTAA)");
		hw_at_resp_ok();
		return 0;
	}

	if (req->form == HW_AT_FORM_GET) {
		hw_at_resp_line("AT+NJM=1");
		hw_at_resp_ok();
		return 0;
	}

	if (req->form == HW_AT_FORM_SET) {
		unsigned long v = strtoul(req->args, NULL, 10);

		if (v != 1UL) {
			param_error();
			return -EINVAL;
		}
		hw_at_resp_ok();
		return 0;
	}

	param_error();
	return -EINVAL;
}

int hw_at_cmd_adr(const struct hw_at_request *req)
{
	struct hw_runtime_cfg cfg;
	unsigned long v;

	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("AT+ADR: get or set the adaptive data rate setting (0 = OFF, 1 = ON)");
		hw_at_resp_ok();
		return 0;
	}

	hw_storage_get_active_cfg(&cfg);

	if (req->form == HW_AT_FORM_GET) {
		hw_at_resp_line("AT+ADR=%u", hw_lorawan_get_adr() ? 1U : 0U);
		hw_at_resp_ok();
		return 0;
	}

	if (req->form != HW_AT_FORM_SET) {
		param_error();
		return -EINVAL;
	}

	v = strtoul(req->args, NULL, 10);
	if ((v != 0UL) && (v != 1UL)) {
		param_error();
		return -EINVAL;
	}

	cfg.adr = (uint8_t)v;
	cfg.valid_mask |= HW_RUNTIME_CFG_VALID_LW_OPTS;
	hw_lorawan_set_adr(v != 0UL);
	if (save_cfg(&cfg) != 0) {
		hw_at_resp_error(NULL);
		return -EIO;
	}

	hw_at_resp_ok();
	return 0;
}

int hw_at_cmd_recv(const struct hw_at_request *req)
{
	char buf[8 + 256];

	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("AT+RECV: print the last received data in hex format");
		hw_at_resp_ok();
		return 0;
	}

	if (req->form != HW_AT_FORM_GET) {
		param_error();
		return -EINVAL;
	}

	if (hw_lorawan_recv_format_and_clear(buf, sizeof(buf)) != 0) {
		hw_at_resp_error(NULL);
		return -EINVAL;
	}

	hw_at_resp_line("AT+RECV=%s", buf);
	hw_at_resp_ok();
	return 0;
}

int hw_at_cmd_join(const struct hw_at_request *req)
{
	struct hw_runtime_cfg cfg;
	const uint8_t *nwkkey;
	char *saveptr = NULL;
	char args_buf[32];
	char *tok;
	unsigned long p1 = 1UL;
	unsigned long p2;
	unsigned long p3;
	unsigned long p4;
	int ret;

	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("AT+JOIN: join network");
		hw_at_resp_ok();
		return 0;
	}

	hw_storage_get_active_cfg(&cfg);

	/* RUI3: AT+JOIN=? returns AT_BUSY_ERROR while joining. */
	if (req->form == HW_AT_FORM_GET) {
		if (hw_lorawan_is_joining()) {
			busy_error();
			return -EBUSY;
		}
		hw_at_resp_line("AT+JOIN=%u:%u:%u:%u", cfg.join_cmd, cfg.join_auto,
				cfg.join_interval_s, cfg.join_attempts);
		hw_at_resp_ok();
		return 0;
	}

	if ((req->form != HW_AT_FORM_EXEC) && (req->form != HW_AT_FORM_SET)) {
		param_error();
		return -EINVAL;
	}

	if (!nwm_is_lorawan()) {
		hw_at_resp_error(NULL);
		return -EINVAL;
	}

	if (hw_lora_p2p_is_active()) {
		busy_error();
		return -EBUSY;
	}

	p2 = cfg.join_auto;
	p3 = cfg.join_interval_s;
	p4 = cfg.join_attempts;

	if (req->form == HW_AT_FORM_SET) {
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
		cfg.valid_mask |= HW_RUNTIME_CFG_VALID_LW_OPTS;
		if (save_cfg(&cfg) != 0) {
			hw_at_resp_error(NULL);
			return -EIO;
		}

		/* Param1=0: stop joining (RUI3). */
		if (p1 == 0UL) {
			(void)hw_lorawan_join_stop();
			hw_at_resp_ok();
			return 0;
		}
	}

	/* EXEC AT+JOIN or SET with Param1=1: start join using (stored) params. */
	if (hw_lorawan_is_busy()) {
		busy_error();
		return -EBUSY;
	}

	if (((cfg.valid_mask & HW_RUNTIME_CFG_VALID_DEVEUI) == 0U) ||
	    ((cfg.valid_mask & HW_RUNTIME_CFG_VALID_APPEUI) == 0U) ||
	    ((cfg.valid_mask & HW_RUNTIME_CFG_VALID_APPKEY) == 0U)) {
		param_error();
		return -EINVAL;
	}

	nwkkey = ((cfg.valid_mask & HW_RUNTIME_CFG_VALID_NWKKEY) != 0U) ? cfg.nwkkey : cfg.appkey;

	ret = hw_lorawan_join_otaa_async(cfg.deveui, cfg.appeui, cfg.appkey, nwkkey,
					 cfg.join_interval_s, cfg.join_attempts);
	if (ret == -EBUSY) {
		busy_error();
		return ret;
	}
	if (ret != 0) {
		hw_at_resp_error(NULL);
		return ret;
	}

	/* RUI3: OK means join started; completion via +EVT */
	hw_at_resp_ok();
	return 0;
}

int hw_at_cmd_send(const struct hw_at_request *req)
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

	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("AT+SEND: send data along with the application port");
		hw_at_resp_ok();
		return 0;
	}

	if (req->form != HW_AT_FORM_SET) {
		param_error();
		return -EINVAL;
	}

	if (!nwm_is_lorawan()) {
		hw_at_resp_error(NULL);
		return -EINVAL;
	}

	if (!hw_lorawan_is_joined()) {
		hw_at_resp_line("AT_NO_NETWORK_JOINED");
		return -ENOTCONN;
	}

	if (hw_lorawan_is_busy()) {
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

	msg_type = (hw_lorawan_get_cfm() != 0U) ? LORAWAN_MSG_CONFIRMED : LORAWAN_MSG_UNCONFIRMED;

	ret = hw_lorawan_send_async((uint8_t)port_ul, payload, (uint8_t)bin_len, msg_type);
	if (ret == -EBUSY) {
		busy_error();
		return ret;
	}
	if (ret == -ENOTCONN) {
		hw_at_resp_line("AT_NO_NETWORK_JOINED");
		return ret;
	}
	if (ret < 0) {
		param_error();
		return ret;
	}

	hw_at_resp_ok();
	return 0;
}

int hw_at_cmd_class(const struct hw_at_request *req)
{
	if (req->form == HW_AT_FORM_HELP) {
		hw_at_resp_line("AT+CLASS: get or set the device class (A = class A, B = class B, C = class C)");
		hw_at_resp_ok();
		return 0;
	}

	if (req->form == HW_AT_FORM_GET) {
		hw_at_resp_line("AT+CLASS=A");
		hw_at_resp_ok();
		return 0;
	}

	if (req->form == HW_AT_FORM_SET) {
		if ((req->args == NULL) || (strcasecmp(req->args, "A") != 0)) {
			if ((req->args != NULL) &&
			    ((strcasecmp(req->args, "B") == 0) || (strcasecmp(req->args, "C") == 0))) {
				hw_at_resp_line("AT_NO_CLASSB_ENABLE");
				return -ENOTSUP;
			}
			param_error();
			return -EINVAL;
		}
		hw_at_resp_ok();
		return 0;
	}

	param_error();
	return -EINVAL;
}

/* Used by P2P AT handlers. */
bool hw_at_nwm_is_p2p(void)
{
	return nwm_is_p2p();
}
