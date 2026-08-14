/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RAK_AT_P2P_SVC_H_
#define RAK_AT_P2P_SVC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rak_at_p2p_ops {
	bool (*is_busy)(void);
	bool (*is_active)(void);
	int (*params_set)(const char *param);
	int (*params_format)(char *out, size_t out_len);
	int (*send_payload)(const uint8_t *data, size_t len);
	int (*recv_set)(uint16_t time_ms);
	uint16_t (*recv_get)(void);
};

void rak_at_p2p_set_ops(const struct rak_at_p2p_ops *ops);
const struct rak_at_p2p_ops *rak_at_p2p_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* RAK_AT_P2P_SVC_H_ */
