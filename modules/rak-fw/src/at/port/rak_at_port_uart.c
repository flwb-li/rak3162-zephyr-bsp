/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rak_at/rak_at_port.h>

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/atomic.h>

#define CONSOLE_NODE DT_CHOSEN(zephyr_console)
#define RAK_AT_RX_BUF_SIZE 128
#define RAK_AT_RX_TIMEOUT_US 50000

LOG_MODULE_REGISTER(rak_at_port, LOG_LEVEL_INF);

static const struct device *const console_dev = DEVICE_DT_GET(CONSOLE_NODE);
static atomic_t lp_mode;
static atomic_t rx_running;
static rak_at_port_rx_handler_t rx_handler;
static void *rx_handler_user_data;
static uint8_t rx_buf_a[RAK_AT_RX_BUF_SIZE];
static uint8_t rx_buf_b[RAK_AT_RX_BUF_SIZE];
static bool rx_buf_a_in_use;
static bool rx_buf_b_in_use;
static const struct rak_at_port_lp_ops *lp_ops;

static int console_resume(void)
{
	int ret = pm_device_action_run(console_dev, PM_DEVICE_ACTION_RESUME);

	if ((ret < 0) && (ret != -EALREADY)) {
		return ret;
	}
	return 0;
}

static int console_suspend(void)
{
	int ret = pm_device_action_run(console_dev, PM_DEVICE_ACTION_SUSPEND);

	if ((ret < 0) && (ret != -EALREADY)) {
		return ret;
	}
	return 0;
}

static void lp_disarm(void)
{
	if ((lp_ops != NULL) && (lp_ops->disarm_wake != NULL)) {
		lp_ops->disarm_wake();
	}
}

static int lp_arm(void)
{
	if ((lp_ops != NULL) && (lp_ops->arm_wake != NULL)) {
		return lp_ops->arm_wake();
	}
	return -ENOTSUP;
}

static int start_async_rx(void)
{
	int ret;

	if (atomic_get(&lp_mode) != 0) {
		return 0;
	}
	if (atomic_get(&rx_running) != 0) {
		return 0;
	}

	ret = console_resume();
	if (ret != 0) {
		return ret;
	}

	memset(rx_buf_a, 0, sizeof(rx_buf_a));
	memset(rx_buf_b, 0, sizeof(rx_buf_b));
	rx_buf_a_in_use = true;
	rx_buf_b_in_use = false;

	ret = uart_rx_enable(console_dev, rx_buf_a, sizeof(rx_buf_a), RAK_AT_RX_TIMEOUT_US);
	if (ret != 0) {
		LOG_ERR("uart_rx_enable failed: %d", ret);
		return ret;
	}

	atomic_set(&rx_running, 1);
	return 0;
}

static void stop_async_rx(void)
{
	if (atomic_cas(&rx_running, 1, 0)) {
		(void)uart_rx_disable(console_dev);
	}
}

static void uart_callback(const struct device *dev, struct uart_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	switch (evt->type) {
	case UART_RX_RDY:
		if (rx_handler != NULL) {
			const uint8_t *data = &evt->data.rx.buf[evt->data.rx.offset];

			rx_handler(data, evt->data.rx.len, false, rx_handler_user_data);
		}
		break;
	case UART_RX_BUF_REQUEST:
		if (!rx_buf_a_in_use) {
			rx_buf_a_in_use = true;
			(void)uart_rx_buf_rsp(dev, rx_buf_a, sizeof(rx_buf_a));
		} else if (!rx_buf_b_in_use) {
			rx_buf_b_in_use = true;
			(void)uart_rx_buf_rsp(dev, rx_buf_b, sizeof(rx_buf_b));
		} else {
			LOG_WRN("No free async RX buffer available");
		}
		break;
	case UART_RX_BUF_RELEASED:
		if (evt->data.rx_buf.buf == rx_buf_a) {
			rx_buf_a_in_use = false;
		} else if (evt->data.rx_buf.buf == rx_buf_b) {
			rx_buf_b_in_use = false;
		}
		break;
	case UART_RX_DISABLED:
		atomic_set(&rx_running, 0);
		if (rx_handler != NULL) {
			rx_handler(NULL, 0, true, rx_handler_user_data);
		}
		if (atomic_get(&lp_mode) == 0) {
			(void)start_async_rx();
		}
		break;
	case UART_RX_STOPPED:
		LOG_WRN("UART RX stopped, reason=%d", evt->data.rx_stop.reason);
		atomic_set(&rx_running, 0);
		if (rx_handler != NULL) {
			rx_handler(NULL, 0, true, rx_handler_user_data);
		}
		(void)uart_rx_disable(dev);
		break;
	default:
		break;
	}
}

static void tx_begin_lp(void)
{
	lp_disarm();
	(void)console_resume();
}

static void tx_end_lp(void)
{
	if (atomic_get(&rx_running) != 0) {
		return;
	}
	if (atomic_get(&lp_mode) == 0) {
		return;
	}

	(void)console_suspend();
	(void)lp_arm();
}

void rak_at_port_set_lp_ops(const struct rak_at_port_lp_ops *ops)
{
	lp_ops = ops;
}

int rak_at_port_init(void)
{
	int ret;

	if (!device_is_ready(console_dev)) {
		LOG_ERR("Console device not ready");
		return -ENODEV;
	}

	ret = uart_callback_set(console_dev, uart_callback, NULL);
	if (ret != 0) {
		LOG_ERR("uart_callback_set failed: %d", ret);
		return ret;
	}

	ret = start_async_rx();
	if (ret != 0) {
		return ret;
	}

	LOG_INF("RAK AT UART port ready");
	return 0;
}

int rak_at_port_set_rx_handler(rak_at_port_rx_handler_t handler, void *user_data)
{
	rx_handler = handler;
	rx_handler_user_data = user_data;
	return 0;
}

void rak_at_port_putc(char c)
{
#if defined(CONFIG_RAK_AT_UART_LP)
	bool lp = atomic_get(&lp_mode) != 0;

	if (lp) {
		tx_begin_lp();
	}
	uart_poll_out(console_dev, (unsigned char)c);
	if (lp) {
		tx_end_lp();
	}
#else
	uart_poll_out(console_dev, (unsigned char)c);
#endif
}

void rak_at_port_puts(const char *s)
{
#if defined(CONFIG_RAK_AT_UART_LP)
	bool lp;
#endif

	if (s == NULL) {
		return;
	}

#if defined(CONFIG_RAK_AT_UART_LP)
	lp = atomic_get(&lp_mode) != 0;
	if (lp) {
		tx_begin_lp();
	}
#endif
	while (*s != '\0') {
		uart_poll_out(console_dev, (unsigned char)*s++);
	}
#if defined(CONFIG_RAK_AT_UART_LP)
	if (lp) {
		tx_end_lp();
	}
#endif
}

#if defined(CONFIG_RAK_AT_UART_LP)
void rak_at_port_lp_enter(void)
{
	if (!atomic_cas(&lp_mode, 0, 1)) {
		return;
	}

#if defined(CONFIG_RAK_AT_UART_LP_KEEP_RX)
	/*
	 * Keep async RX enabled. ASYNC_LOW_POWER still gates UARTE clocks
	 * between bytes, and the first host character is not lost.
	 */
	LOG_INF("AT UART LP enter (keep RX)");
	return;
#else
	int ret;

	LOG_INF("AT UART LP enter");
	k_msleep(20);
	stop_async_rx();
	k_msleep(5);
	(void)console_suspend();

	ret = lp_arm();
	if (ret != 0) {
		LOG_WRN("LP wake arm unavailable (%d)", ret);
	}
#endif
}

void rak_at_port_lp_exit(void)
{
	if (!atomic_cas(&lp_mode, 1, 0)) {
#if !defined(CONFIG_RAK_AT_UART_LP_KEEP_RX)
		lp_disarm();
#endif
		return;
	}

#if defined(CONFIG_RAK_AT_UART_LP_KEEP_RX)
	LOG_INF("AT UART LP exit (keep RX)");
#else
	lp_disarm();
	(void)console_resume();
	/* Brief gap before enabling RX reduces remux framing junk. */
	k_msleep(5);
	(void)start_async_rx();
	LOG_INF("AT UART LP exit");
#endif
}

bool rak_at_port_lp_active(void)
{
	return atomic_get(&lp_mode) != 0;
}
#endif
