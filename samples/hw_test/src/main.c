#include <zephyr/version.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "at/hw_at.h"
#include "core/hw_app.h"
#include "core/hw_console.h"
#include "core/hw_led.h"
#include "core/hw_xo_cap.h"
#include "storage/hw_storage.h"

LOG_MODULE_REGISTER(hw_main, LOG_LEVEL_INF);

int main(void)
{
	int ret;
	struct hw_runtime_cfg active_cfg;

	ret = hw_console_init();
	if (ret != 0) {
		LOG_ERR("Console init failed: %d", ret);
		return ret;
	}
	LOG_INF("Console init done");

	ret = hw_storage_init();
	if (ret != 0) {
		LOG_ERR("Storage init failed: %d", ret);
	} else {
		LOG_INF("Storage init done");
		hw_storage_get_active_cfg(&active_cfg);
		hw_runtime_apply_stored_caps(&active_cfg);
	}

	hw_at_init();
	LOG_INF("AT framework init done (LoRaWAN OTAA via AT+JOIN)");

	ret = hw_led_start();
	if (ret != 0) {
		LOG_ERR("LED blink start failed: %d", ret);
	}

	ret = hw_app_start();
	if (ret != 0) {
		LOG_ERR("Runtime task start failed: %d", ret);
		return ret;
	}

	LOG_INF("zephyr_version: %s", KERNEL_VERSION_STRING);

	while (1) {
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
