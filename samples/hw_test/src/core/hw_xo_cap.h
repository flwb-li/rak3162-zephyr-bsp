/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HW_XO_CAP_H_
#define HW_XO_CAP_H_

#include <stdint.h>

#include "storage/hw_storage.h"

/** HFXO: 4000–17000 fF, step 250. LFXO: 4000–18000 fF, step 500. */

int hw_hfxo_cap_apply_ff(uint32_t femtofarads);
uint32_t hw_hfxo_cap_default_ff(void);

int hw_lfxo_cap_apply_ff(uint32_t femtofarads);
uint32_t hw_lfxo_cap_default_ff(void);

/** Apply persisted HFXO/LFXO load caps from @p cfg. */
void hw_runtime_apply_stored_caps(const struct hw_runtime_cfg *cfg);

#endif /* HW_XO_CAP_H_ */
