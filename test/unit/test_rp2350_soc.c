/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The RP2350 port's arithmetic and encodings (src/hal/cortex-m/rp2350/
 * soc_calc.h), checked on x86: everything in the SoC files that is not a
 * register access.
 *
 * The IMAGE_DEF words are literals on purpose, as the wire vectors are: they
 * are the minimal secure Arm executable block for an RP2350 as [RP-DS]'s boot
 * ROM chapter lays it out, and a change to the macros that build them must
 * fail here rather than leave the boot ROM refusing the image on the bench.
 */

#include "unity.h"
#include "../../src/hal/cortex-m/rp2350/soc_calc.h"

void setUp(void) {}
void tearDown(void) {}

static void test_image_def_is_the_minimal_secure_arm_exe_block(void)
{
    static const uint32_t want[5] = {
        0xFFFFDED3u,    /* PICOBIN_BLOCK_MARKER_START */
        0x10210142u,    /* IMAGE_TYPE, 1 word: EXE, Secure, Arm, RP2350 */
        0x000001FFu,    /* LAST, 1 word of items before it */
        0x00000000u,    /* the link: a loop of one block */
        0xAB123579u,    /* PICOBIN_BLOCK_MARKER_END */
    };
    static const uint32_t got[TFW_RP_IMAGE_DEF_WORDS] = TFW_RP_IMAGE_DEF_INIT;

    TEST_ASSERT_EQUAL_UINT32(5u, TFW_RP_IMAGE_DEF_WORDS);
    TEST_ASSERT_EQUAL_HEX32_ARRAY(want, got, 5u);
}

static void test_xosc_startup_delay_for_the_12mhz_crystal(void)
{
    /* ((12000 + 128) / 256) * 64 = 47 * 64 */
    TEST_ASSERT_EQUAL_UINT32(3008u, tfw_rp_xosc_startup(12000000u));
    TEST_ASSERT_TRUE(tfw_rp_xosc_startup(12000000u) <= TFW_RP_XOSC_DELAY_MAX);
}

static void test_uart_divisors_at_115200_from_12mhz(void)
{
    uint32_t ibrd = 0u;
    uint32_t fbrd = 0u;

    /* 4 * 12e6 / 115200 = 416.67, rounded 417 = 6 * 64 + 33 */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_rp_uart_divisors(12000000u, 115200u, &ibrd, &fbrd));
    TEST_ASSERT_EQUAL_UINT32(6u, ibrd);
    TEST_ASSERT_EQUAL_UINT32(33u, fbrd);
}

static void test_uart_divisors_at_9600_from_12mhz(void)
{
    uint32_t ibrd = 0u;
    uint32_t fbrd = 0u;

    /* 4 * 12e6 / 9600 = 5000 = 78 * 64 + 8 */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_rp_uart_divisors(12000000u, 9600u, &ibrd, &fbrd));
    TEST_ASSERT_EQUAL_UINT32(78u, ibrd);
    TEST_ASSERT_EQUAL_UINT32(8u, fbrd);
}

static void test_uart_divisors_match_the_datasheet_worked_example(void)
{
    uint32_t ibrd = 0u;
    uint32_t fbrd = 0u;

    /* [RP-DS] §12.1.7.1: 125 MHz at 115200 baud gives BRDI 67, m 52. */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_rp_uart_divisors(125000000u, 115200u, &ibrd, &fbrd));
    TEST_ASSERT_EQUAL_UINT32(67u, ibrd);
    TEST_ASSERT_EQUAL_UINT32(52u, fbrd);
}

static void test_uart_divisors_refuse_what_the_part_cannot_make(void)
{
    uint32_t ibrd = 0xA5u;
    uint32_t fbrd = 0x5Au;

    /* No rate at all. */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_rp_uart_divisors(12000000u, 0u, &ibrd, &fbrd));
    /* Faster than clk / 16: the integer divisor would be 0. */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_rp_uart_divisors(12000000u, 1000000u, &ibrd, &fbrd));
    /* Slower than clk / (16 * 65535): the integer divisor would not fit. */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_rp_uart_divisors(12000000u, 11u, &ibrd, &fbrd));
    TEST_ASSERT_EQUAL_UINT32(0xA5u, ibrd);
    TEST_ASSERT_EQUAL_UINT32(0x5Au, fbrd);
}

static void test_uart_divisors_at_the_edges_of_the_integer_range(void)
{
    uint32_t ibrd = 0u;
    uint32_t fbrd = 0u;

    /* clk / 16 exactly: divisor 1.0 */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_rp_uart_divisors(12000000u, 750000u, &ibrd, &fbrd));
    TEST_ASSERT_EQUAL_UINT32(1u, ibrd);
    TEST_ASSERT_EQUAL_UINT32(0u, fbrd);
    /* 4 * 12e6 / 12 = 4000000 = 62500 * 64 */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_rp_uart_divisors(12000000u, 12u, &ibrd, &fbrd));
    TEST_ASSERT_EQUAL_UINT32(62500u, ibrd);
    TEST_ASSERT_EQUAL_UINT32(0u, fbrd);
}

static void test_pad_value_input_and_output(void)
{
    /* IE, 4 mA drive, Schmitt; ISO, OD, PUE and PDE clear */
    TEST_ASSERT_EQUAL_HEX32(0x52u, tfw_rp_pad_value(0u));
}

static void test_pad_value_input_pullup(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x5Au, tfw_rp_pad_value(1u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_image_def_is_the_minimal_secure_arm_exe_block);
    RUN_TEST(test_xosc_startup_delay_for_the_12mhz_crystal);
    RUN_TEST(test_uart_divisors_at_115200_from_12mhz);
    RUN_TEST(test_uart_divisors_at_9600_from_12mhz);
    RUN_TEST(test_uart_divisors_match_the_datasheet_worked_example);
    RUN_TEST(test_uart_divisors_refuse_what_the_part_cannot_make);
    RUN_TEST(test_uart_divisors_at_the_edges_of_the_integer_range);
    RUN_TEST(test_pad_value_input_and_output);
    RUN_TEST(test_pad_value_input_pullup);
    return UNITY_END();
}
