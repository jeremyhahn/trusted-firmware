/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <avr/io.h>
#include "ag/hal.h"
#include "ag/config.h"

static uint8_t ref_bits;   /* the REFS field, already positioned */

/*
 * THE REFERENCE ENCODING, WHICH IS INVERTED ON PART OF THIS FAMILY.
 *
 * On every mega part the ADMUX reference field is two bits: REFS1:REFS0 of 01
 * selects AVcc and 00 selects the external AREF pin.
 *
 * On the ATtiny25/45/85 it is THREE bits, REFS2:REFS1:REFS0, and the
 * assignments are not an extension of the mega's -- they are different:
 *
 *      000  Vcc            001  external AREF
 *      010  internal 1.1V  110/111  internal 2.56V
 *
 * So the mega's "AVcc" pattern, _BV(REFS0), is the tiny's "external AREF".
 * Pasting the mega encoding here would leave a device reporting that it is
 * measuring against Vcc while it is measuring against whatever is on PB0 --
 * which on an unconnected pin is a reading that is stable, plausible and wrong,
 * and which no test on the host could ever see.
 *
 * REFS2's existence is the discriminator, and it is exactly the same kind of
 * fact as the MUX5 conditional below: a property of the ADC peripheral on a
 * particular part, in the one file that is allowed to know about registers.
 * The ATtiny85's board profile asserts REFS2 is where it thinks it is, so a
 * part that took this arm without having the encoding would fail to build.
 */
#ifdef REFS2
#define AG_ADC_REFBITS_VCC      ((uint8_t)0u)
#define AG_ADC_REFBITS_EXTERNAL ((uint8_t)_BV(REFS0))
#define AG_ADC_REFBITS_INTERNAL ((uint8_t)_BV(REFS1))
#else
#define AG_ADC_REFBITS_VCC      ((uint8_t)_BV(REFS0))
#define AG_ADC_REFBITS_EXTERNAL ((uint8_t)0u)
#define AG_ADC_REFBITS_INTERNAL ((uint8_t)(_BV(REFS1) | _BV(REFS0)))
#endif

ag_status_t ag_hal_adc_init(void)
{
    /* The ADC wants 50-200 kHz for full 10-bit accuracy. A prescaler of 128
     * gives 125 kHz at 16 MHz and 62.5 kHz at 8 MHz, both inside the window.
     * Running it faster trades resolution for speed, and a converter that is
     * quietly 8-bit is worse than a slow one that is honestly 10-bit. */
    ADCSRA = (uint8_t)(_BV(ADEN) | _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0));
    ref_bits = AG_ADC_REFBITS_VCC;
    return AG_OK;
}

ag_status_t ag_hal_adc_set_ref(ag_adc_ref_t ref)
{
    switch (ref) {
    case AG_ADC_REF_VCC:      ref_bits = AG_ADC_REFBITS_VCC;      return AG_OK;
    case AG_ADC_REF_EXTERNAL:
#ifdef AG_ADC_NO_EXTERNAL_REF
        /*
         * The AREF pin is spoken for on this board, so there is no external
         * reference to select.
         *
         * On the ATtiny85 AREF is PB0, which this build uses as the software
         * UART's receive line. Selecting the external reference there would
         * either contend with the host's transmitter, or -- with nothing
         * connected -- make the serial line itself the voltage every reading is
         * measured against. Both produce readings that look plausible and are
         * meaningless.
         *
         * Refused rather than accepted-and-ignored: a host that asked for a
         * reference and got AG_OK would trust the numbers that followed.
         */
        return AG_ERR_UNSUPPORTED;
#else
        ref_bits = AG_ADC_REFBITS_EXTERNAL;
        return AG_OK;
#endif
    case AG_ADC_REF_INTERNAL: ref_bits = AG_ADC_REFBITS_INTERNAL; return AG_OK;
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
