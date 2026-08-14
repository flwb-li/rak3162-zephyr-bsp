/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RAK_AT_PORT_H_
#define RAK_AT_PORT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rak_at_port_rx_handler_t)(const uint8_t *data, size_t len, bool idle_boundary,
					 void *user_data);

/**
 * @brief Optional board hooks for UART low-power wake (e.g. GPIO Sense on RX).
 *
 * Used only when CONFIG_RAK_AT_UART_LP_KEEP_RX=n: lp_enter suspends UART then
 * calls arm_wake(); lp_exit disarms and restores async RX. With KEEP_RX=y,
 * UART RX stays enabled and these hooks are unused.
 */
struct rak_at_port_lp_ops {
	int (*arm_wake)(void);
	void (*disarm_wake)(void);
};

int rak_at_port_init(void);
int rak_at_port_set_rx_handler(rak_at_port_rx_handler_t handler, void *user_data);
void rak_at_port_putc(char c);
void rak_at_port_puts(const char *s);

void rak_at_port_set_lp_ops(const struct rak_at_port_lp_ops *ops);

#if defined(CONFIG_RAK_AT_UART_LP)
void rak_at_port_lp_enter(void);
void rak_at_port_lp_exit(void);
bool rak_at_port_lp_active(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* RAK_AT_PORT_H_ */
