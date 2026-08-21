/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RAK_AT_LORAWAN_SVC_H_
#define RAK_AT_LORAWAN_SVC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/lorawan/lorawan.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rak_at_lorawan_ops {
	int (*ensure_started)(void);
	int (*join_otaa_async)(const uint8_t deveui[8], const uint8_t joineui[8],
			       const uint8_t appkey[16], const uint8_t nwkkey[16],
			       uint8_t interval_s, uint8_t attempts);
	int (*join_stop)(void);
	int (*stop)(void);
	int (*send_async)(uint8_t port, const uint8_t *data, uint8_t len,
			  enum lorawan_message_type type);
	bool (*is_joined)(void);
	bool (*is_started)(void);
	bool (*is_busy)(void);
	bool (*is_joining)(void);
	bool (*band_supported)(uint8_t band);
	int (*apply_band)(uint8_t band);
	bool (*mask_supported)(uint8_t band);
	uint16_t (*mask_default)(uint8_t band);
	int (*apply_chmask)(uint16_t rui_mask);
	void (*set_cfm)(uint8_t cfm);
	uint8_t (*get_cfm)(void);
	uint8_t (*get_cfs)(void);
	void (*set_adr)(bool enable);
	bool (*get_adr)(void);
	int (*get_devaddr)(uint32_t *devaddr);
	int (*recv_format_and_clear)(char *out, size_t out_len);
};

void rak_at_lorawan_set_ops(const struct rak_at_lorawan_ops *ops);
const struct rak_at_lorawan_ops *rak_at_lorawan_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* RAK_AT_LORAWAN_SVC_H_ */
