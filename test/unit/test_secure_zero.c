/* tfw_secure_zero (src/core/secure_zero.c): it zeroes exactly n bytes and
 * nothing past them. The guard bytes either side are 0xA5. */

#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "secure_zero.h"

#define GUARD 0xA5u
#define PAD   8u

static uint8_t buf[PAD + 32u + PAD];

void setUp(void) { memset(buf, GUARD, sizeof buf); }
void tearDown(void) {}

static void assert_zeroed_exactly(size_t n)
{
    size_t i;

    tfw_secure_zero(&buf[PAD], n);
    for (i = 0u; i < sizeof buf; i++) {
        if (i >= PAD && i < PAD + n) {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00u, buf[i], "inside the range");
        } else {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(GUARD, buf[i], "a guard byte moved");
        }
    }
}

static void test_secure_zero_of_zero_bytes_leaves_the_buffer_untouched(void) { assert_zeroed_exactly(0u); }
static void test_secure_zero_of_one_byte(void)                               { assert_zeroed_exactly(1u); }
static void test_secure_zero_of_seven_bytes(void)                            { assert_zeroed_exactly(7u); }
static void test_secure_zero_of_thirty_two_bytes(void)                       { assert_zeroed_exactly(32u); }

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_secure_zero_of_zero_bytes_leaves_the_buffer_untouched);
    RUN_TEST(test_secure_zero_of_one_byte);
    RUN_TEST(test_secure_zero_of_seven_bytes);
    RUN_TEST(test_secure_zero_of_thirty_two_bytes);
    return UNITY_END();
}
