/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350 SoC's half of what both images share (src/hal/cortex-m/
 * hal_early.h): the clocks, the watchdog, and the boot ROM's IMAGE_DEF block.
 * tfw-boot links this file as well (mk/boot.mk), so each exists once.
 *
 * Clocks. tfw_cm_soc_early runs before .data and .bss exist, so it is
 * register writes and polls only. It starts the 12 MHz crystal and runs
 * clk_ref, clk_sys and clk_peri from it undivided, which is TFW_CM_CPU_HZ in
 * board_profile.h. No PLL: nothing here needs the speed, and a crystal clock
 * is the one the UART's divisors and the watchdog's tick can both count on.
 * Each step writes the state it wants rather than toggling, so the second
 * run, the application's after tfw-boot's, changes nothing.
 *
 * Watchdog. The tick generator divides clk_ref to 1 us and the watchdog
 * counts LOAD down once a tick; at zero it resets what PSM.WDSEL names. WDSEL
 * is everything but the two oscillators, so the reset does not stop the clock
 * it runs from. A pet reloads the count. The period is 8 s, the same two AVR
 * watchdog periods mps2-tz resets after (boot/boot_hal.h's trial deadline).
 *
 * IMAGE_DEF. The boot ROM boots an image only if a valid block sits in its
 * first 4 KiB; the linker script keeps .image_def right after the vector
 * table. Its words are in soc_calc.h, pinned by a host test.
 */

#include <stdint.h>

#include "tfw/config.h"
#include "hal_early.h"
#include "soc_regs.h"
#include "soc_calc.h"
#include "soc_reset.h"

#define XOSC_HZ        12000000uL
#define WDOG_PERIOD_US 8000000uL

_Static_assert(TFW_CM_CPU_HZ == XOSC_HZ,
               "clk_sys is the crystal, undivided");
_Static_assert(WDOG_PERIOD_US <= 0xFFFFFFuL, "the watchdog's LOAD is 24 bits");
_Static_assert(((XOSC_HZ / 1000u) + 128u) / 256u * TFW_RP_XOSC_DELAY_MULT
                   <= TFW_RP_XOSC_DELAY_MAX,
               "XOSC STARTUP.DELAY is 14 bits");

__attribute__((section(".image_def"), used))
const uint32_t tfw_rp_image_def[TFW_RP_IMAGE_DEF_WORDS] = TFW_RP_IMAGE_DEF_INIT;

void tfw_cm_soc_early(void)
{
    TFW_RP_CLR(TFW_RP_WATCHDOG_BASE) = TFW_RP_WATCHDOG_CTRL_ENABLE;

    TFW_RP_XOSC_STARTUP = tfw_rp_xosc_startup(XOSC_HZ);
    TFW_RP_XOSC_CTRL    = TFW_RP_XOSC_CTRL_1_15MHZ | TFW_RP_XOSC_CTRL_ENABLE;
    while ((TFW_RP_XOSC_STATUS & TFW_RP_XOSC_STATUS_STABLE) == 0u) {
    }

    /* clk_sys onto clk_ref first, so clk_ref can move under it glitch-free. */
    TFW_RP_CLK_SYS_CTRL = TFW_RP_CLK_SYS_SRC_REF;
    while (TFW_RP_CLK_SYS_SELECTED != (1u << TFW_RP_CLK_SYS_SRC_REF)) {
    }
    TFW_RP_CLK_REF_CTRL = TFW_RP_CLK_REF_SRC_XOSC;
    while (TFW_RP_CLK_REF_SELECTED != (1u << TFW_RP_CLK_REF_SRC_XOSC)) {
    }
    TFW_RP_CLK_REF_DIV  = TFW_RP_CLK_DIV_1;
    TFW_RP_CLK_SYS_DIV  = TFW_RP_CLK_DIV_1;
    TFW_RP_CLK_PERI_DIV = TFW_RP_CLK_DIV_1;
    TFW_RP_CLK_PERI_CTRL = TFW_RP_CLK_PERI_ENABLE;

    TFW_RP_TICKS_WD_CYCLES = XOSC_HZ / 1000000uL;
    TFW_RP_TICKS_WD_CTRL   = TFW_RP_TICKS_ENABLE;

    /* The GPIO blocks every pin and the UART need. */
    TFW_RP_CLR(TFW_RP_RESETS_RESET) = TFW_RP_RESET_IO_BANK0 | TFW_RP_RESET_PADS_BANK0;
    while ((TFW_RP_RESETS_DONE & (TFW_RP_RESET_IO_BANK0 | TFW_RP_RESET_PADS_BANK0))
           != (TFW_RP_RESET_IO_BANK0 | TFW_RP_RESET_PADS_BANK0)) {
    }
}

void tfw_cm_soc_watchdog_arm(void)
{
    tfw_cm_reset_mark    = TFW_CM_MARK_WATCHDOG;
    TFW_RP_PSM_WDSEL     = TFW_RP_PSM_WDSEL_ALL & ~TFW_RP_PSM_WDSEL_OSC;
    TFW_RP_WATCHDOG_LOAD = WDOG_PERIOD_US;
    TFW_RP_WATCHDOG_CTRL = TFW_RP_WATCHDOG_CTRL_ENABLE | TFW_RP_WATCHDOG_CTRL_PAUSE;
}

void tfw_cm_soc_watchdog_pet(void)
{
    TFW_RP_WATCHDOG_LOAD = WDOG_PERIOD_US;
}

/* WATCHDOG.REASON and POWMAN.CHIP_RESET, decoded by soc_reset.c. */
tfw_reset_cause_t tfw_cm_soc_reset_cause(uint32_t mark)
{
    return tfw_rp2350_reset_take(&TFW_RP_WATCHDOG_REASON, &TFW_RP_POWMAN_CHIP_RESET,
                                 mark == TFW_CM_MARK_RESETREQ ? 1u : 0u);
}
