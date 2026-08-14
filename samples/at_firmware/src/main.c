/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Thin AT firmware sample: board bring-up + System ON idle / GRTC-timed uplink.
 * Framework join/send APIs live in rak-fw; OTAA defaults and sleep cycle live here.
 * Explicit AT+SLEEP still uses board System OFF (see radio_bind).
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/version.h>

#include "board/board_at_lp.h"
#include "core/bus_pm.h"
#include "core/led.h"
#include "radio/radio_bind.h"
#include "storage/storage.h"

#include <rak_at/rak_at.h>
#include <rak_at/rak_at_cfg.h>
#include <rak_at/rak_at_port.h>
#include <rak_at/rak_at_util.h>
#include <rak_fw/board.h>
#include <rak_fw/lora_p2p.h>
#include <rak_fw/lorawan.h>

LOG_MODULE_REGISTER(at_firmware, LOG_LEVEL_INF);

/* Temporary test credentials; only seed fields missing from NVS. */
#define APP_OTAA_DEVEUI_HEX "0011223344556677"
#define APP_OTAA_APPEUI_HEX "0011223344556677"
#define APP_OTAA_APPKEY_HEX "00112233445566778899AABBCCDDEEFF"

#define APP_UPLINK_PORT 2U
#define APP_NVM_SETTLE_MS 100U
#define APP_MIN_SLEEP_MS 1000U
#define APP_JOIN_RETRY_MS 10000U

static struct k_work_delayable uplink_work;
static struct k_work_delayable join_retry_work;
static bool cycle_started;
static bool uplink_in_flight;
static int64_t uplink_mark_ms;

static uint32_t app_send_interval_ms(void)
{
	return rak3162_storage_get_send_interval_s() * 1000U;
}

static bool app_auto_uplink_enabled(void)
{
	return rak3162_storage_get_send_interval_s() != 0U;
}

static void log_reset_cause(void)
{
	uint32_t cause = 0U;
	int ret = hwinfo_get_reset_cause(&cause);

	if (ret == 0) {
		LOG_INF("Reset cause: 0x%08x", cause);
		(void)hwinfo_clear_reset_cause();
	}
}

static uint32_t compute_next_uplink_delay_ms(bool compensate_interval)
{
	uint32_t interval_ms = app_send_interval_ms();
	int64_t elapsed_ms;
	int64_t remain_ms;

	if (interval_ms == 0U) {
		return 0U;
	}

	if (!compensate_interval || (uplink_mark_ms <= 0)) {
		return interval_ms;
	}

	elapsed_ms = k_uptime_get() - uplink_mark_ms;
	if (elapsed_ms < 0) {
		elapsed_ms = 0;
	}

	/* System ON retains RAM, so there is no cold-boot budget to subtract. */
	remain_ms = (int64_t)interval_ms - elapsed_ms;
	if (remain_ms < (int64_t)APP_MIN_SLEEP_MS) {
		remain_ms = APP_MIN_SLEEP_MS;
	}

	return (uint32_t)remain_ms;
}

static void schedule_next_uplink(uint32_t minimum_idle_ms, bool compensate_interval)
{
	uint32_t delay_ms;

	if (!app_auto_uplink_enabled()) {
		(void)k_work_cancel_delayable(&uplink_work);
		LOG_INF("Auto uplink disabled (AT+SENDINT=0); use AT+SEND");
		return;
	}

	delay_ms = compute_next_uplink_delay_ms(compensate_interval);
	if (delay_ms < minimum_idle_ms) {
		delay_ms = minimum_idle_ms;
	}

	/*
	 * Zephyr's tickless timeout is backed by the nRF GRTC. Returning from
	 * this handler lets the idle thread enter System ON WFI with RAM kept.
	 */
	LOG_INF("System ON idle %u ms (GRTC, target TX interval %u s)", delay_ms,
		rak3162_storage_get_send_interval_s());
	(void)k_work_reschedule(&uplink_work, K_MSEC(delay_ms));
}

static bool lorawan_mode_active(void)
{
	struct rak_at_runtime_cfg cfg;

	rak_at_cfg_get_active(&cfg);
	return cfg.nwm == RAK_AT_NWM_LORAWAN;
}

static void autojoin_on_boot(void);

static void join_retry_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_INF("Retrying LoRaWAN autojoin");
	autojoin_on_boot();
}

static void schedule_join_retry(uint32_t delay_ms)
{
	LOG_INF("Join retry in %u ms (System ON/GRTC)", delay_ms);
	(void)k_work_reschedule(&join_retry_work, K_MSEC(delay_ms));
}

static void uplink_work_handler(struct k_work *work)
{
	uint8_t payload[4];
	uint32_t uplink_counter;
	int ret;

	ARG_UNUSED(work);

	if (!cycle_started || uplink_in_flight || !app_auto_uplink_enabled()) {
		return;
	}

	if (!lorawan_mode_active()) {
		return;
	}

	if (!rak_fw_lorawan_is_joined()) {
		schedule_join_retry(APP_JOIN_RETRY_MS);
		return;
	}

	ret = rak_fw_lorawan_get_uplink_counter(&uplink_counter);
	if (ret != 0) {
		LOG_WRN("FCnt read failed: %d", ret);
		schedule_next_uplink(0U, false);
		return;
	}

	sys_put_be32(uplink_counter, payload);
	ret = rak_fw_lorawan_send_async(APP_UPLINK_PORT, payload, sizeof(payload),
					LORAWAN_MSG_UNCONFIRMED);
	if (ret == 0) {
		uplink_mark_ms = k_uptime_get();
		LOG_INF("Uplink queued (FCnt=%u)", uplink_counter);
		uplink_in_flight = true;
		return;
	}

	if (ret == -EBUSY) {
		(void)k_work_reschedule(&uplink_work, K_MSEC(100U));
		return;
	}

	LOG_ERR("Uplink queue failed: %d", ret);
	schedule_next_uplink(0U, false);
}

static void on_send_done(int result)
{
	if (!uplink_in_flight) {
		return;
	}

	uplink_in_flight = false;
	radio_bind_prepare_system_on_idle();

	if (!app_auto_uplink_enabled()) {
		return;
	}

	if (result == 0) {
		schedule_next_uplink(APP_NVM_SETTLE_MS, true);
	} else {
		LOG_WRN("Uplink failed: %d; retry after full interval", result);
		schedule_next_uplink(0U, false);
	}
}

static void on_join_status(bool joined)
{
	if (joined) {
		(void)k_work_cancel_delayable(&join_retry_work);
		if (cycle_started) {
			uplink_in_flight = false;
			if (app_auto_uplink_enabled()) {
				(void)k_work_reschedule(&uplink_work, K_NO_WAIT);
			} else {
				radio_bind_prepare_system_on_idle();
			}
			return;
		}
		cycle_started = true;
		uplink_in_flight = false;

		if (!app_auto_uplink_enabled()) {
			LOG_INF("LoRaWAN session ready; auto uplink off (AT+SENDINT=0)");
			radio_bind_prepare_system_on_idle();
			return;
		}

		LOG_INF("LoRaWAN session ready: send one Port%u uplink", APP_UPLINK_PORT);
		(void)k_work_reschedule(&uplink_work, K_NO_WAIT);
		return;
	}

	LOG_WRN("Join unavailable; retry after full interval");
	radio_bind_prepare_system_on_idle();
	schedule_join_retry(APP_JOIN_RETRY_MS);
}

static int seed_test_otaa_creds_if_needed(struct rak_at_runtime_cfg *cfg)
{
	uint8_t value[RAK_AT_KEY_BIN_LEN];
	bool changed = false;
	int ret;

	if ((cfg->valid_mask & RAK_AT_CFG_VALID_DEVEUI) == 0U) {
		ret = rak_at_parse_hex_bytes(APP_OTAA_DEVEUI_HEX,
					     strlen(APP_OTAA_DEVEUI_HEX),
					     value, RAK_AT_EUI_BIN_LEN);
		if (ret != 0) {
			return ret;
		}
		memcpy(cfg->deveui, value, sizeof(cfg->deveui));
		cfg->valid_mask |= RAK_AT_CFG_VALID_DEVEUI;
		changed = true;
	}

	if ((cfg->valid_mask & RAK_AT_CFG_VALID_APPEUI) == 0U) {
		ret = rak_at_parse_hex_bytes(APP_OTAA_APPEUI_HEX,
					     strlen(APP_OTAA_APPEUI_HEX),
					     value, RAK_AT_EUI_BIN_LEN);
		if (ret != 0) {
			return ret;
		}
		memcpy(cfg->appeui, value, sizeof(cfg->appeui));
		cfg->valid_mask |= RAK_AT_CFG_VALID_APPEUI;
		changed = true;
	}

	if ((cfg->valid_mask & RAK_AT_CFG_VALID_APPKEY) == 0U) {
		ret = rak_at_parse_hex_bytes(APP_OTAA_APPKEY_HEX,
					     strlen(APP_OTAA_APPKEY_HEX),
					     value, RAK_AT_KEY_BIN_LEN);
		if (ret != 0) {
			return ret;
		}
		memcpy(cfg->appkey, value, sizeof(cfg->appkey));
		cfg->valid_mask |= RAK_AT_CFG_VALID_APPKEY;
		changed = true;
	}

	if (!changed) {
		return 0;
	}

	ret = rak_at_cfg_set_and_apply(cfg);
	if (ret != 0) {
		LOG_WRN("Failed to persist test OTAA credentials: %d", ret);
		return ret;
	}

	LOG_INF("Seeded missing test OTAA credentials");
	return 0;
}

static void autojoin_on_boot(void)
{
	struct rak_at_runtime_cfg cfg;
	const uint8_t *nwkkey;
	int ret;

	rak_at_cfg_get_active(&cfg);
	(void)seed_test_otaa_creds_if_needed(&cfg);
	rak_at_cfg_get_active(&cfg);

	if (cfg.nwm != RAK_AT_NWM_LORAWAN) {
		LOG_INF("Autojoin skipped: NWM=%u", cfg.nwm);
		return;
	}

	if (cfg.join_auto == 0U) {
		LOG_INF("Autojoin skipped: join_auto=0");
		return;
	}

	if (((cfg.valid_mask & RAK_AT_CFG_VALID_DEVEUI) == 0U) ||
	    ((cfg.valid_mask & RAK_AT_CFG_VALID_APPEUI) == 0U) ||
	    ((cfg.valid_mask & RAK_AT_CFG_VALID_APPKEY) == 0U)) {
		LOG_INF("Autojoin skipped: provision AT+DEVEUI/APPEUI/APPKEY first");
		return;
	}

	nwkkey = ((cfg.valid_mask & RAK_AT_CFG_VALID_NWKKEY) != 0U) ? cfg.nwkkey : cfg.appkey;
	ret = rak_fw_lorawan_ensure_started();
	if (ret != 0) {
		LOG_WRN("LoRaWAN start failed: %d", ret);
		schedule_join_retry(APP_JOIN_RETRY_MS);
		return;
	}

	if (rak_fw_lorawan_is_joined()) {
		LOG_INF("Autojoin skipped: already joined");
		return;
	}

	ret = rak_fw_lorawan_join_otaa_async(cfg.deveui, cfg.appeui, cfg.appkey, nwkkey,
					     cfg.join_interval_s, 1U);
	if (ret != 0) {
		LOG_WRN("Autojoin queue failed: %d", ret);
		schedule_join_retry(APP_JOIN_RETRY_MS);
		return;
	}

	LOG_INF("Autojoin queued");
}

static int cmd_sendint(const struct rak_at_request *req)
{
	uint32_t interval_s;
	char *end = NULL;

	if (req->form == RAK_AT_FORM_HELP) {
		rak_at_resp_line(
			"AT+SENDINT: set/get auto uplink interval seconds (0=off, max %u)",
			RAK3162_SENDINT_MAX_S);
		rak_at_resp_ok();
		return 0;
	}

	if (req->form == RAK_AT_FORM_GET) {
		rak_at_resp_line("AT+SENDINT=%u", rak3162_storage_get_send_interval_s());
		rak_at_resp_ok();
		return 0;
	}

	if (req->form != RAK_AT_FORM_SET) {
		rak_at_resp_line("AT_PARAM_ERROR");
		return -EINVAL;
	}

	interval_s = (uint32_t)strtoul(req->args, &end, 10);
	if ((end == req->args) || (*end != '\0') || (interval_s > RAK3162_SENDINT_MAX_S)) {
		rak_at_resp_line("AT_PARAM_ERROR");
		return -EINVAL;
	}

	if (rak3162_storage_set_send_interval_s(interval_s) != 0) {
		rak_at_resp_error(NULL);
		return -EIO;
	}

	if (interval_s == 0U) {
		(void)k_work_cancel_delayable(&uplink_work);
		if (cycle_started && !uplink_in_flight) {
			radio_bind_prepare_system_on_idle();
		}
	} else if (cycle_started && rak_fw_lorawan_is_joined() && !uplink_in_flight) {
		schedule_next_uplink(0U, false);
	}

	rak_at_resp_ok();
	return 0;
}

static void app_policy_start(void)
{
	k_work_init_delayable(&uplink_work, uplink_work_handler);
	k_work_init_delayable(&join_retry_work, join_retry_work_handler);
	rak_fw_lorawan_set_join_status_cb(on_join_status);
	rak_fw_lorawan_set_send_done_cb(on_send_done);
	(void)rak_at_register_command("SENDINT", cmd_sendint, "AT+SENDINT");
	autojoin_on_boot();
}

int main(void)
{
	int ret;

	ret = rak_at_port_init();
	if (ret != 0) {
		LOG_ERR("AT port init failed: %d", ret);
		return ret;
	}

	ret = board_at_lp_init();
	if (ret != 0) {
		LOG_ERR("Board AT LP init failed: %d", ret);
		return ret;
	}
	LOG_INF("AT port init done");
	log_reset_cause();

	ret = rak3162_storage_init();
	if (ret != 0) {
		LOG_ERR("Storage init failed: %d", ret);
	} else {
		LOG_INF("Storage init done");
	}
	rak3162_storage_bind_at_cfg();

	rak_fw_lora_p2p_init();
	rak_fw_lorawan_init();
	radio_bind_at();

	rak_at_init();
	rak_at_register_standard_commands();
	LOG_INF("AT framework init done");

	/* Start with secondary buses suspended; resume only around RF windows. */
	rak3162_bus_pm_suspend();

	ret = rak3162_led_start();
	if (ret != 0) {
		LOG_ERR("LED init failed: %d", ret);
	}

	ret = rak_at_start();
	if (ret != 0) {
		LOG_ERR("AT runtime start failed: %d", ret);
		return ret;
	}

	app_policy_start();

	LOG_INF("zephyr_version: %s", KERNEL_VERSION_STRING);
	LOG_INF("AT firmware ready (SENDINT=%u s, System ON idle)",
		rak3162_storage_get_send_interval_s());

	k_sleep(K_FOREVER);

	return 0;
}
