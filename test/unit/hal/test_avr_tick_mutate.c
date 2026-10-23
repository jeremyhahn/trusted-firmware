/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/avr/hal_tick.c, register-level (#163): Timer0 set up for a true
 * millisecond, the compare interrupt counting it, and millis reading the
 * count inside the one atomic section.
 *
 * Built at 16 MHz, where the compare value is 249. avr/interrupt.h's ISR()
 * makes the vector a plain function, so a test fires the tick by calling
 * TIMER0_COMPA_vect(). To pin the read inside ATOMIC_BLOCK, a tick can be
 * made to land just before the block masks interrupts and another to be
 * pending when it unmasks: a read in the block sees the first and not the
 * second.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "tfw/hal.h"
#include "avr/io.h"

void TIMER0_COMPA_vect(void);
tfw_status_t tfw_hal_tick_init(void);      /* declared where called, as src/main.c does */

static uint8_t  regs[TFW_AVR_REGS];
static unsigned accesses;
static int      depth, blocks, edge_ticks;

volatile uint8_t *tfw_avr_reg(unsigned id)
{
    TEST_ASSERT_TRUE_MESSAGE(id == TFW_AVR_TCCR0A || id == TFW_AVR_TCCR0B ||
                             id == TFW_AVR_OCR0A || id == TFW_AVR_TIMSK0,
                             "hal_tick.c touched a register outside Timer0");
    accesses++;
    return &regs[id];
}

volatile uint8_t *tfw_avr_mem(unsigned addr)
{
    (void)addr;
    TEST_FAIL_MESSAGE("hal_tick.c reached a register by address");
    return 0;
}

volatile uint16_t *tfw_avr_mem16(unsigned addr)
{
    (void)addr;
    TEST_FAIL_MESSAGE("hal_tick.c made a word access");
    return 0;
}

int tfw_avr_atomic_enter(void)
{
    if (edge_ticks) {
        TIMER0_COMPA_vect();          /* in just before the mask */
    }
    depth++;
    blocks++;
    return 1;
}

int tfw_avr_atomic_leave(void)
{
    depth--;
    if (edge_ticks) {
        TIMER0_COMPA_vect();          /* pending, taken on the unmask */
    }
    return 0;
}

void setUp(void)
{
    memset(regs, 0, sizeof regs);
    accesses = 0u;
    depth = blocks = edge_ticks = 0;
}

void tearDown(void) {}

static void test_init_is_ctc_clk64_compare_249(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_tick_init());
    TEST_ASSERT_EQUAL_HEX8(0x02u, regs[TFW_AVR_TCCR0A]);    /* WGM01 alone */
    TEST_ASSERT_EQUAL_HEX8(0x03u, regs[TFW_AVR_TCCR0B]);    /* CS01 | CS00 */
    TEST_ASSERT_EQUAL_UINT8(249u, regs[TFW_AVR_OCR0A]);     /* 250 counts of 4 us */
    TEST_ASSERT_EQUAL_HEX8(0x02u, regs[TFW_AVR_TIMSK0]);    /* OCIE0A */
    TEST_ASSERT_EQUAL_INT(0, blocks);
}

static void test_init_keeps_the_mask_registers_other_bits(void)
{
    regs[TFW_AVR_TCCR0A] = 0xFFu;
    regs[TFW_AVR_TCCR0B] = 0xFFu;
    regs[TFW_AVR_TIMSK0] = 0xA5u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_tick_init());
    TEST_ASSERT_EQUAL_HEX8(0x02u, regs[TFW_AVR_TCCR0A]);    /* assigned whole */
    TEST_ASSERT_EQUAL_HEX8(0x03u, regs[TFW_AVR_TCCR0B]);
    TEST_ASSERT_EQUAL_HEX8(0xA7u, regs[TFW_AVR_TIMSK0]);    /* ORed in */
}

static void test_each_compare_is_one_millisecond(void)
{
    tfw_ms_t start = tfw_hal_millis();
    unsigned i;

    TEST_ASSERT_EQUAL_UINT32(0u, start);    /* the first test to tick: none yet */
    TEST_ASSERT_EQUAL_INT(1, blocks);
    TEST_ASSERT_EQUAL_INT(0, depth);
    TIMER0_COMPA_vect();
    TEST_ASSERT_EQUAL_UINT32(start + 1u, tfw_hal_millis());
    for (i = 0u; i < 999u; i++) {
        TIMER0_COMPA_vect();
    }
    TEST_ASSERT_EQUAL_UINT32(start + 1000u, tfw_hal_millis());
    TEST_ASSERT_EQUAL_UINT32(start + 1000u, tfw_hal_millis());   /* reading moves nothing */
    TEST_ASSERT_EQUAL_INT(4, blocks);
    TEST_ASSERT_EQUAL_UINT(0u, accesses);
}

static void test_millis_reads_inside_the_atomic_block(void)
{
    tfw_ms_t start = tfw_hal_millis();

    edge_ticks = 1;
    TEST_ASSERT_EQUAL_UINT32(start + 1u, tfw_hal_millis());     /* not start, not +2 */
    edge_ticks = 0;
    TEST_ASSERT_EQUAL_UINT32(start + 2u, tfw_hal_millis());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_is_ctc_clk64_compare_249);
    RUN_TEST(test_init_keeps_the_mask_registers_other_bits);
    RUN_TEST(test_each_compare_is_one_millisecond);
    RUN_TEST(test_millis_reads_inside_the_atomic_block);
    return UNITY_END();
}
