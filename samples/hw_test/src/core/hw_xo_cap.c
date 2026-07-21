#include "core/hw_xo_cap.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hw_xo_cap, LOG_LEVEL_INF);

#if defined(CONFIG_SOC_SERIES_NRF54LX)

#include <hal/nrf_oscillators.h>
#include <soc.h>

#define HFXO_NODE DT_NODELABEL(hfxo)
#define LFXO_NODE DT_NODELABEL(lfxo)

static void ficr_trim_slope_offset(uint32_t trim, uint32_t slope_msk, uint32_t slope_pos,
				   uint32_t offset_msk, uint32_t offset_pos, int32_t *slope,
				   uint32_t *offset)
{
	uint32_t slope_field = (trim & slope_msk) >> slope_pos;
	uint32_t slope_mask = slope_msk >> slope_pos;
	uint32_t slope_sign = slope_mask - (slope_mask >> 1);

	*slope = (int32_t)(slope_field ^ slope_sign) - (int32_t)slope_sign;
	*offset = (trim & offset_msk) >> offset_pos;
}

static int hfxo_ff_to_intcap(uint32_t femtofarads, uint32_t *intcap_out)
{
	int32_t slope_m;
	uint32_t offset_m;
	uint32_t mid_val;

	ficr_trim_slope_offset(NRF_FICR->XOSC32MTRIM, FICR_XOSC32MTRIM_SLOPE_Msk,
			       FICR_XOSC32MTRIM_SLOPE_Pos, FICR_XOSC32MTRIM_OFFSET_Msk,
			       FICR_XOSC32MTRIM_OFFSET_Pos, &slope_m, &offset_m);

	mid_val = (((femtofarads - 5500UL) * (uint32_t)(slope_m + 791UL)) +
		   (offset_m << 2UL) * 1000UL) >>
		  8UL;
	*intcap_out = mid_val / 1000UL;
	if ((mid_val % 1000UL) >= 500UL) {
		(*intcap_out)++;
	}
	return 0;
}

static int lfxo_ff_to_intcap(uint32_t femtofarads, uint32_t *intcap_out)
{
	int32_t slope_k;
	uint32_t offset_k;
	uint32_t mid_val;

	ficr_trim_slope_offset(NRF_FICR->XOSC32KTRIM, FICR_XOSC32KTRIM_SLOPE_Msk,
			       FICR_XOSC32KTRIM_SLOPE_Pos, FICR_XOSC32KTRIM_OFFSET_Msk,
			       FICR_XOSC32KTRIM_OFFSET_Pos, &slope_k, &offset_k);

	mid_val = (2UL * femtofarads - 12000UL) * (uint32_t)(slope_k + 392UL) +
		  (offset_k << 3UL) * 1000UL;
	*intcap_out = mid_val / 512000UL;
	if (mid_val % 512000UL >= 256000UL) {
		(*intcap_out)++;
	}
	return 0;
}

static int validate_ff(uint32_t femtofarads, uint32_t min_ff, uint32_t max_ff, uint32_t step_ff)
{
	if ((femtofarads < min_ff) || (femtofarads > max_ff)) {
		return -EINVAL;
	}
	if (((femtofarads - min_ff) % step_ff) != 0U) {
		return -EINVAL;
	}
	return 0;
}

uint32_t hw_hfxo_cap_default_ff(void)
{
#if DT_NODE_HAS_PROP(HFXO_NODE, load_capacitance_femtofarad)
	return (uint32_t)DT_PROP(HFXO_NODE, load_capacitance_femtofarad);
#else
	return 12000U;
#endif
}

int hw_hfxo_cap_apply_ff(uint32_t femtofarads)
{
	uint32_t intcap;
	int ret;

#if !DT_ENUM_HAS_VALUE(HFXO_NODE, load_capacitors, internal)
	return -ENOTSUP;
#endif

	ret = validate_ff(femtofarads, 4000U, 17000U, 250U);
	if (ret != 0) {
		return ret;
	}

	(void)hfxo_ff_to_intcap(femtofarads, &intcap);
	nrf_oscillators_hfxo_cap_set(NRF_OSCILLATORS, true, intcap);
	return 0;
}

uint32_t hw_lfxo_cap_default_ff(void)
{
#if DT_NODE_HAS_PROP(LFXO_NODE, load_capacitance_femtofarad)
	return (uint32_t)DT_PROP(LFXO_NODE, load_capacitance_femtofarad);
#else
	return 16000U;
#endif
}

int hw_lfxo_cap_apply_ff(uint32_t femtofarads)
{
	uint32_t intcap;
	int ret;

#if !DT_ENUM_HAS_VALUE(LFXO_NODE, load_capacitors, internal)
	return -ENOTSUP;
#endif

	ret = validate_ff(femtofarads, 4000U, 18000U, 500U);
	if (ret != 0) {
		return ret;
	}

	(void)lfxo_ff_to_intcap(femtofarads, &intcap);
	nrf_oscillators_lfxo_cap_set(NRF_OSCILLATORS, intcap);
	return 0;
}

#else /* !CONFIG_SOC_SERIES_NRF54LX */

uint32_t hw_hfxo_cap_default_ff(void)
{
	return 12000U;
}

int hw_hfxo_cap_apply_ff(uint32_t femtofarads)
{
	ARG_UNUSED(femtofarads);
	return -ENOTSUP;
}

uint32_t hw_lfxo_cap_default_ff(void)
{
	return 16000U;
}

int hw_lfxo_cap_apply_ff(uint32_t femtofarads)
{
	ARG_UNUSED(femtofarads);
	return -ENOTSUP;
}

#endif /* CONFIG_SOC_SERIES_NRF54LX */

void hw_runtime_apply_stored_caps(const struct hw_runtime_cfg *cfg)
{
	int ret;

	if (cfg == NULL) {
		return;
	}

	if ((cfg->valid_mask & HW_RUNTIME_CFG_VALID_HFXO_CAP) != 0U) {
		ret = hw_hfxo_cap_apply_ff(cfg->hfxo_cap_ff);
		if (ret != 0) {
			LOG_WRN("HFXO cap apply failed: %d (stored %u fF)", ret, cfg->hfxo_cap_ff);
		} else {
			LOG_INF("HFXO load cap applied: %u fF", cfg->hfxo_cap_ff);
		}
	}

	if ((cfg->valid_mask & HW_RUNTIME_CFG_VALID_LFXO_CAP) != 0U) {
		ret = hw_lfxo_cap_apply_ff(cfg->lfxo_cap_ff);
		if (ret != 0) {
			LOG_WRN("LFXO cap apply failed: %d (stored %u fF)", ret, cfg->lfxo_cap_ff);
		} else {
			LOG_INF("LFXO load cap applied: %u fF", cfg->lfxo_cap_ff);
		}
	}
}
