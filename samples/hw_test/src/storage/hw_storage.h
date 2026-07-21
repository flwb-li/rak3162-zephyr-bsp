#ifndef HW_STORAGE_H_
#define HW_STORAGE_H_

#include <stdbool.h>
#include <stdint.h>

#define HW_SN_LEN 18
#define HW_EUI_BIN_LEN 8
#define HW_KEY_BIN_LEN 16

enum hw_runtime_cfg_valid_bits {
	HW_RUNTIME_CFG_VALID_SN = 1U << 0,
	HW_RUNTIME_CFG_VALID_DEVEUI = 1U << 1,
	HW_RUNTIME_CFG_VALID_APPEUI = 1U << 2,
	HW_RUNTIME_CFG_VALID_HFXO_CAP = 1U << 3,
	HW_RUNTIME_CFG_VALID_LFXO_CAP = 1U << 4,
	HW_RUNTIME_CFG_VALID_APPKEY = 1U << 5,
	HW_RUNTIME_CFG_VALID_NWKKEY = 1U << 6,
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
};

int hw_storage_init(void);

void hw_storage_get_active_cfg(struct hw_runtime_cfg *cfg);
void hw_storage_get_pending_cfg(struct hw_runtime_cfg *cfg, bool *valid);

int hw_storage_set_pending_cfg(const struct hw_runtime_cfg *cfg);
int hw_storage_apply_pending_cfg(void);

#endif /* HW_STORAGE_H_ */
