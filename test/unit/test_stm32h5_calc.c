/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 SoC's arithmetic (src/hal/cortex-m/stm32h5/h5_calc.h), checked
 * on x86: everything its HAL and tfw-boot's UART compute before a register
 * write. The register writes themselves are the part's and need the board.
 *
 * The expected values are literals worked from ST's sources (cited in
 * docs/boards/stm32h573i-dk.md), never from the code under test: the USART's
 * BRR is the kernel clock over the rate with OVER8 0; VOS3 takes 20 MHz a
 * wait state to 100 MHz; the IWDG divides the LSI by 4 << PR into a 12-bit
 * reload; GPIOA is at 0x42020000 and the banks 0x400 apart.
 */

#include <stdint.h>

#include "unity.h"
#include "../../src/hal/cortex-m/stm32h5/h5_calc.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------- USART BRR */

static void test_brr_at_the_session_rate_is_556(void)
{
    /* 64 MHz / 115200 = 555.6: rounded to the nearest, 0.08% fast. */
    TEST_ASSERT_EQUAL_UINT32(556u, tfw_h5_usart_brr(64000000u, 115200u));
}

static void test_brr_rounds_to_the_nearest(void)
{
    TEST_ASSERT_EQUAL_UINT32(6667u, tfw_h5_usart_brr(64000000u, 9600u));   /* 6666.7 */
    TEST_ASSERT_EQUAL_UINT32(1111u, tfw_h5_usart_brr(64000000u, 57600u));  /* 1111.1 */
}

static void test_brr_holds_the_usarts_range(void)
{
    TEST_ASSERT_EQUAL_UINT32(16u, tfw_h5_usart_brr(64000000u, 4000000u));
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_h5_usart_brr(64000000u, 5000000u));   /* 12.8: below 16 */
    TEST_ASSERT_EQUAL_UINT32(64000u, tfw_h5_usart_brr(64000000u, 1000u));
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_h5_usart_brr(64000000u, 900u));       /* 71111: past 0xFFFF */
}

static void test_brr_refuses_a_zero_rate(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_h5_usart_brr(64000000u, 0u));
}

static void test_brr_does_not_overflow_at_the_widest_inputs(void)
{
    /* kernel + baud / 2 would wrap in 32 bits. */
    TEST_ASSERT_EQUAL_UINT32(0u, tfw_h5_usart_brr(0xFFFFFFFFu, 0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFu, tfw_h5_usart_brr(0xFFFFFFFFu, 65537u));
}

/* ------------------------------------------------------- flash wait states */

static void test_wait_states_step_every_20_mhz(void)
{
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_h5_flash_ws_vos3(20000000u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_h5_flash_ws_vos3(20000001u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_h5_flash_ws_vos3(40000000u));
    TEST_ASSERT_EQUAL_UINT8(2u, tfw_h5_flash_ws_vos3(60000000u));
    TEST_ASSERT_EQUAL_UINT8(3u, tfw_h5_flash_ws_vos3(80000000u));
    TEST_ASSERT_EQUAL_UINT8(4u, tfw_h5_flash_ws_vos3(80000001u));
    TEST_ASSERT_EQUAL_UINT8(4u, tfw_h5_flash_ws_vos3(100000000u));
}

static void test_the_hsi_needs_three_wait_states(void)
{
    TEST_ASSERT_EQUAL_UINT8(3u, tfw_h5_flash_ws_vos3(64000000u));
}

static void test_a_clock_past_vos3_has_no_setting(void)
{
    TEST_ASSERT_EQUAL_UINT8(0xFFu, tfw_h5_flash_ws_vos3(100000001u));
    TEST_ASSERT_EQUAL_UINT8(0xFFu, tfw_h5_flash_ws_vos3(250000000u));
}

/* -------------------------------------------------------------------- IWDG */

static void test_four_seconds_from_32_khz_is_divider_32_reload_3999(void)
{
    uint8_t  pr = 0xEEu;
    uint16_t rl = 0xEEEEu;

    /* 128000 ticks; /4, /8 and /16 leave more than 4096; /32 leaves 4000. */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_h5_iwdg_timing(32000u, 4u, &pr, &rl));
    TEST_ASSERT_EQUAL_UINT8(3u, pr);
    TEST_ASSERT_EQUAL_UINT16(3999u, rl);
}

static void test_the_finest_divider_is_taken(void)
{
    uint8_t  pr = 0xEEu;
    uint16_t rl = 0xEEEEu;

    /* 32000 ticks / 4 = 8000; / 8 = 4000. */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_h5_iwdg_timing(32000u, 1u, &pr, &rl));
    TEST_ASSERT_EQUAL_UINT8(1u, pr);
    TEST_ASSERT_EQUAL_UINT16(3999u, rl);
}

static void test_the_longest_period_takes_the_last_divider(void)
{
    uint8_t  pr = 0xEEu;
    uint16_t rl = 0xEEEEu;

    /* 3200000 ticks / 1024 = 3125; / 512 = 6250 does not fit. */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_h5_iwdg_timing(32000u, 100u, &pr, &rl));
    TEST_ASSERT_EQUAL_UINT8(8u, pr);
    TEST_ASSERT_EQUAL_UINT16(3124u, rl);
}

static void test_a_period_no_divider_reaches_is_refused_untouched(void)
{
    uint8_t  pr = 0xEEu;
    uint16_t rl = 0xEEEEu;

    /* 32000000 ticks / 1024 = 31250: past the 12-bit reload. */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_h5_iwdg_timing(32000u, 1000u, &pr, &rl));
    /* No tick at all: nothing to reload to. */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_h5_iwdg_timing(32000u, 0u, &pr, &rl));
    TEST_ASSERT_EQUAL_UINT8(0xEEu, pr);
    TEST_ASSERT_EQUAL_UINT16(0xEEEEu, rl);
}

/* -------------------------------------------------------------------- GPIO */

static void test_bank_bases_are_st_s(void)
{
    /* GPIOA_BASE_NS, GPIOF_BASE_NS and GPIOI_BASE_NS in stm32h573xx.h. */
    TEST_ASSERT_EQUAL_HEX32(0x42020000u, tfw_h5_gpio_base(0x42020000u, 0x400u, 0u));
    TEST_ASSERT_EQUAL_HEX32(0x42021400u, tfw_h5_gpio_base(0x42020000u, 0x400u, 5u));
    TEST_ASSERT_EQUAL_HEX32(0x42022000u, tfw_h5_gpio_base(0x42020000u, 0x400u, 8u));
}

static void test_a_two_bit_field_keeps_its_neighbours(void)
{
    /* Reset MODER of a bank: every pin analog (0b11). */
    TEST_ASSERT_EQUAL_HEX32(0xFFEBFFFFu, tfw_h5_field2(tfw_h5_field2(0xFFFFFFFFu, 9u, 2u), 10u, 2u));
    TEST_ASSERT_EQUAL_HEX32(0x00000001u, tfw_h5_field2(0u, 0u, 1u));
    TEST_ASSERT_EQUAL_HEX32(0x40000000u, tfw_h5_field2(0u, 15u, 1u));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFF3u, tfw_h5_field2(0xFFFFFFFFu, 1u, 0u));
    /* A value wider than the field is cut to it, not spilled into pin 1. */
    TEST_ASSERT_EQUAL_HEX32(0x00000003u, tfw_h5_field2(0u, 0u, 7u));
}

static void test_a_four_bit_field_is_the_pins_index_in_its_eight(void)
{
    /* PA9 and PA10 to AF7 in AFRH: shifts 4 and 8. */
    TEST_ASSERT_EQUAL_HEX32(0x00000770u, tfw_h5_field4(tfw_h5_field4(0u, 9u, 7u), 10u, 7u));
    /* Back to AF0, the rest of the register kept. */
    TEST_ASSERT_EQUAL_HEX32(0x1234500Fu, tfw_h5_field4(tfw_h5_field4(0x1234577Fu, 9u, 0u), 10u, 0u));
    TEST_ASSERT_EQUAL_HEX32(0x70000000u, tfw_h5_field4(0u, 7u, 7u));
    TEST_ASSERT_EQUAL_HEX32(0x70000000u, tfw_h5_field4(0u, 15u, 7u));
    TEST_ASSERT_EQUAL_HEX32(0x0000000Fu, tfw_h5_field4(0u, 0u, 0x1Fu));
}

static void test_tone_psc_arr_1khz(void)
{
    uint16_t psc = 0xEEEEu;
    uint16_t arr = 0xEEEEu;

    /* 1 kHz toggles 2000 times a second: 64 MHz / 2000 = 32000 ticks per
     * match, inside the 16-bit counter undivided. */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_h5_tone_psc_arr(64000000u, 1000u, &psc, &arr));
    TEST_ASSERT_EQUAL_UINT16(0u, psc);
    TEST_ASSERT_EQUAL_UINT16(31999u, arr);
}

static void test_tone_takes_the_finest_prescaler(void)
{
    uint16_t psc = 0u;
    uint16_t arr = 0u;

    /* 440 Hz: 72727 ticks a match, past 16 bits; /2 gives 36364 (rounded). */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_h5_tone_psc_arr(64000000u, 440u, &psc, &arr));
    TEST_ASSERT_EQUAL_UINT16(1u, psc);
    TEST_ASSERT_EQUAL_UINT16(36363u, arr);
    /* 20 Hz: 1600000 ticks; 1600000 / 65536 needs /25, leaving 64000. */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_h5_tone_psc_arr(64000000u, 20u, &psc, &arr));
    TEST_ASSERT_EQUAL_UINT16(24u, psc);
    TEST_ASSERT_EQUAL_UINT16(63999u, arr);
    /* The wire's top, 65535 Hz: 488.3 ticks, rounded to 488. */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_h5_tone_psc_arr(64000000u, 65535u, &psc, &arr));
    TEST_ASSERT_EQUAL_UINT16(0u, psc);
    TEST_ASSERT_EQUAL_UINT16(487u, arr);
    /* 1 Hz, the wire's bottom: 32000000 ticks, /489 leaves 65440. */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_h5_tone_psc_arr(64000000u, 1u, &psc, &arr));
    TEST_ASSERT_EQUAL_UINT16(488u, psc);
    TEST_ASSERT_EQUAL_UINT16(65439u, arr);
}

static void test_tone_a_rate_the_timer_cannot_reach_is_refused_untouched(void)
{
    uint16_t psc = 0xEEEEu;
    uint16_t arr = 0xEEEEu;

    /* No frequency. */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_h5_tone_psc_arr(64000000u, 0u, &psc, &arr));
    /* Past half the clock: under two counts a match. */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_h5_tone_psc_arr(64000000u, 40000000u, &psc, &arr));
    /* The widest clock at 1 Hz still fits: /32768, inside the prescaler. */
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_h5_tone_psc_arr(0xFFFFFFFFu, 1u, &psc, &arr));
    TEST_ASSERT_EQUAL_UINT16(32767u, psc);
    psc = 0xEEEEu;
    arr = 0xEEEEu;
    /* One count a match: the counter cannot toggle at the clock's half. */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_h5_tone_psc_arr(1000u, 1000u, &psc, &arr));
    TEST_ASSERT_EQUAL_UINT16(0xEEEEu, psc);
    TEST_ASSERT_EQUAL_UINT16(0xEEEEu, arr);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_brr_at_the_session_rate_is_556);
    RUN_TEST(test_brr_rounds_to_the_nearest);
    RUN_TEST(test_brr_holds_the_usarts_range);
    RUN_TEST(test_brr_refuses_a_zero_rate);
    RUN_TEST(test_brr_does_not_overflow_at_the_widest_inputs);
    RUN_TEST(test_wait_states_step_every_20_mhz);
    RUN_TEST(test_the_hsi_needs_three_wait_states);
    RUN_TEST(test_a_clock_past_vos3_has_no_setting);
    RUN_TEST(test_four_seconds_from_32_khz_is_divider_32_reload_3999);
    RUN_TEST(test_the_finest_divider_is_taken);
    RUN_TEST(test_the_longest_period_takes_the_last_divider);
    RUN_TEST(test_a_period_no_divider_reaches_is_refused_untouched);
    RUN_TEST(test_bank_bases_are_st_s);
    RUN_TEST(test_a_two_bit_field_keeps_its_neighbours);
    RUN_TEST(test_a_four_bit_field_is_the_pins_index_in_its_eight);
    RUN_TEST(test_tone_psc_arr_1khz);
    RUN_TEST(test_tone_takes_the_finest_prescaler);
    RUN_TEST(test_tone_a_rate_the_timer_cannot_reach_is_refused_untouched);
    return UNITY_END();
}
