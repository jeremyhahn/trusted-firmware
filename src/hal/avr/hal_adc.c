/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <avr/io.h>
#include "ag/hal.h"
#include "ag/config.h"

static uint8_t ref_bits;   /* REFS1:REFS0, already positioned */

ag_status_t ag_hal_adc_init(void)
{
    /* The ADC wants 50-200 kHz for full 10-bit accuracy. At 16 MHz that means
     * a prescaler of 128, giving 125 kHz. Running it faster trades resolution
     * for speed, and a converter that is quietly 8-bit is worse than a slow
     * one that is honestly 10-bit. */
    ADCSRA = (uint8_t)(_BV(ADEN) | _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0));
    ref_bits = (uint8_t)_BV(REFS0);        /* AVcc */
    return AG_OK;
}

ag_status_t ag_hal_adc_set_ref(ag_adc_ref_t ref)
{
    switch (ref) {
    case AG_ADC_REF_VCC:      ref_bits = (uint8_t)_BV(REFS0);              return AG_OK;
    case AG_ADC_REF_EXTERNAL: ref_bits = 0u;                                return AG_OK;
    case AG_ADC_REF_INTERNAL: ref_bits = (uint8_t)(_BV(REFS1) | _BV(REFS0)); return AG_OK;
    default:                  return AG_ERR_RANGE;
    }
}

ag_status_t ag_hal_adc_read(uint8_t channel, uint16_t *out_raw)
{
    uint8_t low;

    if (channel >= AG_ADC_COUNT) {
        return AG_ERR_BAD_PIN;
    }
    /* Channels above 7 live behind MUX5, which is in ADCSRB rather than ADMUX
     * -- a part with eight channels has no such bit, so this is one of the few
     * places the HAL is allowed a conditional. Writing the low three bits of
     * the channel and forgetting MUX5 does not fail: it silently converts a
     * DIFFERENT pin, which is the worst way for a sensor reading to be wrong. */
#ifdef MUX5
    if (channel > 7u) { ADCSRB |= (uint8_t)_BV(MUX5); }
    else              { ADCSRB &= (uint8_t)~_BV(MUX5); }
#endif
    /* MUX2:0 select within the bank; MUX5 selects the bank. Channel 8 is
     * bank 1 channel 0, not MUX3 -- MUX3 selects a DIFFERENTIAL pair. */
    ADMUX = (uint8_t)(ref_bits | (channel & 0x07u));
    ADCSRA |= (uint8_t)_BV(ADSC);
    while (ADCSRA & _BV(ADSC)) {
        /* ~112 us at 125 kHz. Polled rather than interrupt-driven: the whole
         * conversion is shorter than one byte at 115200 baud, so an interrupt
         * would add state for no latency anyone can observe. Interrupts stay
         * ENABLED throughout, so the tick keeps counting and the UART keeps
         * filling its ring while this spins. */
    }
    /* ADCL must be read first: reading it latches ADCH, and reading ADCH first
     * returns a value from a different conversion. */
    low      = ADCL;
    *out_raw = (uint16_t)(low | ((uint16_t)ADCH << 8));
    return AG_OK;
}
