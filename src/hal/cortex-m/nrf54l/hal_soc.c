/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nrf54l SoC's half of what both images share (src/hal/cortex-m/
 * hal_early.h): the reset-time setup the vendor's SystemInit does, the
 * watchdog, the reset cause (decoded by soc_reset.c) and the vector table's
 * tail.
 *
 * tfw_cm_soc_early runs before .data and .bss exist, so it touches no global.
 * In order it
 *   1. stops a running watchdog. This port does not assume a software reset
 *      stops WDT30 (on Nordic's earlier parts the watchdog runs on through
 *      one), and its registers are locked while it runs, so a trial's
 *      watchdog would outlive the image that armed it.
 *      tfw_cm_soc_watchdog_arm sets STOPEN, so the stop below takes. A
 *      watchdog some other image started without STOPEN cannot be stopped;
 *      it is reloaded instead, which gives this image one full period to arm
 *      and pet it.
 *   2. does what the nrfx MDK's SystemInit (mdk/system_nrf54l.c) does for a
 *      Secure nRF54L15 image, less what belongs to the TrustZone and KMU
 *      slice (the SAU, approtect and the tamper controller): the PLL at
 *      128 MHz, the factory trims copied from FICR, the device configuration
 *      word, errata 32, 40 and 31 on the first silicon revision, and the
 *      RESETREAS pin-reset fold.
 *   3. starts the high-frequency crystal and does not wait: the UARTE runs
 *      from the internal oscillator until it is up, as the vendor's drivers
 *      do.
 *
 * The watchdog counts the 32.768 kHz clock down from CRV and resets the
 * system at zero; a pet reloads it. An image that stops petting is reset 8 s
 * after the last pet, as on mps2-tz (two of its 4 s periods), which the
 * trial's deadline is written against (boot/boot_hal.h).
 */

#include <stdint.h>

#include "tfw/config.h"
#include "hal_early.h"
#include "soc_regs.h"
#include "soc_reset.h"
#include "soc_tables.h"

#define WDOG_TIMEOUT_S 8uL
#define WDOG_CRV       (WDOG_TIMEOUT_S * TFW_NRF_WDT_LFCLK_HZ - 1uL)

/* The stop takes effect within a few cycles of the 32.768 kHz clock; this
 * bound is far longer at 128 MHz and only keeps a watchdog that will not stop
 * from hanging the reset. */
#define WDOG_STOP_SPINS 100000u

_Static_assert(WDOG_CRV >= 0xFuL, "CRV's minimum is 0xF");

static void watchdog_stop(void)
{
    if ((TFW_NRF_WDT_RUNSTATUS & 1u) == 0u) {
        return;
    }
    TFW_NRF_WDT_TSEN = TFW_NRF_WDT_KEY;
    TFW_NRF_WDT_STOP = 1u;
    for (uint32_t i = 0u; i < WDOG_STOP_SPINS; i++) {
        if ((TFW_NRF_WDT_RUNSTATUS & 1u) == 0u) {
            TFW_NRF_WDT_EV_STOPPED = 0u;
            TFW_NRF_WDT_EV_TIMEOUT = 0u;
            return;
        }
    }
    TFW_NRF_WDT_RR0 = TFW_NRF_WDT_KEY;
}

/* The FICR trim table: { ADDR, DATA } pairs, ended by an erased or zero
 * address. */
static void copy_trims(void)
{
    for (uint32_t i = 0u; i < TFW_NRF_FICR_TRIMCNF_COUNT; i++) {
        uint32_t addr = TFW_CM_REG(TFW_NRF_FICR_TRIMCNF + 8u * i);

        if (addr == 0xFFFFFFFFuL || addr == 0u) {
            return;
        }
        TFW_CM_REG(addr) = TFW_CM_REG(TFW_NRF_FICR_TRIMCNF + 8u * i + 4u);
    }
}

void tfw_cm_soc_early(void)
{
    watchdog_stop();

    TFW_NRF_PLL_FREQ = TFW_NRF_PLL_FREQ_CK128M;
    copy_trims();

    /* "Device configuration for ES PDK", unconditional on the nRF54L15. */
    if (TFW_CM_REG(0x50120440u) == 0u) {
        TFW_CM_REG(0x50120440u) = 0xC8u;
    }

    if (tfw_nrf54l_is_rev1(TFW_NRF_FICR_PART, TFW_NRF_FICR_REVISION)) {
        if (TFW_NRF_FICR_ERRATA32_TRIM <= 0x180A1D00uL) {        /* errata 32 */
            TFW_CM_REG(0x50120640u) = 0x1EA9E040uL;
        }
        TFW_CM_REG(0x5008A7ACu) = 0x040A0078uL;                  /* errata 40 */
        TFW_CM_REG(0x50120624u) = 20u | (1u << 5);               /* errata 31 */
        TFW_CM_REG(0x5012063Cu) &= ~(uint32_t)(1u << 19);
    }

    /* A pin reset says so alone: the other reasons it latched are stale. */
    if ((TFW_NRF_RESETREAS & TFW_NRF_RESETREAS_RESETPIN) != 0u) {
        TFW_NRF_RESETREAS = ~(uint32_t)TFW_NRF_RESETREAS_RESETPIN;
    }

    TFW_NRF_CLOCK_XOSTART = 1u;
}

void tfw_cm_soc_watchdog_arm(void)
{
    tfw_cm_reset_mark = TFW_CM_MARK_WATCHDOG;
    if ((TFW_NRF_WDT_RUNSTATUS & 1u) != 0u) {
        /* Armed already, and locked: arming again is a pet. */
        TFW_NRF_WDT_RR0 = TFW_NRF_WDT_KEY;
        return;
    }
    TFW_NRF_WDT_INTEN  = 0u;
    TFW_NRF_WDT_NMIEN  = 0u;
    TFW_NRF_WDT_CRV    = WDOG_CRV;
    TFW_NRF_WDT_RREN   = TFW_NRF_WDT_RREN_RR0;
    TFW_NRF_WDT_CONFIG = TFW_NRF_WDT_CONFIG_SLEEP | TFW_NRF_WDT_CONFIG_STOPEN;
    TFW_NRF_WDT_START  = 1u;
}

void tfw_cm_soc_watchdog_pet(void)
{
    TFW_NRF_WDT_RR0 = TFW_NRF_WDT_KEY;
}

/* RESETREAS is the cause (soc_reset.c); the marker only says whether a
 * reset request was the firmware's own. */
tfw_reset_cause_t tfw_cm_soc_reset_cause(uint32_t mark)
{
    return tfw_nrf54l_reset_take(&TFW_NRF_RESETREAS,
                                 mark == TFW_CM_MARK_RESETREQ ? 1u : 0u);
}

/* ---- the vector table's tail
 *
 * The core's table (src/hal/cortex-m/hal_early.c, 96 words) ends at IRQ 79;
 * the console UARTE is IRQ 198. These are IRQs 80-198, placed straight after it by
 * mk/arch/cortex-m/nrf54l.ld, which asserts the two meet. Only the sources
 * this port enables have an entry; the rest are 0, and nothing enables them.
 * An entry is weak, as the core's are: the driver that owns the interrupt
 * defines it, and a build without that driver parks here. */
typedef void (*soc_vector_t)(void);

void tfw_nrf54l_unhandled(void);
void tfw_nrf54l_unhandled(void)
{
    for (;;) { }
}

void tfw_cm_irq198(void) __attribute__((weak, alias("tfw_nrf54l_unhandled")));

#define SOC_IRQ_FIRST 80u
#define SOC_IRQ_LAST  TFW_NRF_UARTE_IRQ

const soc_vector_t tfw_nrf54l_vectors[SOC_IRQ_LAST - SOC_IRQ_FIRST + 1u]
    __attribute__((section(".vectors.soc"), used)) = {
    [TFW_NRF_UARTE_IRQ - SOC_IRQ_FIRST] = tfw_cm_irq198,
};
