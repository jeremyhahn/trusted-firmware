/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "unity.h"
#include "crc16.h"
#include "vectors.h"

void setUp(void) {}
void tearDown(void) {}

/* --------------------------------------------------------------- ag_crc16 */

/* The one value every CRC-16/CCITT-FALSE implementation in the world agrees
 * on. If this fails, the polynomial, the initial value or the bit order is
 * wrong, and nothing else in the suite is worth reading. */
static void test_crc16_matches_the_published_check_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(AGV_CRC_CHECK_RESULT,
        ag_crc16((const uint8_t *)AGV_CRC_CHECK_INPUT, AGV_CRC_CHECK_LEN));
}

/* Folding nothing must return the seed untouched. An implementation that
 * special-cases a zero length, or that folds one byte before testing the
 * loop condition, fails here and nowhere else. */
static void test_crc16_of_empty_input_is_the_initial_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(AGV_CRC_EMPTY_RESULT, ag_crc16((const uint8_t *)"", 0));
    TEST_ASSERT_EQUAL_HEX16(AG_CRC16_INIT,        ag_crc16((const uint8_t *)"", 0));
}

/* A zero byte is not a no-op: it still shifts the register eight times. This
 * separates "returned the seed" from "processed one byte", which the empty
 * case alone cannot. */
static void test_crc16_of_a_zero_byte_is_not_the_initial_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(AGV_CRC_ZERO_RESULT, ag_crc16((const uint8_t *)"\x00", 1));
    TEST_ASSERT_NOT_EQUAL(AG_CRC16_INIT,         ag_crc16((const uint8_t *)"\x00", 1));
}

static void test_crc16_of_two_zero_bytes(void)
{
    TEST_ASSERT_EQUAL_HEX16(AGV_CRC_ZEROZERO_RESULT, ag_crc16((const uint8_t *)"\x00\x00", 2));
}

/* 0xFF drives the high bit on the first fold, so every branch of the bitwise
 * loop is exercised before any other byte has been seen. */
static void test_crc16_of_all_ones_byte(void)
{
    TEST_ASSERT_EQUAL_HEX16(AGV_CRC_FF_RESULT, ag_crc16((const uint8_t *)"\xFF", 1));
}

/* THE DISCRIMINATING CASE. These inputs are permutations of each other, so a
 * checksum that sums or XORs its bytes returns the same value for both and
 * passes every test above. The frame codec depends on order sensitivity: a
 * transposition inside a payload must not produce the same CRC. */
static void test_crc16_is_order_sensitive(void)
{
    uint16_t a = ag_crc16((const uint8_t *)AGV_CRC_ORDER_A, 2);
    uint16_t b = ag_crc16((const uint8_t *)AGV_CRC_ORDER_B, 2);

    TEST_ASSERT_EQUAL_HEX16(AGV_CRC_ORDER_A_RESULT, a);
    TEST_ASSERT_EQUAL_HEX16(AGV_CRC_ORDER_B_RESULT, b);
    TEST_ASSERT_NOT_EQUAL(a, b);
}

/* A single-bit change anywhere must change the CRC. This is the property the
 * wire actually relies on, stated directly rather than inferred from the
 * check value. */
static void test_crc16_detects_every_single_bit_flip_in_a_frame_sized_buffer(void)
{
    uint8_t  buf[132];
    uint16_t clean;
    size_t   i;
    int      bit;

    for (i = 0; i < sizeof buf; i++) {
        buf[i] = (uint8_t)(i * 7u + 3u);
    }
    clean = ag_crc16(buf, (uint16_t)sizeof buf);

    for (i = 0; i < sizeof buf; i++) {
        for (bit = 0; bit < 8; bit++) {
            uint16_t dirty;
            buf[i] ^= (uint8_t)(1u << bit);
            dirty = ag_crc16(buf, (uint16_t)sizeof buf);
            buf[i] ^= (uint8_t)(1u << bit);
            TEST_ASSERT_NOT_EQUAL(clean, dirty);
        }
    }
}

/* -------------------------------------------------------- ag_crc16_update */

/* The incremental and bulk forms must be the same function. The framer folds
 * bytes as they arrive and never has the whole frame in one buffer, so if
 * these two ever diverge the framer and the vectors stop agreeing. */
static void test_crc16_update_incrementally_equals_bulk(void)
{
    const uint8_t *msg = (const uint8_t *)AGV_CRC_CHECK_INPUT;
    uint16_t       crc = AG_CRC16_INIT;
    uint8_t        i;

    for (i = 0; i < AGV_CRC_CHECK_LEN; i++) {
        crc = ag_crc16_update(crc, msg[i]);
    }
    TEST_ASSERT_EQUAL_HEX16(ag_crc16(msg, AGV_CRC_CHECK_LEN), crc);
    TEST_ASSERT_EQUAL_HEX16(AGV_CRC_CHECK_RESULT, crc);
}

/* Bad input: a caller that seeds with the wrong value must not accidentally
 * land on the right answer. Guards against an implementation that ignores the
 * incoming crc and re-seeds internally. */
static void test_crc16_update_honours_the_seed_it_is_given(void)
{
    TEST_ASSERT_NOT_EQUAL(ag_crc16_update(AG_CRC16_INIT, 0x00u),
                          ag_crc16_update(0x0000u,       0x00u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc16_matches_the_published_check_value);
    RUN_TEST(test_crc16_of_empty_input_is_the_initial_value);
    RUN_TEST(test_crc16_of_a_zero_byte_is_not_the_initial_value);
    RUN_TEST(test_crc16_of_two_zero_bytes);
    RUN_TEST(test_crc16_of_all_ones_byte);
    RUN_TEST(test_crc16_is_order_sensitive);
    RUN_TEST(test_crc16_detects_every_single_bit_flip_in_a_frame_sized_buffer);
    RUN_TEST(test_crc16_update_incrementally_equals_bulk);
    RUN_TEST(test_crc16_update_honours_the_seed_it_is_given);
    return UNITY_END();
}
