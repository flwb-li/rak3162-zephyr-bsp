#include "core/hw_led.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hw_led, LOG_LEVEL_INF);

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)

#if !DT_NODE_HAS_STATUS(LED0_NODE, okay)
#error "Unsupported board: led0 alias is not defined"
#endif

#if !DT_NODE_HAS_STATUS(LED1_NODE, okay)
#error "Unsupported board: led1 alias is not defined"
#endif

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

K_THREAD_STACK_DEFINE(led_blink_stack, 512);

static struct k_thread led_blink_thread;
static bool led_started;

static void led_blink_task(void *arg1, void *arg2, void *arg3)
{
	bool led0_on = true;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		(void)gpio_pin_set_dt(&led0, led0_on ? 1 : 0);
		(void)gpio_pin_set_dt(&led1, led0_on ? 0 : 1);
		led0_on = !led0_on;
		k_sleep(K_SECONDS(1));
	}
}

int hw_led_start(void)
{
	int ret;

	if (led_started) {
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

	k_thread_create(&led_blink_thread, led_blink_stack, K_THREAD_STACK_SIZEOF(led_blink_stack), led_blink_task, NULL,
			NULL, NULL, 7, 0, K_NO_WAIT);

	led_started = true;
	LOG_INF("Board LED blink started");
	return 0;
}

void hw_led_prepare_poweroff(void)
{
	if (led_started) {
		k_thread_suspend(&led_blink_thread);
	}

	if (gpio_is_ready_dt(&led0)) {
		(void)gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
		(void)gpio_pin_set_dt(&led0, 0);
	}

	if (gpio_is_ready_dt(&led1)) {
		(void)gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
		(void)gpio_pin_set_dt(&led1, 0);
	}
}
