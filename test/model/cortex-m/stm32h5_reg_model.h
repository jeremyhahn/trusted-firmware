/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Host substitute for the Cortex-M register header, for the stm32h5 tests
 * that model the part's registers (test/unit/test_stm32h5_*.c). It sits in
 * test/model/cortex-m/, the one place lint-arch-leak lets spell TFW_CM_REG.
 * The including test declares or defines `static volatile uint32_t
 * *model_reg(uintptr_t addr)` before this header and includes the driver
 * after it, so the driver's every register access goes to the model.
 */
#include <stdint.h>

#define TFW_CM_REGS_H
#define TFW_CM_REG(addr) (*model_reg((uintptr_t)(addr)))
