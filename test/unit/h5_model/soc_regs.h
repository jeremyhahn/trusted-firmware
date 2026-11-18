/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 SoC registers src/crypto/stm32h5/provider.c names, pointed at
 * test/unit/test_h5_pka.c's model (#544): the provider's PKA base lands on the
 * model's anchor, its RCC on the model's RCC block, and AHB2ENR is a variable.
 */
#ifndef TFW_TEST_H5_MODEL_SOC_REGS_H
#define TFW_TEST_H5_MODEL_SOC_REGS_H

#include <stdint.h>

extern uint8_t           regs_anchor[];
extern volatile uint32_t h5_model_ahb2enr;

#define TFW_H5_GPIOA_BASE  ((uintptr_t)regs_anchor - 0xA2000u)
#define TFW_H5_RCC_BASE    ((uintptr_t)regs_anchor + 0x10000u)
#define TFW_H5_RCC_AHB2ENR h5_model_ahb2enr

#endif
