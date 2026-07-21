#include "at/hw_at.h"
#include "lora/hw_lora_test.h"
#include "storage/hw_storage.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/util.h>

static void p2p_param_error(void)
{
    hw_at_resp_line("AT_PARAM_ERROR");
}

static void p2p_busy_error(void)
{
    hw_at_resp_line("AT_BUSY_ERROR");
}

static void p2p_resp_line_at_value(const char *name, const char *value)
{
    char buf[192];

    (void)snprintf(buf, sizeof(buf), "AT+%s=%s", name, value);
    hw_at_resp_line("%s", buf);
}

static bool psend_valid_hex(const char *s, size_t n)
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

static bool eui_valid_hex(const char *s, size_t exact_len)
{
    if ((s == NULL) || (strlen(s) != exact_len)) {
        return false;
    }

    return psend_valid_hex(s, exact_len);
}

static int parse_hex_bytes(const char *hex, size_t hex_len, uint8_t *buf, size_t buf_len)
{
    if (!eui_valid_hex(hex, hex_len)) {
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
        hw_at_resp_line("Description: Device EUI");
        hw_at_resp_line("Example: AT+DEVEUI?   AT+DEVEUI=?   AT+DEVEUI=<8 hex>");
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
            p2p_param_error();
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

    p2p_param_error();
    return -EINVAL;
}

int hw_at_cmd_appeui(const struct hw_at_request *req)
{
    struct hw_runtime_cfg cfg;

    if (req->form == HW_AT_FORM_HELP) {
        hw_at_resp_line("Description: Application identifier");
        hw_at_resp_line("Example: AT+APPEUI?   AT+APPEUI=?   AT+APPEUI=<8 hex>");
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
            p2p_param_error();
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

    p2p_param_error();
    return -EINVAL;
}

int hw_at_cmd_p2p(const struct hw_at_request *req)
{
    char buf[128];

    if (req->form == HW_AT_FORM_HELP) {
        hw_at_resp_line("Description: View/set P2P parameters in RAM only");
        hw_at_resp_line("AT+P2P     View current value");
        hw_at_resp_line("AT+P2P=?   Get current value");
        hw_at_resp_line("AT+P2P=<freq>:<sf>:<bw>:<cr>:<preamble>:<tx_power>  Set");
        hw_at_resp_line("BW: 0/125 1/250 2/500 3/7.8 4/10.4 5/15.63 6/20.83 7/31.25 8/41.67 9/62.5");
        hw_at_resp_line("Default: 868000000:7:125:0:8:14");
        hw_at_resp_ok();
        return 0;
    }

    if (req->form == HW_AT_FORM_GET) {
        if (hw_lora_p2p_params_format(buf, sizeof(buf)) != 0) {
            hw_at_resp_error(NULL);
            return -EIO;
        }
        p2p_resp_line_at_value("P2P", buf);
        hw_at_resp_ok();
        return 0;
    }

    if (req->form == HW_AT_FORM_EXEC) {
        if (hw_lora_p2p_params_format(buf, sizeof(buf)) != 0) {
            hw_at_resp_error(NULL);
            return -EIO;
        }
        p2p_resp_line_at_value("P2P", buf);
        hw_at_resp_ok();
        return 0;
    }

    if (req->form == HW_AT_FORM_SET) {
        if ((req->args == NULL) || (req->args[0] == '\0')) {
            p2p_param_error();
            return -EINVAL;
        }
        {
            int s = hw_lora_p2p_params_set(req->args);

            if (s == -EBUSY) {
                p2p_busy_error();
                return -EBUSY;
            }
            if (s != 0) {
                p2p_param_error();
                return -EINVAL;
            }
        }
        hw_at_resp_ok();
        return 0;
    }

    p2p_param_error();
    return -EINVAL;
}

int hw_at_cmd_precv(const struct hw_at_request *req)
{
    char buf[32];

    if (req->form == HW_AT_FORM_HELP) {
        hw_at_resp_line("Description: P2P received data");
        hw_at_resp_line("AT+PRECV : enter P2P RX mode for a period of time (ms)");
        hw_at_resp_line("AT+PRECV=? : get current PRECV value");
        hw_at_resp_line("AT+PRECV=<time>");
        hw_at_resp_line("Range: 0, 1..65532, 65533, 65534, 65535");
        hw_at_resp_line("65535: RX until one packet is received");
        hw_at_resp_line("65534: continuous RX, must use AT+PRECV=0 to exit");
        hw_at_resp_line("65533: continuous RX and still allow P2P TX");
        hw_at_resp_line("0: stop RX mode and switch to TX mode");
        hw_at_resp_ok();
        return 0;
    }

    if ((req->form == HW_AT_FORM_GET) || (req->form == HW_AT_FORM_EXEC)) {
        (void)snprintf(buf, sizeof(buf), "%u", (unsigned int)hw_lora_p2p_recv_get());
        p2p_resp_line_at_value("PRECV", buf);
        hw_at_resp_ok();
        return 0;
    }

    if (req->form != HW_AT_FORM_SET) {
        p2p_param_error();
        return -EINVAL;
    }
    if ((req->args == NULL) || (req->args[0] == '\0')) {
        p2p_param_error();
        return -EINVAL;
    }

    {
        char *end = NULL;
        unsigned long value;
        int s;

        errno = 0;
        value = strtoul(req->args, &end, 10);
        if ((errno != 0) || (end == req->args) || (*end != '\0') || (value > 65535UL)) {
            p2p_param_error();
            return -EINVAL;
        }
        s = hw_lora_p2p_recv_set((uint16_t)value);
        if (s == -EBUSY) {
            p2p_busy_error();
            return -EBUSY;
        }
        if (s == -EINVAL) {
            p2p_param_error();
            return -EINVAL;
        }
        if (s != 0) {
            hw_at_resp_error(NULL);
            return s;
        }
    }

    hw_at_resp_ok();
    return 0;
}

int hw_at_cmd_psend(const struct hw_at_request *req)
{
    uint8_t out[256];
    size_t olen;
    size_t hexlen;

    if (req->form == HW_AT_FORM_HELP) {
        hw_at_resp_line("Description: P2P send hex payload through USP");
        hw_at_resp_line("AT+PSEND=<hex>");
        hw_at_resp_line("Payload length: 2..500 hex chars, even length, 1..256 bytes");
        hw_at_resp_ok();
        return 0;
    }

    if (req->form != HW_AT_FORM_SET) {
        p2p_param_error();
        return -EINVAL;
    }
    if (req->args == NULL) {
        p2p_param_error();
        return -EINVAL;
    }

    hexlen = strlen(req->args);
    if ((hexlen < 2U) || (hexlen > 500U) || ((hexlen & 1U) != 0U)) {
        p2p_param_error();
        return -EINVAL;
    }
    if (!psend_valid_hex(req->args, hexlen)) {
        p2p_param_error();
        return -EINVAL;
    }

    olen = hexlen / 2U;
    if ((olen < 1U) || (olen > 256U)) {
        p2p_param_error();
        return -EINVAL;
    }

    if (hex2bin(req->args, hexlen, out, sizeof(out)) != olen) {
        p2p_param_error();
        return -EINVAL;
    }

    {
        int s = hw_lora_p2p_send_payload(out, olen);

        if (s == -EBUSY) {
            p2p_busy_error();
            return -EBUSY;
        }
        if (s == -EINVAL) {
            p2p_param_error();
            return -EINVAL;
        }
        if (s != 0) {
            hw_at_resp_error(NULL);
            return s;
        }
    }

    hw_at_resp_ok();
    return 0;
}

int hw_at_cmd_cw(const struct hw_at_request *req)
{
    char buf[48];

    if (req->form == HW_AT_FORM_HELP) {
        hw_at_resp_line("AT+CW: start continuous wave");
        hw_at_resp_ok();
        return 0;
    }

    if (req->form == HW_AT_FORM_GET) {
        if (hw_lora_cw_format(buf, sizeof(buf)) != 0) {
            hw_at_resp_error(NULL);
            return -EIO;
        }
        p2p_resp_line_at_value("CW", buf);
        hw_at_resp_ok();
        return 0;
    }

    if (req->form != HW_AT_FORM_SET) {
        p2p_param_error();
        return -EINVAL;
    }
    if ((req->args == NULL) || (req->args[0] == '\0')) {
        p2p_param_error();
        return -EINVAL;
    }

    {
        int s = hw_lora_cw_start(req->args);

        if (s == -EBUSY) {
            p2p_busy_error();
            return -EBUSY;
        }
        if (s == -EINVAL) {
            p2p_param_error();
            return -EINVAL;
        }
        if (s != 0) {
            hw_at_resp_error(NULL);
            return s;
        }
    }

    hw_at_resp_ok();
    return 0;
}

