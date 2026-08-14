/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RAK_AT_CFG_H_
#define RAK_AT_CFG_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RAK_AT_SN_LEN 18
#define RAK_AT_EUI_BIN_LEN 8
#define RAK_AT_KEY_BIN_LEN 16

#define RAK_AT_NWM_P2P_LORA 0U
#define RAK_AT_NWM_LORAWAN 1U

#define RAK_AT_BAND_EU433 0U
#define RAK_AT_BAND_CN470 1U
#define RAK_AT_BAND_RU864 2U
#define RAK_AT_BAND_IN865 3U
#define RAK_AT_BAND_EU868 4U
#define RAK_AT_BAND_US915 5U
#define RAK_AT_BAND_AU915 6U
#define RAK_AT_BAND_KR920 7U
#define RAK_AT_BAND_AS923 8U

enum rak_at_cfg_valid_bits {
	RAK_AT_CFG_VALID_SN = 1U << 0,
	RAK_AT_CFG_VALID_DEVEUI = 1U << 1,
	RAK_AT_CFG_VALID_APPEUI = 1U << 2,
	RAK_AT_CFG_VALID_HFXO_CAP = 1U << 3,
	RAK_AT_CFG_VALID_LFXO_CAP = 1U << 4,
	RAK_AT_CFG_VALID_APPKEY = 1U << 5,
	RAK_AT_CFG_VALID_NWKKEY = 1U << 6,
	RAK_AT_CFG_VALID_LW_OPTS = 1U << 7,
};

/**
 * @brief Persistent runtime configuration blob (NVS layout must stay stable).
 *
 * Layout matches the historical rak_at_runtime_cfg used by RAK3162 product firmware.
 */
struct rak_at_runtime_cfg {
	uint32_t valid_mask;
	char sn[RAK_AT_SN_LEN + 1];
	uint8_t deveui[RAK_AT_EUI_BIN_LEN];
	uint8_t appeui[RAK_AT_EUI_BIN_LEN];
	uint8_t appkey[RAK_AT_KEY_BIN_LEN];
	uint8_t nwkkey[RAK_AT_KEY_BIN_LEN];
	uint32_t hfxo_cap_ff;
	uint32_t lfxo_cap_ff;
	uint8_t nwm;
	uint8_t band;
	uint8_t cfm;
	uint8_t adr;
	uint8_t join_cmd;
	uint8_t join_auto;
	uint8_t join_interval_s;
	uint8_t join_attempts;
};

struct rak_at_cfg_ops {
	void (*get_active)(struct rak_at_runtime_cfg *cfg);
	int (*set_and_apply)(const struct rak_at_runtime_cfg *cfg);
};

void rak_at_cfg_set_ops(const struct rak_at_cfg_ops *ops);
void rak_at_cfg_get_active(struct rak_at_runtime_cfg *cfg);
int rak_at_cfg_set_and_apply(const struct rak_at_runtime_cfg *cfg);

#ifdef __cplusplus
}
#endif

#endif /* RAK_AT_CFG_H_ */
