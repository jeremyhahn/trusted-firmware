/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/avr/hal_gpio.c, register-level (#163).
 *
 * A pin is a PINx data-memory address and a bit, DDRx and PORTx at +1 and +2.
 * The <avr/io.h> stand-in's _SFR_MEM8 calls tfw_avr_mem() below, which
 * indexes mem[], a model of data memory, and counts any access made outside an
 * ATOMIC_BLOCK. Every test diffs mem[] against a snapshot, so a write to any
 * byte but the expected one fails. Two ports: PINB on a 328P (0x23) and PINL
 * on a 2560 (0x109), past the low I/O space.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "tfw/hal.h"
#include "avr/io.h"

#define MEM 0x200u

static uint8_t mem[MEM], before[MEM];
static int     atomic_depth, atomic_blocks, outside_atomic, accesses;

volatile uint8_t *tfw_avr_reg(unsigned id)
{
    (void)id;
    TEST_FAIL_MESSAGE("hal_gpio.c names a fixed register");
    return 0;
}

volatile uint8_t *tfw_avr_mem(unsigned addr)
{
    TEST_ASSERT_TRUE(addr < MEM);
    accesses++;
    if (atomic_depth == 0) {
        outside_atomic++;
    }
    return &mem[addr];
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

static const uint16_t ports[] = { 0x23u, 0x109u };

static void fill(uint8_t pin, uint8_t ddr, uint8_t port, uint16_t at)
{
    unsigned i;

    for (i = 0u; i < MEM; i++) {
        mem[i] = (uint8_t)(i * 7u + 3u);
    }
    mem[at]      = pin;
    mem[at + 1u] = ddr;
    mem[at + 2u] = port;
    memcpy(before, mem, sizeof mem);
    atomic_depth = atomic_blocks = outside_atomic = accesses = 0;
}

/* Every byte but `at` as it was. */
static void only_changed(uint16_t at)
{
    unsigned i;

    for (i = 0u; i < MEM; i++) {
        if (i != at) {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(before[i], mem[i], "a stray write");
        }
    }
}

void setUp(void) {}
void tearDown(void) {}

static void test_output_sets_only_its_ddr_bit(void)
{
    unsigned     n, b;
    tfw_pinref_t p;

    for (n = 0u; n < 2u; n++) {
        for (b = 0u; b < 8u; b++) {
            p.port = ports[n];
            p.bit  = (uint8_t)b;
            fill(0x00u, 0x5Au, 0x00u, p.port);
            TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_gpio_mode(&p, TFW_PIN_OUTPUT));
            TEST_ASSERT_EQUAL_HEX8((uint8_t)(0x5Au | (1u << b)), mem[p.port + 1u]);
            only_changed((uint16_t)(p.port + 1u));
            TEST_ASSERT_EQUAL_INT(1, atomic_blocks);
            TEST_ASSERT_EQUAL_INT(0, outside_atomic);
        }
    }
}

static void test_input_clears_direction_and_pullup(void)
{
    unsigned     n, b;
    tfw_pinref_t p;

    for (n = 0u; n < 2u; n++) {
        for (b = 0u; b < 8u; b++) {
            p.port = ports[n];
            p.bit  = (uint8_t)b;
            fill(0xFFu, 0xFFu, 0xA5u | (uint8_t)(1u << b), p.port);
            TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_gpio_mode(&p, TFW_PIN_INPUT));
            TEST_ASSERT_EQUAL_HEX8((uint8_t)~(1u << b), mem[p.port + 1u]);
            TEST_ASSERT_EQUAL_HEX8((uint8_t)((0xA5u | (1u << b)) & ~(1u << b)), mem[p.port + 2u]);
            TEST_ASSERT_EQUAL_HEX8(0xFFu, mem[p.port]);
            TEST_ASSERT_EQUAL_INT(1, atomic_blocks);
            TEST_ASSERT_EQUAL_INT(0, outside_atomic);
            mem[p.port + 1u] = before[p.port + 1u];
            mem[p.port + 2u] = before[p.port + 2u];
            only_changed(0xFFFFu);
        }
    }
}

static void test_pullup_clears_direction_and_sets_latch(void)
{
    unsigned     n, b;
    tfw_pinref_t p;

    for (n = 0u; n < 2u; n++) {
        for (b = 0u; b < 8u; b++) {
            p.port = ports[n];
            p.bit  = (uint8_t)b;
            fill(0x00u, 0xC3u | (uint8_t)(1u << b), 0x18u, p.port);
            TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_gpio_mode(&p, TFW_PIN_INPUT_PULLUP));
            TEST_ASSERT_EQUAL_HEX8((uint8_t)((0xC3u | (1u << b)) & ~(1u << b)), mem[p.port + 1u]);
            TEST_ASSERT_EQUAL_HEX8((uint8_t)(0x18u | (1u << b)), mem[p.port + 2u]);
            TEST_ASSERT_EQUAL_INT(1, atomic_blocks);
            TEST_ASSERT_EQUAL_INT(0, outside_atomic);
            mem[p.port + 1u] = before[p.port + 1u];
            mem[p.port + 2u] = before[p.port + 2u];
            only_changed(0xFFFFu);
        }
    }
}

static void test_unknown_mode_is_refused_untouched(void)
{
    static const unsigned bad[] = { 3u, 4u, 0x7Fu, 0xFFu };
    unsigned              i;
    tfw_pinref_t          p = { 0x23u, 3u };

    for (i = 0u; i < sizeof bad / sizeof bad[0]; i++) {
        fill(0x11u, 0x22u, 0x33u, p.port);
        TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_hal_gpio_mode(&p, (tfw_pin_mode_t)bad[i]));
        only_changed(0xFFFFu);
        TEST_ASSERT_EQUAL_INT(0, accesses);
    }
}

static void test_write_drives_only_its_latch_bit(void)
{
    static const uint8_t levels[] = { 1u, 2u, 0x80u, 0xFFu };
    unsigned             n, b, l;
    tfw_pinref_t         p;

    for (n = 0u; n < 2u; n++) {
        for (b = 0u; b < 8u; b++) {
            p.port = ports[n];
            p.bit  = (uint8_t)b;
            for (l = 0u; l < sizeof levels; l++) {     /* any non-zero is high */
                fill(0xFFu, 0xFFu, 0x24u & (uint8_t)~(1u << b), p.port);
                TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_gpio_write(&p, levels[l]));
                TEST_ASSERT_EQUAL_HEX8((uint8_t)((0x24u & ~(1u << b)) | (1u << b)), mem[p.port + 2u]);
                only_changed((uint16_t)(p.port + 2u));
                TEST_ASSERT_EQUAL_INT(1, atomic_blocks);
                TEST_ASSERT_EQUAL_INT(0, outside_atomic);
            }
            fill(0x00u, 0x00u, 0xDBu | (uint8_t)(1u << b), p.port);
            TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_gpio_write(&p, 0u));
            TEST_ASSERT_EQUAL_HEX8((uint8_t)((0xDBu | (1u << b)) & ~(1u << b)), mem[p.port + 2u]);
            only_changed((uint16_t)(p.port + 2u));
            TEST_ASSERT_EQUAL_INT(1, atomic_blocks);
            TEST_ASSERT_EQUAL_INT(0, outside_atomic);
        }
    }
}

static void test_read_is_the_pin_register_as_0_or_1(void)
{
    unsigned     n, b;
    uint8_t      v;
    tfw_pinref_t p;

    for (n = 0u; n < 2u; n++) {
        for (b = 0u; b < 8u; b++) {
            p.port = ports[n];
            p.bit  = (uint8_t)b;
            /* PORT and DDR say the opposite of PIN: only PIN is the wire. */
            fill((uint8_t)(1u << b), (uint8_t)~(1u << b), (uint8_t)~(1u << b), p.port);
            v = 0xEEu;
            TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_gpio_read(&p, &v));
            TEST_ASSERT_EQUAL_UINT8(1u, v);
            fill((uint8_t)~(1u << b), (uint8_t)(1u << b), (uint8_t)(1u << b), p.port);
            v = 0xEEu;
            TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_hal_gpio_read(&p, &v));
            TEST_ASSERT_EQUAL_UINT8(0u, v);
            only_changed(0xFFFFu);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_output_sets_only_its_ddr_bit);
    RUN_TEST(test_input_clears_direction_and_pullup);
    RUN_TEST(test_pullup_clears_direction_and_sets_latch);
    RUN_TEST(test_unknown_mode_is_refused_untouched);
    RUN_TEST(test_write_drives_only_its_latch_bit);
    RUN_TEST(test_read_is_the_pin_register_as_0_or_1);
    return UNITY_END();
}
