#ifndef HW_STORAGE_H_
#define HW_STORAGE_H_

#include <stdbool.h>
#include <stdint.h>

#define HW_SN_LEN 18
#define HW_EUI_BIN_LEN 8
#define HW_KEY_BIN_LEN 16

/* RUI3 AT+NWM values (P2P_FSK=2 not supported). */
#define HW_NWM_P2P_LORA 0U
#define HW_NWM_LORAWAN 1U

/* RUI3 AT+BAND codes (9..12 unsupported on Zephyr). */
#define HW_BAND_EU433 0U
#define HW_BAND_CN470 1U
#define HW_BAND_RU864 2U
#define HW_BAND_IN865 3U
#define HW_BAND_EU868 4U
#define HW_BAND_US915 5U
#define HW_BAND_AU915 6U
#define HW_BAND_KR920 7U
#define HW_BAND_AS923 8U

enum hw_runtime_cfg_valid_bits {
	HW_RUNTIME_CFG_VALID_SN = 1U << 0,
	HW_RUNTIME_CFG_VALID_DEVEUI = 1U << 1,
	HW_RUNTIME_CFG_VALID_APPEUI = 1U << 2,
	HW_RUNTIME_CFG_VALID_HFXO_CAP = 1U << 3,
	HW_RUNTIME_CFG_VALID_LFXO_CAP = 1U << 4,
	HW_RUNTIME_CFG_VALID_APPKEY = 1U << 5,
	HW_RUNTIME_CFG_VALID_NWKKEY = 1U << 6,
	/** nwm / band / cfm / join_* / adr have been explicitly set or defaulted. */
	HW_RUNTIME_CFG_VALID_LW_OPTS = 1U << 7,
};

struct hw_runtime_cfg {
	uint32_t valid_mask;
	char sn[HW_SN_LEN + 1];
	uint8_t deveui[HW_EUI_BIN_LEN];
	uint8_t appeui[HW_EUI_BIN_LEN];
	uint8_t appkey[HW_KEY_BIN_LEN];
	uint8_t nwkkey[HW_KEY_BIN_LEN];
	/** Total HFXO load capacitance in femtofarads (binding: 4000–17000, step 250). */
	uint32_t hfxo_cap_ff;
	/** Total LFXO load capacitance in femtofarads (binding: 4000–18000, step 500). */
	uint32_t lfxo_cap_ff;
	/** Network working mode: 0=P2P_LORA, 1=LoRaWAN. */
	uint8_t nwm;
	/** RUI3 band code (default 4=EU868). */
	uint8_t band;
	/** Confirmation mode for AT+SEND (0=off, 1=on). */
	uint8_t cfm;
	/** Adaptive data rate (0=off, 1=on). */
	uint8_t adr;
	/** AT+JOIN Param1: 1=join, 0=stop (stored for GET). */
	uint8_t join_cmd;
	/** AT+JOIN Param2: auto-join on power up (0=off, 1=on). */
	uint8_t join_auto;
	/** AT+JOIN Param3: reattempt interval seconds (7–255, default 8). */
	uint8_t join_interval_s;
	/** AT+JOIN Param4: join attempts (0=unlimited until success/stop; 1–255 finite). */
	uint8_t join_attempts;
};

void hw_storage_apply_lw_defaults(struct hw_runtime_cfg *cfg);

int hw_storage_init(void);

void hw_storage_get_active_cfg(struct hw_runtime_cfg *cfg);
void hw_storage_get_pending_cfg(struct hw_runtime_cfg *cfg, bool *valid);

int hw_storage_set_pending_cfg(const struct hw_runtime_cfg *cfg);
int hw_storage_apply_pending_cfg(void);

#endif /* HW_STORAGE_H_ */
