/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * PWM on a CTIMER in PWM mode, 8-bit duty at about 1 kHz (1001 Hz from FRO12M)
 * as the AVR and the ESP32 give. A tfw_pwmref_t names the timer (unit, CTIMER
 * 0-4), its match channel (0-2; match 3 is the period) and the pin (port,
 * bit). The period, the duty's match value and the timer's register sequence
 * are ctimer_pwm.c, host-tested on a register model; this file is the SYSCON
 * and pin side.
 *
 * Nothing is set up at boot. The first write to a timer gates its clock on,
 * releases its reset, selects FRO12M at divide 1, enables its function clock
 * and starts it; the first write to a pin hands it to the timer's match output
 * (PCR MUX TFW_MCXN_PWM_PIN_FN, the board's). A stop takes the channel out of
 * PWM mode, returns the pin to a GPIO output driven low, and stops the timer
 * when it was the last channel on it.
 *
 * The clock select, gates, resets and function-clock enable are the device
 * header MCXN947_cm33_core0.h and fsl_clock.h; the sequence is fsl_ctimer.c's
 * CTIMER_Init and CTIMER_SetupPwmPeriod, at the mcux-sdk commit
 * docs/reference/README.md pins. Not run on the board.
 */
#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "ctimer_pwm.h"

#define CTIMER_CLK_HZ 12000000uL   /* FRO12M, TFW_MCXN_CTIMERCLKSEL_FRO12M */

/* The gate and reset of CTIMER n: control register index and bit. */
static const uint8_t ct_ctrl_reg[TFW_MCXN_CTIMER_COUNT] = { 1u, 1u, 1u, 2u, 2u };
static const uint8_t ct_ctrl_bit[TFW_MCXN_CTIMER_COUNT] = { 26u, 27u, 22u, 21u, 22u };

static uint8_t running;      /* bit n: CTIMER n started */
static uint32_t pin_muxed[TFW_MCXN_PORT_COUNT]; /* bit b of port p: on the timer */

static volatile uint32_t *ct_regs(uint8_t unit)
{
    return (volatile uint32_t *)TFW_MCXN_CTIMER_BASE(unit);
}

static uint8_t is_ref(const tfw_pwmref_t *p)
{
    return p != 0 && p->unit < TFW_MCXN_CTIMER_COUNT &&
           p->channel < TFW_MCXN_CTIMER_PWM_CHANNELS &&
           p->port < TFW_MCXN_PORT_COUNT && p->bit < 32u;
}

static tfw_status_t ct_start(uint8_t unit)
{
    const uint32_t bit = 1u << ct_ctrl_bit[unit];

    TFW_MCXN_AHBCLKCTRLSET(ct_ctrl_reg[unit]) = bit;
    TFW_MCXN_PRESETCTRLCLR(ct_ctrl_reg[unit]) = bit;
    TFW_MCXN_CTIMERCLKSEL(unit) = TFW_MCXN_CTIMERCLKSEL_FRO12M;
    TFW_MCXN_CTIMERCLKDIV(unit) = TFW_MCXN_CLKDIV_RESET | TFW_MCXN_CLKDIV_HALT;
    TFW_MCXN_CTIMERCLKDIV(unit) = 0u;
    TFW_MCXN_CTIMERGLOBALSTARTEN |= 1u << unit;
    return mcxn_ctimer_pwm_start(ct_regs(unit), CTIMER_CLK_HZ);
}

tfw_status_t tfw_hal_pwm_write(const tfw_pwmref_t *p, uint8_t duty)
{
    tfw_status_t st;

    if (!is_ref(p)) {
        return TFW_ERR_BAD_PIN;
    }
    if ((running & (1u << p->unit)) == 0u) {
        st = ct_start(p->unit);
        if (st != TFW_OK) {
            return st;
        }
        running |= (uint8_t)(1u << p->unit);
    }
    st = mcxn_ctimer_pwm_duty(ct_regs(p->unit), p->channel, duty);
    if (st != TFW_OK) {
        return st;
    }
    if ((pin_muxed[p->port] & (1u << p->bit)) == 0u) {
        TFW_MCXN_PORT_PCR(p->port, p->bit) =
            (TFW_MCXN_PWM_PIN_FN << TFW_MCXN_PCR_MUX_SHIFT) | TFW_MCXN_PCR_IBE;
        pin_muxed[p->port] |= 1u << p->bit;
    }
    return TFW_OK;
}

tfw_status_t tfw_hal_pwm_stop(const tfw_pwmref_t *p)
{
    uint32_t bit;

    if (!is_ref(p)) {
        return TFW_ERR_BAD_PIN;
    }
    bit = 1u << p->bit;
    /* The pin first: a GPIO output at 0, then the function back to GPIO. */
    TFW_MCXN_GPIO_PCOR(p->port) = bit;
    TFW_MCXN_GPIO_PDDR(p->port) |= bit;
    TFW_MCXN_PORT_PCR(p->port, p->bit) = TFW_MCXN_PCR_IBE;
    pin_muxed[p->port] &= ~bit;
    if ((running & (1u << p->unit)) != 0u &&
        mcxn_ctimer_pwm_halt(ct_regs(p->unit), p->channel) != 0u) {
        running &= (uint8_t)~(1u << p->unit);
    }
    return TFW_OK;
}
