/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "core/led.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led, LOG_LEVEL_INF);

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "Unsupported board: led0 alias is not defined"
#endif

#if !DT_NODE_HAS_STATUS(LED1_NODE, okay)
#error "Unsupported board: led1 alias is not defined"
#endif

#define LED_PULSE_MS 80U

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

static bool led_ready;

static void leds_off(void)
{
	if (gpio_is_ready_dt(&led0)) {
		(void)gpio_pin_set_dt(&led0, 0);
	}
	if (gpio_is_ready_dt(&led1)) {
		(void)gpio_pin_set_dt(&led1, 0);
	}
}

static void pulse_led(const struct gpio_dt_spec *led)
{
	if (!gpio_is_ready_dt(led)) {
		return;
	}

	(void)gpio_pin_set_dt(led, 1);
	k_msleep(LED_PULSE_MS);
	(void)gpio_pin_set_dt(led, 0);
}

int rak3162_led_start(void)
{
	int ret;

	if (led_ready) {
		return 0;
	}

	if (!gpio_is_ready_dt(&led0) || !gpio_is_ready_dt(&led1)) {
		LOG_ERR("LED GPIO device is not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		LOG_ERR("Failed to configure LED0: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		LOG_ERR("Failed to configure LED1: %d", ret);
		return ret;
	}

	led_ready = true;
	leds_off();
	LOG_INF("Board LEDs ready (event indication)");
	return 0;
}

void rak3162_led_indicate_joined(void)
{
	if (!led_ready) {
		return;
	}

	pulse_led(&led0);
	pulse_led(&led1);
}

void rak3162_led_indicate_tx(void)
{
	if (!led_ready) {
		return;
	}

	pulse_led(&led0);
}

void rak3162_led_prepare_poweroff(void)
{
	if (gpio_is_ready_dt(&led0)) {
		(void)gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
		(void)gpio_pin_set_dt(&led0, 0);
	}

	if (gpio_is_ready_dt(&led1)) {
		(void)gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
		(void)gpio_pin_set_dt(&led1, 0);
	}
}
