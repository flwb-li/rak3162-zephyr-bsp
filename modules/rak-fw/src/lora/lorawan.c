/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rak_fw/lorawan.h>
#include <rak_fw/board.h>


#include <errno.h>
#include <stdarg.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#if defined(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif

#include <LoRaMac.h>
#include <LoRaMacCrypto.h>
#include <sx126x/sx126x.h>
#include <sx126x-board.h>

LOG_MODULE_REGISTER(rak_fw_lw, LOG_LEVEL_INF);

#define LW_PAYLOAD_MAX 128
#define LW_RECV_HEX_MAX (LW_PAYLOAD_MAX * 2)

enum {
	LW_JOB_NONE = 0,
	LW_JOB_JOIN,
	LW_JOB_SEND,
};

struct lw_join_job {
	uint8_t deveui[8];
	uint8_t joineui[8];
	uint8_t appkey[16];
	uint8_t nwkkey[16];
	uint8_t interval_s;
	uint8_t attempts; /* 0 = unlimited (RUI3 default) */
};

struct lw_send_job {
	uint8_t port;
	uint8_t len;
	enum lorawan_message_type type;
	uint8_t data[LW_PAYLOAD_MAX];
};

static bool lw_started;
static bool lw_joined;
static bool lw_busy;
static bool radio_cold_sleeping;
static rak_fw_lorawan_event_handler_t event_handler;
static rak_fw_lorawan_rf_state_handler_t rf_state_handler;
static rak_fw_lorawan_join_status_cb_t join_status_cb;
static rak_fw_lorawan_send_done_cb_t send_done_cb;

void rak_fw_lorawan_set_handlers(rak_fw_lorawan_event_handler_t new_event_handler,
			     rak_fw_lorawan_rf_state_handler_t new_rf_state_handler)
{
	event_handler = new_event_handler;
	rf_state_handler = new_rf_state_handler;
}

void rak_fw_lorawan_set_join_status_cb(rak_fw_lorawan_join_status_cb_t cb)
{
	join_status_cb = cb;
}

void rak_fw_lorawan_set_send_done_cb(rak_fw_lorawan_send_done_cb_t cb)
{
	send_done_cb = cb;
}

static void notify_join_status(bool joined)
{
	if (join_status_cb != NULL) {
		join_status_cb(joined);
	}
}

static void notify_send_done(int result)
{
	if (send_done_cb != NULL) {
		send_done_cb(result);
	}
}

static void rf_front_enable(void)
{
	const struct rak_fw_board_ops *bops = rak_fw_board_ops();

	if (rf_state_handler != NULL) {
		rf_state_handler(true);
	}

	/* Wake secondary buses / ANT_SW for the whole Join/TX/RX window. */
	if ((bops != NULL) && (bops->rf_window_enter != NULL)) {
		bops->rf_window_enter();
	}

	if (lw_started) {
		/*
		 * SetSleep() and LoRaMacDeInitialization() disable DIO1. SPI CS
		 * wakes the transceiver; SX126xWakeup() re-enables DIO1 so TX/RX
		 * done IRQs can complete lorawan_join() / lorawan_send().
		 */
		SX126xWakeup();
		radio_cold_sleeping = false;
	}
}

static void rf_front_disable(void)
{
	const struct rak_fw_board_ops *bops = rak_fw_board_ops();

	if ((bops != NULL) && (bops->rf_window_exit != NULL)) {
		bops->rf_window_exit();
	}
	if (lw_joined && (rf_state_handler != NULL)) {
		rf_state_handler(false);
	}
}

static int cfg_get_active(struct rak_at_runtime_cfg *cfg)
{
	const struct rak_fw_cfg_ops *cops = rak_fw_cfg_ops();

	if ((cops == NULL) || (cops->get_active == NULL) || (cfg == NULL)) {
		return -ENODEV;
	}
	cops->get_active(cfg);
	return 0;
}

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
static uint8_t last_rx_data[LW_PAYLOAD_MAX];
static bool last_rx_valid;

static uint8_t pending_job;
static struct lw_join_job join_job;
static struct lw_send_job send_job;

static void lw_work_handler(struct k_work *work);
static int queue_send_locked(uint8_t port, const uint8_t *data, uint8_t len,
			     enum lorawan_message_type type);

/* LoRaMAC OTAA (soft-se crypto + multi-region) needs ample WQ stack. */
#define LW_WQ_STACK_SIZE 12288

static K_THREAD_STACK_DEFINE(lw_wq_stack, LW_WQ_STACK_SIZE);
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
	k_thread_name_set(&lw_wq.thread, "lorawan");
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

	if (event_handler != NULL) {
		event_handler(line);
	}
}

static void dl_callback(uint8_t port, uint8_t flags, int16_t rssi, int8_t snr, uint8_t len,
			const uint8_t *hex_data)
{
	char line[192];
	size_t pos = 0U;
	uint8_t copy_len;

	ARG_UNUSED(flags);

	copy_len = (len > LW_PAYLOAD_MAX) ? LW_PAYLOAD_MAX : len;
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

	if (event_handler != NULL) {
		event_handler(line);
	}
}

static struct lorawan_downlink_cb downlink_cb = {
	.port = LW_RECV_PORT_ANY,
	.cb = dl_callback,
};

int rak_fw_lorawan_band_to_region(uint8_t band, enum lorawan_region *region)
{
	if (region == NULL) {
		return -EINVAL;
	}

	/* RAK3162 SX1262 is HF-only. */
	if ((band == RAK_AT_BAND_EU433) || (band == RAK_AT_BAND_CN470)) {
		return -EINVAL;
	}

	switch (band) {
#if defined(CONFIG_LORAMAC_REGION_RU864)
	case RAK_AT_BAND_RU864:
		*region = LORAWAN_REGION_RU864;
		return 0;
#endif
#if defined(CONFIG_LORAMAC_REGION_IN865)
	case RAK_AT_BAND_IN865:
		*region = LORAWAN_REGION_IN865;
		return 0;
#endif
#if defined(CONFIG_LORAMAC_REGION_EU868)
	case RAK_AT_BAND_EU868:
		*region = LORAWAN_REGION_EU868;
		return 0;
#endif
#if defined(CONFIG_LORAMAC_REGION_US915)
	case RAK_AT_BAND_US915:
		*region = LORAWAN_REGION_US915;
		return 0;
#endif
#if defined(CONFIG_LORAMAC_REGION_AU915)
	case RAK_AT_BAND_AU915:
		*region = LORAWAN_REGION_AU915;
		return 0;
#endif
#if defined(CONFIG_LORAMAC_REGION_KR920)
	case RAK_AT_BAND_KR920:
		*region = LORAWAN_REGION_KR920;
		return 0;
#endif
#if defined(CONFIG_LORAMAC_REGION_AS923)
	case RAK_AT_BAND_AS923:
		*region = LORAWAN_REGION_AS923;
		return 0;
#endif
	default:
		return -EINVAL;
	}
}

bool rak_fw_lorawan_band_supported(uint8_t band)
{
	enum lorawan_region region;

	return rak_fw_lorawan_band_to_region(band, &region) == 0;
}

#define LW_CHMASK_WORDS 6

bool rak_fw_lorawan_mask_supported(uint8_t band)
{
	return (band == RAK_AT_BAND_US915) || (band == RAK_AT_BAND_AU915);
}

uint16_t rak_fw_lorawan_mask_default(uint8_t band)
{
	if ((band == RAK_AT_BAND_US915) || (band == RAK_AT_BAND_AU915)) {
		return 0x01FFU;
	}
	return 0U;
}

static uint16_t rui_mask_max_for_band(uint8_t band)
{
	return rak_fw_lorawan_mask_default(band);
}

/* RUI3 AT+MASK: each bit is an 8-channel group. 0 means regional default. */
static int rui_mask_to_mac(uint8_t band, uint16_t rui, uint16_t out[LW_CHMASK_WORDS])
{
	uint16_t max = rui_mask_max_for_band(band);
	bool hybrid_500;
	uint8_t g;

	if (max == 0U) {
		return -ENOTSUP;
	}

	memset(out, 0, sizeof(uint16_t) * LW_CHMASK_WORDS);
	if (rui == 0U) {
		rui = rak_fw_lorawan_mask_default(band);
	}
	rui &= max;
	hybrid_500 = (band == RAK_AT_BAND_US915) || (band == RAK_AT_BAND_AU915);

	for (g = 0U; g < 12U; g++) {
		if ((rui & (uint16_t)BIT(g)) == 0U) {
			continue;
		}
		/* US915/AU915 bit 8: all eight 500 kHz uplink channels. */
		if (hybrid_500 && (g == 8U)) {
			out[4] |= 0x00FFU;
			continue;
		}
		out[g / 2U] |= ((g & 1U) != 0U) ? 0xFF00U : 0x00FFU;
		if (hybrid_500 && (g < 8U)) {
			out[4] |= (uint16_t)BIT(g);
		}
	}

	return 0;
}

static int apply_chmask_locked(uint8_t band, uint16_t rui)
{
	uint16_t mac_mask[LW_CHMASK_WORDS];
	MibRequestConfirm_t mib;
	int ret;

	if (!rak_fw_lorawan_mask_supported(band)) {
		return 0;
	}

	ret = rui_mask_to_mac(band, rui, mac_mask);
	if (ret != 0) {
		return ret;
	}

	mib.Type = MIB_CHANNELS_DEFAULT_MASK;
	mib.Param.ChannelsDefaultMask = mac_mask;
	if (LoRaMacMibSetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
		return -EINVAL;
	}

	ret = lorawan_set_channels_mask(mac_mask, LORAWAN_CHANNELS_MASK_SIZE_US915);
	if (ret != 0) {
		return ret;
	}

#if defined(REGION_US915) || defined(REGION_AU915)
	mib.Type = MIB_NVM_CTXS;
	if ((LoRaMacMibGetRequestConfirm(&mib) == LORAMAC_STATUS_OK) &&
	    (mib.Param.Contexts != NULL)) {
		memcpy(mib.Param.Contexts->RegionGroup1.ChannelsMaskRemaining,
		       mib.Param.Contexts->RegionGroup2.ChannelsMask,
		       sizeof(uint16_t) * LW_CHMASK_WORDS);
	}
#endif

	LOG_INF("Applied AT+MASK=0x%04X (BAND=%u)", rui, band);
	return 0;
}

int rak_fw_lorawan_apply_chmask(uint16_t rui_mask)
{
	struct rak_at_runtime_cfg cfg;
	uint16_t max;
	int ret;

	ensure_lock();
	k_mutex_lock(&lw_lock, K_FOREVER);

	if (lw_busy || lw_joining) {
		k_mutex_unlock(&lw_lock);
		return -EBUSY;
	}

	ret = cfg_get_active(&cfg);
	if (ret != 0) {
		k_mutex_unlock(&lw_lock);
		return ret;
	}
	if (!rak_fw_lorawan_mask_supported(cfg.band)) {
		k_mutex_unlock(&lw_lock);
		return -ENOTSUP;
	}

	max = rui_mask_max_for_band(cfg.band);
	if ((rui_mask != 0U) && ((rui_mask & (uint16_t)~max) != 0U)) {
		k_mutex_unlock(&lw_lock);
		return -EINVAL;
	}

	if (!lw_started) {
		k_mutex_unlock(&lw_lock);
		return 0;
	}

	ret = apply_chmask_locked(cfg.band, rui_mask);
	k_mutex_unlock(&lw_lock);
	return ret;
}

#define LW_NVM_KEY_MAC_GROUP1    "lorawan/nvm/MacGroup1"
#define LW_NVM_KEY_MAC_GROUP2    "lorawan/nvm/MacGroup2"
#define LW_NVM_KEY_REGION_GROUP1 "lorawan/nvm/RegionGroup1"
#define LW_NVM_KEY_REGION_GROUP2 "lorawan/nvm/RegionGroup2"
#define LW_NVM_KEY_CLASS_B       "lorawan/nvm/ClassB"

static LoRaMacRegion_t lorawan_region_to_loramac(enum lorawan_region region)
{
	switch (region) {
	case LORAWAN_REGION_AS923:
		return LORAMAC_REGION_AS923;
	case LORAWAN_REGION_AU915:
		return LORAMAC_REGION_AU915;
	case LORAWAN_REGION_CN470:
		return LORAMAC_REGION_CN470;
	case LORAWAN_REGION_CN779:
		return LORAMAC_REGION_CN779;
	case LORAWAN_REGION_EU433:
		return LORAMAC_REGION_EU433;
	case LORAWAN_REGION_EU868:
		return LORAMAC_REGION_EU868;
	case LORAWAN_REGION_KR920:
		return LORAMAC_REGION_KR920;
	case LORAWAN_REGION_IN865:
		return LORAMAC_REGION_IN865;
	case LORAWAN_REGION_US915:
		return LORAMAC_REGION_US915;
	case LORAWAN_REGION_RU864:
		return LORAMAC_REGION_RU864;
	default:
		return LORAMAC_REGION_EU868;
	}
}

/*
 * Zephyr lorawan_start() restores MacGroup2 (including Region) after
 * LoRaMacInitialization(). Keep Crypto/DevNonce; drop region-dependent keys
 * so a previous EU868 (etc.) blob cannot rewind the newly selected region.
 */
static void lorawan_nvm_drop_region_ctx(void)
{
	if (!IS_ENABLED(CONFIG_LORAWAN_NVM_SETTINGS) || !IS_ENABLED(CONFIG_SETTINGS)) {
		return;
	}

#if defined(CONFIG_SETTINGS)
	(void)settings_subsys_init();
	(void)settings_delete(LW_NVM_KEY_MAC_GROUP1);
	(void)settings_delete(LW_NVM_KEY_MAC_GROUP2);
	(void)settings_delete(LW_NVM_KEY_REGION_GROUP1);
	(void)settings_delete(LW_NVM_KEY_REGION_GROUP2);
	(void)settings_delete(LW_NVM_KEY_CLASS_B);
#endif
}

static void lorawan_nvm_discard_if_region_mismatch(enum lorawan_region want)
{
#if defined(CONFIG_SETTINGS)
	LoRaMacNvmDataGroup2_t mac2;
	ssize_t n;

	if (!IS_ENABLED(CONFIG_LORAWAN_NVM_SETTINGS)) {
		return;
	}

	(void)settings_subsys_init();
	n = settings_load_one(LW_NVM_KEY_MAC_GROUP2, &mac2, sizeof(mac2));
	if (n < 0) {
		return;
	}
	if ((n != (ssize_t)sizeof(mac2)) ||
	    (mac2.Region != lorawan_region_to_loramac(want))) {
		LOG_INF("Dropping LoRaWAN region NVM (stored region %d, BAND region %d, size %d)",
			(n == (ssize_t)sizeof(mac2)) ? (int)mac2.Region : -1, (int)want,
			(int)n);
		lorawan_nvm_drop_region_ctx();
	}
#else
	ARG_UNUSED(want);
#endif
}

static bool lorawan_nvm_region_is(enum lorawan_region want)
{
	MibRequestConfirm_t mib = {
		.Type = MIB_NVM_CTXS,
	};

	if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
		return false;
	}
	if ((mib.Param.Contexts == NULL)) {
		return false;
	}

	return mib.Param.Contexts->MacGroup2.Region == lorawan_region_to_loramac(want);
}

static int lorawan_start_for_region(enum lorawan_region region)
{
	int ret;

	lorawan_nvm_discard_if_region_mismatch(region);

	ret = lorawan_start();
	if (ret != 0) {
		return ret;
	}

	if (lorawan_nvm_region_is(region)) {
		return 0;
	}

	LOG_WRN("NVM restore overwrote LoRaWAN region; retry without region ctx");
	lorawan_nvm_drop_region_ctx();
	if (LoRaMacDeInitialization() != LORAMAC_STATUS_OK) {
		return -EBUSY;
	}

	return lorawan_start();
}

static int apply_region_from_cfg(enum lorawan_region *out_region)
{
	struct rak_at_runtime_cfg cfg;
	enum lorawan_region region;
	int ret;

	ret = cfg_get_active(&cfg);
	if (ret < 0) {
		return ret;
	}
	ret = rak_fw_lorawan_band_to_region(cfg.band, &region);
	if (ret < 0) {
		LOG_ERR("Unsupported BAND=%u in config", cfg.band);
		return ret;
	}

	ret = lorawan_set_region(region);
	if (ret < 0) {
		LOG_ERR("lorawan_set_region failed: %d", ret);
		return ret;
	}

	if (out_region != NULL) {
		*out_region = region;
	}

	return 0;
}

static bool lorawan_session_is_active(void)
{
	MibRequestConfirm_t mib = {
		.Type = MIB_NETWORK_ACTIVATION,
	};

	if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
		return false;
	}

	return mib.Param.NetworkActivation != ACTIVATION_TYPE_NONE;
}

static int lorawan_clear_session(void)
{
	MibRequestConfirm_t mib = {
		.Type = MIB_NETWORK_ACTIVATION,
		.Param.NetworkActivation = ACTIVATION_TYPE_NONE,
	};
	LoRaMacStatus_t status;

	status = LoRaMacMibSetRequestConfirm(&mib);
	if (status == LORAMAC_STATUS_BUSY) {
		return -EBUSY;
	}
	if (status != LORAMAC_STATUS_OK) {
		return -EIO;
	}

	lw_joined = false;
	return 0;
}

int rak_fw_lorawan_apply_band(uint8_t band)
{
	enum lorawan_region region;
	int ret;
	bool dropped_session = false;

	ret = rak_fw_lorawan_band_to_region(band, &region);
	if (ret != 0) {
		return ret;
	}

	ensure_lock();
	k_mutex_lock(&lw_lock, K_FOREVER);

	if (lw_busy || lw_joining) {
		k_mutex_unlock(&lw_lock);
		return -EBUSY;
	}

	if (lw_joined || (lw_started && lorawan_session_is_active())) {
		ret = lorawan_clear_session();
		if (ret != 0) {
			k_mutex_unlock(&lw_lock);
			return ret;
		}
		dropped_session = true;
	}

	ret = lorawan_set_region(region);
	if (ret != 0) {
		k_mutex_unlock(&lw_lock);
		return ret;
	}

	if (!lw_started) {
		k_mutex_unlock(&lw_lock);
		if (dropped_session) {
			notify_join_status(false);
		}
		return 0;
	}

	if (LoRaMacDeInitialization() != LORAMAC_STATUS_OK) {
		k_mutex_unlock(&lw_lock);
		return -EBUSY;
	}

	/* Deinit leaves the radio asleep (DIO1 off); force wake on next RF window. */
	radio_cold_sleeping = true;

	ret = lorawan_start_for_region(region);
	if (ret != 0) {
		lw_started = false;
		k_mutex_unlock(&lw_lock);
		LOG_ERR("lorawan_start after BAND change failed: %d", ret);
		if (dropped_session) {
			notify_join_status(false);
		}
		return ret;
	}

	if (lorawan_session_is_active()) {
		ret = lorawan_clear_session();
		if (ret != 0) {
			k_mutex_unlock(&lw_lock);
			return ret;
		}
		dropped_session = true;
	}

	{
		struct rak_at_runtime_cfg cfg;

		if (cfg_get_active(&cfg) == 0) {
			(void)apply_chmask_locked(band, cfg.chmask);
		}
	}

	k_mutex_unlock(&lw_lock);
	if (dropped_session) {
		notify_join_status(false);
	}
	LOG_INF("LoRaWAN region applied (BAND=%u)", band);
	return 0;
}

void rak_fw_lorawan_init(void)
{
	ensure_lock();
	ensure_wq();
	k_sem_init(&join_stop_sem, 0, 1);
	/* RF front-end starts off; enabled only for Join/TX/RX windows. */
	rf_front_disable();
}

int rak_fw_lorawan_ensure_started(void)
{
	const struct device *lora_dev;
	struct rak_at_runtime_cfg cfg;
	enum lorawan_region region;
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

	ret = apply_region_from_cfg(&region);
	if (ret < 0) {
		k_mutex_unlock(&lw_lock);
		return ret;
	}

	ret = lorawan_start_for_region(region);
	if (ret < 0) {
		LOG_ERR("lorawan_start failed: %d", ret);
		k_mutex_unlock(&lw_lock);
		return ret;
	}

	lorawan_register_downlink_callback(&downlink_cb);

	ret = cfg_get_active(&cfg);
	if (ret < 0) {
		k_mutex_unlock(&lw_lock);
		return ret;
	}
	lw_cfm = cfg.cfm;
	lw_adr = (cfg.adr != 0U);
	lorawan_enable_adr(lw_adr);

	(void)apply_chmask_locked(cfg.band, cfg.chmask);

	/*
	 * Keep LoRaMAC NVM enabled for monotonic DevNonce storage, but do not
	 * restore an OTAA data session across reset (RUI3-style behaviour).
	 */
	if (lorawan_session_is_active()) {
		ret = lorawan_clear_session();
		if (ret != 0) {
			k_mutex_unlock(&lw_lock);
			LOG_ERR("Failed to clear restored LoRaWAN session: %d", ret);
			return ret;
		}
		LOG_INF("Cleared restored LoRaWAN session; OTAA required");
	}
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

static int queue_send_locked(uint8_t port, const uint8_t *data, uint8_t len,
			     enum lorawan_message_type type)
{
	if (!lw_joined) {
		return -ENOTCONN;
	}

	if (lw_busy) {
		return -EBUSY;
	}

	if ((data == NULL) || (len == 0U) || (len > LW_PAYLOAD_MAX)) {
		return -EINVAL;
	}

	send_job.port = port;
	send_job.len = len;
	send_job.type = type;
	memcpy(send_job.data, data, len);
	pending_job = LW_JOB_SEND;
	lw_busy = true;
	return 0;
}


static void lw_work_handler(struct k_work *work)
{
	int ret = 0;
	int send_result = 0;
	uint8_t job;
	struct lw_join_job jcopy;
	struct lw_send_job scopy;

	ARG_UNUSED(work);

	ensure_lock();
	k_mutex_lock(&lw_lock, K_FOREVER);
	job = pending_job;
	if (job == LW_JOB_JOIN) {
		jcopy = join_job;
	} else if (job == LW_JOB_SEND) {
		scopy = send_job;
	}
	pending_job = LW_JOB_NONE;
	k_mutex_unlock(&lw_lock);

	if (job == LW_JOB_JOIN) {
		struct lorawan_join_config join_cfg;
		uint8_t attempt = 0U;
		bool stopped = false;
		int last_err = -EIO;

		rf_front_enable();
		ret = rak_fw_lorawan_ensure_started();
		if (ret != 0) {
			rf_front_disable();
			emit_join_failed(ret);
			notify_join_status(false);
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

			rf_front_enable();
			{
				const struct rak_fw_board_ops *bops = rak_fw_board_ops();

				if ((bops != NULL) && (bops->indicate_tx != NULL)) {
					bops->indicate_tx();
				}
			}
			ret = lorawan_join(&join_cfg);
			rf_front_disable();
			last_err = ret;
			LOG_INF("lorawan_join returned %d", ret);

			/* AT+JOIN=0 during OTAA cannot preempt lorawan_join(); honor stop
			 * after return so a late air success does not keep the session.
			 */
			k_mutex_lock(&lw_lock, K_FOREVER);
			stopped = lw_join_stop;
			k_mutex_unlock(&lw_lock);
			if (stopped) {
				if (ret == 0) {
					(void)lorawan_clear_session();
					LOG_INF("Join success discarded after AT+JOIN=0");
				} else {
					LOG_INF("Join stopped by AT+JOIN=0");
				}
				break;
			}

			if (ret == 0) {
				k_mutex_lock(&lw_lock, K_FOREVER);
				lw_joined = true;
				k_mutex_unlock(&lw_lock);
				LOG_INF("Join succeeded");
				evt_line("+EVT:JOINED");
				{
					const struct rak_fw_board_ops *bops = rak_fw_board_ops();

					if ((bops != NULL) && (bops->indicate_joined != NULL)) {
						bops->indicate_joined();
					}
				}
				/* rf_front_disable() ran before lw_joined; arm AT Sense now. */
				if (rf_state_handler != NULL) {
					rf_state_handler(false);
				}
				notify_join_status(true);
				break;
			}

			LOG_ERR("lorawan_join failed: %d", ret);

			/* Finite attempts exhausted? */
			if ((jcopy.attempts != 0U) && (attempt >= jcopy.attempts)) {
				emit_join_failed(last_err);
				notify_join_status(false);
				break;
			}

			/* Wait reattempt interval (interruptible by stop). */
			if (join_wait_interval(jcopy.interval_s)) {
				LOG_INF("Join stopped during reattempt wait");
				break;
			}
		}
	} else if (job == LW_JOB_SEND) {
		rf_front_enable();
		ret = lorawan_send(scopy.port, scopy.data, scopy.len, scopy.type);
		send_result = ret;

		/* Print +EVT while UART is still fully muxed (rf_front_enable
		 * did lp_exit). Suspending UART first garbles TX_DONE.
		 */
		if (ret == 0) {
			evt_line("+EVT:TX_DONE");
			{
				const struct rak_fw_board_ops *bops = rak_fw_board_ops();

				if ((bops != NULL) && (bops->indicate_tx != NULL)) {
					bops->indicate_tx();
				}
			}

			if (scopy.type == LORAWAN_MSG_CONFIRMED) {
				lw_cfs = 1U;
				evt_line("+EVT:SEND_CONFIRMED_OK");
			}
		} else if (ret == -EAGAIN) {
			/* Duty-cycle / no legal channel: do not fake TX_DONE. */
			LOG_WRN("lorawan_send delayed by duty cycle (-EAGAIN)");
		} else {
			LOG_ERR("lorawan_send failed: %d", ret);
			if (scopy.type == LORAWAN_MSG_CONFIRMED) {
				lw_cfs = 0U;
				evt_line("+EVT:SEND_CONFIRMED_FAILED");
			}
		}

		rf_front_disable();
	}

done:
	k_mutex_lock(&lw_lock, K_FOREVER);
	lw_busy = false;
	lw_joining = false;
	lw_join_stop = false;
	k_mutex_unlock(&lw_lock);

	if (job == LW_JOB_SEND) {
		notify_send_done(send_result);
	}
}

int rak_fw_lorawan_join_otaa_async(const uint8_t deveui[8], const uint8_t joineui[8],
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

	/* AT+JOIN=1 explicitly starts a new OTAA session, even if already joined. */
	if (lw_started && lw_joined) {
		int ret = lorawan_clear_session();

		if (ret != 0) {
			k_mutex_unlock(&lw_lock);
			return ret;
		}
		LOG_INF("Active LoRaWAN session cleared for OTAA rejoin");
	}

	memcpy(join_job.deveui, deveui, sizeof(join_job.deveui));
	memcpy(join_job.joineui, joineui, sizeof(join_job.joineui));
	memcpy(join_job.appkey, appkey, sizeof(join_job.appkey));
	memcpy(join_job.nwkkey, nwkkey, sizeof(join_job.nwkkey));
	join_job.interval_s = (interval_s < 7U) ? 8U : interval_s;
	join_job.attempts = attempts;
	pending_job = LW_JOB_JOIN;
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

int rak_fw_lorawan_join_stop(void)
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

int rak_fw_lorawan_stop(void)
{
	int ret = 0;

	ensure_lock();
	k_mutex_lock(&lw_lock, K_FOREVER);

	if (lw_busy || lw_joining) {
		k_mutex_unlock(&lw_lock);
		return -EBUSY;
	}

	if (lw_started) {
		if (lw_joined || lorawan_session_is_active()) {
			ret = lorawan_clear_session();
			if (ret != 0) {
				k_mutex_unlock(&lw_lock);
				return ret;
			}
		}

		if (LoRaMacDeInitialization() != LORAMAC_STATUS_OK) {
			k_mutex_unlock(&lw_lock);
			return -EBUSY;
		}

		radio_cold_sleeping = true;
		lw_started = false;
	}

	lw_joined = false;
	k_mutex_unlock(&lw_lock);

	notify_join_status(false);
	LOG_INF("LoRaWAN stack stopped");
	return 0;
}




int rak_fw_lorawan_send_async(uint8_t port, const uint8_t *data, uint8_t len,
			  enum lorawan_message_type type)
{
	int ret;

	ensure_lock();
	ensure_wq();
	k_mutex_lock(&lw_lock, K_FOREVER);
	ret = queue_send_locked(port, data, len, type);
	k_mutex_unlock(&lw_lock);

	if (ret != 0) {
		return ret;
	}

	k_work_submit_to_queue(&lw_wq, &lw_work);
	return 0;
}

int rak_fw_lorawan_get_uplink_counter(uint32_t *counter)
{
	LoRaMacCryptoStatus_t status;

	if (counter == NULL) {
		return -EINVAL;
	}

	ensure_lock();
	k_mutex_lock(&lw_lock, K_FOREVER);
	if (!lw_started || !lw_joined || lw_busy) {
		k_mutex_unlock(&lw_lock);
		return -EAGAIN;
	}

	status = LoRaMacCryptoGetFCntUp(counter);
	k_mutex_unlock(&lw_lock);

	return (status == LORAMAC_CRYPTO_SUCCESS) ? 0 : -EIO;
}

void rak_fw_lorawan_radio_cold_sleep(void)
{
	SleepParams_t sleep_params = { 0 };

	if (radio_cold_sleeping) {
		return;
	}

	/* Retain SX1262 configuration across System ON idle intervals. */
	sleep_params.Fields.WarmStart = 1;
	SX126xSetSleep(sleep_params);
	/* NSS must stay high after SetSleep; SPI CS release + settle. */
	k_msleep(2);
	radio_cold_sleeping = true;
	LOG_INF("SX1262 warm sleep (WarmStart=1)");
}

bool rak_fw_lorawan_is_joined(void)
{
	return lw_joined;
}

bool rak_fw_lorawan_is_started(void)
{
	return lw_started;
}

bool rak_fw_lorawan_is_busy(void)
{
	return lw_busy;
}

bool rak_fw_lorawan_is_joining(void)
{
	return lw_joining;
}

enum lorawan_class rak_fw_lorawan_get_class(void)
{
	return LORAWAN_CLASS_A;
}

void rak_fw_lorawan_set_cfm(uint8_t cfm)
{
	lw_cfm = (cfm != 0U) ? 1U : 0U;
}

uint8_t rak_fw_lorawan_get_cfm(void)
{
	return lw_cfm;
}

uint8_t rak_fw_lorawan_get_cfs(void)
{
	return lw_cfs;
}

void rak_fw_lorawan_set_adr(bool enable)
{
	lw_adr = enable;
	if (lw_started) {
		lorawan_enable_adr(enable);
	}
}

bool rak_fw_lorawan_get_adr(void)
{
	return lw_adr;
}

int rak_fw_lorawan_get_devaddr(uint32_t *devaddr)
{
	MibRequestConfirm_t mib = {
		.Type = MIB_DEV_ADDR,
	};

	if (devaddr == NULL) {
		return -EINVAL;
	}

	ensure_lock();
	k_mutex_lock(&lw_lock, K_FOREVER);

	if (!lw_started || !lw_joined) {
		k_mutex_unlock(&lw_lock);
		return -ENOTCONN;
	}

	if (LoRaMacMibGetRequestConfirm(&mib) != LORAMAC_STATUS_OK) {
		k_mutex_unlock(&lw_lock);
		return -EIO;
	}

	*devaddr = mib.Param.DevAddr;
	k_mutex_unlock(&lw_lock);
	return 0;
}

int rak_fw_lorawan_recv_format_and_clear(char *out, size_t out_len)
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
