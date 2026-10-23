/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/avr/hal_adc.c, register-level (#163), built once per ADC shape:
 * TFW_TEST_ADC_PART 0 is the 328P (eight channels, the mega's reference
 * encoding), 1 the 2560 (sixteen, MUX5 in ADCSRB), 2 the ATtiny85 (four,
 * REFS2's encoding, no external reference). The make rule passes the part's
 * REFS2 or MUX5 as avr-libc would define it.
 *
 * tfw_avr_reg() models the converter: ADSC stays set for POLLS reads of ADCSRA
 * and then clears with a result in ADCL:ADCH that names the channel ADMUX and
 * ADCSRB selected, and it records whether ADCL was read first and whether
 * either was read before the conversion finished.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "tfw/hal.h"
#include "tfw/config.h"
#include "avr/io.h"

#define PART   TFW_TEST_ADC_PART
#define POLLS  3

static uint8_t regs[TFW_AVR_REGS];
static int     converting, polls, early_read, adch_first, low_read, adcsrb_touched;

/* What the converter returns for a selection: 10 bits naming it. */
static uint16_t result_for(uint8_t admux, uint8_t adcsrb)
{
    unsigned ch = (admux & 0x1Fu) | ((PART == 1 && (adcsrb & 0x08u) != 0u) ? 0x20u : 0u);
    return (uint16_t)((ch * 29u + 0x105u) & 0x3FFu);
}

volatile uint8_t *tfw_avr_reg(unsigned id)
{
    TEST_ASSERT_TRUE(id < TFW_AVR_REGS);
    if (id == TFW_AVR_ADCSRB) {
        adcsrb_touched++;
    }
    if (id == TFW_AVR_ADCSRA && (regs[TFW_AVR_ADCSRA] & 0x40u) != 0u) {
        if (!converting) {
            converting = 1;          /* the |= that set ADSC has landed */
            polls      = 0;
        } else if (++polls == POLLS) {
            uint16_t r = result_for(regs[TFW_AVR_ADMUX], regs[TFW_AVR_ADCSRB]);
            regs[TFW_AVR_ADCL]   = (uint8_t)r;
            regs[TFW_AVR_ADCH]   = (uint8_t)(r >> 8);
            regs[TFW_AVR_ADCSRA] = (uint8_t)(regs[TFW_AVR_ADCSRA] & ~0x40u);
            converting           = 0;
        }
    }
    if (id == TFW_AVR_ADCL || id == TFW_AVR_ADCH) {
        if (converting || (regs[TFW_AVR_ADCSRA] & 0x40u) != 0u) {
            early_read++;
        }
        if (id == TFW_AVR_ADCL) {
            low_read = 1;
        } else if (!low_read) {
            adch_first++;
        }
    }
    return &regs[id];
}

static const uint8_t ref_vcc[]  = { 0x40u, 0x40u, 0x00u };
static const uint8_t ref_ext[]  = { 0x00u, 0x00u, 0x40u };   /* tiny: refused */
static const uint8_t ref_int[]  = { 0xC0u, 0xC0u, 0x80u };

static uint16_t read_ok(uint8_t ch)
{
    uint16_t v = 0xEEEEu;

    low_read = 0;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_read(ch, &v));
    TEST_ASSERT_EQUAL_INT(0, early_read);
    TEST_ASSERT_EQUAL_INT(0, adch_first);
    TEST_ASSERT_EQUAL_INT(0, converting);
    TEST_ASSERT_EQUAL_INT(POLLS, polls);       /* waited for ADSC to clear */
    return v;
}

void setUp(void)
{
    memset(regs, 0, sizeof regs);
    converting = polls = early_read = adch_first = low_read = adcsrb_touched = 0;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_init());
}

void tearDown(void) {}

static void test_init_enables_at_div128_and_selects_vcc(void)
{
    regs[TFW_AVR_ADCSRA] = 0x38u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_set_ref(TFW_ADC_REF_INTERNAL));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_init());
    TEST_ASSERT_EQUAL_HEX8(0x87u, regs[TFW_AVR_ADCSRA]);    /* ADEN | ADPS2:0, nothing else */
    (void)read_ok(1u);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(ref_vcc[PART] | 1u), regs[TFW_AVR_ADMUX]);
}

static void test_reference_encoding_for_the_part(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_set_ref(TFW_ADC_REF_INTERNAL));
    (void)read_ok(2u);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(ref_int[PART] | 2u), regs[TFW_AVR_ADMUX]);

    if (PART == 2) {
        TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_hal_adc_set_ref(TFW_ADC_REF_EXTERNAL));
        (void)read_ok(2u);                                   /* still internal */
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(ref_int[PART] | 2u), regs[TFW_AVR_ADMUX]);
    } else {
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_set_ref(TFW_ADC_REF_EXTERNAL));
        (void)read_ok(2u);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(ref_ext[PART] | 2u), regs[TFW_AVR_ADMUX]);
    }

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_set_ref(TFW_ADC_REF_VCC));
    (void)read_ok(3u);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(ref_vcc[PART] | 3u), regs[TFW_AVR_ADMUX]);
}

static void test_unknown_reference_is_refused_and_kept(void)
{
    static const unsigned bad[] = { 3u, 4u, 0xFFu };
    unsigned              i;

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_adc_set_ref(TFW_ADC_REF_INTERNAL));
    for (i = 0u; i < sizeof bad / sizeof bad[0]; i++) {
        TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_adc_set_ref((tfw_adc_ref_t)bad[i]));
    }
    (void)read_ok(0u);
    TEST_ASSERT_EQUAL_HEX8(ref_int[PART], regs[TFW_AVR_ADMUX]);
}

static void test_every_channel_converts_its_own_pin(void)
{
    uint8_t ch;

    for (ch = 0u; ch < TFW_ADC_COUNT; ch++) {
        uint8_t  bank = (uint8_t)(ch > 7u ? 0x08u : 0u);
        uint16_t v;

        regs[TFW_AVR_ADCSRB] = (uint8_t)(0xA0u | (ch > 7u ? 0u : 0x08u));  /* stale MUX5 */
        regs[TFW_AVR_ADMUX]  = 0x1Fu;
        regs[TFW_AVR_ADCSRA] = (uint8_t)(regs[TFW_AVR_ADCSRA] | 0x01u);
        v = read_ok(ch);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(ref_vcc[PART] | (ch & 0x07u)), regs[TFW_AVR_ADMUX]);
        TEST_ASSERT_EQUAL_HEX8(0x87u | 0x01u, regs[TFW_AVR_ADCSRA]);   /* ADSC done, rest kept */
        if (PART == 1) {
            TEST_ASSERT_EQUAL_HEX8((uint8_t)(0xA0u | bank), regs[TFW_AVR_ADCSRB]);
            TEST_ASSERT_EQUAL_HEX16(result_for((uint8_t)(ch & 0x07u), bank), v);
        } else {
            TEST_ASSERT_EQUAL_INT(0, adcsrb_touched);  /* no MUX5 on this part */
            TEST_ASSERT_EQUAL_HEX16(result_for(ch, 0u), v);
        }
        adcsrb_touched = 0;
    }
}

static void test_result_is_ten_bits_low_byte_first(void)
{
    uint16_t v;

    regs[TFW_AVR_ADMUX] = 0u;
    v = read_ok(1u);
    TEST_ASSERT_EQUAL_HEX16(result_for((uint8_t)(ref_vcc[PART] | 1u), 0u), v);
    TEST_ASSERT_TRUE(v > 0xFFu);                               /* ADCH carried */
}

static void test_channel_past_the_board_is_a_bad_pin(void)
{
    static const uint8_t bad[] = { TFW_ADC_COUNT, TFW_ADC_COUNT + 1u, 0x7Fu, 0xFFu };
    unsigned             i, r;

    for (i = 0u; i < sizeof bad; i++) {
        uint16_t v = 0xEEEEu;

        memset(regs, 0x11, sizeof regs);
        regs[TFW_AVR_ADCSRA] = 0x87u;
        TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_PIN, tfw_hal_adc_read(bad[i], &v));
        TEST_ASSERT_EQUAL_HEX16(0xEEEEu, v);
        TEST_ASSERT_EQUAL_HEX8(0x87u, regs[TFW_AVR_ADCSRA]);   /* no conversion */
        for (r = 0u; r < TFW_AVR_REGS; r++) {
            if (r != TFW_AVR_ADCSRA) {
                TEST_ASSERT_EQUAL_HEX8(0x11u, regs[r]);
            }
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_enables_at_div128_and_selects_vcc);
    RUN_TEST(test_reference_encoding_for_the_part);
    RUN_TEST(test_unknown_reference_is_refused_and_kept);
    RUN_TEST(test_every_channel_converts_its_own_pin);
    RUN_TEST(test_result_is_ten_bits_low_byte_first);
    RUN_TEST(test_channel_past_the_board_is_a_bad_pin);
    return UNITY_END();
}
