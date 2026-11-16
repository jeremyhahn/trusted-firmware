/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Tone on the i.MX 95 M7 (SPEC row `imx95`, #22), built in the soft:TONE=1 row
 * only (mk/firmware.mk's TONE_SRCS). The GPT (gpt.h) counts in restart mode
 * and matches output compare 1 every half period; its interrupt toggles the
 * tone pin through hal_gpio.c. The compare pad itself is not used: routing it
 * is a pin mux the System Manager owns.
 *
 * As src/hal/avr/hal_tone.c: one tone at a time, a new start retunes, zero
 * hertz or a stop drives the pin low. The timer's base is zero until
 * imx95-design.md question 17 is answered, and then every call answers
 * TFW_ERR_UNSUPPORTED without touching a register.
 */
#include <stdint.h>

#include "tfw/hal.h"
#include "cm_regs.h"
#include "gpt.h"

_Static_assert(TFW_IMX95_GPT_IRQ == 0u,
               "the service routine below is the GPT's vector, tfw_cm_irq0");

void tfw_cm_irq0(void);

static tfw_pinref_t    tone_pin;
static volatile uint8_t tone_active;
static volatile uint8_t tone_level;

/* Its callers return at a zero base before they get here; the test here as
 * well keeps the control write off address zero while the base is unknown. */
static void gpt_halt(void)
{
    if (TFW_IMX95_GPT_BASE == 0u) {
        return;
    }
    TFW_IMX95_GPT_REG(TFW_IMX95_GPT_IR) = 0u;
    TFW_IMX95_GPT_REG(TFW_IMX95_GPT_CR) = 0u;
    TFW_IMX95_GPT_REG(TFW_IMX95_GPT_SR) = TFW_IMX95_GPT_SR_OF1;
}

tfw_status_t tfw_hal_tone_start(const tfw_pinref_t *p, uint16_t hz)
{
    uint32_t     pr, ocr;
    uint32_t     primask;
    tfw_status_t st;

    if (TFW_IMX95_GPT_BASE == 0u) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (hz == 0u) {
        return tfw_hal_tone_stop();
    }
    tfw_imx95_gpt_half(TFW_IMX95_GPT_CLK_HZ, hz, &pr, &ocr);
    st = tfw_hal_gpio_mode(p, TFW_PIN_OUTPUT);
    if (st != TFW_OK) {
        return st;
    }
    primask = tfw_cm_irq_save();
    gpt_halt();
    if (tone_active && (tone_pin.port != p->port || tone_pin.bit != p->bit)) {
        (void)tfw_hal_gpio_write(&tone_pin, 0u);
    }
    tone_pin    = *p;
    tone_active = 1u;
    TFW_IMX95_GPT_REG(TFW_IMX95_GPT_PR)   = pr;
    TFW_IMX95_GPT_REG(TFW_IMX95_GPT_OCR1) = ocr;
    TFW_IMX95_GPT_REG(TFW_IMX95_GPT_IR)   = TFW_IMX95_GPT_IR_OF1IE;
    /* gpt_halt() froze the counter where it was; ENMOD, set while EN is
     * clear, makes the enable restart it from zero, so a retune's first
     * compare comes a full half period on even when the old count is past
     * the new OCR1 (Codex P1 on #443). */
    TFW_IMX95_GPT_REG(TFW_IMX95_GPT_CR)   =
        TFW_IMX95_GPT_CR_CLKSRC(TFW_IMX95_GPT_CR_CLKSRC_PER) | TFW_IMX95_GPT_CR_ENMOD;
    TFW_IMX95_GPT_REG(TFW_IMX95_GPT_CR)   =
        TFW_IMX95_GPT_CR_CLKSRC(TFW_IMX95_GPT_CR_CLKSRC_PER) | TFW_IMX95_GPT_CR_ENMOD |
        TFW_IMX95_GPT_CR_EN;
    tfw_cm_nvic_enable(TFW_IMX95_GPT_IRQ);
    tfw_cm_irq_restore(primask);
    return TFW_OK;
}

tfw_status_t tfw_hal_tone_stop(void)
{
    uint32_t primask;

    if (TFW_IMX95_GPT_BASE == 0u) {
        return TFW_ERR_UNSUPPORTED;
    }
    primask = tfw_cm_irq_save();
    gpt_halt();
    if (tone_active) {
        (void)tfw_hal_gpio_write(&tone_pin, 0u);
    }
    tone_active = 0u;
    tone_level  = 0u;
    tfw_cm_irq_restore(primask);
    return TFW_OK;
}

/* Compare 1 matched: half a period has passed. */
void tfw_cm_irq0(void)
{
    if (TFW_IMX95_GPT_BASE == 0u) {
        return;
    }
    TFW_IMX95_GPT_REG(TFW_IMX95_GPT_SR) = TFW_IMX95_GPT_SR_OF1;
    if (tone_active) {
        tone_level ^= 1u;
        (void)tfw_hal_gpio_write(&tone_pin, tone_level);
    }
}
