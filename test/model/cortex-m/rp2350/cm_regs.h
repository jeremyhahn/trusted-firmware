/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Host stand-in for src/hal/cortex-m/cm_regs.h, for the RP2350 HAL tests.
 *
 * Found ahead of the real header by -Itest/model/cortex-m/rp2350 in
 * mk/host.mk, so soc_regs.h and the HAL sources compile unchanged and every
 * register access lands in rp2350_model.c. Only what the RP2350 sources
 * under test reach is defined here.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>
#include "rp2350_model.h"

#define TFW_CM_REG(addr) (*tfw_rp_model_reg((uintptr_t)(addr)))

#endif /* TFW_CM_REGS_H */
