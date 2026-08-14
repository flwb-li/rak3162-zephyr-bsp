/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "core/bus_pm.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(bus_pm, LOG_LEVEL_INF);

struct bus_dev {
	const struct device *dev;
	const char *name;
};

static const struct bus_dev buses[] = {
#if DT_NODE_HAS_STATUS(DT_NODELABEL(uart21), okay)
	{ .dev = DEVICE_DT_GET(DT_NODELABEL(uart21)), .name = "uart21" },
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(i2c30), okay)
	{ .dev = DEVICE_DT_GET(DT_NODELABEL(i2c30)), .name = "i2c30" },
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi00), okay)
	{ .dev = DEVICE_DT_GET(DT_NODELABEL(spi00)), .name = "spi00" },
#endif
};

static atomic_t suspended;

static int run_action(const struct device *dev, enum pm_device_action action)
{
	int ret;

	if ((dev == NULL) || !device_is_ready(dev)) {
		return -ENODEV;
	}

	ret = pm_device_action_run(dev, action);
	if ((ret == -EALREADY) || (ret == -ENOTSUP)) {
		return 0;
	}

	return ret;
}

void rak3162_bus_pm_suspend(void)
{
	if (!atomic_cas(&suspended, 0, 1)) {
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(buses); i++) {
		int ret = run_action(buses[i].dev, PM_DEVICE_ACTION_SUSPEND);

		if (ret != 0) {
			LOG_WRN("%s suspend failed: %d", buses[i].name, ret);
		}
	}
}

void rak3162_bus_pm_resume(void)
{
	if (!atomic_cas(&suspended, 1, 0)) {
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(buses); i++) {
		int ret = run_action(buses[i].dev, PM_DEVICE_ACTION_RESUME);

		if (ret != 0) {
			LOG_WRN("%s resume failed: %d", buses[i].name, ret);
		}
	}
}
