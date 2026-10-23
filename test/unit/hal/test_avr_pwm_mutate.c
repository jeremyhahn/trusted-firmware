/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/avr/hal_pwm.c, register-level (#163).
 *
 * A PWM channel is a TCCRnA address, an OCR address, the COMnx1 bit and
 * whether the timer is 16-bit. Byte registers go through _SFR_MEM8 into mem[],
 * a model of data memory diffed against a snapshot after each call; a word
 * register goes through _SFR_MEM16, which the suite records as one access so
 * a 16-bit OCR written as two bytes, or as one byte, fails. Every access is
 * checked to fall inside an ATOMIC_BLOCK.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "tfw/hal.h"
#include "avr/io.h"

#define MEM 0x100u

static uint8_t  mem[MEM], before[MEM];
static uint16_t word;
static unsigned word_addr, word_accesses;
static int      atomic_depth, atomic_blocks, outside_atomic;

volatile uint8_t *tfw_avr_reg(unsigned id)
{
    (void)id;
    TEST_FAIL_MESSAGE("hal_pwm.c names a fixed register");
    return 0;
}

volatile uint8_t *tfw_avr_mem(unsigned addr)
{
    TEST_ASSERT_TRUE(addr < MEM);
    if (atomic_depth == 0) {
        outside_atomic++;
    }
    return &mem[addr];
}

volatile uint16_t *tfw_avr_mem16(unsigned addr)
{
    if (atomic_depth == 0) {
        outside_atomic++;
    }
    word_addr = addr;
    word_accesses++;
    return &word;
}

int tfw_avr_atomic_enter(void)
{
    atomic_depth++;
    atomic_blocks++;
    return 1;
}

int tfw_avr_atomic_leave(void)
{
    atomic_depth--;
    return 0;
}

/* Timer0 OC0A on a 328P (TCCR0A 0x44, OCR0A 0x47, COM0A1 7), Timer2 OC2B
 * (0xB0, 0xB4, COM2B1 5), and Timer1 OC1A, 16-bit (0x80, 0x88, COM1A1 7). */
static const tfw_pwmref_t t0a = { 0x44u, 0x47u, 7u, 0u };
static const tfw_pwmref_t t2b = { 0xB0u, 0xB4u, 5u, 0u };
static const tfw_pwmref_t t1a = { 0x80u, 0x88u, 7u, 1u };
static const tfw_pwmref_t t1c = { 0x80u, 0x8Cu, 3u, 1u };   /* COM1C1 on a 2560 */

static void fill(void)
{
    unsigned i;

    for (i = 0u; i < MEM; i++) {
        mem[i] = (uint8_t)(i * 5u + 1u);
    }
    memcpy(before, mem, sizeof mem);
    word          = 0xEEEEu;
    word_addr     = 0u;
    word_accesses = 0u;
    atomic_depth = atomic_blocks = outside_atomic = 0;
}

static void only_changed(unsigned a, unsigned b)
{
    unsigned i;

    for (i = 0u; i < MEM; i++) {
        if (i != a && i != b) {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(before[i], mem[i], "a stray write");
        }
    }
}

void setUp(void) {}
void tearDown(void) {}

static const uint8_t duties[] = { 0u, 1u, 0x7Fu, 0x80u, 0xFFu };

static void test_write_8bit_connects_pin_and_sets_ocr(void)
{
    const tfw_pwmref_t *ch[] = { &t0a, &t2b };
    unsigned            c, d;

    for (c = 0u; c < 2u; c++) {
        for (d = 0u; d < sizeof duties; d++) {
            const tfw_pwmref_t *p = ch[c];

            fill();
            mem[p->tccra]  = 0x02u;               /* WGM01 already, kept */
            before[p->tccra] = 0x02u;
            TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_pwm_write(p, duties[d]));
            TEST_ASSERT_EQUAL_HEX8((uint8_t)(0x02u | (1u << p->com_bit) | 0x01u), mem[p->tccra]);
            TEST_ASSERT_EQUAL_HEX8(duties[d], mem[p->ocr]);
            TEST_ASSERT_EQUAL_UINT(0u, word_accesses);
            only_changed(p->tccra, p->ocr);
            TEST_ASSERT_EQUAL_INT(1, atomic_blocks);
            TEST_ASSERT_EQUAL_INT(0, outside_atomic);
        }
    }
}

static void test_write_16bit_is_one_word_store(void)
{
    const tfw_pwmref_t *ch[] = { &t1a, &t1c };
    unsigned            c, d;

    for (c = 0u; c < 2u; c++) {
        for (d = 0u; d < sizeof duties; d++) {
            const tfw_pwmref_t *p = ch[c];

            fill();
            TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_pwm_write(p, duties[d]));
            TEST_ASSERT_EQUAL_HEX8((uint8_t)(before[p->tccra] | (1u << p->com_bit) | 0x01u),
                                   mem[p->tccra]);
            TEST_ASSERT_EQUAL_UINT(1u, word_accesses);
            TEST_ASSERT_EQUAL_HEX(p->ocr, word_addr);
            TEST_ASSERT_EQUAL_HEX16(duties[d], word);         /* high byte zero */
            only_changed(p->tccra, 0xFFFFu);                  /* no byte to OCR */
            TEST_ASSERT_EQUAL_INT(1, atomic_blocks);
            TEST_ASSERT_EQUAL_INT(0, outside_atomic);
        }
    }
}

static void test_stop_disconnects_only_its_pin(void)
{
    const tfw_pwmref_t *ch[] = { &t0a, &t2b, &t1a, &t1c };
    unsigned            c;

    for (c = 0u; c < 4u; c++) {
        const tfw_pwmref_t *p = ch[c];

        fill();
        mem[p->tccra]    = 0xFFu;
        before[p->tccra] = 0xFFu;
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_pwm_stop(p));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)~(1u << p->com_bit), mem[p->tccra]);
        TEST_ASSERT_EQUAL_UINT(0u, word_accesses);            /* duty left alone */
        only_changed(p->tccra, 0xFFFFu);
        TEST_ASSERT_EQUAL_INT(1, atomic_blocks);
        TEST_ASSERT_EQUAL_INT(0, outside_atomic);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_write_8bit_connects_pin_and_sets_ocr);
    RUN_TEST(test_write_16bit_is_one_word_store);
    RUN_TEST(test_stop_disconnects_only_its_pin);
    return UNITY_END();
}
