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

/* The bound refusal in give() for a one-byte write: at capacity the cursor
 * stays at capacity and nothing lands past the buffer. */
static void test_a_u8_past_capacity_is_refused_and_the_cursor_stays(void)
{
    uint8_t      area[3];
    tfw_writer_t s;

    memset(area, 0xEE, sizeof area);
    tfw_writer_init(&s, area, 2u);
    tfw_write_u16(&s, 0x2211u);

    tfw_write_u8(&s, 0x5Au);
    TEST_ASSERT_FALSE(tfw_writer_ok(&s));
    TEST_ASSERT_EQUAL_UINT8(2u, tfw_writer_len(&s));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_remaining(&s));
    TEST_ASSERT_EQUAL_HEX8(0xEEu, area[2]);
}

/* The bound refusal for a u16 with one byte of room mid-buffer: the one byte
 * is not half-filled and the cursor does not move. */
static void test_a_u16_with_one_byte_of_room_is_refused_and_writes_nothing(void)
{
    uint8_t      area[2];
    tfw_writer_t s;

    memset(area, 0xEE, sizeof area);
    tfw_writer_init(&s, area, (uint8_t)sizeof area);
    tfw_write_u8(&s, 0x11u);

    tfw_write_u16(&s, 0x3322u);
    TEST_ASSERT_FALSE(tfw_writer_ok(&s));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_writer_len(&s));
    TEST_ASSERT_EQUAL_HEX8(0x11u, area[0]);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, area[1]);
}

/* The bound refusal for a u32 with three bytes of room mid-buffer. */
static void test_a_u32_with_three_bytes_of_room_is_refused_and_writes_nothing(void)
{
    uint8_t      i;

    for (i = 0u; i < CAP - 3u; i++) {
        tfw_write_u8(&w, i);
    }
    tfw_write_u32(&w, 0x77665544u);
    TEST_ASSERT_FALSE(tfw_writer_ok(&w));
    TEST_ASSERT_EQUAL_UINT8(CAP - 3u, tfw_writer_len(&w));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEEu, &buf[CAP - 3u], 3u);
}

/* The sticky refusal in give(): after an overflow every write is refused,
 * a zero-length blob included, and none of them moves the cursor or stores. */
static void test_every_write_after_an_overflow_is_refused_and_writes_nothing(void)
{
    static const uint8_t big[CAP + 1u] = { 0 };
    const uint8_t        one = 0x5Au;

    tfw_write_blob(&w, big, CAP + 1u);
    TEST_ASSERT_FALSE(tfw_writer_ok(&w));

    tfw_write_u8(&w, 0x5Au);
    tfw_write_u16(&w, 0x3322u);
    tfw_write_u32(&w, 0x77665544u);
    tfw_write_blob(&w, &one, 1u);
    tfw_write_blob(&w, &one, 0u);
    TEST_ASSERT_FALSE(tfw_writer_ok(&w));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_UINT8(CAP, tfw_writer_remaining(&w));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xEEu, buf, CAP);
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
    RUN_TEST(test_a_u8_past_capacity_is_refused_and_the_cursor_stays);
    RUN_TEST(test_a_u16_with_one_byte_of_room_is_refused_and_writes_nothing);
    RUN_TEST(test_a_u32_with_three_bytes_of_room_is_refused_and_writes_nothing);
    RUN_TEST(test_every_write_after_an_overflow_is_refused_and_writes_nothing);
    return UNITY_END();
}
