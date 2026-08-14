/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rak_fw/lora_p2p.h>

#include <rak_fw/lorawan.h>

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(rak_fw_p2p, LOG_LEVEL_INF);

#define LORA_NODE DT_ALIAS(lora0)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(LORA_NODE), "lora0 alias required");

#define P2P_DEFAULT_FREQ_HZ 868000000U
#define P2P_DEFAULT_SF 7U
#define P2P_DEFAULT_PREAMBLE 8U
#define P2P_DEFAULT_TX_DBM 14

#define P2P_PRECV_STOP 0U
#define P2P_PRECV_CONT_TX_OK 65533U
#define P2P_PRECV_CONT_LOCKED 65534U
#define P2P_PRECV_ONE_SHOT 65535U

#define P2P_RX_STACK_SIZE 2048
#define P2P_MAX_PAYLOAD 256

enum p2p_rx_mode {
	P2P_RX_OFF = 0,
	P2P_RX_TIMED,
	P2P_RX_CONT_TX_OK,
	P2P_RX_CONT_LOCKED,
	P2P_RX_ONE_SHOT,
};

struct p2p_params {
	uint32_t freq_hz;
	uint8_t sf;
	enum lora_signal_bandwidth bw;
	uint8_t cr_rui; /* 0..3 */
	uint16_t preamble;
	int8_t tx_dbm;
};

static const struct device *lora_dev;
static struct k_mutex p2p_lock;
static bool lock_ready;

static struct p2p_params p2p = {
	.freq_hz = P2P_DEFAULT_FREQ_HZ,
	.sf = P2P_DEFAULT_SF,
	.bw = BW_125_KHZ,
	.cr_rui = 0U,
	.preamble = P2P_DEFAULT_PREAMBLE,
	.tx_dbm = P2P_DEFAULT_TX_DBM,
};

static enum p2p_rx_mode rx_mode = P2P_RX_OFF;
static uint16_t rx_configured_ms = P2P_PRECV_STOP;
static bool tx_in_progress;
static bool async_rx_active;
static bool rx_thread_running;
static bool rx_abort_requested;

static uint8_t tx_buf[P2P_MAX_PAYLOAD];
static size_t tx_len;
static uint8_t rx_buf[P2P_MAX_PAYLOAD];

static struct k_work evt_work;
static char evt_line[600];
static rak_fw_lora_p2p_event_handler_t event_handler;

static struct k_sem rx_kick_sem;
static struct k_thread rx_thread;
static K_THREAD_STACK_DEFINE(rx_stack, P2P_RX_STACK_SIZE);

void rak_fw_lora_p2p_set_event_handler(rak_fw_lora_p2p_event_handler_t handler)
{
	event_handler = handler;
}

static void ensure_lock(void)
{
	if (!lock_ready) {
		k_mutex_init(&p2p_lock);
		lock_ready = true;
	}
}

static bool lorawan_blocks_p2p(void)
{
	return rak_fw_lorawan_is_started();
}

static enum lora_coding_rate cr_from_rui(uint8_t cr_rui)
{
	switch (cr_rui) {
	case 0:
		return CR_4_5;
	case 1:
		return CR_4_6;
	case 2:
		return CR_4_7;
	case 3:
	default:
		return CR_4_8;
	}
}

static const char *bw_to_khz_str(enum lora_signal_bandwidth bw)
{
	switch (bw) {
	case BW_250_KHZ:
		return "250";
	case BW_500_KHZ:
		return "500";
	case BW_125_KHZ:
	default:
		return "125";
	}
}

static bool parse_bw_token(const char *token, enum lora_signal_bandwidth *bw)
{
	if ((strcmp(token, "0") == 0) || (strcmp(token, "125") == 0)) {
		*bw = BW_125_KHZ;
		return true;
	}
	if ((strcmp(token, "1") == 0) || (strcmp(token, "250") == 0)) {
		*bw = BW_250_KHZ;
		return true;
	}
	if ((strcmp(token, "2") == 0) || (strcmp(token, "500") == 0)) {
		*bw = BW_500_KHZ;
		return true;
	}

	return false;
}

static bool split_colon_fields(char *buf, char **parts, size_t part_count)
{
	char *save = NULL;
	char *tok = strtok_r(buf, ":", &save);

	for (size_t i = 0; i < part_count; i++) {
		if (tok == NULL) {
			return false;
		}
		parts[i] = tok;
		tok = strtok_r(NULL, ":", &save);
	}

	return tok == NULL;
}

static void evt_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (event_handler != NULL) {
		event_handler(evt_line);
	}
}

static void evt_submit(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintk(evt_line, sizeof(evt_line), fmt, ap);
	va_end(ap);
	(void)k_work_submit(&evt_work);
}

static void bytes_to_hex_upper(const uint8_t *src, size_t len, char *dst)
{
	static const char hex[] = "0123456789ABCDEF";

	for (size_t i = 0; i < len; i++) {
		dst[i * 2U] = hex[(src[i] >> 4) & 0x0F];
		dst[i * 2U + 1U] = hex[src[i] & 0x0F];
	}
	dst[len * 2U] = '\0';
}

static int apply_modem_config(bool tx)
{
	struct lora_modem_config cfg = {
		.frequency = p2p.freq_hz,
		.bandwidth = p2p.bw,
		.datarate = (enum lora_datarate)p2p.sf,
		.coding_rate = cr_from_rui(p2p.cr_rui),
		.preamble_len = p2p.preamble,
		.tx_power = p2p.tx_dbm,
		.tx = tx,
		.iq_inverted = false,
		.public_network = false,
	};

	return lora_config(lora_dev, &cfg);
}

static enum p2p_rx_mode rx_mode_from_time(uint16_t time_ms)
{
	if (time_ms == P2P_PRECV_STOP) {
		return P2P_RX_OFF;
	}
	if (time_ms == P2P_PRECV_CONT_TX_OK) {
		return P2P_RX_CONT_TX_OK;
	}
	if (time_ms == P2P_PRECV_CONT_LOCKED) {
		return P2P_RX_CONT_LOCKED;
	}
	if (time_ms == P2P_PRECV_ONE_SHOT) {
		return P2P_RX_ONE_SHOT;
	}
	return P2P_RX_TIMED;
}

static int stop_async_rx_locked(void)
{
	int ret = 0;

	if (async_rx_active) {
		ret = lora_recv_async(lora_dev, NULL, NULL);
		async_rx_active = false;
	}

	return ret;
}

static void async_rx_cb(const struct device *dev, uint8_t *data, uint16_t size, int16_t rssi,
			int8_t snr, void *user_data)
{
	char hex[(P2P_MAX_PAYLOAD * 2U) + 1U];
	size_t copy_len;
	bool stop_after = false;

	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	copy_len = MIN((size_t)size, sizeof(rx_buf));
	memcpy(rx_buf, data, copy_len);
	bytes_to_hex_upper(rx_buf, copy_len, hex);
	evt_submit("+EVT:RXP2P:%d:%d:%s", (int)rssi, (int)snr, hex);

	k_mutex_lock(&p2p_lock, K_FOREVER);
	if (rx_mode == P2P_RX_ONE_SHOT) {
		stop_after = true;
		rx_mode = P2P_RX_OFF;
		rx_configured_ms = P2P_PRECV_STOP;
		async_rx_active = false;
	}
	k_mutex_unlock(&p2p_lock);

	if (stop_after) {
		(void)lora_recv_async(lora_dev, NULL, NULL);
	}
}

static int start_async_rx_locked(void)
{
	int ret;

	ret = apply_modem_config(false);
	if (ret < 0) {
		return ret;
	}

	ret = lora_recv_async(lora_dev, async_rx_cb, NULL);
	if (ret < 0) {
		return ret;
	}

	async_rx_active = true;
	return 0;
}

static void rx_thread_entry(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (1) {
		uint16_t window_ms;
		int16_t rssi = 0;
		int8_t snr = 0;
		int len;
		int ret;

		k_sem_take(&rx_kick_sem, K_FOREVER);

		k_mutex_lock(&p2p_lock, K_FOREVER);
		if ((rx_mode != P2P_RX_TIMED) || rx_abort_requested) {
			rx_abort_requested = false;
			rx_thread_running = false;
			k_mutex_unlock(&p2p_lock);
			continue;
		}
		window_ms = rx_configured_ms;
		rx_thread_running = true;
		k_mutex_unlock(&p2p_lock);

		ret = apply_modem_config(false);
		if (ret < 0) {
			evt_submit("+EVT:RXP2P RECEIVE TIMEOUT");
			k_mutex_lock(&p2p_lock, K_FOREVER);
			rx_mode = P2P_RX_OFF;
			rx_configured_ms = P2P_PRECV_STOP;
			rx_thread_running = false;
			k_mutex_unlock(&p2p_lock);
			continue;
		}

		len = lora_recv(lora_dev, rx_buf, (uint8_t)MIN(sizeof(rx_buf), 255U), K_MSEC(window_ms),
				&rssi, &snr);
		if (len > 0) {
			char hex[(P2P_MAX_PAYLOAD * 2U) + 1U];

			bytes_to_hex_upper(rx_buf, (size_t)len, hex);
			evt_submit("+EVT:RXP2P:%d:%d:%s", (int)rssi, (int)snr, hex);
		} else {
			evt_submit("+EVT:RXP2P RECEIVE TIMEOUT");
		}

		k_mutex_lock(&p2p_lock, K_FOREVER);
		rx_mode = P2P_RX_OFF;
		rx_configured_ms = P2P_PRECV_STOP;
		rx_thread_running = false;
		rx_abort_requested = false;
		k_mutex_unlock(&p2p_lock);
	}
}

static int stop_rx_locked(void)
{
	int ret = 0;

	if (rx_thread_running) {
		rx_abort_requested = true;
		/* Timed RX blocks in lora_recv; cancel by stopping async path only.
		 * Driver releases on timeout; force sleep via config TX then sleep is awkward.
		 * Best-effort: leave abort flag; next kick ignored until thread exits.
		 */
	}

	ret = stop_async_rx_locked();
	rx_mode = P2P_RX_OFF;
	rx_configured_ms = P2P_PRECV_STOP;
	return ret;
}

void rak_fw_lora_p2p_init(void)
{
	ensure_lock();
	k_work_init(&evt_work, evt_work_handler);
	k_sem_init(&rx_kick_sem, 0, 1);

	lora_dev = DEVICE_DT_GET(LORA_NODE);
	if (!device_is_ready(lora_dev)) {
		LOG_ERR("lora0 not ready");
		return;
	}

	(void)k_thread_create(&rx_thread, rx_stack, K_THREAD_STACK_SIZEOF(rx_stack), rx_thread_entry,
			      NULL, NULL, NULL, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	k_thread_name_set(&rx_thread, "p2p_rx");
	LOG_INF("P2P ready (Zephyr LoRa API)");
}

bool rak_fw_lora_p2p_is_busy(void)
{
	bool busy;

	ensure_lock();
	k_mutex_lock(&p2p_lock, K_FOREVER);
	busy = tx_in_progress || async_rx_active || rx_thread_running ||
	       (rx_mode == P2P_RX_CONT_LOCKED);
	k_mutex_unlock(&p2p_lock);
	return busy;
}

bool rak_fw_lora_p2p_is_active(void)
{
	bool active;

	ensure_lock();
	k_mutex_lock(&p2p_lock, K_FOREVER);
	active = (rx_mode != P2P_RX_OFF) || tx_in_progress;
	k_mutex_unlock(&p2p_lock);
	return active;
}

int rak_fw_lora_p2p_params_format(char *out, size_t out_len)
{
	int n;

	if ((out == NULL) || (out_len == 0U)) {
		return -EINVAL;
	}

	ensure_lock();
	k_mutex_lock(&p2p_lock, K_FOREVER);
	n = snprintk(out, out_len, "%u:%u:%s:%u:%u:%d", p2p.freq_hz, p2p.sf, bw_to_khz_str(p2p.bw),
		     p2p.cr_rui, p2p.preamble, (int)p2p.tx_dbm);
	k_mutex_unlock(&p2p_lock);

	return (n < 0) || ((size_t)n >= out_len) ? -EINVAL : 0;
}

int rak_fw_lora_p2p_params_set(const char *param)
{
	char buf[128];
	char *parts[6];
	unsigned long freq;
	unsigned long sf;
	unsigned long cr;
	unsigned long preamble;
	long tx_power;
	enum lora_signal_bandwidth bw;
	char *end;

	if ((param == NULL) || (param[0] == '\0') || (strlen(param) >= sizeof(buf))) {
		return -EINVAL;
	}

	if (lorawan_blocks_p2p()) {
		return -EBUSY;
	}

	memcpy(buf, param, strlen(param) + 1U);
	if (!split_colon_fields(buf, parts, 6U)) {
		return -EINVAL;
	}

	errno = 0;
	freq = strtoul(parts[0], &end, 10);
	if ((errno != 0) || (*end != '\0') || (freq < 150000000UL) || (freq > 960000000UL)) {
		return -EINVAL;
	}

	errno = 0;
	sf = strtoul(parts[1], &end, 10);
	if ((errno != 0) || (*end != '\0') || (sf < 6UL) || (sf > 12UL)) {
		return -EINVAL;
	}

	if (!parse_bw_token(parts[2], &bw)) {
		return -EINVAL;
	}

	errno = 0;
	cr = strtoul(parts[3], &end, 10);
	if ((errno != 0) || (*end != '\0') || (cr > 3UL)) {
		return -EINVAL;
	}

	errno = 0;
	preamble = strtoul(parts[4], &end, 10);
	if ((errno != 0) || (*end != '\0') || (preamble < 2UL) || (preamble > 65535UL)) {
		return -EINVAL;
	}

	errno = 0;
	tx_power = strtol(parts[5], &end, 10);
	if ((errno != 0) || (*end != '\0') || (tx_power < 5) || (tx_power > 22)) {
		return -EINVAL;
	}

	ensure_lock();
	k_mutex_lock(&p2p_lock, K_FOREVER);
	if (tx_in_progress ||
	    ((rx_mode != P2P_RX_OFF) && (rx_mode != P2P_RX_CONT_TX_OK))) {
		k_mutex_unlock(&p2p_lock);
		return -EBUSY;
	}

	p2p.freq_hz = (uint32_t)freq;
	p2p.sf = (uint8_t)sf;
	p2p.bw = bw;
	p2p.cr_rui = (uint8_t)cr;
	p2p.preamble = (uint16_t)preamble;
	p2p.tx_dbm = (int8_t)tx_power;
	k_mutex_unlock(&p2p_lock);
	return 0;
}

uint16_t rak_fw_lora_p2p_recv_get(void)
{
	uint16_t v;

	ensure_lock();
	k_mutex_lock(&p2p_lock, K_FOREVER);
	v = rx_configured_ms;
	k_mutex_unlock(&p2p_lock);
	return v;
}

int rak_fw_lora_p2p_recv_set(uint16_t time_ms)
{
	enum p2p_rx_mode mode;
	int ret = 0;

	if (lorawan_blocks_p2p()) {
		return -EBUSY;
	}

	if (!device_is_ready(lora_dev)) {
		return -ENODEV;
	}

	mode = rx_mode_from_time(time_ms);

	ensure_lock();
	k_mutex_lock(&p2p_lock, K_FOREVER);

	if (tx_in_progress) {
		k_mutex_unlock(&p2p_lock);
		return -EBUSY;
	}

	if (time_ms == P2P_PRECV_STOP) {
		ret = stop_rx_locked();
		k_mutex_unlock(&p2p_lock);
		return ret;
	}

	if (rx_mode == P2P_RX_CONT_LOCKED) {
		k_mutex_unlock(&p2p_lock);
		return -EBUSY;
	}

	(void)stop_async_rx_locked();
	if (rx_thread_running) {
		rx_abort_requested = true;
	}

	rx_mode = mode;
	rx_configured_ms = time_ms;

	if ((mode == P2P_RX_CONT_TX_OK) || (mode == P2P_RX_CONT_LOCKED) ||
	    (mode == P2P_RX_ONE_SHOT)) {
		ret = start_async_rx_locked();
		if (ret < 0) {
			rx_mode = P2P_RX_OFF;
			rx_configured_ms = P2P_PRECV_STOP;
		}
	} else if (mode == P2P_RX_TIMED) {
		k_sem_give(&rx_kick_sem);
	}

	k_mutex_unlock(&p2p_lock);
	return ret;
}

int rak_fw_lora_p2p_send_payload(const uint8_t *data, size_t len)
{
	int ret;
	bool resume_rx = false;

	if ((data == NULL) || (len == 0U) || (len > sizeof(tx_buf))) {
		return -EINVAL;
	}

	if (lorawan_blocks_p2p()) {
		return -EBUSY;
	}

	if (!device_is_ready(lora_dev)) {
		return -ENODEV;
	}

	ensure_lock();
	k_mutex_lock(&p2p_lock, K_FOREVER);

	if (tx_in_progress || (rx_mode == P2P_RX_CONT_LOCKED) ||
	    (rx_mode == P2P_RX_TIMED) || (rx_mode == P2P_RX_ONE_SHOT)) {
		k_mutex_unlock(&p2p_lock);
		return -EBUSY;
	}

	if (rx_mode == P2P_RX_CONT_TX_OK) {
		resume_rx = true;
		(void)stop_async_rx_locked();
	}

	memcpy(tx_buf, data, len);
	tx_len = len;
	tx_in_progress = true;
	k_mutex_unlock(&p2p_lock);

	ret = apply_modem_config(true);
	if (ret < 0) {
		k_mutex_lock(&p2p_lock, K_FOREVER);
		tx_in_progress = false;
		if (resume_rx) {
			(void)start_async_rx_locked();
		}
		k_mutex_unlock(&p2p_lock);
		return ret;
	}

	ret = lora_send(lora_dev, tx_buf, (uint32_t)tx_len);

	k_mutex_lock(&p2p_lock, K_FOREVER);
	tx_in_progress = false;
	if (resume_rx && (rx_mode == P2P_RX_CONT_TX_OK)) {
		(void)start_async_rx_locked();
	}
	k_mutex_unlock(&p2p_lock);

	if (ret < 0) {
		return ret;
	}

	evt_submit("+EVT:TXP2P DONE");
	return 0;
}
