/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SBCon registers (sbcon.h), at the base the board profile states:
 * TFW_MPS2_I2C_BASE, the shield 0 SBCon of the board's FPGA image. Linked only
 * beside hal_bus.c, when a bus is on (mk/arch/cortex-m.mk ARCH_BUS_SRCS).
 */
#include <stdint.h>

#include "soc_regs.h"
#include "sbcon.h"

uint32_t tfw_mps2_sb_lines(void)
{
    return TFW_MPS2_SB_CONTROL & (TFW_SB_SCL | TFW_SB_SDA);
}

void tfw_mps2_sb_release(uint32_t mask)
{
    TFW_MPS2_SB_CONTROLS = mask;
}

void tfw_mps2_sb_drive_low(uint32_t mask)
{
    TFW_MPS2_SB_CONTROLC = mask;
}
