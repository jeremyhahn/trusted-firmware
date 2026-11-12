/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Tone on CTIMER4 with no CPU in the loop: timer mode, the prescaler at 0,
 * match 0 at half the period resetting the counter (MCR MR0R) and toggling
 * its external output CT4_MAT0 (EMR EMC0), the pin handed to that output
 * (PCR MUX TFW_MCXN_TONE_PIN_FN, the board's). The divider is ctimer_tone.h.
 * CTIMER0 is the PWM's; the tone takes a timer of its own so the two never
 * share a period.
 *
 * One tone at a time, as on AVR and ESP32: a second start retargets the same
 * timer, a stop halts it with the counter held in reset and returns the pin to
 * a GPIO output driven low. The first start gates the timer's clock on,
 * releases its reset, selects FRO12M at divide 1 and enables its function
 * clock; the sequence is the device header MCXN947_cm33_core0.h and
 * fsl_clock.c's CLOCK_SetClkDiv, as hal_soc.c does it, at the mcux-sdk commit
 * docs/reference/README.md pins. Not run on the board.
 */
#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "ctimer_tone.h"

#define TONE_CLK_HZ 12000000uL   /* FRO12M, TFW_MCXN_CTCLKSEL_FRO12M */

static uint8_t tone_clocked;     /* the timer's clock and reset are set up */
static uint8_t tone_port = 0xFFu; /* the pin on the timer, or none */
static uint8_t tone_bit;

static void tone_clock(void)
{
    TFW_MCXN_AHBCLKCTRLSET(2) = TFW_MCXN_CTRL2_TIMER4;
    TFW_MCXN_PRESETCTRLCLR(2) = TFW_MCXN_CTRL2_TIMER4;
    TFW_MCXN_CT4CLKSEL = TFW_MCXN_CTCLKSEL_FRO12M;
    TFW_MCXN_CT4CLKDIV = TFW_MCXN_CLKDIV_RESET | TFW_MCXN_CLKDIV_HALT;
    TFW_MCXN_CT4CLKDIV = 0u;
    TFW_MCXN_CTGLOBALSTARTEN |= TFW_MCXN_CTGLOBAL_CT4;
    tone_clocked = 1u;
}

/* The pin back to GPIO, as an output driven low. */
static void tone_pin_release(void)
{
    if (tone_port < TFW_MCXN_PORT_COUNT) {
        TFW_MCXN_GPIO_PCOR(tone_port) = 1u << tone_bit;
        TFW_MCXN_GPIO_PDDR(tone_port) |= 1u << tone_bit;
        TFW_MCXN_PORT_PCR(tone_port, tone_bit) = TFW_MCXN_PCR_IBE;
    }
    tone_port = 0xFFu;
}

tfw_status_t tfw_hal_tone_start(const tfw_pinref_t *p, uint16_t hz)
{
    uint32_t mr;
    tfw_status_t st;

    if (p == 0 || p->port >= TFW_MCXN_PORT_COUNT || p->bit >= 32u) {
        return TFW_ERR_BAD_PIN;
    }
    if (hz == 0u) {
        return tfw_hal_tone_stop();
    }
    st = tfw_mcxn_tone_match(TONE_CLK_HZ, hz, &mr);
    if (st != TFW_OK) {
        return st;
    }
    if (!tone_clocked) {
        tone_clock();
    }
    /* Stopped and held at 0 while it is reprogrammed, so a retarget starts a
     * fresh half period from a low output instead of finishing the old one. */
    TFW_MCXN_CT4_TCR = TFW_MCXN_CT_TCR_CRST;
    TFW_MCXN_CT4_PR  = 0u;
    TFW_MCXN_CT4_MR0 = mr;
    TFW_MCXN_CT4_MCR = TFW_MCXN_CT_MCR_MR0R;
    TFW_MCXN_CT4_EMR = TFW_MCXN_CT_EMR_EMC_TOGGLE << TFW_MCXN_CT_EMR_EMC0_SHIFT;
    if (tone_port < TFW_MCXN_PORT_COUNT &&
        (tone_port != p->port || tone_bit != p->bit)) {
        tone_pin_release();
    }
    /* The mux on every start: a GPIO mode command (or the scheduler, through
     * tfw_hal_gpio_mode) may have taken the same pin back since the last. */
    TFW_MCXN_PORT_PCR(p->port, p->bit) =
        ((uint32_t)TFW_MCXN_TONE_PIN_FN << TFW_MCXN_PCR_MUX_SHIFT) | TFW_MCXN_PCR_IBE;
    tone_port = p->port;
    tone_bit  = p->bit;
    TFW_MCXN_CT4_TCR = TFW_MCXN_CT_TCR_CEN;
    return TFW_OK;
}

tfw_status_t tfw_hal_tone_stop(void)
{
    if (tone_clocked) {
        TFW_MCXN_CT4_TCR = TFW_MCXN_CT_TCR_CRST;
        TFW_MCXN_CT4_EMR = 0u;      /* no action on match, the output low */
    }
    tone_pin_release();
    return TFW_OK;
}
