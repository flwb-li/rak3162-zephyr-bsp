/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rak_at/rak_at.h>
#include <rak_at/rak_at_cfg.h>
#include <rak_at/rak_at_lorawan_svc.h>
#include <rak_at/rak_at_p2p_svc.h>
#include <rak_at/rak_at_port.h>
#include <rak_at/rak_at_util.h>

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#define RAK_AT_MAX_COMMANDS 128

LOG_MODULE_REGISTER(rak_at, LOG_LEVEL_INF);

struct rak_at_command {
	const char *name;
	rak_at_handler_t handler;
	const char *help;
};

static struct rak_at_command command_table[RAK_AT_MAX_COMMANDS];
static size_t command_count;
static bool echo_enabled = true;

static const struct rak_at_cfg_ops *cfg_ops;
static const struct rak_at_lorawan_ops *lorawan_ops;
static const struct rak_at_p2p_ops *p2p_ops;

void rak_at_cfg_set_ops(const struct rak_at_cfg_ops *ops)
{
	cfg_ops = ops;
}

void rak_at_cfg_get_active(struct rak_at_runtime_cfg *cfg)
{
	if ((cfg_ops != NULL) && (cfg_ops->get_active != NULL) && (cfg != NULL)) {
		cfg_ops->get_active(cfg);
	} else if (cfg != NULL) {
		memset(cfg, 0, sizeof(*cfg));
	}
}

int rak_at_cfg_set_and_apply(const struct rak_at_runtime_cfg *cfg)
{
	if ((cfg_ops == NULL) || (cfg_ops->set_and_apply == NULL)) {
		return -ENOTSUP;
	}

	return cfg_ops->set_and_apply(cfg);
}

void rak_at_lorawan_set_ops(const struct rak_at_lorawan_ops *ops)
{
	lorawan_ops = ops;
}

const struct rak_at_lorawan_ops *rak_at_lorawan_ops(void)
{
	return lorawan_ops;
}

void rak_at_p2p_set_ops(const struct rak_at_p2p_ops *ops)
{
	p2p_ops = ops;
}

const struct rak_at_p2p_ops *rak_at_p2p_ops(void)
{
	return p2p_ops;
}

static void str_to_upper(char *s)
{
	rak_at_str_to_upper(s);
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

static struct rak_at_command *find_command(const char *name)
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
	rak_at_port_puts(status);
	rak_at_port_puts("\r\n");
}

void rak_at_resp_ok(void)
{
	resp_status("OK");
}

void rak_at_resp_error(const char *err)
{
	ARG_UNUSED(err);
	resp_status("AT_ERROR");
}

void rak_at_resp_line(const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintk(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	rak_at_port_puts(buf);
	rak_at_port_puts("\r\n");
}

void rak_at_evt(const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	vsnprintk(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	rak_at_port_puts(buf);
	rak_at_port_puts("\r\n");
}

void rak_at_set_echo(bool enabled)
{
	echo_enabled = enabled;
}

bool rak_at_is_echo_enabled(void)
{
	return echo_enabled;
}

int rak_at_register_command(const char *name, rak_at_handler_t handler, const char *help)
{
	if ((name == NULL) || (handler == NULL) || (command_count >= RAK_AT_MAX_COMMANDS)) {
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
	rak_at_print_command_list();
	rak_at_resp_ok();
	return 0;
}

void rak_at_print_command_list(void)
{
	rak_at_resp_line("- AT+<CMD>? : help on <CMD>");
	rak_at_resp_line("- AT+<CMD> : run <CMD>");
	rak_at_resp_line("- AT+<CMD>=<value> : set the value");
	rak_at_resp_line("- AT+<CMD>=? : get the value");
	for (size_t i = 0; i < command_count; i++) {
		rak_at_resp_line("%s", command_table[i].help);
	}
}

static int cmd_atz(const struct rak_at_request *req)
{
	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("ATZ : triggers a reset on the MCU.");
		rak_at_resp_ok();
		return 0;
	}
	if (req->form != RAK_AT_FORM_EXEC) {
		rak_at_resp_line("AT_PARAM_ERROR");
		return -EINVAL;
	}

	LOG_INF("ATZ reboot requested");
	k_msleep(20);
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

static int cmd_ate(const struct rak_at_request *req)
{
	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line("ATE0/ATE1 : disable/enable command echo");
		rak_at_resp_ok();
		return 0;
	}

	if (req->form == RAK_AT_FORM_GET) {
		rak_at_resp_line("AT+E=%u", echo_enabled ? 1U : 0U);
		rak_at_resp_ok();
		return 0;
	}

	if (req->form == RAK_AT_FORM_SET) {
		if ((req->args == NULL) ||
		    ((req->args[0] != '0') && (req->args[0] != '1')) ||
		    (req->args[1] != '\0')) {
			rak_at_resp_line("AT_PARAM_ERROR");
			return -EINVAL;
		}
		echo_enabled = (req->args[0] == '1');
		rak_at_resp_ok();
		return 0;
	}

	rak_at_resp_line("AT_PARAM_ERROR");
	return -EINVAL;
}

void rak_at_init(void)
{
	command_count = 0U;
	echo_enabled = true;

	(void)rak_at_register_command("Z", cmd_atz, "ATZ");
	(void)rak_at_register_command("E", cmd_ate, "ATE0/ATE1");
}

#if !defined(CONFIG_RAK_AT_LORAWAN)
void rak_at_register_lorawan_commands(void)
{
}
#endif

#if !defined(CONFIG_RAK_AT_LORA_P2P)
void rak_at_register_lora_p2p_commands(void)
{
}
#endif

void rak_at_process_line(char *line)
{
	struct rak_at_command *cmd;
	struct rak_at_request req = {
		.form = RAK_AT_FORM_EXEC,
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
		rak_at_resp_ok();
		return;
	}
	if (strcasecmp(p, "AT?") == 0) {
		(void)cmd_global_help();
		return;
	}
	if (strncasecmp(p, "AT", 2) != 0) {
		rak_at_resp_error(NULL);
		return;
	}

	suffix = p + 2;
	if (*suffix == '+') {
		suffix++;
	}
	if (*suffix == '\0') {
		rak_at_resp_ok();
		return;
	}

	name = suffix;
	name_len = strlen(name);
	if ((name_len >= 2U) && (name[name_len - 2U] == '=') && (name[name_len - 1U] == '?')) {
		name[name_len - 2U] = '\0';
		req.form = RAK_AT_FORM_GET;
	} else if ((name_len >= 1U) && (name[name_len - 1U] == '?')) {
		name[name_len - 1U] = '\0';
		req.form = RAK_AT_FORM_HELP;
	} else {
		eq = strchr(name, '=');
		if (eq != NULL) {
			*eq = '\0';
			req.form = RAK_AT_FORM_SET;
			req.args = eq + 1;
		}
	}

	/* ATE0/ATE1 shorthand. */
	if ((req.form == RAK_AT_FORM_EXEC) && (name[0] == 'E') &&
	    ((name[1] == '0') || (name[1] == '1')) && (name[2] == '\0')) {
		req.form = RAK_AT_FORM_SET;
		req.args = &name[1];
		name[1] = '\0';
	}

	str_to_upper(name);
	req.name = name;

	cmd = find_command(name);
	if (cmd == NULL) {
		rak_at_resp_error(NULL);
		return;
	}

	if (cmd->handler(&req) != 0) {
		LOG_DBG("AT command handled with error: %s", name);
	}
}
