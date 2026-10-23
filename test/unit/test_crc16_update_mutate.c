/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/proto/crc16_update.c, one fold at a time, against literals.
 *
 * test_crc16 pins the whole-message results the wire vectors carry. This suite
 * pins the single fold underneath them, so that a fault in it is named by the
 * byte or seed bit it mishandles: CRC-16/CCITT-FALSE is polynomial 0x1021,
 * MSB first, no reflection, so folding a byte into a zero register gives the
 * byte's entry of the published 256-entry table, and folding a zero byte into
 * a seed shifts the seed through the same eight steps. Each expected value is
 * the table's, written out; none is computed by the code under test.
 */

#include "unity.h"
#include "crc16.h"

void setUp(void) {}
void tearDown(void) {}

/* One bit of the incoming byte at a time, then all eight: a wrong shift of the
 * byte into the register, a wrong loop count or a wrong polynomial moves at
 * least one of these. */
static void test_each_byte_bit_folds_to_its_table_entry(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x1021u, tfw_crc16_update(0x0000u, 0x01u));
    TEST_ASSERT_EQUAL_HEX16(0x2042u, tfw_crc16_update(0x0000u, 0x02u));
    TEST_ASSERT_EQUAL_HEX16(0x4084u, tfw_crc16_update(0x0000u, 0x04u));
    TEST_ASSERT_EQUAL_HEX16(0x8108u, tfw_crc16_update(0x0000u, 0x08u));
    TEST_ASSERT_EQUAL_HEX16(0x1231u, tfw_crc16_update(0x0000u, 0x10u));
    TEST_ASSERT_EQUAL_HEX16(0x2462u, tfw_crc16_update(0x0000u, 0x20u));
    TEST_ASSERT_EQUAL_HEX16(0x48C4u, tfw_crc16_update(0x0000u, 0x40u));
    TEST_ASSERT_EQUAL_HEX16(0x9188u, tfw_crc16_update(0x0000u, 0x80u));
    TEST_ASSERT_EQUAL_HEX16(0x1EF0u, tfw_crc16_update(0x0000u, 0xFFu));
}

/* A zero register and a zero byte stay zero: no constant is folded in. */
static void test_zero_into_zero_is_zero(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0000u, tfw_crc16_update(0x0000u, 0x00u));
}

/* The seed's low byte moves up eight places untouched; its high byte is
 * folded as a message byte would be. */
static void test_the_seed_is_shifted_through_the_same_eight_steps(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0100u, tfw_crc16_update(0x0001u, 0x00u));
    TEST_ASSERT_EQUAL_HEX16(0x8000u, tfw_crc16_update(0x0080u, 0x00u));
    TEST_ASSERT_EQUAL_HEX16(0x1021u, tfw_crc16_update(0x0100u, 0x00u));
    TEST_ASSERT_EQUAL_HEX16(0x9188u, tfw_crc16_update(0x8000u, 0x00u));
    TEST_ASSERT_EQUAL_HEX16(0xE1F0u, tfw_crc16_update(0xFFFFu, 0x00u));
    TEST_ASSERT_EQUAL_HEX16(0xCC9Cu, tfw_crc16_update(0x1D0Fu, 0x00u));
}

/* The byte is XORed into the register's high byte, not ORed or added: 0xFF
 * into 0xFFFF cancels the high byte and leaves the low byte's shift. */
static void test_the_byte_meets_the_seed_by_xor(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFF00u, tfw_crc16_update(0xFFFFu, 0xFFu));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, tfw_crc16_update(0x0100u, 0x01u));
}

/* The published check value, one fold at a time from the initial value. */
static void test_folding_123456789_gives_0x29b1(void)
{
    static const uint8_t msg[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    uint16_t crc = 0xFFFFu;
    unsigned i;

    for (i = 0u; i < sizeof msg; i++) {
        crc = tfw_crc16_update(crc, msg[i]);
    }
    TEST_ASSERT_EQUAL_HEX16(0x29B1u, crc);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_byte_bit_folds_to_its_table_entry);
    RUN_TEST(test_zero_into_zero_is_zero);
    RUN_TEST(test_the_seed_is_shifted_through_the_same_eight_steps);
    RUN_TEST(test_the_byte_meets_the_seed_by_xor);
    RUN_TEST(test_folding_123456789_gives_0x29b1);
    return UNITY_END();
}
