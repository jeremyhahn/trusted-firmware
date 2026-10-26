/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MCXN94x's half of what both images share (src/hal/cortex-m/
 * hal_early.h): the clock tree, the RAM, the pin banks and the windowed
 * watchdog WWDT0. tfw-boot will link this file as well (mk/boot.mk), so the
 * clock comes up once per image, the same way in both.
 *
 * tfw_cm_soc_early runs before .data and .bss exist, so it touches registers
 * and nothing else:
 *
 *   1. RAM ECC off, the SDK's SystemInit step: the ECC words become RAM and
 *      the banks give the 312 KiB profile.mk counts. Writing it again in the
 *      second image is a no-op, and the handoff block survives it.
 *   2. FRO12M as main clock with the AHB divider at 1: the SDK's
 *      BOARD_BootClockFRO12M, less its regulator, flash wait-state and SRAM
 *      voltage steps, which keep their reset values. Those values are the
 *      ones a 12 MHz core runs at; that is to confirm against the reference
 *      manual's power chapter (docs/boards/frdm-mcxn947.md).
 *   3. The five pin controllers and GPIO banks clocked and out of reset, so
 *      hal_gpio.c and hal_uart.c only ever program pins.
 *
 * What SystemInit also does and this file does not: it turns the voltage and
 * clock glitch detectors off. They stay on. Nor does anything here touch the
 * EdgeLock subsystem, a fuse, or core 1.
 *
 * The watchdog. WWDT0 counts TC down at the 1 MHz FRO through its own fixed
 * divide-by-4 (the prescaler is to confirm), so 2,000,000 counts are 8 s, the
 * AVR's longest period, which the trial's deadline is written against
 * (boot/boot_hal.h). With WDRESET set, an underflow resets the chip. Once
 * WDEN is set only a reset clears it, so there is no disarm: a chip reset
 * clears the watchdog, and a fresh boot starts with it off. A feed is the two
 * writes 0xAA, 0x55 with no other WWDT access between them, so it runs with
 * interrupts masked.
 */

#include <stdint.h>

#include "tfw/config.h"
#include "hal_early.h"
#include "soc_regs.h"
#include "soc_reset.h"

#define WWDT_TC_COUNT 2000000uL

_Static_assert(WWDT_TC_COUNT <= TFW_MCXN_WWDT_TC_MAX, "WWDT0's TC is 24 bits");
_Static_assert(TFW_CM_CPU_HZ == 12000000uL,
               "hal_soc.c runs the core from FRO12M: TFW_CM_CPU_HZ must say 12 MHz");

void tfw_cm_soc_early(void)
{
    uint32_t banks = 0u;

    TFW_MCXN_ECC_ENABLE_CTRL = 0u;

    TFW_MCXN_AHBCLKCTRLSET(2) = TFW_MCXN_CTRL2_SCG;
    TFW_MCXN_SCG_SIRCCSR |= TFW_MCXN_SCG_SIRC_PERIPH_EN;
    TFW_MCXN_SCG_RCCR = (TFW_MCXN_SCG_RCCR & ~TFW_MCXN_SCG_SCS_MASK) |
                        (TFW_MCXN_SCG_SCS_SIRC << TFW_MCXN_SCG_SCS_SHIFT);
    while ((TFW_MCXN_SCG_CSR & TFW_MCXN_SCG_SCS_MASK) !=
           (TFW_MCXN_SCG_SCS_SIRC << TFW_MCXN_SCG_SCS_SHIFT)) {
    }
    TFW_MCXN_AHBCLKDIV = TFW_MCXN_CLKDIV_RESET | TFW_MCXN_CLKDIV_HALT;
    TFW_MCXN_AHBCLKDIV = 0u;

    for (uint32_t n = 0u; n < TFW_MCXN_PORT_COUNT; n++) {
        banks |= TFW_MCXN_CTRL0_PORT(n) | TFW_MCXN_CTRL0_GPIO(n);
    }
    TFW_MCXN_AHBCLKCTRLSET(0) = banks;
    TFW_MCXN_PRESETCTRLCLR(0) = banks;
}

static void wwdt_feed(void)
{
    uint32_t primask = tfw_cm_irq_save();

    TFW_MCXN_WWDT_FEED = TFW_MCXN_WWDT_FEED_1;
    TFW_MCXN_WWDT_FEED = TFW_MCXN_WWDT_FEED_2;
    tfw_cm_irq_restore(primask);
}

void tfw_cm_soc_watchdog_arm(void)
{
    tfw_cm_reset_mark = TFW_CM_MARK_WATCHDOG;
    TFW_MCXN_CLOCK_CTRL |= TFW_MCXN_CLOCK_CTRL_FRO1MHZ_ENA | TFW_MCXN_CLOCK_CTRL_FRO1MHZ_CLK_ENA;
    TFW_MCXN_AHBCLKCTRLSET(0) = TFW_MCXN_CTRL0_WWDT0;
    TFW_MCXN_WDT0CLKDIV = TFW_MCXN_CLKDIV_RESET | TFW_MCXN_CLKDIV_HALT;
    TFW_MCXN_WDT0CLKDIV = 0u;
    TFW_MCXN_WWDT_TC  = WWDT_TC_COUNT;
    TFW_MCXN_WWDT_MOD = TFW_MCXN_WWDT_MOD_WDEN | TFW_MCXN_WWDT_MOD_WDRESET;
    wwdt_feed();
}

void tfw_cm_soc_watchdog_pet(void)
{
    wwdt_feed();
}

/* CMC0's SRS, decoded by soc_reset.c. */
tfw_reset_cause_t tfw_cm_soc_reset_cause(uint32_t mark)
{
    return tfw_mcxn_reset_take(&TFW_MCXN_CMC_SRS, mark == TFW_CM_MARK_RESETREQ ? 1u : 0u);
}
