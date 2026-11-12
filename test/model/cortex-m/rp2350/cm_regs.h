/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's src/hal/cortex-m/cm_regs.h for the RP2350 flash EEPROM test
 * (test/unit/test_rp2350_eeprom.c): the names hal_eeprom.c and soc_regs.h
 * use, with the register access going to the model's registers and the
 * PRIMASK instructions to the model's mask (nor_model.c). mk/host.mk puts
 * this directory ahead of src/hal/cortex-m/ for that test only.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>

volatile uint32_t *tfw_rp_model_reg(uintptr_t addr);
uint32_t           tfw_rp_model_irq_save(void);
void               tfw_rp_model_irq_restore(uint32_t primask);

#define TFW_CM_REG(addr) (*tfw_rp_model_reg((uintptr_t)(addr)))

static inline uint32_t tfw_cm_irq_save(void) { return tfw_rp_model_irq_save(); }

static inline void tfw_cm_irq_restore(uint32_t primask)
{
    tfw_rp_model_irq_restore(primask);
}

#endif /* TFW_CM_REGS_H */
