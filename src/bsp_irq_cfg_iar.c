
#include <bsp_api.h>

void bsp_irq_cfg(void)
{
	volatile uint32_t *icu_security_attribution = &R_CPSCU->ICUSARG;

	R_BSP_RegisterProtectDisable(BSP_REG_PROTECT_SAR);

	for (uint32_t word = 0U; word < BSP_ICU_VECTOR_MAX_ENTRIES / 32U; word++) {
		icu_security_attribution[word] = 0U;
		NVIC->ITNS[word] = 0U;
	}

	R_BSP_RegisterProtectEnable(BSP_REG_PROTECT_SAR);
}