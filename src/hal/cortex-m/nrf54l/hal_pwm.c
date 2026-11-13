/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * PWM on nrf54l: PWM20, unit 0 in the board's table, channels 0-3
 * (boards/nrf54l15-dk/board.c). Up-counting at 1 MHz (PRESCALER DIV_16 of
 * the 16 MHz PWM_CLK) to COUNTERTOP 1000, so a 1 kHz period; the wire's
 * 8-bit duty is scaled to the 1000 counts.
 *
 * The compare values are not registers: the peripheral reads them by EasyDMA
 * from a RAM sequence, one half-word per channel (DECODER LOAD Individual).
 * A new duty is a store to the sequence and a fresh TASKS_DMA.SEQ[0].START,
 * which loads all four values again; LOOP 0 and REFRESH 0 play it once and
 * the last values hold until TASKS_STOP.
 *
 * Duty 0 and 255 leave the peripheral out of it, as the AVR's do
 * (src/hal/avr/hal_pwm.c): the channel's PSEL is disconnected and the pin
 * driven low or high as a GPIO. Stop disconnects the channel and leaves the
 * pin a plain GPIO; the last channel out stops the sequence. ENABLE stays set
 * once the unit is first used: with every PSEL disconnected it drives no pin.
 *
 * The peripheral is configured on first use, not at boot, for the AVR's
 * reason: a board that never asks for a PWM does not have one running.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "soc_tables.h"

#define PWM_TOP 1000u

_Static_assert(PWM_TOP <= TFW_NRF_PWM_COUNTERTOP_MAX, "COUNTERTOP is 15 bits");

/* The sequence the peripheral reads, one value per channel. Not static so
 * the host test can read what was played (test/unit/test_nrf54l_pwm.c);
 * nothing else names it. In RAM, as EasyDMA requires: never const. */
volatile uint16_t tfw_nrf54l_pwm_seq[TFW_NRF_PWM_CHANNELS];

static uint8_t configured;
static uint8_t active;   /* bit n: channel n is connected and playing */

static const uint32_t port_base[3] = {
    TFW_NRF_P0_BASE, TFW_NRF_P1_BASE, TFW_NRF_P2_BASE,
};

static tfw_status_t check(const tfw_pwmref_t *p)
{
    if (p->unit != 0u || p->channel >= TFW_NRF_PWM_CHANNELS) {
        return TFW_ERR_UNSUPPORTED;
    }
    if (!tfw_nrf54l_pin_exists(p->port, p->bit)) {
        return TFW_ERR_BAD_PIN;
    }
    return TFW_OK;
}

static void configure(void)
{
    TFW_NRF_PWM_SHORTS        = 0u;
    TFW_NRF_PWM_INTEN         = 0u;
    TFW_NRF_PWM_MODE          = TFW_NRF_PWM_MODE_UP;
    TFW_NRF_PWM_PRESCALER     = TFW_NRF_PWM_PRESCALER_DIV16;
    TFW_NRF_PWM_COUNTERTOP    = PWM_TOP;
    TFW_NRF_PWM_DECODER       = TFW_NRF_PWM_DECODER_INDIV;
    TFW_NRF_PWM_LOOP          = 0u;
    TFW_NRF_PWM_SEQ0_REFRESH  = 0u;
    TFW_NRF_PWM_SEQ0_ENDDELAY = 0u;
    TFW_NRF_PWM_SEQ0_PTR      = (uint32_t)(uintptr_t)tfw_nrf54l_pwm_seq;
    TFW_NRF_PWM_SEQ0_MAXCNT   = (uint32_t)sizeof tfw_nrf54l_pwm_seq;
    TFW_NRF_PWM_ENABLE        = TFW_NRF_PWM_ENABLE_ON;
    configured = 1u;
}

/* Take channel ch off its pin; the last one out stops the sequence. */
static void release(uint8_t ch)
{
    TFW_NRF_PWM_PSEL_OUT(ch) = TFW_NRF54L_PSEL_DISCONNECTED;
    tfw_nrf54l_pwm_seq[ch] = 0u;
    if (active & (1u << ch)) {
        active = (uint8_t)(active & ~(1u << ch));
        if (active == 0u) {
            TFW_NRF_PWM_STOP = 1u;
        }
    }
}

/* A PWM command takes its pin from the tone, as a second tone start
 * retargets (hal_tone.c): last command wins. GPIOTE's task mode overrides
 * the pin's GPIO and PWM drive alike, so the tone is stopped, not shared. */
static void take_from_tone(const tfw_pwmref_t *p)
{
#if TFW_FEATURE_TONE
    if (tfw_nrf54l_tone_owns(p->port, p->bit)) {
        (void)tfw_hal_tone_stop();
    }
#else
    (void)p;
#endif
}

void tfw_nrf54l_pwm_release_pin(uint8_t port, uint8_t bit)
{
    uint32_t psel = tfw_nrf54l_psel(port, bit);
    uint8_t  ch;

    for (ch = 0u; ch < TFW_NRF_PWM_CHANNELS; ch++) {
        if (TFW_NRF_PWM_PSEL_OUT(ch) == psel) {
            release(ch);
        }
    }
}

tfw_status_t tfw_hal_pwm_write(const tfw_pwmref_t *p, uint8_t duty)
{
    uint32_t     bit;
    tfw_status_t st = check(p);

    if (st != TFW_OK) {
        return st;
    }
    take_from_tone(p);
    bit = 1uL << p->bit;
    if (duty == 0u || duty == 255u) {
        release(p->channel);
        if (duty) {
            TFW_NRF_GPIO_OUTSET(port_base[p->port]) = bit;
        } else {
            TFW_NRF_GPIO_OUTCLR(port_base[p->port]) = bit;
        }
        TFW_NRF_GPIO_PIN_CNF(port_base[p->port], p->bit) = TFW_NRF_PIN_CNF_OUTPUT;
        return TFW_OK;
    }
    if (!configured) {
        configure();
    }
    tfw_nrf54l_pwm_seq[p->channel] = (uint16_t)(TFW_NRF_PWM_VALUE_HIGH_FIRST |
        (((uint32_t)duty * PWM_TOP + 127u) / 255u));
    /* The pin idles low between periods' end and STOP, as a GPIO output. */
    TFW_NRF_GPIO_OUTCLR(port_base[p->port]) = bit;
    TFW_NRF_GPIO_PIN_CNF(port_base[p->port], p->bit) = TFW_NRF_PIN_CNF_OUTPUT;
    TFW_NRF_PWM_PSEL_OUT(p->channel) = tfw_nrf54l_psel(p->port, p->bit);
    active = (uint8_t)(active | (1u << p->channel));
    TFW_NRF_PWM_SEQ0_START = 1u;
    return TFW_OK;
}

tfw_status_t tfw_hal_pwm_stop(const tfw_pwmref_t *p)
{
    tfw_status_t st = check(p);

    if (st != TFW_OK) {
        return st;
    }
    take_from_tone(p);
    release(p->channel);
    /* Stop is the wire's duty zero (src/caps/pwm.c): the pin goes low as a
     * GPIO output, even after a duty-255 OUTSET left it high. */
    TFW_NRF_GPIO_OUTCLR(port_base[p->port]) = 1uL << p->bit;
    return TFW_OK;
}
