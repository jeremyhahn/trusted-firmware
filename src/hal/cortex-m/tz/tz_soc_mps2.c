/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * mps2-an505's opening for TZ=1 (#590), as QEMU models the AN505 and its
 * SSE-200 (hw/arm/mps2-tz.c, armsse.c, tz-mpc.c, iotkit-secctl.c):
 *
 *   - the SSRAM-0 MPC marks the blocks of the NS code and NS RAM regions
 *     Non-secure (its LUT resets all Secure);
 *   - the Secure Privilege Control block's NSCCFG.CODENSC lets the code
 *     region's Secure alias be NSC, without which the IDAU's Secure would
 *     outrank the SAU's NSC and an SG from Non-secure code would fault;
 *   - the expansion APB PPCs 1-3, where UART0 sits, are made Non-secure.
 *     PPC 0, the MPCs' own registers, stays Secure.
 */
#include <stdint.h>
#include "tz_s.h"

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define MPS2_SPCTRL          0x50080000u
#define SPCTRL_NSCCFG        REG(MPS2_SPCTRL + 0x014u)
#define SPCTRL_APBNSPPCEXP(n) REG(MPS2_SPCTRL + 0x080u + 4u * (n))

#define SSRAM0_MPC           0x58007000u
#define MPC_BLK_CFG          REG(SSRAM0_MPC + 0x014u)
#define MPC_BLK_IDX          REG(SSRAM0_MPC + 0x018u)
#define MPC_BLK_LUT          REG(SSRAM0_MPC + 0x01Cu)

/* SSRAM-0's Non-secure alias base; the MPC indexes blocks from it. */
#define SSRAM0_NS_BASE       0x00000000u

static void mpc_open(uint32_t base, uint32_t size)
{
    uint32_t shift = MPC_BLK_CFG + 5u;
    uint32_t b = (base - SSRAM0_NS_BASE) >> shift;
    uint32_t end = (base - SSRAM0_NS_BASE + size - 1u) >> shift;

    for (; b <= end; b++) {
        uint32_t lut;
        MPC_BLK_IDX = b / 32u;
        lut = MPC_BLK_LUT;
        MPC_BLK_IDX = b / 32u;
        MPC_BLK_LUT = lut | (1u << (b % 32u));
    }
}

void tz_soc_open(void)
{
    uint32_t n;

    mpc_open(TZ_NS_CODE_BASE, TZ_NS_CODE_SIZE);
    mpc_open(TZ_NS_RAM_BASE, TZ_NS_RAM_SIZE);
    SPCTRL_NSCCFG |= 1u;   /* CODENSC */
    for (n = 1u; n <= 3u; n++)
        SPCTRL_APBNSPPCEXP(n) = 0xFFFFu;
    __asm volatile("dsb\n\tisb" ::: "memory");
}
