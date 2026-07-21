#include "hw_at.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <strings.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/util.h>

#include "core/hw_console.h"
#include "core/hw_led.h"
#include "core/hw_xo_cap.h"
#include "storage/hw_storage.h"

#include "config.h"

#define HW_AT_MAX_COMMANDS 128

LOG_MODULE_REGISTER(hw_at, LOG_LEVEL_INF);

struct hw_at_command {
    const char *name;
    hw_at_handler_t handler;
    const char *help;
};

struct hw_at_runtime {
    bool echo;
    bool locked;
    struct hw_runtime_cfg cfg;
};

static struct hw_at_command command_table[HW_AT_MAX_COMMANDS];
static size_t command_count;
static struct hw_at_runtime at_rt;
static uint32_t rtc_wakeup_delay_s;

static void load_runtime_from_storage(void)
{
    hw_storage_get_active_cfg(&at_rt.cfg);
}

static void save_runtime_to_storage(void)
{
    if (hw_storage_set_pending_cfg(&at_rt.cfg) == 0) {
        (void)hw_storage_apply_pending_cfg();
    }
}

static bool cfg_is_valid(uint32_t bit)
{
    return (at_rt.cfg.valid_mask & bit) != 0U;
}

static void cfg_mark_valid(uint32_t bit)
{
    at_rt.cfg.valid_mask |= bit;
}

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

static void str_to_upper(char *s)
{
    while (*s != '\0') {
        *s = (char)toupper((unsigned char)*s);
        s++;
    }
}

static char *trim(char *s)
{
    char *end;

    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }

    end = s + strlen(s) - 1;
    while ((end > s) && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return s;
}

static struct hw_at_command *find_command(const char *name)
{
    for (size_t i = 0; i < command_count; i++) {
        if (strcmp(command_table[i].name, name) == 0) {
            return &command_table[i];
        }
    }
    return NULL;
}

static void resp_status(const char *status)
{
    hw_console_puts(status);
    hw_console_puts("\r\n");
}

static void resp_ok(void)
{
    resp_status("OK");
}

static void resp_at_error(void)
{
    resp_status("AT_ERROR");
}

static void resp_param_error(void)
{
    resp_status("AT_PARAM_ERROR");
}

static void resp_cmd_value(const char *cmd, const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintk(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    hw_console_puts("AT+");
    hw_console_puts(cmd);
    hw_console_puts("=");
    hw_console_puts(buf);
    hw_console_puts("\r\n");
}

void hw_at_resp_ok(void)
{
    resp_ok();
}

void hw_at_resp_error(const char *err)
{
    ARG_UNUSED(err);
    resp_at_error();
}

void hw_at_resp_line(const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintk(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    hw_console_puts(buf);
    hw_console_puts("\r\n");
}

int hw_at_register_command(const char *name, hw_at_handler_t handler, const char *help)
{
    if ((name == NULL) || (handler == NULL) || (command_count >= HW_AT_MAX_COMMANDS)) {
        return -ENOMEM;
    }
    command_table[command_count].name = name;
    command_table[command_count].handler = handler;
    command_table[command_count].help = help;
    command_count++;
    return 0;
}

static int cmd_global_help(void)
{
    hw_at_resp_line("- AT+<CMD>? : help on <CMD>");
    hw_at_resp_line("- AT+<CMD> : run <CMD>");
    hw_at_resp_line("- AT+<CMD>=<value> : set the value");
    hw_at_resp_line("- AT+<CMD>=? : get the value");
    for (size_t i = 0; i < command_count; i++) {
        hw_at_resp_line("%s", command_table[i].help);
    }
    resp_ok();
    return 0;
}

static int cmd_atz(const struct hw_at_request *req)
{
    if (req->form == HW_AT_FORM_HELP) {
        hw_at_resp_line("ATZ : triggers a reset on the MCU.");
        resp_ok();
        return 0;
    }
    if (req->form != HW_AT_FORM_EXEC) {
        resp_param_error();
        return -EINVAL;
    }

    LOG_INF("ATZ reboot requested");
    k_msleep(20);
    sys_reboot(SYS_REBOOT_COLD);
    return 0;
}

static int cmd_atsn(const struct hw_at_request *req)
{
    if (req->form == HW_AT_FORM_HELP) {
        hw_at_resp_line("Description: Serial number");
        hw_at_resp_line("Example: AT+SN?   AT+SN=?  AT+SN=<18 char>");
        resp_ok();
        return 0;
    }

    if (req->form == HW_AT_FORM_GET) {
        if (!cfg_is_valid(HW_RUNTIME_CFG_VALID_SN)) {
            resp_cmd_value("SN", "%s", "");
            resp_ok();
            return 0;
        }
        resp_cmd_value("SN", "%s", at_rt.cfg.sn);
        resp_ok();
        return 0;
    }

    if (req->form == HW_AT_FORM_SET) {
        if (!is_printable_ascii_string(req->args, HW_SN_LEN)) {
            resp_param_error();
            return -EINVAL;
        }

        load_runtime_from_storage();
        snprintf(at_rt.cfg.sn, sizeof(at_rt.cfg.sn), "%s", req->args);
        cfg_mark_valid(HW_RUNTIME_CFG_VALID_SN);
        save_runtime_to_storage();
        resp_ok();
        return 0;
    }

    resp_param_error();
    return -EINVAL;
}

static int cmd_buildtime(const struct hw_at_request *req)
{
    const char *value = "";
    value = __DATE__ " " __TIME__;
    if ((req->form == HW_AT_FORM_GET) || (req->form == HW_AT_FORM_HELP)) {
        resp_cmd_value(req->name, "%s", value);
        resp_ok();
        return 0;
    } else {
        resp_at_error();
        return -EINVAL;
    }
}
static int cmd_atver(const struct hw_at_request *req)
{
    if ((req->form == HW_AT_FORM_GET) || (req->form == HW_AT_FORM_HELP)) {
        resp_cmd_value(req->name, "%s", SOFTWARE_VERSION);
        resp_ok();
        return 0;
    } else {
        resp_at_error();
        return -EINVAL;
    }
}

static int cmd_sleep(const struct hw_at_request *req)
{
    if (req->form == HW_AT_FORM_HELP) {
        hw_at_resp_line("AT+SLEEP=<delay_ms>");
        hw_at_resp_line("Delay then enter System OFF (optional GRTC wakeup via AT+RTC)");
        resp_ok();
        return 0;
    }

    if (req->form == HW_AT_FORM_SET) {
        unsigned long delay_ms = 0;
        int parsed = 0;
        int ret;

        if (req->args == NULL) {
            resp_param_error();
            return -EINVAL;
        }

        parsed = sscanf(req->args, "%lu", &delay_ms);
        if (parsed != 1) {
            resp_param_error();
            return -EINVAL;
        }

        if (rtc_wakeup_delay_s > 0U) {
            ret = z_nrf_grtc_wakeup_prepare((uint64_t)rtc_wakeup_delay_s * 1000000ULL);
            if (ret != 0) {
                LOG_WRN("GRTC wakeup prepare failed: %d", ret);
                resp_status("RTC_WAKEUP_ERROR");
                return ret;
            }
            LOG_INF("GRTC wakeup armed: %u s", rtc_wakeup_delay_s);
        }

        resp_ok();
        hw_led_prepare_poweroff();
        k_msleep((uint32_t)delay_ms);

        const struct device *cons = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
        if (device_is_ready(cons)) {
            ret = pm_device_action_run(cons, PM_DEVICE_ACTION_SUSPEND);
            if (ret < 0) {
                LOG_WRN("console suspend failed: %d", ret);
            }
        }
        (void)hwinfo_clear_reset_cause();
        LOG_INF("Entering system off");
        sys_poweroff();
        return 0;
    } else {
        resp_at_error();
        return -EINVAL;
    }
}

static int cmd_rtc(const struct hw_at_request *req)
{
    if (req->form == HW_AT_FORM_HELP) {
        hw_at_resp_line("AT+RTC=?           Get wakeup delay (s)");
        hw_at_resp_line("AT+RTC=<s>         Set wakeup delay for next AT+SLEEP");
        hw_at_resp_line("AT+RTC=0           Disable RTC wakeup");
        resp_ok();
        return 0;
    }

    if (req->form == HW_AT_FORM_GET) {
        resp_cmd_value("RTC", "%u", rtc_wakeup_delay_s);
        resp_ok();
        return 0;
    }

    if (req->form == HW_AT_FORM_SET) {
        unsigned long v = 0U;

        if (req->args == NULL) {
            resp_param_error();
            return -EINVAL;
        }

        if (sscanf(req->args, "%lu", &v) != 1) {
            resp_param_error();
            return -EINVAL;
        }

        if (v > UINT32_MAX) {
            resp_param_error();
            return -EINVAL;
        }

        rtc_wakeup_delay_s = (uint32_t)v;
        resp_ok();
        return 0;
    }

    resp_param_error();
    return -EINVAL;
}

static int cmd_hfxocap(const struct hw_at_request *req)
{
    if (req->form == HW_AT_FORM_HELP) {
        hw_at_resp_line("Description: HFXO internal load capacitance (femtofarads)");
        hw_at_resp_line("Range: 4000-17000, step 250");
        hw_at_resp_line("Example: AT+HFXOCAP=?  AT+HFXOCAP=15000");
        resp_ok();
        return 0;
    }

    if (req->form == HW_AT_FORM_GET) {
        uint32_t cap_ff = cfg_is_valid(HW_RUNTIME_CFG_VALID_HFXO_CAP)
                                  ? at_rt.cfg.hfxo_cap_ff
                                  : hw_hfxo_cap_default_ff();

        resp_cmd_value("HFXOCAP", "%u", cap_ff);
        resp_ok();
        return 0;
    }

    if (req->form == HW_AT_FORM_SET) {
        unsigned long v = 0U;

        if (req->args == NULL) {
            resp_param_error();
            return -EINVAL;
        }

        if (sscanf(req->args, "%lu", &v) != 1) {
            resp_param_error();
            return -EINVAL;
        }

        if (hw_hfxo_cap_apply_ff((uint32_t)v) != 0) {
            resp_param_error();
            return -EINVAL;
        }

        at_rt.cfg.hfxo_cap_ff = (uint32_t)v;
        cfg_mark_valid(HW_RUNTIME_CFG_VALID_HFXO_CAP);
        save_runtime_to_storage();
        resp_ok();
        return 0;
    }

    resp_param_error();
    return -EINVAL;
}

static int cmd_lfxocap(const struct hw_at_request *req)
{
    if (req->form == HW_AT_FORM_HELP) {
        hw_at_resp_line("Description: LFXO internal load capacitance (femtofarads)");
        hw_at_resp_line("Range: 4000-18000, step 500");
        hw_at_resp_line("Example: AT+LFXOCAP=?  AT+LFXOCAP=16000");
        resp_ok();
        return 0;
    }

    if (req->form == HW_AT_FORM_GET) {
        uint32_t cap_ff = cfg_is_valid(HW_RUNTIME_CFG_VALID_LFXO_CAP)
                                  ? at_rt.cfg.lfxo_cap_ff
                                  : hw_lfxo_cap_default_ff();

        resp_cmd_value("LFXOCAP", "%u", cap_ff);
        resp_ok();
        return 0;
    }

    if (req->form == HW_AT_FORM_SET) {
        unsigned long v = 0U;

        if (req->args == NULL) {
            resp_param_error();
            return -EINVAL;
        }

        if (sscanf(req->args, "%lu", &v) != 1) {
            resp_param_error();
            return -EINVAL;
        }

        if (hw_lfxo_cap_apply_ff((uint32_t)v) != 0) {
            resp_param_error();
            return -EINVAL;
        }

        at_rt.cfg.lfxo_cap_ff = (uint32_t)v;
        cfg_mark_valid(HW_RUNTIME_CFG_VALID_LFXO_CAP);
        save_runtime_to_storage();
        resp_ok();
        return 0;
    }

    resp_param_error();
    return -EINVAL;
}

void hw_at_init(void)
{
    command_count = 0U;
    load_runtime_from_storage();
    hw_console_set_echo(true);

    (void)hw_at_register_command("Z", cmd_atz, "ATZ");
    (void)hw_at_register_command("SN", cmd_atsn, "AT+SN");
    (void)hw_at_register_command("BUILDTIME", cmd_buildtime, "AT+BUILDTIME");
    (void)hw_at_register_command("VER", cmd_atver, "AT+VER");
    (void)hw_at_register_command("RTC", cmd_rtc, "AT+RTC");
    (void)hw_at_register_command("SLEEP", cmd_sleep, "AT+SLEEP");
    (void)hw_at_register_command("DEVEUI", hw_at_cmd_deveui, "AT+DEVEUI");
    (void)hw_at_register_command("APPEUI", hw_at_cmd_appeui, "AT+APPEUI");
    (void)hw_at_register_command("P2P", hw_at_cmd_p2p, "AT+P2P");
    (void)hw_at_register_command("PRECV", hw_at_cmd_precv, "AT+PRECV");
    (void)hw_at_register_command("PSEND", hw_at_cmd_psend, "AT+PSEND");
    (void)hw_at_register_command("CW", hw_at_cmd_cw, "AT+CW");
    (void)hw_at_register_command("APPKEY", hw_at_cmd_appkey, "AT+APPKEY");
    (void)hw_at_register_command("NWKKEY", hw_at_cmd_nwkkey, "AT+NWKKEY");
    (void)hw_at_register_command("JOIN", hw_at_cmd_join, "AT+JOIN");
    (void)hw_at_register_command("SEND", hw_at_cmd_send, "AT+SEND");
    (void)hw_at_register_command("CLASS", hw_at_cmd_class, "AT+CLASS");
    (void)hw_at_register_command("TEST", hw_at_cmd_test, "AT+TEST");
    (void)hw_at_register_command("HFXOCAP", cmd_hfxocap, "AT+HFXOCAP");
    (void)hw_at_register_command("LFXOCAP", cmd_lfxocap, "AT+LFXOCAP");
    (void)hw_at_register_command("BLECW", hw_at_cmd_blecw, "AT+BLECW");
    (void)hw_at_register_command("BLECWSTOP", hw_at_cmd_blecwstop, "AT+BLECWSTOP");
}

void hw_at_process_line(char *line)
{
    struct hw_at_command *cmd;
    struct hw_at_request req = {
        .form = HW_AT_FORM_EXEC,
        .args = NULL,
        .raw = line,
    };
    char *p;
    char *name;
    char *suffix;
    char *eq;
    size_t name_len;

    if (line == NULL) {
        return;
    }

    p = trim(line);
    if (*p == '\0') {
        return;
    }

    LOG_DBG("AT line: %s", p);

    if (strcasecmp(p, "AT") == 0) {
        resp_ok();
        return;
    }
    if (strcasecmp(p, "AT?") == 0) {
        (void)cmd_global_help();
        return;
    }
    if (strncasecmp(p, "AT", 2) != 0) {
        resp_at_error();
        return;
    }

    suffix = p + 2;
    if (*suffix == '+') {
        suffix++;
    }
    if (*suffix == '\0') {
        resp_ok();
        return;
    }

    name = suffix;
    name_len = strlen(name);
    if ((name_len >= 2U) && (name[name_len - 2U] == '=') && (name[name_len - 1U] == '?')) {
        name[name_len - 2U] = '\0';
        req.form = HW_AT_FORM_GET;
    } else if ((name_len >= 1U) && (name[name_len - 1U] == '?')) {
        name[name_len - 1U] = '\0';
        req.form = HW_AT_FORM_HELP;
    } else {
        eq = strchr(name, '=');
        if (eq != NULL) {
            *eq = '\0';
            req.form = HW_AT_FORM_SET;
            req.args = eq + 1;
        }
    }

    /* ATE0/ATE1 shorthand. */
    if ((req.form == HW_AT_FORM_EXEC) && (name[0] == 'E') && ((name[1] == '0') || (name[1] == '1')) &&
        (name[2] == '\0')) {
        req.form = HW_AT_FORM_SET;
        req.args = &name[1];
        name[1] = '\0';
    }

    /* ATR / ATZ / ATE without plus sign. */
    if ((name[0] == 'R') && (name[1] == '\0')) {
        name[0] = 'R';
    }

    str_to_upper(name);
    req.name = name;

    if (at_rt.locked && (strcmp(name, "PWORD") != 0) && (strcmp(name, "ATM") != 0)) {
        resp_status("COMMAND_LOCKED");
        return;
    }

    cmd = find_command(name);
    if (cmd == NULL) {
        resp_at_error();
        return;
    }

    if (cmd->handler(&req) != 0) {
        LOG_DBG("AT command handled with error: %s", name);
    }
}
