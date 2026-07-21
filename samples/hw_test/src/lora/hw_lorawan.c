#include "lora/hw_lorawan.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "core/hw_console.h"

LOG_MODULE_REGISTER(hw_lorawan, LOG_LEVEL_INF);

static bool lw_started;
static bool lw_joined;
static struct k_mutex lw_lock;
static bool lw_lock_ready;

static void ensure_lock(void)
{
	if (!lw_lock_ready) {
		k_mutex_init(&lw_lock);
		lw_lock_ready = true;
	}
}

static void dl_callback(uint8_t port, uint8_t flags, int16_t rssi, int8_t snr, uint8_t len,
			const uint8_t *hex_data)
{
	char line[160];
	size_t pos = 0U;

	pos += (size_t)snprintk(line + pos, sizeof(line) - pos, "+EVT:RX,PORT=%u,RSSI=%d,SNR=%d,LEN=%u,DATA=",
				port, rssi, snr, len);

	if ((hex_data != NULL) && (len > 0U)) {
		for (uint8_t i = 0U; (i < len) && (pos + 2U < sizeof(line)); i++) {
			pos += (size_t)snprintk(line + pos, sizeof(line) - pos, "%02X", hex_data[i]);
		}
	}

	hw_console_puts(line);
	hw_console_puts("\r\n");
}

static struct lorawan_downlink_cb downlink_cb = {
	.port = LW_RECV_PORT_ANY,
	.cb = dl_callback,
};

int hw_lorawan_ensure_started(void)
{
	const struct device *lora_dev;
	int ret;

	ensure_lock();
	k_mutex_lock(&lw_lock, K_FOREVER);

	if (lw_started) {
		k_mutex_unlock(&lw_lock);
		return 0;
	}

	lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
	if (!device_is_ready(lora_dev)) {
		LOG_ERR("LoRa device not ready");
		k_mutex_unlock(&lw_lock);
		return -ENODEV;
	}

#if defined(CONFIG_LORAMAC_REGION_EU868)
	ret = lorawan_set_region(LORAWAN_REGION_EU868);
	if (ret < 0) {
		LOG_ERR("lorawan_set_region failed: %d", ret);
		k_mutex_unlock(&lw_lock);
		return ret;
	}
#endif

	ret = lorawan_start();
	if (ret < 0) {
		LOG_ERR("lorawan_start failed: %d", ret);
		k_mutex_unlock(&lw_lock);
		return ret;
	}

	lorawan_register_downlink_callback(&downlink_cb);
	lw_started = true;
	k_mutex_unlock(&lw_lock);
	LOG_INF("LoRaWAN stack started");
	return 0;
}

int hw_lorawan_join_otaa(const uint8_t deveui[8], const uint8_t joineui[8], const uint8_t appkey[16],
			 const uint8_t nwkkey[16])
{
	struct lorawan_join_config join_cfg;
	int ret;

	ret = hw_lorawan_ensure_started();
	if (ret != 0) {
		return ret;
	}

	ensure_lock();
	k_mutex_lock(&lw_lock, K_FOREVER);

	memset(&join_cfg, 0, sizeof(join_cfg));
	join_cfg.mode = LORAWAN_ACT_OTAA;
	join_cfg.dev_eui = (uint8_t *)deveui;
	join_cfg.otaa.join_eui = (uint8_t *)joineui;
	join_cfg.otaa.app_key = (uint8_t *)appkey;
	join_cfg.otaa.nwk_key = (uint8_t *)nwkkey;
	join_cfg.otaa.dev_nonce = 0U;

	LOG_INF("Joining network (OTAA)");
	ret = lorawan_join(&join_cfg);
	if (ret < 0) {
		lw_joined = false;
		LOG_ERR("lorawan_join failed: %d", ret);
		k_mutex_unlock(&lw_lock);
		return ret;
	}

	lw_joined = true;
	k_mutex_unlock(&lw_lock);
	LOG_INF("Join succeeded");
	return 0;
}

int hw_lorawan_send(uint8_t port, const uint8_t *data, uint8_t len, enum lorawan_message_type type)
{
	int ret;

	ensure_lock();
	k_mutex_lock(&lw_lock, K_FOREVER);

	if (!lw_joined) {
		k_mutex_unlock(&lw_lock);
		return -ENOTCONN;
	}

	ret = lorawan_send(port, (uint8_t *)data, len, type);
	k_mutex_unlock(&lw_lock);
	return ret;
}

bool hw_lorawan_is_joined(void)
{
	return lw_joined;
}

bool hw_lorawan_is_started(void)
{
	return lw_started;
}

enum lorawan_class hw_lorawan_get_class(void)
{
	return LORAWAN_CLASS_A;
}
