/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/avr/hal_crc16.c alone (#163), over test/mock/avr_regs's
 * <util/crc16.h>: the firmware's fast CRC update must be the MSB-first 0x1021
 * update with the caller's running value and byte in their places, not one of
 * avr-libc's two other updates, which the stand-in also carries so a wrong
 * pick builds. Every expected value is a literal, computed once by hand from
 * the polynomial: a vector computed from the code it pins would move with it.
 */
#include <stdint.h>

#include "unity.h"
#include "proto/crc16.h"

void setUp(void) {}
void tearDown(void) {}

static uint16_t run(uint16_t crc, const char *s)
{
    while (*s) {
        crc = tfw_crc16_update(crc, (uint8_t)*s++);
    }
    return crc;
}

/* The two catalogue check values of the same polynomial, one per initial value. */
static void test_check_values_of_both_variants(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x29B1u, run(0xFFFFu, "123456789"));   /* CRC-16/CCITT-FALSE */
    TEST_ASSERT_EQUAL_HEX16(0x31C3u, run(0x0000u, "123456789"));   /* CRC-16/XMODEM */
}

/* The firmware's own initial value is the CCITT-FALSE one. */
static void test_the_wire_initial_value_is_ccitt_false(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, TFW_CRC16_INIT);
    TEST_ASSERT_EQUAL_HEX16(0x29B1u, run(TFW_CRC16_INIT, "123456789"));
}

/* Single steps that separate the running value from the byte and each bit of
 * the shift from its feedback: a byte of zero still moves a non-zero value, a
 * zero value is moved by the byte alone, and the feedback lands on the top bit. */
static void test_single_steps(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xE1F0u, tfw_crc16_update(0xFFFFu, 0x00u));
    TEST_ASSERT_EQUAL_HEX16(0x1021u, tfw_crc16_update(0x0000u, 0x01u));
    TEST_ASSERT_EQUAL_HEX16(0x9188u, tfw_crc16_update(0x0000u, 0x80u));
    TEST_ASSERT_EQUAL_HEX16(0x9188u, tfw_crc16_update(0x8000u, 0x00u));
    TEST_ASSERT_EQUAL_HEX16(0x0100u, tfw_crc16_update(0x0001u, 0x00u));
    TEST_ASSERT_EQUAL_HEX16(0x3C40u, tfw_crc16_update(0x1234u, 0x56u));
    TEST_ASSERT_EQUAL_HEX16(0xFF00u, tfw_crc16_update(0xFFFFu, 0xFFu));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, tfw_crc16_update(0x0000u, 0x00u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_check_values_of_both_variants);
    RUN_TEST(test_the_wire_initial_value_is_ccitt_false);
    RUN_TEST(test_single_steps);
    return UNITY_END();
}
