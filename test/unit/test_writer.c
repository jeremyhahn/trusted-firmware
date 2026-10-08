/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include "unity.h"
#include "writer.h"

#define CAP 16u

static uint8_t     buf[CAP];
static tfw_writer_t w;

void setUp(void)
{
    memset(buf, 0xEE, sizeof buf);          /* poison, so "untouched" is visible */
    tfw_writer_init(&w, buf, CAP);
}
void tearDown(void) {}

static void test_writer_starts_empty_and_clean(void)
{
    TEST_ASSERT_TRUE(tfw_writer_ok(&w));
    TEST_ASSERT_EQUAL_UINT8(0u,  tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_UINT8(CAP, tfw_writer_remaining(&w));
}

static void test_writer_encodes_scalars_little_endian(void)
{
    tfw_write_u8(&w,  0x11u);
    tfw_write_u16(&w, 0x3322u);
    tfw_write_u32(&w, 0x77665544u);

    TEST_ASSERT_TRUE(tfw_writer_ok(&w));
    TEST_ASSERT_EQUAL_UINT8(7u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8(0x11u, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22u, buf[1]);  /* low byte first */
    TEST_ASSERT_EQUAL_HEX8(0x33u, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x44u, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x55u, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x66u, buf[5]);
    TEST_ASSERT_EQUAL_HEX8(0x77u, buf[6]);
}

static void test_writer_copies_a_blob(void)
{
    const uint8_t src[3] = { 0xA1u, 0xB2u, 0xC3u };
    tfw_write_blob(&w, src, 3u);

    TEST_ASSERT_TRUE(tfw_writer_ok(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(src, buf, 3u);
}

/* Filling the buffer exactly must succeed. The off-by-one lives here. */
static void test_writer_permits_filling_to_capacity_exactly(void)
{
    uint8_t i;
    for (i = 0u; i < CAP; i++) {
        tfw_write_u8(&w, i);
    }
    TEST_ASSERT_TRUE(tfw_writer_ok(&w));
    TEST_ASSERT_EQUAL_UINT8(CAP, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_UINT8(0u,  tfw_writer_remaining(&w));
}

static void test_writer_refuses_one_byte_past_capacity(void)
{
    uint8_t i;
    for (i = 0u; i < CAP; i++) {
        tfw_write_u8(&w, i);
    }
    tfw_write_u8(&w, 0xFFu);
    TEST_ASSERT_FALSE(tfw_writer_ok(&w));
}

/* THE DEFECT THIS TYPE EXISTS TO PREVENT.
 *
 * The firmware being replaced chained strcat() into a fixed char[375] with no
 * check, so an over-long field wrote past the buffer into whatever followed it
 * in SRAM. Here the refused write must leave the buffer untouched -- not
 * partially filled, not truncated. A half-written field would produce a
 * well-formed frame carrying a wrong value, which is worse than one that
 * reports failure. */
static void test_writer_overflowing_write_stores_nothing(void)
{
    tfw_writer_t tight;
    uint8_t     small[4];
    const uint8_t src[8] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };

    memset(small, 0xEE, sizeof small);
    tfw_writer_init(&tight, small, (uint8_t)sizeof small);

    tfw_write_blob(&tight, src, 8u);          /* needs 8, has 4 */

    TEST_ASSERT_FALSE(tfw_writer_ok(&tight));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&tight));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEEu, small, sizeof small);  /* nothing written */
}

/* THE STICKY PROPERTY. After one overflow every later write must be refused,
 * including one that would have fitted -- otherwise a handler that emitted a
 * long field followed by a short one produces a frame missing its middle. */
static void test_writer_overflow_is_sticky(void)
{
    tfw_writer_t tight;
    uint8_t     small[2];

    memset(small, 0xEE, sizeof small);
    tfw_writer_init(&tight, small, (uint8_t)sizeof small);

    tfw_write_u32(&tight, 0xDEADBEEFu);       /* needs 4, has 2 */
    TEST_ASSERT_FALSE(tfw_writer_ok(&tight));

    tfw_write_u8(&tight, 0x5Au);              /* would fit, must still refuse */
    TEST_ASSERT_FALSE(tfw_writer_ok(&tight));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEEu, small, sizeof small);
}

/* Bad input: a length that would wrap pos + n if the bound were written the
 * naive way. cap 16, n 250 -- the check must refuse, not wrap into a pass. */
static void test_writer_refuses_a_length_that_would_wrap_the_cursor(void)
{
    static const uint8_t big[250] = { 0 };
    tfw_write_blob(&w, big, 250u);

    TEST_ASSERT_FALSE(tfw_writer_ok(&w));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
}

static void test_writer_zero_length_blob_is_a_no_op(void)
{
    tfw_write_blob(&w, buf, 0u);
    TEST_ASSERT_TRUE(tfw_writer_ok(&w));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_writer_starts_empty_and_clean);
    RUN_TEST(test_writer_encodes_scalars_little_endian);
    RUN_TEST(test_writer_copies_a_blob);
    RUN_TEST(test_writer_permits_filling_to_capacity_exactly);
    RUN_TEST(test_writer_refuses_one_byte_past_capacity);
    RUN_TEST(test_writer_overflowing_write_stores_nothing);
    RUN_TEST(test_writer_overflow_is_sticky);
    RUN_TEST(test_writer_refuses_a_length_that_would_wrap_the_cursor);
    RUN_TEST(test_writer_zero_length_blob_is_a_no_op);
    return UNITY_END();
}
