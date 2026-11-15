/*
 * TFW_CM_REG's target for test/unit/test_mps2_gpio_line.c (built with
 * -DTFW_CM_MODEL_REGFILE): the suite models GPIO block 0's line itself, and
 * the tfw_cm_* name lives here, under test/model/cortex-m/, as lint-arch-leak
 * requires.
 */
#include <stdint.h>

#include "cm_regs.h"

volatile uint32_t *mps2_gpio_line_model_reg(uintptr_t addr);

volatile uint32_t *tfw_cm_model_reg(uintptr_t addr)
{
    return mps2_gpio_line_model_reg(addr);
}
