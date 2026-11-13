/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Tone on nrf54l, with no CPU in the loop: TIMER20 in timer mode counts to
 * CC[0], its COMPARE0-to-CLEAR short restarts it, and its COMPARE0 event is
 * published on DPPIC20 channel 0, to which GPIOTE20 channel 0's OUT task
 * subscribes, toggling the pin. All three are in peripheral domain 20, with
 * GPIO port P1 (soc_regs.h), so the tone is on a P1 pin; a pin on P0 or P2
 * is TFW_ERR_BAD_PIN. The PWM peripherals are not used.
 *
 * One tone at a time, as on AVR and ESP32: a second start retargets, stop
 * stops the TIMER, unlinks and frees the GPIOTE channel and the DPPI channel
 * and leaves the pin a GPIO output driven low.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "soc_tables.h"
#include "tone_calc.h"

#define TONE_PORT       1u   /* P1, GPIOTE20's port */
#define TONE_GPIOTE_CH  0u
#define TONE_DPPI_CH    0u

static uint8_t tone_active;
static uint8_t tone_bit;

int tfw_nrf54l_tone_owns(uint8_t port, uint8_t bit)
{
    return tone_active && port == TONE_PORT && bit == tone_bit;
}

tfw_status_t tfw_hal_tone_stop(void)
{
    if (!tone_active) {
        return TFW_OK;
    }
    TFW_NRF_TIMER_STOP  = 1u;
    TFW_NRF_TIMER_CLEAR = 1u;
    TFW_NRF_TIMER_PUBLISH_CMP0 = 0u;
    TFW_NRF_TIMER_SHORTS       = 0u;
    TFW_NRF_DPPIC_CHENCLR = 1uL << TONE_DPPI_CH;
    TFW_NRF_GPIOTE_SUB_OUT(TONE_GPIOTE_CH) = 0u;
    /* The GPIO's own output low before GPIOTE lets go of the pin, so the pin
     * is low whichever half of the wave it stopped in. */
    TFW_NRF_GPIO_OUTCLR(TFW_NRF_P1_BASE) = 1uL << tone_bit;
    TFW_NRF_GPIO_PIN_CNF(TFW_NRF_P1_BASE, tone_bit) = TFW_NRF_PIN_CNF_OUTPUT;
    TFW_NRF_GPIOTE_CONFIG(TONE_GPIOTE_CH) = 0u;   /* MODE Disabled: the pin is the GPIO's */
    tone_active = 0u;
    return TFW_OK;
}

tfw_status_t tfw_hal_tone_start(const tfw_pinref_t *p, uint16_t hz)
{
    uint32_t pre;
    uint32_t cc;

    if (hz == 0u) {
        return tfw_hal_tone_stop();
    }
    if (p->port != TONE_PORT || !tfw_nrf54l_pin_exists(p->port, p->bit)) {
        return TFW_ERR_BAD_PIN;
    }
    if (tfw_nrf54l_tone_calc(hz, &pre, &cc) != TFW_OK) {
        return TFW_ERR_RANGE;
    }
    (void)tfw_hal_tone_stop();   /* a second start retargets */
    /* and takes the pin from a PWM channel on it: last command wins. */
#if TFW_FEATURE_PWM
    tfw_nrf54l_pwm_release_pin(TONE_PORT, p->bit);
#endif

    /* TIMER: stopped and cleared, then the period and the published event. */
    TFW_NRF_TIMER_STOP      = 1u;
    TFW_NRF_TIMER_CLEAR     = 1u;
    TFW_NRF_TIMER_MODE      = TFW_NRF_TIMER_MODE_TIMER;
    TFW_NRF_TIMER_BITMODE   = TFW_NRF_TIMER_BITMODE_16;
    TFW_NRF_TIMER_PRESCALER = pre;
    TFW_NRF_TIMER_CC0       = cc;
    TFW_NRF_TIMER_SHORTS    = TFW_NRF_TIMER_SHORTS_CMP0_CLEAR;
    TFW_NRF_TIMER_PUBLISH_CMP0 = TFW_NRF_DPPI_EN | TONE_DPPI_CH;

    /* GPIOTE: the pin, starting low, toggled by OUT[0] from the channel. */
    TFW_NRF_GPIOTE_CONFIG(TONE_GPIOTE_CH) = TFW_NRF_GPIOTE_CONFIG_TASK
        | ((uint32_t)p->bit << TFW_NRF_GPIOTE_CONFIG_PSEL_POS)
        | ((uint32_t)TONE_PORT << TFW_NRF_GPIOTE_CONFIG_PORT_POS)
        | TFW_NRF_GPIOTE_CONFIG_TOGGLE;
    TFW_NRF_GPIOTE_SUB_OUT(TONE_GPIOTE_CH) = TFW_NRF_DPPI_EN | TONE_DPPI_CH;

    /* DPPI: the link, then the TIMER runs. */
    TFW_NRF_DPPIC_CHENSET = 1uL << TONE_DPPI_CH;
    TFW_NRF_TIMER_START   = 1u;

    tone_bit    = p->bit;
    tone_active = 1u;
    return TFW_OK;
}
