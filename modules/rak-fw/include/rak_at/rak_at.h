/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RAK_AT_H_
#define RAK_AT_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AT command form decoded from input line (RUI3-like syntax).
 *
 * - EXEC: `AT+XXX` (or `ATZ`, `ATE0/1` shorthand)
 * - HELP: `AT+XXX?`
 * - GET:  `AT+XXX=?`
 * - SET:  `AT+XXX=<args>`
 */
enum rak_at_form {
	RAK_AT_FORM_EXEC = 0,
	RAK_AT_FORM_HELP,
	RAK_AT_FORM_GET,
	RAK_AT_FORM_SET,
};

struct rak_at_request {
	const char *name;
	enum rak_at_form form;
	const char *args;
	const char *raw;
};

typedef int (*rak_at_handler_t)(const struct rak_at_request *req);

int rak_at_register_command(const char *name, rak_at_handler_t handler, const char *help);

/**
 * @brief Initialize AT core (parser/registry). Does not register domain commands.
 */
void rak_at_init(void);

/**
 * @brief Start UART RX line assembly worker (must be called after port init).
 */
int rak_at_start(void);

/**
 * @brief Drop partial RX line and queued lines (Sense wake settle).
 */
void rak_at_rx_purge(void);

void rak_at_process_line(char *line);

void rak_at_resp_ok(void);
void rak_at_resp_error(const char *err);
void rak_at_resp_line(const char *fmt, ...);

/** Asynchronous event line (+EVT:...), same transport as responses. */
void rak_at_evt(const char *fmt, ...);

void rak_at_set_echo(bool enabled);
bool rak_at_is_echo_enabled(void);

/** Register standard system AT (SN/VER/RTC/SLEEP), RUI3-style framework set. */
void rak_at_register_system_commands(void);

/** Register system + optional LoRaWAN/P2P AT sets (framework standard surface). */
void rak_at_register_standard_commands(void);

/** Register optional RUI3 LoRaWAN command set (CONFIG_RAK_AT_LORAWAN). */
void rak_at_register_lorawan_commands(void);

/** Register optional RUI3 LoRa P2P command set (CONFIG_RAK_AT_LORA_P2P). */
void rak_at_register_lora_p2p_commands(void);

#ifdef __cplusplus
}
#endif

#endif /* RAK_AT_H_ */
