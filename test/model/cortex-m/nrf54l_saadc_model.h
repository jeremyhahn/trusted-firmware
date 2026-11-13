/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SAADC register model's stand-in for cm_regs.h, for
 * test/unit/test_nrf54l_saadc.c: it takes cm_regs.h's include guard, so the
 * driver's own header is skipped, and sends TFW_CM_REG to saadc_model_reg,
 * which the test defines. Included once, ahead of hal_adc.c.
 */
#ifndef TFW_NRF54L_SAADC_MODEL_H
#define TFW_NRF54L_SAADC_MODEL_H

#include <stdint.h>

#define TFW_CM_REGS_H
static volatile uint32_t *saadc_model_reg(uintptr_t addr);
#define TFW_CM_REG(addr) (*saadc_model_reg((uintptr_t)(addr)))

#endif /* TFW_NRF54L_SAADC_MODEL_H */
