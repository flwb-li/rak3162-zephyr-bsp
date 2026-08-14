/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * RAK3162 board hook: arm P1.07 GPIO Sense while AT UART is suspended so a
 * host start-bit can wake System ON idle (RUI3-like LPM behaviour).
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <rak_at/rak_at.h>
#include <rak_at/rak_at_port.h>

LOG_MODULE_REGISTER(board_at_lp, LOG_LEVEL_INF);

/* Sense wake is unused when CONFIG_RAK_AT_UART_LP_KEEP_RX keeps UARTE RX alive. */
#if defined(CONFIG_RAK_AT_UART_LP_KEEP_RX)
#define BOARD_AT_RX_SENSE_ENABLE 0
#else
#define BOARD_AT_RX_SENSE_ENABLE 1
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio1), okay) && BOARD_AT_RX_SENSE_ENABLE
#define BOARD_AT_HAS_RX_SENSE 1
#define BOARD_AT_RX_PIN 7
static const struct gpio_dt_spec at_rx_gpio = {
	.port = DEVICE_DT_GET(DT_NODELABEL(gpio1)),
	.pin = BOARD_AT_RX_PIN,
	/* Keep MCU pull-up so Sense does not float when the host UART is open. */
	.dt_flags = GPIO_ACTIVE_LOW | GPIO_PULL_UP,
};
static struct gpio_callback at_rx_cb;
static struct k_work at_rx_wake_work;
static atomic_t sense_armed;
#else
#define BOARD_AT_HAS_RX_SENSE 0
#endif

#if BOARD_AT_HAS_RX_SENSE
static void rx_sense_disarm(void)
{
	if (!atomic_cas(&sense_armed, 1, 0)) {
		return;
	}
	(void)gpio_pin_interrupt_configure_dt(&at_rx_gpio, GPIO_INT_DISABLE);
}

static int rx_sense_arm(void)
{
	int ret;

	if (!device_is_ready(at_rx_gpio.port)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&at_rx_gpio, GPIO_INPUT);
	if (ret != 0) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&at_rx_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		return ret;
	}

	atomic_set(&sense_armed, 1);
	return 0;
}

static void at_rx_wake_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	rak_at_port_lp_exit();
	/*
	 * GPIO→UART remux and mid-frame host bytes produce framing noise
	 * (often echoed as 'T'). Settle, purge the AT line buffer, then
	 * tell the host it is safe to send a full command.
	 */
	k_msleep(50);
	rak_at_rx_purge();
	rak_at_evt("+EVT:UART_WAKE");
}

static void at_rx_gpio_handler(const struct device *port, struct gpio_callback *cb,
			       gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	rx_sense_disarm();
	(void)k_work_submit(&at_rx_wake_work);
}

static const struct rak_at_port_lp_ops board_lp_ops = {
	.arm_wake = rx_sense_arm,
	.disarm_wake = rx_sense_disarm,
};
#endif

int board_at_lp_init(void)
{
#if BOARD_AT_HAS_RX_SENSE
	int ret;

	if (!gpio_is_ready_dt(&at_rx_gpio)) {
		LOG_ERR("AT RX GPIO not ready");
		return -ENODEV;
	}

	k_work_init(&at_rx_wake_work, at_rx_wake_work_handler);
	gpio_init_callback(&at_rx_cb, at_rx_gpio_handler, BIT(at_rx_gpio.pin));
	ret = gpio_add_callback(at_rx_gpio.port, &at_rx_cb);
	if (ret != 0) {
		return ret;
	}

	rak_at_port_set_lp_ops(&board_lp_ops);
	LOG_INF("Board AT RX Sense hook ready (P1.07)");
#else
	LOG_WRN("Board AT RX Sense DISABLED for idle-current test (P1.07)");
#endif
	return 0;
}
