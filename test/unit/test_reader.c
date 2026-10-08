/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include "unity.h"
#include "reader.h"

static const uint8_t PAYLOAD[] = {
    0x11,                    /* u8  */
    0x22, 0x33,              /* u16 -> 0x3322 little-endian */
    0x44, 0x55, 0x66, 0x77,  /* u32 -> 0x77665544 */
    0xAA, 0xBB               /* blob */
};

static tfw_reader_t r;

void setUp(void)    { tfw_reader_init(&r, PAYLOAD, (uint8_t)sizeof PAYLOAD); }
void tearDown(void) {}

static void test_reader_starts_clean_and_reports_the_whole_payload(void)
{
    TEST_ASSERT_TRUE(tfw_reader_ok(&r));
    TEST_ASSERT_EQUAL_UINT8(sizeof PAYLOAD, tfw_reader_remaining(&r));
}

/* Little-endian, assembled by shift rather than by cast. If this ever reads
 * 0x2233 the codec has been "optimised" into a pointer cast and will disagree
 * with the host on any machine that is not little-endian. */
static void test_reader_decodes_scalars_little_endian(void)
{
    TEST_ASSERT_EQUAL_HEX8 (0x11u,       tfw_read_u8(&r));
    TEST_ASSERT_EQUAL_HEX16(0x3322u,     tfw_read_u16(&r));
    TEST_ASSERT_EQUAL_HEX32(0x77665544u, tfw_read_u32(&r));
    TEST_ASSERT_TRUE(tfw_reader_ok(&r));
}

static void test_reader_returns_a_pointer_into_the_payload_for_a_blob(void)
{
    const uint8_t *p;
    (void)tfw_read_u8(&r); (void)tfw_read_u16(&r); (void)tfw_read_u32(&r);

    p = tfw_read_blob(&r, 2u);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_HEX8(0xAAu, p[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBBu, p[1]);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_reader_remaining(&r));
    TEST_ASSERT_TRUE(tfw_reader_ok(&r));
}

/* Consuming exactly the whole payload must succeed. An off-by-one in the
 * bound check shows up here and nowhere else. */
static void test_reader_permits_reading_exactly_to_the_end(void)
{
    TEST_ASSERT_NOT_NULL(tfw_read_blob(&r, (uint8_t)sizeof PAYLOAD));
    TEST_ASSERT_TRUE(tfw_reader_ok(&r));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_reader_remaining(&r));
}

static void test_reader_refuses_one_byte_past_the_end(void)
{
    TEST_ASSERT_NULL(tfw_read_blob(&r, (uint8_t)(sizeof PAYLOAD + 1u)));
    TEST_ASSERT_FALSE(tfw_reader_ok(&r));
}

/* THE STICKY PROPERTY. After one underflow every later read must fail too,
 * even a read that would have fitted. This is what lets a handler pull six
 * arguments and check once instead of checking six times and getting the
 * fifth wrong. */
static void test_reader_underflow_is_sticky(void)
{
    tfw_reader_t small;
    uint8_t     one = 0x5Au;

    tfw_reader_init(&small, &one, 1u);
    TEST_ASSERT_EQUAL_HEX16(0u, tfw_read_u16(&small));   /* asks for 2, has 1 */
    TEST_ASSERT_FALSE(tfw_reader_ok(&small));

    /* a byte IS available, but the cursor is failed and must stay failed */
    TEST_ASSERT_EQUAL_HEX8(0u, tfw_read_u8(&small));
    TEST_ASSERT_FALSE(tfw_reader_ok(&small));
}

/* A failed read yields zero, and zero is a legal value -- so the return can
 * never be used as a success signal. Stated as a test so the contract is
 * executable rather than only written in the header. */
static void test_reader_zero_return_is_not_a_success_signal(void)
{
    tfw_reader_t empty;
    tfw_reader_init(&empty, PAYLOAD, 0u);

    TEST_ASSERT_EQUAL_HEX8(0u, tfw_read_u8(&empty));
    TEST_ASSERT_FALSE(tfw_reader_ok(&empty));
}

/* Bad input: a length that would overflow pos + n if the bound were written
 * the naive way. cap is small, n is large, and the check must still refuse
 * rather than wrap into a false pass. */
static void test_reader_refuses_a_length_that_would_wrap_the_cursor(void)
{
    tfw_reader_t small;
    tfw_reader_init(&small, PAYLOAD, 4u);

    TEST_ASSERT_NULL(tfw_read_blob(&small, 250u));
    TEST_ASSERT_FALSE(tfw_reader_ok(&small));
}

static void test_reader_zero_length_blob_is_a_no_op(void)
{
    const uint8_t *p = tfw_read_blob(&r, 0u);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(tfw_reader_ok(&r));
    TEST_ASSERT_EQUAL_UINT8(sizeof PAYLOAD, tfw_reader_remaining(&r));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reader_starts_clean_and_reports_the_whole_payload);
    RUN_TEST(test_reader_decodes_scalars_little_endian);
    RUN_TEST(test_reader_returns_a_pointer_into_the_payload_for_a_blob);
    RUN_TEST(test_reader_permits_reading_exactly_to_the_end);
    RUN_TEST(test_reader_refuses_one_byte_past_the_end);
    RUN_TEST(test_reader_underflow_is_sticky);
    RUN_TEST(test_reader_zero_return_is_not_a_success_signal);
    RUN_TEST(test_reader_refuses_a_length_that_would_wrap_the_cursor);
    RUN_TEST(test_reader_zero_length_blob_is_a_no_op);
    return UNITY_END();
}
