/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The i.MX 95 General Purpose Timer the M7's tone runs on (SPEC row `imx95`,
 * #22): output compare 1 in restart mode, its interrupt toggling the tone pin.
 *
 * IMX95RM is not held. The offsets and bits are Linux's,
 * drivers/clocksource/timer-imx-gpt.c (the V2 register layout), facts only;
 * every one is TO CONFIRM against IMX95RM. The timer's base, its clock and its
 * interrupt are docs/multi-arch/imx95-design.md question 17: the base is zero
 * until then, and hal_tone.c answers TFW_ERR_UNSUPPORTED without touching a
 * register. The System Manager owns the timer's clock and pads; nothing here
 * programs either.
 */
#ifndef TFW_IMX95_GPT_H
#define TFW_IMX95_GPT_H

#include <stdint.h>

/* The timer's base, as the M7 sees it: zero, unsupported (TO CONFIRM,
 * question 17). A host test sets its own. */
#ifndef TFW_IMX95_GPT_BASE
#define TFW_IMX95_GPT_BASE   0x00000000uL
#endif
/* The clock the timer counts, the System Manager's to set (TO CONFIRM,
 * question 17). */
#define TFW_IMX95_GPT_CLK_HZ 24000000uL
/* The timer's interrupt on the M7's NVIC, and so hal_tone.c's vector,
 * tfw_cm_irq0: a placeholder slot (TO CONFIRM, question 17), never enabled
 * while the base is zero. */
#define TFW_IMX95_GPT_IRQ    0u

/* Registers (timer-imx-gpt.c; TO CONFIRM). */
#define TFW_IMX95_GPT_CR     0x00u   /* control */
#define TFW_IMX95_GPT_PR     0x04u   /* prescaler: divide by PR+1, 12 bits */
#define TFW_IMX95_GPT_SR     0x08u   /* status, write 1 to clear */
#define TFW_IMX95_GPT_IR     0x0Cu   /* interrupt enable */
#define TFW_IMX95_GPT_OCR1   0x10u   /* output compare 1 */
#define TFW_IMX95_GPT_CNT    0x24u   /* counter */

/* CR bits (timer-imx-gpt.c; TO CONFIRM). FRR clear is restart mode: the
 * counter returns to zero on the compare 1 match. */
#define TFW_IMX95_GPT_CR_EN          (1uL << 0)
/* ENMOD set: EN rising restarts the main and prescaler counters from zero;
 * clear, they resume the values they froze at when EN fell. */
#define TFW_IMX95_GPT_CR_ENMOD       (1uL << 1)
#define TFW_IMX95_GPT_CR_CLKSRC(n)   (((uint32_t)(n) & 7u) << 6)
#define TFW_IMX95_GPT_CR_CLKSRC_PER  2u      /* the peripheral clock */
#define TFW_IMX95_GPT_CR_FRR         (1uL << 9)
#define TFW_IMX95_GPT_CR_SWR         (1uL << 15)
#define TFW_IMX95_GPT_CR_OM1(n)      (((uint32_t)(n) & 7u) << 20)
#define TFW_IMX95_GPT_CR_OM1_TOGGLE  1u      /* the compare pad, unrouted here */

#define TFW_IMX95_GPT_SR_OF1   (1uL << 0)
#define TFW_IMX95_GPT_IR_OF1IE (1uL << 0)

#define TFW_IMX95_GPT_PR_MAX   0xFFFu

/* A host test sends the timer to its own registers, as the base. */
#ifndef TFW_IMX95_GPT_REG
#define TFW_IMX95_GPT_REG(off) TFW_CM_REG(TFW_IMX95_GPT_BASE + (off))
#else
volatile uint32_t *tfw_test_gpt_reg(uint32_t off);   /* the host test's */
#endif

/* The prescaler and compare value for a tone of `hz`: the interrupt toggles
 * the pin once per match, so a match every half period, clk/(2*hz) counts. In
 * restart mode a match comes every OCR1+1 counts of the divided clock, so
 * OCR1 = half/(PR+1) - 1, with PR the smallest divider that fits 32 bits.
 * hz zero, or a half period under one count, gives PR 0 and OCR1 0. */
static inline void tfw_imx95_gpt_half(uint32_t clk_hz, uint32_t hz, uint32_t *pr, uint32_t *ocr)
{
    uint64_t half;
    uint32_t div = 1u;

    *pr  = 0u;
    *ocr = 0u;
    if (hz == 0u) {
        return;
    }
    half = (uint64_t)clk_hz / (2u * (uint64_t)hz);
    if (half == 0u) {
        return;
    }
    while (half / div > 0x100000000uLL && div <= TFW_IMX95_GPT_PR_MAX) {
        div++;
    }
    *pr  = div - 1u;
    *ocr = (uint32_t)(half / div - 1u);
}

#endif /* TFW_IMX95_GPT_H */
