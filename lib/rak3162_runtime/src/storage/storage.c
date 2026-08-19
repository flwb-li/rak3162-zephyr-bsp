/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "storage/storage.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <rak_at/rak_at_cfg.h>

#define RAK3162_SETTINGS_ROOT CONFIG_RAK_AT_SETTINGS_ROOT
#define RAK3162_SETTINGS_ACTIVE_CFG_KEY RAK3162_SETTINGS_ROOT "/active_cfg"
#define RAK3162_SETTINGS_SENDINT_KEY RAK3162_SETTINGS_ROOT "/send_interval_s"

struct rak3162_storage_state {
	struct rak_at_runtime_cfg active_cfg;
	struct rak_at_runtime_cfg pending_cfg;
	bool pending_valid;
	uint32_t send_interval_s;
	bool send_interval_loaded;
};

static struct rak3162_storage_state storage_state;

void rak3162_storage_apply_lw_defaults(struct rak_at_runtime_cfg *cfg)
{
	if (cfg == NULL) {
		return;
	}

	if ((cfg->valid_mask & RAK_AT_CFG_VALID_LW_OPTS) != 0U) {
		return;
	}

	cfg->nwm = RAK_AT_NWM_LORAWAN;
	cfg->band = RAK_AT_BAND_EU868;
	cfg->cfm = 0U;
	cfg->adr = 1U;
	cfg->join_cmd = 1U;
	cfg->join_auto = 1U;
	cfg->join_interval_s = 8U;
	cfg->join_attempts = 0U;
	cfg->valid_mask |= RAK_AT_CFG_VALID_LW_OPTS;
}

static void sanitize_cfg(struct rak_at_runtime_cfg *cfg)
{
	if (cfg == NULL) {
		return;
	}

	cfg->sn[RAK_AT_SN_LEN] = '\0';
	rak3162_storage_apply_lw_defaults(cfg);
}

static int save_active_cfg(const struct rak_at_runtime_cfg *cfg)
{
	return settings_save_one(RAK3162_SETTINGS_ACTIVE_CFG_KEY, cfg, sizeof(*cfg));
}

static void clear_pending_cache(void)
{
	memset(&storage_state.pending_cfg, 0, sizeof(storage_state.pending_cfg));
	storage_state.pending_valid = false;
}

static int rak3162_settings_set(const char *name, size_t len_rd, settings_read_cb read_cb, void *cb_arg)
{
	ARG_UNUSED(len_rd);

	if (strcmp(name, "active_cfg") == 0) {
		ssize_t n;

		memset(&storage_state.active_cfg, 0, sizeof(storage_state.active_cfg));
		n = read_cb(cb_arg, &storage_state.active_cfg, sizeof(storage_state.active_cfg));
		if (n < 0) {
			return (int)n;
		}
		/* Accept shorter legacy blobs; new trailing fields stay zero. */
		if ((n == 0) || ((size_t)n > sizeof(storage_state.active_cfg))) {
			memset(&storage_state.active_cfg, 0, sizeof(storage_state.active_cfg));
		}
		return 0;
	}

	if (strcmp(name, "send_interval_s") == 0) {
		uint32_t value = 0U;
		ssize_t n = read_cb(cb_arg, &value, sizeof(value));

		if (n < 0) {
			return (int)n;
		}
		if ((size_t)n == sizeof(value)) {
			if (value > RAK3162_SENDINT_MAX_S) {
				value = RAK3162_SENDINT_MAX_S;
			}
			storage_state.send_interval_s = value;
			storage_state.send_interval_loaded = true;
		}
		return 0;
	}

	return 0;
}

static int rak3162_settings_commit(void)
{
	sanitize_cfg(&storage_state.active_cfg);
	if (!storage_state.send_interval_loaded) {
		storage_state.send_interval_s = RAK3162_SENDINT_DEFAULT_S;
	}
	return 0;
}

uint32_t rak3162_storage_get_send_interval_s(void)
{
	if (!storage_state.send_interval_loaded) {
		return RAK3162_SENDINT_DEFAULT_S;
	}
	return storage_state.send_interval_s;
}

int rak3162_storage_set_send_interval_s(uint32_t interval_s)
{
	int ret;

	if (interval_s > RAK3162_SENDINT_MAX_S) {
		return -EINVAL;
	}

	storage_state.send_interval_s = interval_s;
	storage_state.send_interval_loaded = true;
	ret = settings_save_one(RAK3162_SETTINGS_SENDINT_KEY, &storage_state.send_interval_s,
				sizeof(storage_state.send_interval_s));
	return ret;
}

SETTINGS_STATIC_HANDLER_DEFINE(rak3162_cfg, RAK3162_SETTINGS_ROOT, NULL, rak3162_settings_set, rak3162_settings_commit,
			       NULL);

int rak3162_storage_set_pending_cfg(const struct rak_at_runtime_cfg *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	storage_state.pending_cfg = *cfg;
	sanitize_cfg(&storage_state.pending_cfg);
	storage_state.pending_valid = true;
	return 0;
}

int rak3162_storage_apply_pending_cfg(void)
{
	if (!storage_state.pending_valid) {
		return 0;
	}

	storage_state.active_cfg = storage_state.pending_cfg;
	sanitize_cfg(&storage_state.active_cfg);
	clear_pending_cache();

	return save_active_cfg(&storage_state.active_cfg);
}

void rak3162_storage_get_active_cfg(struct rak_at_runtime_cfg *cfg)
{
	if (cfg != NULL) {
		sanitize_cfg(&storage_state.active_cfg);
		*cfg = storage_state.active_cfg;
	}
}

void rak3162_storage_get_pending_cfg(struct rak_at_runtime_cfg *cfg, bool *valid)
{
	if (cfg != NULL) {
		*cfg = storage_state.pending_cfg;
		sanitize_cfg(cfg);
	}

	if (valid != NULL) {
		*valid = storage_state.pending_valid;
	}
}

static int cfg_set_and_apply(const struct rak_at_runtime_cfg *cfg)
{
	int ret = rak3162_storage_set_pending_cfg(cfg);

	if (ret != 0) {
		return ret;
	}
	return rak3162_storage_apply_pending_cfg();
}

static const struct rak_at_cfg_ops storage_cfg_ops = {
	.get_active = rak3162_storage_get_active_cfg,
	.set_and_apply = cfg_set_and_apply,
};

void rak3162_storage_bind_at_cfg(void)
{
	rak_at_cfg_set_ops(&storage_cfg_ops);
}

int rak3162_storage_init(void)
{
	int ret;

	memset(&storage_state, 0, sizeof(storage_state));
	storage_state.send_interval_s = RAK3162_SENDINT_DEFAULT_S;

	ret = settings_subsys_init();
	if ((ret != 0) && (ret != -EALREADY)) {
		return ret;
	}

	ret = settings_load_subtree(RAK3162_SETTINGS_ROOT);
	if (ret != 0) {
		return ret;
	}

	sanitize_cfg(&storage_state.active_cfg);
	sanitize_cfg(&storage_state.pending_cfg);
	if (!storage_state.send_interval_loaded) {
		storage_state.send_interval_s = RAK3162_SENDINT_DEFAULT_S;
	}
	return 0;
}
