/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host's src/hal/cortex-m/cm_regs.h for the MPS2 bus registers: every
 * TFW_CM_REG goes to the MPS2 bus model (test/model/mps2_bus_model.h). mk/host.mk
 * puts this directory ahead of src/hal/cortex-m/ when it compiles the image's
 * sbcon.c, pl022.c and hal_adc.c for the host, so mps2/soc_regs.h includes this
 * file and those units compile unchanged.
 *
 * Only TFW_CM_REG: the bus units need nothing else from the register header,
 * and a name missing here is a compile error, which is how a unit reaching for
 * the NVIC or a barrier would show.
 */
#ifndef TFW_CM_REGS_H
#define TFW_CM_REGS_H

#include <stdint.h>

#include "../../mps2_bus_model.h"

#define TFW_CM_REG(addr) (*tfw_mps2_model_reg((uint32_t)(addr)))

#endif /* TFW_CM_REGS_H */
