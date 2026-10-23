/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/avr/hal_tone.c, register-level (#163).
 *
 * Timer2's TCCR2A, TCCR2B and OCR2A go through tfw_avr_reg() into regs[]; the
 * pin's DDR goes through _SFR_MEM8 into mem[], a model of data memory diffed
 * against a snapshot after each call. Every access is checked to fall inside
 * an ATOMIC_BLOCK. The compare values are worked by hand at 16 MHz, clk/64,
 * toggling once per match: top = 16e6 / (128 * hz) - 1, truncated.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "tfw/hal.h"
#include "avr/io.h"

#define MEM 0x100u

static uint8_t mem[MEM], before[MEM];
static uint8_t regs[TFW_AVR_REGS];
static int     atomic_depth, atomic_blocks, outside_atomic;

volatile uint8_t *tfw_avr_reg(unsigned id)
{
    TEST_ASSERT_TRUE_MESSAGE(id == TFW_AVR_TCCR2A || id == TFW_AVR_TCCR2B || id == TFW_AVR_OCR2A,
                             "hal_tone.c touched a register outside Timer2");
    if (atomic_depth == 0) {
        outside_atomic++;
    }
    return &regs[id];
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
    (void)addr;
    TEST_FAIL_MESSAGE("hal_tone.c made a word access");
    return 0;
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

/* OC2A: PB3 on a 328P (PINB 0x23), PB4 on a 2560 (the same PINB). */
static const tfw_pinref_t oc2a_328p = { 0x23u, 3u };
static const tfw_pinref_t oc2a_2560 = { 0x23u, 4u };

#define TCCR2A_SEED 0xB5u   /* COM2A0 set, other bits a pattern to keep */
#define TCCR2B_SEED 0x5Au
#define OCR2A_SEED  0xC3u

static void fill(void)
{
    unsigned i;

    for (i = 0u; i < MEM; i++) {
        mem[i] = (uint8_t)(i * 7u + 3u);
    }
    mem[0x24u] = 0x00u;                       /* DDRB: every pin an input */
    memcpy(before, mem, sizeof mem);
    memset(regs, 0, sizeof regs);
    regs[TFW_AVR_TCCR2A] = TCCR2A_SEED;
    regs[TFW_AVR_TCCR2B] = TCCR2B_SEED;
    regs[TFW_AVR_OCR2A]  = OCR2A_SEED;
    atomic_depth = atomic_blocks = outside_atomic = 0;
}

static void only_changed(unsigned a)
{
    unsigned i;

    for (i = 0u; i < MEM; i++) {
        if (i != a) {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(before[i], mem[i], "a stray write");
        }
    }
}

void setUp(void) {}
void tearDown(void) {}

static const struct {
    uint16_t hz;
    uint8_t  top;
} in_range[] = {
    { 487u, 255u },      /* the lowest reachable: 256.67 -> 256, - 1 */
    { 488u, 255u },
    { 489u, 254u },
    { 1000u, 124u },
    { 4400u, 27u },      /* 28.40 -> 28, - 1 */
    { 20000u, 5u },
    { 62500u, 1u },
    { 65535u, 0u },      /* 1.90 -> 1, - 1: the top of the uint16_t */
};

static void test_start_sets_ctc_toggle_and_top(void)
{
    const tfw_pinref_t *pins[] = { &oc2a_328p, &oc2a_2560 };
    unsigned            p, i;

    for (p = 0u; p < 2u; p++) {
        for (i = 0u; i < sizeof in_range / sizeof in_range[0]; i++) {
            fill();
            mem[0x24u] = before[0x24u] = 0x41u;       /* other outputs kept */
            TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_tone_start(pins[p], in_range[i].hz));
            TEST_ASSERT_EQUAL_HEX8((uint8_t)(0x41u | (1u << pins[p]->bit)), mem[0x24u]);
            only_changed(0x24u);
            TEST_ASSERT_EQUAL_HEX8(0x42u, regs[TFW_AVR_TCCR2A]);   /* COM2A0 | WGM21 */
            TEST_ASSERT_EQUAL_HEX8(0x04u, regs[TFW_AVR_TCCR2B]);   /* CS22 */
            TEST_ASSERT_EQUAL_UINT8(in_range[i].top, regs[TFW_AVR_OCR2A]);
            TEST_ASSERT_EQUAL_INT(1, atomic_blocks);
            TEST_ASSERT_EQUAL_INT(0, outside_atomic);
            TEST_ASSERT_EQUAL_INT(0, atomic_depth);
        }
    }
}

static void test_start_below_reach_is_range_and_touches_nothing(void)
{
    static const uint16_t low[] = { 1u, 100u, 244u, 486u };   /* 486: top 256 */
    unsigned              i;

    for (i = 0u; i < sizeof low / sizeof low[0]; i++) {
        fill();
        TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_tone_start(&oc2a_328p, low[i]));
        only_changed(MEM);
        TEST_ASSERT_EQUAL_HEX8(TCCR2A_SEED, regs[TFW_AVR_TCCR2A]);
        TEST_ASSERT_EQUAL_HEX8(TCCR2B_SEED, regs[TFW_AVR_TCCR2B]);
        TEST_ASSERT_EQUAL_HEX8(OCR2A_SEED, regs[TFW_AVR_OCR2A]);
        TEST_ASSERT_EQUAL_INT(0, atomic_blocks);
    }
}

static void check_stopped(void)
{
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(TCCR2A_SEED & ~0x40u), regs[TFW_AVR_TCCR2A]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, regs[TFW_AVR_TCCR2B]);
    TEST_ASSERT_EQUAL_HEX8(OCR2A_SEED, regs[TFW_AVR_OCR2A]);  /* left alone */
    only_changed(MEM);                                        /* pin left as it was */
    TEST_ASSERT_EQUAL_INT(1, atomic_blocks);
    TEST_ASSERT_EQUAL_INT(0, outside_atomic);
    TEST_ASSERT_EQUAL_INT(0, atomic_depth);
}

static void test_stop_disconnects_pin_and_stops_clock(void)
{
    fill();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_tone_stop());
    check_stopped();
}

static void test_start_zero_hz_is_stop(void)
{
    fill();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_tone_start(&oc2a_328p, 0u));
    check_stopped();
}

static void test_start_then_stop(void)
{
    fill();
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_tone_start(&oc2a_328p, 1000u));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_tone_stop());
    TEST_ASSERT_EQUAL_HEX8(0x02u, regs[TFW_AVR_TCCR2A]);      /* WGM21 stays */
    TEST_ASSERT_EQUAL_HEX8(0x00u, regs[TFW_AVR_TCCR2B]);
    TEST_ASSERT_EQUAL_UINT8(124u, regs[TFW_AVR_OCR2A]);
    TEST_ASSERT_EQUAL_INT(2, atomic_blocks);
    TEST_ASSERT_EQUAL_INT(0, outside_atomic);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_start_sets_ctc_toggle_and_top);
    RUN_TEST(test_start_below_reach_is_range_and_touches_nothing);
    RUN_TEST(test_stop_disconnects_pin_and_stops_clock);
    RUN_TEST(test_start_zero_hz_is_stop);
    RUN_TEST(test_start_then_stop);
    return UNITY_END();
}
