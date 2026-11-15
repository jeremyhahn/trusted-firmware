/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 SoC's half of what both images share (src/hal/cortex-m/
 * hal_early.h): WDOG5, a WDOG32 (soc_regs.h; TO CONFIRM against NXP IMX95RM,
 * the WDOG chapter, and against the System Manager configuration that gives
 * the watchdog to the M7).
 *
 * The watchdog counts up from the 32 kHz LPO divided by 256 and resets the
 * M7 at TOVAL, the first timeout: there is no warning period as on the CMSDK
 * watchdog. So TOVAL is the 8 s after the last pet at which mps2-tz resets,
 * two of its 4 s periods, AVR's longest watchdog period, which the trial's
 * deadline is written against (boot/boot_hal.h).
 *
 * Each configuration is one unlock, then the writes, inside the window the
 * block allows after it, with interrupts off so nothing stretches it. CS keeps
 * UPDATE set so a later configuration is allowed at all: a WDOG32 written
 * without it is fixed until reset. A refresh is one 32-bit store of the
 * refresh key (CS.CMD32EN).
 */

#include <stdint.h>

#include "tfw/config.h"
#include "hal_early.h"
#include "soc_regs.h"
#include "soc_calc.h"

#define WDOG_TIMEOUT_S 8u
#define WDOG_TOVAL     ((TFW_IMX95_WDOG_LPO_HZ / 256u) * WDOG_TIMEOUT_S)

/* A constant, not soc_calc.c's arithmetic: tfw-boot links this file without
 * the rest of the SoC directory (mk/boot.mk's BOOT_SOC_SRCS). */
_Static_assert(WDOG_TOVAL > 0u && WDOG_TOVAL <= 0xFFFFu,
               "the watchdog's timeout is TOVAL's 16 bits");

#define WDOG_CS_BASE (TFW_IMX95_WDOG_CS_UPDATE | TFW_IMX95_WDOG_CS_CLK_LPO | \
                      TFW_IMX95_WDOG_CS_PRES | TFW_IMX95_WDOG_CS_CMD32EN | \
                      TFW_IMX95_WDOG_CS_FLG)

/* A bound on each status wait, so a watchdog the System Manager left locked
 * (UPDATE clear) cannot park the core here: the unlock is then ignored and
 * the configuration with it, which the next reset makes visible. */
#define WDOG_WAIT 1000u

static void wdog_configure(uint32_t cs, uint32_t toval)
{
    uint32_t primask = tfw_cm_irq_save();
    uint32_t n;

    TFW_IMX95_WDOG_CNT = TFW_IMX95_WDOG_UNLOCK;
    for (n = 0u; n < WDOG_WAIT && (TFW_IMX95_WDOG_CS & TFW_IMX95_WDOG_CS_ULK) == 0u; n++) {
    }
    TFW_IMX95_WDOG_TOVAL = toval;
    TFW_IMX95_WDOG_CS    = cs;
    for (n = 0u; n < WDOG_WAIT && (TFW_IMX95_WDOG_CS & TFW_IMX95_WDOG_CS_RCS) == 0u; n++) {
    }
    tfw_cm_irq_restore(primask);
}

void tfw_cm_soc_early(void)
{
    wdog_configure(WDOG_CS_BASE, 0xFFFFu);
}

#if defined(TFW_CM_REG_DUMP) && !defined(TFW_CM_STEP_MARKS)
#error "TFW_CM_REG_DUMP dumps at the step marks: it needs TFW_CM_STEP_MARKS"
#endif

#if defined(TFW_CM_STEP_MARKS)
/* Bench step marks (STEP_MARKS=1 in the board profile; hal_early.h). Each mark
 * goes straight to LPUART7 DATA: TDRE polled, then TC, so a GLOBAL reset that
 * follows (tfw_hal_uart_init) cannot cut it off. With the transmitter off (the
 * reset handler runs before tfw_hal_uart_init), the helper programs BAUD for
 * TFW_UART_BAUD and sets CTRL.TE first. Every poll is bounded: a dead
 * transmitter costs time, not the boot.
 *
 * The pads reach LPUART7 only once the System Manager routes them, in
 * tfw_hal_uart_init, so the marks before it ('R', 'D', 'M') may be lost on the
 * line. The trail keeps the last four in a word outside .data and .bss, and
 * 'P' (the pads requested) is followed by the trail again, in lower case.
 * Marks: R reset entry, D .data/.bss set up, M before main, P pads requested,
 * U tfw_hal_uart_init done. */
#define STEP_POLLS 1000000u

static volatile uint32_t step_trail __attribute__((section(".noinit")));

/* Returns the polls spent waiting for STAT.TDRE (STEP_POLLS: it never set). */
static uint32_t step_send(uint8_t b)
{
    uint32_t n;
    uint32_t waited;

    for (n = 0u; n < STEP_POLLS &&
                 (TFW_IMX95_LPUART_STAT & TFW_IMX95_LPUART_STAT_TDRE) == 0u; n++) { }
    waited = n;
    TFW_IMX95_LPUART_DATA = b;
    for (n = 0u; n < STEP_POLLS &&
                 (TFW_IMX95_LPUART_STAT & TFW_IMX95_LPUART_STAT_TC) == 0u; n++) { }
    return waited;
}

#if defined(TFW_CM_REG_DUMP)
/* Bench register dump (REG_DUMP=1, which sets STEP_MARKS=1; soc_regs.h has
 * the layout): after each mark, LPUART7's registers as the M7 reads them go
 * to memory the System Manager's monitor can read (`md`), since neither it
 * nor the A55 reads the M7's LPUART (bench record sections 11 and 14).
 * Only device memory and locals: it runs before .data and .bss are set up. */
static void step_dump(uint8_t mark, uint32_t waited)
{
    uint32_t rec[TFW_IMX95_REGDUMP_SLOT_WORDS];
    uint32_t n;
    uint32_t w;
    uint32_t i;

    if (mark == (uint8_t)'R') {
        TFW_IMX95_REGDUMP_WORD(0) = TFW_IMX95_REGDUMP_MAGIC;
        TFW_IMX95_REGDUMP_WORD(1) = 0u;
    }
    n      = TFW_IMX95_REGDUMP_WORD(1);
    rec[0] = (uint32_t)mark | (n << 8);
    rec[1] = TFW_IMX95_LPUART_VERID;
    rec[2] = TFW_IMX95_LPUART_PARAM;
    rec[3] = TFW_IMX95_LPUART_GLOBAL;
    rec[4] = TFW_IMX95_LPUART_BAUD;
    rec[5] = TFW_IMX95_LPUART_STAT;
    rec[6] = TFW_IMX95_LPUART_CTRL;
    rec[7] = waited;
    w = TFW_IMX95_REGDUMP_HDR_WORDS + (n % TFW_IMX95_REGDUMP_SLOTS) * TFW_IMX95_REGDUMP_SLOT_WORDS;
    for (i = 0u; i < TFW_IMX95_REGDUMP_SLOT_WORDS; i++) {
        TFW_IMX95_REGDUMP_WORD(w + i) = rec[i];
    }
    TFW_IMX95_REGDUMP_WORD(1) = n + 1u;
}
#define STEP_DUMP(mark, waited) step_dump((mark), (waited))
#else
#define STEP_DUMP(mark, waited) ((void)(waited))
#endif

void tfw_cm_soc_step(uint8_t mark)
{
    uint32_t reg;
    uint32_t t;
    int      i;

    if ((TFW_IMX95_LPUART_CTRL & TFW_IMX95_LPUART_CTRL_TE) == 0u &&
        tfw_imx95_lpuart_baud((uint32_t)TFW_IMX95_LPUART_CLK_HZ,
                              (uint32_t)TFW_UART_BAUD, &reg) == TFW_OK) {
        TFW_IMX95_LPUART_BAUD = reg;
        TFW_IMX95_LPUART_CTRL = TFW_IMX95_LPUART_CTRL_TE;
    }
    STEP_DUMP(mark, step_send(mark));
    if (mark == (uint8_t)'R') {
        step_trail = 0u;
    }
    if (mark == (uint8_t)'P') {
        t = step_trail;
        for (i = 3; i >= 0; i--) {
            uint8_t b = (uint8_t)(t >> (8 * i));
            if (b != 0u) {
                (void)step_send((uint8_t)(b | 0x20u));
            }
        }
    }
    step_trail = (step_trail << 8) | mark;
}
#endif

void tfw_cm_soc_watchdog_arm(void)
{
    tfw_cm_reset_mark = TFW_CM_MARK_WATCHDOG;
    wdog_configure(WDOG_CS_BASE | TFW_IMX95_WDOG_CS_EN,
                   WDOG_TOVAL);
}

void tfw_cm_soc_watchdog_pet(void)
{
    TFW_IMX95_WDOG_CNT = TFW_IMX95_WDOG_REFRESH;
}

/* This firmware reads no M7 reset-status register on the i.MX 95: the marker is the cause. */
tfw_reset_cause_t tfw_cm_soc_reset_cause(uint32_t mark)
{
    return tfw_cm_mark_decode(mark);
}
