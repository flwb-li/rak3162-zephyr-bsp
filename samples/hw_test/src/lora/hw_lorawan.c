/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lora/hw_lorawan.h"

#include "core/hw_console.h"
#include "storage/hw_storage.h"

#include <errno.h>
#include <stdarg.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(hw_lorawan, LOG_LEVEL_INF);

#define HW_LW_PAYLOAD_MAX 128
#define HW_LW_RECV_HEX_MAX (HW_LW_PAYLOAD_MAX * 2)

enum {
	HW_LW_JOB_NONE = 0,
	HW_LW_JOB_JOIN,
	HW_LW_JOB_SEND,
};

struct hw_lw_join_job {
	uint8_t deveui[8];
	uint8_t joineui[8];
	uint8_t appkey[16];
	uint8_t nwkkey[16];
	uint8_t interval_s;
	uint8_t attempts; /* 0 = unlimited (RUI3 default) */
};

struct hw_lw_send_job {
	uint8_t port;
	uint8_t len;
	enum lorawan_message_type type;
	uint8_t data[HW_LW_PAYLOAD_MAX];
};

static bool lw_started;
static bool lw_joined;
static bool lw_busy;
static bool lw_joining;
static bool lw_join_stop;
static bool lw_adr = true;
static uint8_t lw_cfm;
static uint8_t lw_cfs;
static struct k_mutex lw_lock;
static bool lw_lock_ready;
static struct k_sem join_stop_sem;

static uint8_t last_rx_port;
static uint8_t last_rx_len;
static uint8_t last_rx_data[HW_LW_PAYLOAD_MAX];
static bool last_rx_valid;

static uint8_t pending_job;
static struct hw_lw_join_job join_job;
static struct hw_lw_send_job send_job;

static void lw_work_handler(struct k_work *work);
/* LoRaMAC OTAA (soft-se crypto + multi-region) needs >4KB on this WQ thread. */
#define HW_LW_WQ_STACK_SIZE 8192

static K_THREAD_STACK_DEFINE(lw_wq_stack, HW_LW_WQ_STACK_SIZE);
static struct k_work_q lw_wq;
static struct k_work lw_work;
static bool lw_wq_started;

static void ensure_lock(void)
{
	if (!lw_lock_ready) {
		k_mutex_init(&lw_lock);
		lw_lock_ready = true;
	}
}

static void ensure_wq(void)
{
	if (lw_wq_started) {
		return;
	}

	k_work_queue_init(&lw_wq);
	k_work_queue_start(&lw_wq, lw_wq_stack, K_THREAD_STACK_SIZEOF(lw_wq_stack), 5, NULL);
	k_thread_name_set(&lw_wq.thread, "hw_lorawan");
	k_work_init(&lw_work, lw_work_handler);
	lw_wq_started = true;
}

static void evt_line(const char *fmt, ...)
{
	char line[192];
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintk(line, sizeof(line), fmt, ap);
	va_end(ap);

	hw_console_puts(line);
	hw_console_puts("\r\n");
}

static void dl_callback(uint8_t port, uint8_t flags, int16_t rssi, int8_t snr, uint8_t len,
			const uint8_t *hex_data)
{
	char line[192];
	size_t pos = 0U;
	uint8_t copy_len;

	ARG_UNUSED(flags);

	copy_len = (len > HW_LW_PAYLOAD_MAX) ? HW_LW_PAYLOAD_MAX : len;
	last_rx_port = port;
	last_rx_len = copy_len;
	if ((hex_data != NULL) && (copy_len > 0U)) {
		memcpy(last_rx_data, hex_data, copy_len);
	}
	last_rx_valid = true;

	/* RUI3 Class A downlink: +EVT:RX_1:<rssi>:<snr>:UNICAST:<port>:<payload> */
	pos += (size_t)snprintk(line + pos, sizeof(line) - pos, "+EVT:RX_1:%d:%d:UNICAST:%u:",
				rssi, snr, port);

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

int hw_lorawan_band_to_region(uint8_t band, enum lorawan_region *region)
{
	if (region == NULL) {
		return -EINVAL;
	}

	switch (band) {
#if defined(CONFIG_LORAMAC_REGION_EU433)
	case HW_BAND_EU433:
		*region = LORAWAN_REGION_EU433;
		return 0;
#endif
#if defined(CONFIG_LORAMAC_REGION_CN470)
	case HW_BAND_CN470:
		*region = LORAWAN_REGION_CN470;
		return 0;
#endif
#if defined(CONFIG_LORAMAC_REGION_RU864)
	case HW_BAND_RU864:
		*region = LORAWAN_REGION_RU864;
		return 0;
#endif
#if defined(CONFIG_LORAMAC_REGION_IN865)
	case HW_BAND_IN865:
		*region = LORAWAN_REGION_IN865;
		return 0;
#endif
#if defined(CONFIG_LORAMAC_REGION_EU868)
	case HW_BAND_EU868:
		*region = LORAWAN_REGION_EU868;
		return 0;
#endif
#if defined(CONFIG_LORAMAC_REGION_US915)
	case HW_BAND_US915:
		*region = LORAWAN_REGION_US915;
		return 0;
#endif
#if defined(CONFIG_LORAMAC_REGION_AU915)
	case HW_BAND_AU915:
		*region = LORAWAN_REGION_AU915;
		return 0;
#endif
#if defined(CONFIG_LORAMAC_REGION_KR920)
	case HW_BAND_KR920:
		*region = LORAWAN_REGION_KR920;
		return 0;
#endif
#if defined(CONFIG_LORAMAC_REGION_AS923)
	case HW_BAND_AS923:
		*region = LORAWAN_REGION_AS923;
		return 0;
#endif
	default:
		return -EINVAL;
	}
}

bool hw_lorawan_band_supported(uint8_t band)
{
	enum lorawan_region region;

	return hw_lorawan_band_to_region(band, &region) == 0;
}

static int apply_region_from_cfg(void)
{
	struct hw_runtime_cfg cfg;
	enum lorawan_region region;
	int ret;

	hw_storage_get_active_cfg(&cfg);
	ret = hw_lorawan_band_to_region(cfg.band, &region);
	if (ret < 0) {
		LOG_ERR("Unsupported BAND=%u in config", cfg.band);
		return ret;
	}

	ret = lorawan_set_region(region);
	if (ret < 0) {
		LOG_ERR("lorawan_set_region failed: %d", ret);
		return ret;
	}

	return 0;
}

void hw_lorawan_init(void)
{
	ensure_lock();
	ensure_wq();
	k_sem_init(&join_stop_sem, 0, 1);
}

int hw_lorawan_ensure_started(void)
{
	const struct device *lora_dev;
	struct hw_runtime_cfg cfg;
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

	ret = apply_region_from_cfg();
	if (ret < 0) {
		k_mutex_unlock(&lw_lock);
		return ret;
	}

	ret = lorawan_start();
	if (ret < 0) {
		LOG_ERR("lorawan_start failed: %d", ret);
		k_mutex_unlock(&lw_lock);
		return ret;
	}

	lorawan_register_downlink_callback(&downlink_cb);

	hw_storage_get_active_cfg(&cfg);
	lw_cfm = cfg.cfm;
	lw_adr = (cfg.adr != 0U);
	lorawan_enable_adr(lw_adr);

	lw_started = true;
	k_mutex_unlock(&lw_lock);
	LOG_INF("LoRaWAN stack started (BAND=%u ADR=%u)", cfg.band, lw_adr ? 1U : 0U);
	return 0;
}

static void emit_join_failed(int err)
{
	ARG_UNUSED(err);
	/* RUI3 AT+JOIN examples: +EVT:JOIN FAILED */
	evt_line("+EVT:JOIN FAILED");
}

static bool join_wait_interval(uint8_t interval_s)
{
	/* Return true if stop was requested during the wait. */
	while (k_sem_take(&join_stop_sem, K_NO_WAIT) == 0) {
		/* drain */
	}

	if (k_sem_take(&join_stop_sem, K_SECONDS(interval_s)) == 0) {
		return true;
	}

	ensure_lock();
	k_mutex_lock(&lw_lock, K_FOREVER);
	bool stop = lw_join_stop;
	k_mutex_unlock(&lw_lock);
	return stop;
}

static void lw_work_handler(struct k_work *work)
{
	int ret;
	uint8_t job;
	struct hw_lw_join_job jcopy;
	struct hw_lw_send_job scopy;

	ARG_UNUSED(work);

	ensure_lock();
	k_mutex_lock(&lw_lock, K_FOREVER);
	job = pending_job;
	if (job == HW_LW_JOB_JOIN) {
		jcopy = join_job;
	} else if (job == HW_LW_JOB_SEND) {
		scopy = send_job;
	}
	pending_job = HW_LW_JOB_NONE;
	k_mutex_unlock(&lw_lock);

	if (job == HW_LW_JOB_JOIN) {
		struct lorawan_join_config join_cfg;
		uint8_t attempt = 0U;
		bool stopped = false;
		int last_err = -EIO;

		ret = hw_lorawan_ensure_started();
		if (ret != 0) {
			emit_join_failed(ret);
			goto done;
		}

		memset(&join_cfg, 0, sizeof(join_cfg));
		join_cfg.mode = LORAWAN_ACT_OTAA;
		join_cfg.dev_eui = jcopy.deveui;
		join_cfg.otaa.join_eui = jcopy.joineui;
		join_cfg.otaa.app_key = jcopy.appkey;
		join_cfg.otaa.nwk_key = jcopy.nwkkey;
		/* With LORAWAN_NVM_SETTINGS, stack owns DevNonce. */
		join_cfg.otaa.dev_nonce = 0U;

		/* RUI3: attempts==0 → retry until success or AT+JOIN=0.
		 * JOIN FAILED is emitted only after attempts are exhausted
		 * (not on every intermediate failure).
		 */
		while (true) {
			ensure_lock();
			k_mutex_lock(&lw_lock, K_FOREVER);
			stopped = lw_join_stop;
			k_mutex_unlock(&lw_lock);
			if (stopped) {
				LOG_INF("Join stopped by AT+JOIN=0");
				break;
			}

			attempt++;
			LOG_INF("Joining network (OTAA) attempt %u%s", attempt,
				(jcopy.attempts == 0U) ? " (unlimited)" : "");

			ret = lorawan_join(&join_cfg);
			last_err = ret;
			LOG_INF("lorawan_join returned %d", ret);

			if (ret == 0) {
				k_mutex_lock(&lw_lock, K_FOREVER);
				lw_joined = true;
				k_mutex_unlock(&lw_lock);
				LOG_INF("Join succeeded");
				evt_line("+EVT:JOINED");
				break;
			}

			LOG_ERR("lorawan_join failed: %d", ret);

			/* Finite attempts exhausted? */
			if ((jcopy.attempts != 0U) && (attempt >= jcopy.attempts)) {
				emit_join_failed(last_err);
				break;
			}

			/* Wait reattempt interval (interruptible by stop). */
			if (join_wait_interval(jcopy.interval_s)) {
				LOG_INF("Join stopped during reattempt wait");
				break;
			}
		}
	} else if (job == HW_LW_JOB_SEND) {
		ret = lorawan_send(scopy.port, scopy.data, scopy.len, scopy.type);
		evt_line("+EVT:TX_DONE");

		if (scopy.type == LORAWAN_MSG_CONFIRMED) {
			if (ret == 0) {
				lw_cfs = 1U;
				evt_line("+EVT:SEND_CONFIRMED_OK");
			} else {
				lw_cfs = 0U;
				evt_line("+EVT:SEND_CONFIRMED_FAILED");
			}
		} else if (ret < 0) {
			LOG_ERR("lorawan_send failed: %d", ret);
		}
	}

done:
	k_mutex_lock(&lw_lock, K_FOREVER);
	lw_busy = false;
	lw_joining = false;
	lw_join_stop = false;
	k_mutex_unlock(&lw_lock);
}

int hw_lorawan_join_otaa_async(const uint8_t deveui[8], const uint8_t joineui[8],
			       const uint8_t appkey[16], const uint8_t nwkkey[16],
			       uint8_t interval_s, uint8_t attempts)
{
	ensure_lock();
	ensure_wq();
	k_mutex_lock(&lw_lock, K_FOREVER);

	if (lw_busy) {
		k_mutex_unlock(&lw_lock);
		return -EBUSY;
	}

	memcpy(join_job.deveui, deveui, sizeof(join_job.deveui));
	memcpy(join_job.joineui, joineui, sizeof(join_job.joineui));
	memcpy(join_job.appkey, appkey, sizeof(join_job.appkey));
	memcpy(join_job.nwkkey, nwkkey, sizeof(join_job.nwkkey));
	join_job.interval_s = (interval_s < 7U) ? 8U : interval_s;
	join_job.attempts = attempts;
	pending_job = HW_LW_JOB_JOIN;
	lw_busy = true;
	lw_joining = true;
	lw_join_stop = false;
	lw_joined = false;
	k_mutex_unlock(&lw_lock);

	/* Clear any stale stop signal. */
	while (k_sem_take(&join_stop_sem, K_NO_WAIT) == 0) {
	}

	k_work_submit_to_queue(&lw_wq, &lw_work);
	return 0;
}

int hw_lorawan_join_stop(void)
{
	ensure_lock();
	k_mutex_lock(&lw_lock, K_FOREVER);

	if (!lw_joining) {
		/* Idle or non-join work: RUI3 still accepts AT+JOIN=0 as OK. */
		k_mutex_unlock(&lw_lock);
		return 0;
	}

	lw_join_stop = true;
	k_mutex_unlock(&lw_lock);
	k_sem_give(&join_stop_sem);
	return 0;
}

void hw_lorawan_autojoin_on_boot(void)
{
	struct hw_runtime_cfg cfg;
	const uint8_t *nwkkey;
	int ret;

	hw_storage_get_active_cfg(&cfg);

	if (cfg.nwm != HW_NWM_LORAWAN) {
		return;
	}
	if (cfg.join_auto == 0U) {
		return;
	}
	if (((cfg.valid_mask & HW_RUNTIME_CFG_VALID_DEVEUI) == 0U) ||
	    ((cfg.valid_mask & HW_RUNTIME_CFG_VALID_APPEUI) == 0U) ||
	    ((cfg.valid_mask & HW_RUNTIME_CFG_VALID_APPKEY) == 0U)) {
		LOG_WRN("Auto-join enabled but keys incomplete; skip");
		return;
	}

	nwkkey = ((cfg.valid_mask & HW_RUNTIME_CFG_VALID_NWKKEY) != 0U) ? cfg.nwkkey
								       : cfg.appkey;

	LOG_INF("Auto-join on boot (interval=%u attempts=%u)", cfg.join_interval_s,
		cfg.join_attempts);
	ret = hw_lorawan_join_otaa_async(cfg.deveui, cfg.appeui, cfg.appkey, nwkkey,
					 cfg.join_interval_s, cfg.join_attempts);
	if (ret != 0) {
		LOG_ERR("Auto-join start failed: %d", ret);
	}
}

int hw_lorawan_send_async(uint8_t port, const uint8_t *data, uint8_t len,
			  enum lorawan_message_type type)
{
	ensure_lock();
	ensure_wq();
	k_mutex_lock(&lw_lock, K_FOREVER);

	if (!lw_joined) {
		k_mutex_unlock(&lw_lock);
		return -ENOTCONN;
	}

	if (lw_busy) {
		k_mutex_unlock(&lw_lock);
		return -EBUSY;
	}

	if ((data == NULL) || (len == 0U) || (len > HW_LW_PAYLOAD_MAX)) {
		k_mutex_unlock(&lw_lock);
		return -EINVAL;
	}

	send_job.port = port;
	send_job.len = len;
	send_job.type = type;
	memcpy(send_job.data, data, len);
	pending_job = HW_LW_JOB_SEND;
	lw_busy = true;
	k_mutex_unlock(&lw_lock);

	k_work_submit_to_queue(&lw_wq, &lw_work);
	return 0;
}

bool hw_lorawan_is_joined(void)
{
	return lw_joined;
}

bool hw_lorawan_is_started(void)
{
	return lw_started;
}

bool hw_lorawan_is_busy(void)
{
	return lw_busy;
}

bool hw_lorawan_is_joining(void)
{
	return lw_joining;
}

enum lorawan_class hw_lorawan_get_class(void)
{
	return LORAWAN_CLASS_A;
}

void hw_lorawan_set_cfm(uint8_t cfm)
{
	lw_cfm = (cfm != 0U) ? 1U : 0U;
}

uint8_t hw_lorawan_get_cfm(void)
{
	return lw_cfm;
}

uint8_t hw_lorawan_get_cfs(void)
{
	return lw_cfs;
}

void hw_lorawan_set_adr(bool enable)
{
	lw_adr = enable;
	if (lw_started) {
		lorawan_enable_adr(enable);
	}
}

bool hw_lorawan_get_adr(void)
{
	return lw_adr;
}

int hw_lorawan_recv_format_and_clear(char *out, size_t out_len)
{
	size_t need;
	size_t pos = 0U;

	if ((out == NULL) || (out_len < 4U)) {
		return -EINVAL;
	}

	ensure_lock();
	k_mutex_lock(&lw_lock, K_FOREVER);

	if (!last_rx_valid || (last_rx_len == 0U)) {
		k_mutex_unlock(&lw_lock);
		(void)snprintk(out, out_len, "0:");
		return 0;
	}

	need = 4U + ((size_t)last_rx_len * 2U);
	if (need >= out_len) {
		k_mutex_unlock(&lw_lock);
		return -ENOMEM;
	}

	pos += (size_t)snprintk(out + pos, out_len - pos, "%u:", last_rx_port);
	for (uint8_t i = 0U; i < last_rx_len; i++) {
		pos += (size_t)snprintk(out + pos, out_len - pos, "%02X", last_rx_data[i]);
	}

	last_rx_valid = false;
	last_rx_len = 0U;
	k_mutex_unlock(&lw_lock);
	return 0;
}
