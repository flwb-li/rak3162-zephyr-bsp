/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RAK3162_STORAGE_H_
#define RAK3162_STORAGE_H_

#include <rak_at/rak_at_cfg.h>

#include <stdbool.h>
#include <stdint.h>

/** Default automatic uplink interval (seconds). 0 disables auto uplink. */
#define RAK3162_SENDINT_DEFAULT_S 10U
/** RUI3-compatible upper bound for ATC+/AT+SENDINT. */
#define RAK3162_SENDINT_MAX_S 2147483U

void rak3162_storage_apply_lw_defaults(struct rak_at_runtime_cfg *cfg);

int rak3162_storage_init(void);

void rak3162_storage_get_active_cfg(struct rak_at_runtime_cfg *cfg);
void rak3162_storage_get_pending_cfg(struct rak_at_runtime_cfg *cfg, bool *valid);

int rak3162_storage_set_pending_cfg(const struct rak_at_runtime_cfg *cfg);
int rak3162_storage_apply_pending_cfg(void);

/** Automatic uplink interval in seconds (0 = off). Stored outside active_cfg blob. */
uint32_t rak3162_storage_get_send_interval_s(void);
int rak3162_storage_set_send_interval_s(uint32_t interval_s);

/** Bind storage backend into the RAK AT framework. */
void rak3162_storage_bind_at_cfg(void);

#endif /* RAK3162_STORAGE_H_ */
