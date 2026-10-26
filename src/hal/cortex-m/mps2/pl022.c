/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The shield 0 SPI's registers (pl022.h): the PL022 at the base the board
 * profile states, TFW_MPS2_SPI_BASE, and the FPGA I/O block's MISC register,
 * which carries the chip select. Linked only beside hal_bus.c, when a bus is on
 * (mk/arch/cortex-m.mk ARCH_BUS_SRCS).
 */
#include <stdint.h>

#include "soc_regs.h"
#include "pl022.h"

uint32_t tfw_mps2_ssp_read(uint32_t off)
{
    return TFW_MPS2_SSP(off);
}

void tfw_mps2_ssp_write(uint32_t off, uint32_t v)
{
    TFW_MPS2_SSP(off) = v;
}

uint32_t tfw_mps2_misc_read(void)
{
    return TFW_MPS2_FPGAIO_MISC;
}

void tfw_mps2_misc_write(uint32_t v)
{
    TFW_MPS2_FPGAIO_MISC = v;
}
