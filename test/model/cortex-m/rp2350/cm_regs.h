/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's src/hal/cortex-m/cm_regs.h for the RP2350 converter test: every
 * register access goes to the register model (test/model/rp2350_model.h).
 * mk/host.mk puts this directory ahead of src/hal/cortex-m/ for that test, so
 * soc_regs.h includes this file and hal_adc.c compiles unchanged. Only the
 * names the RP2350 SoC header and hal_adc.c use are here; a name they come to
 * need is a compile error in the host test, which is how a drift shows.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>

#include "../../rp2350_model.h"

#define TFW_CM_REG(addr) (*tfw_rp2350_model_reg((uintptr_t)(addr)))

#endif /* TFW_CM_REGS_H */
