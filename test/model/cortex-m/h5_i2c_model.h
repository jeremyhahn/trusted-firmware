/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's cm_regs.h for the STM32H5 I2C test (test/unit/test_stm32h5_i2c.c):
 * included ahead of src/hal/cortex-m/stm32h5/hal_bus.c, it takes cm_regs.h's
 * guard and sends every register access to the test's model of I2C4, so the
 * driver compiles unchanged. hal_bus.c reaches registers only through
 * TFW_CM_REG; a name it starts to use from cm_regs.h is a compile error here,
 * which is how a drift shows.
 */
#ifndef TFW_TEST_H5_I2C_MODEL_H
#define TFW_TEST_H5_I2C_MODEL_H

#include <stdint.h>

/* The model's cell for one access to addr; the next call commits it. */
volatile uint32_t *tfw_h5_i2c_model_reg(uintptr_t addr);

#define TFW_CM_REGS_H
#define TFW_CM_REG(addr) (*tfw_h5_i2c_model_reg((uintptr_t)(addr)))

#endif /* TFW_TEST_H5_I2C_MODEL_H */
