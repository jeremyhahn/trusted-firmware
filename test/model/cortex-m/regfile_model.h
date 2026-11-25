/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A register file for host tests built with -DTFW_CM_MODEL_REGFILE: the
 * target of cm_regs.h's TFW_CM_REG. A word for each address the SoC files
 * touch, zero until written, as the registers the ports use reset.
 */
#ifndef TFW_REGFILE_MODEL_H
#define TFW_REGFILE_MODEL_H

#include <stdint.h>

/* Every word back to zero and unallocated. */
void regfile_model_reset(void);

/* The word at addr, zero if nothing wrote it; allocates nothing. */
uint32_t regfile_model_peek(uintptr_t addr);

/* Set the word at addr to val, allocating it as TFW_CM_REG would. */
void regfile_model_poke(uintptr_t addr, uint32_t val);

/* From the n-th lookup of addr on (counted from this call), the word at
 * addr reads 0. regfile_model_reset disarms it. */
void regfile_model_clear_after(uintptr_t addr, unsigned n);

#endif /* TFW_REGFILE_MODEL_H */
