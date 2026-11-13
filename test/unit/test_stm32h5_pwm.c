/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 PWM's register values (src/hal/cortex-m/stm32h5/h5_calc.h,
 * hal_pwm.c) through tfw_hal_pwm_write and tfw_hal_pwm_stop on the register
 * file (test/model/cortex-m/regfile_model.c): the prescaler for about 1 kHz,
 * the duty's compare value, each channel's CCMR and CCER bits, and stop
 * leaving the pin a low GPIO output. A board has not run them.
 */
#include <stdint.h>

#include "unity.h"
#include "tfw/hal.h"
#include "soc_regs.h"
#include "h5_calc.h"
#include "regfile_model.h"
#include "../../src/caps/pwm.h"

/* The Arduino header's D6 and D5: PH10 and PH11, TIM5 channels 1 and 2
 * (boards/stm32h573i-dk/board.c). */
#define D6     4u
#define D5     5u
#define GPIOH  tfw_h5_gpio_base(TFW_H5_GPIOA_BASE, TFW_H5_GPIO_STRIDE, 7u)

/* TIM5 register addresses (RM0481 TIMx register map), for the register
 * file's peek, which reads a word without allocating it. */
#define TIM5_CCMR1_ADDR    ((uintptr_t)TFW_H5_TIM5_BASE + 0x18u)
#define TIM5_CCMR2_ADDR    ((uintptr_t)TFW_H5_TIM5_BASE + 0x1Cu)
#define TIM5_CCR_ADDR(ch)  ((uintptr_t)TFW_H5_TIM5_BASE + 0x30u + 4u * (uintptr_t)(ch))

/* PWM_WRITE as the wire hands it to the command: pin, duty. */
static tfw_status_t wire_write(uint8_t pin, uint8_t duty)
{
    uint8_t      req[2] = { pin, duty };
    tfw_reader_t r;

    tfw_reader_init(&r, req, (uint8_t)sizeof req);
    return tfw_pwm_cmd_write(&r, NULL);
}

static uint32_t mode_of(uint8_t bit)
{
    return (TFW_H5_GPIO_MODER(GPIOH) >> (2u * bit)) & 0x3u;
}

static uint32_t af_of(uint8_t bit)
{
    return (TFW_H5_GPIO_AFRH(GPIOH) >> (4u * (bit & 7u))) & 0xFu;
}

/* TIM5's channel ch on PH(9 + ch): D6 and D5 for channels 1 and 2, a pin of
 * its own for each of the others. */
static tfw_pwmref_t ref_of(uint8_t channel)
{
    tfw_pwmref_t ref = { .unit = TFW_H5_PWM_UNIT, .channel = channel, .port = 7u,
                         .bit = (uint8_t)(9u + channel) };

    return ref;
}

static tfw_status_t hal_write(uint8_t channel, uint8_t duty)
{
    tfw_pwmref_t ref = ref_of(channel);

    return tfw_hal_pwm_write(&ref, duty);
}

static tfw_status_t hal_stop(uint8_t channel)
{
    tfw_pwmref_t ref = ref_of(channel);

    return tfw_hal_pwm_stop(&ref);
}

/* PWM mode 1: the output is high while the counter, 0 to ARR, is below CCR. */
static uint32_t high_counts(uint8_t channel)
{
    uint32_t arr = TFW_H5_TIM_ARR;
    uint32_t ccr = TFW_H5_TIM_CCR(channel);
    uint32_t n   = 0u;
    uint32_t cnt;

    for (cnt = 0u; cnt <= arr; cnt++) {
        if (cnt < ccr) {
            n++;
        }
    }
    return n;
}

/* The pin is a GPIO output and its last BSRR write reset it: low. */
static void assert_low_output(uint8_t bit)
{
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_MODE_OUTPUT, mode_of(bit));
    TEST_ASSERT_EQUAL_HEX32(1u << (bit + 16u), TFW_H5_GPIO_BSRR(GPIOH));
}

void setUp(void)
{
    regfile_model_reset();
}

/* hal_pwm.c keeps which channels are live; stop them all so each test
 * starts from reset. */
void tearDown(void)
{
    uint8_t ch;

    for (ch = 1u; ch <= 4u; ch++) {
        (void)hal_stop(ch);
    }
}

/* The command makes the pin a GPIO output before each write (src/caps/
 * pwm.c); a second write must hand it back to the timer, or PWM_WRITE
 * answers OK with the pin a plain output. */
void test_each_wire_write_gives_the_pin_back_to_the_timer(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, wire_write(D6, 100u));
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_MODE_AF, mode_of(10u));
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_PWM_AF, af_of(10u));
    TEST_ASSERT_EQUAL(TFW_OK, wire_write(D6, 200u));
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_MODE_AF, mode_of(10u));
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_PWM_AF, af_of(10u));
    TEST_ASSERT_EQUAL_UINT32(200u, TFW_H5_TIM_CCR(1u));
}

void test_pwm_psc_1khz(void)
{
    uint16_t psc = tfw_h5_pwm_psc(64000000uL);
    uint32_t hz  = 64000000uL / ((uint32_t)psc + 1u) / TFW_H5_PWM_STEPS;

    TEST_ASSERT_EQUAL_UINT16(250u, psc);
    TEST_ASSERT_UINT32_WITHIN(5u, 1000u, hz);
}

void test_pwm_psc_holds_its_range(void)
{
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_h5_pwm_psc(0u));
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_h5_pwm_psc(255000u));
    /* The widest 32-bit clock: 16843 rounded, no overflow. */
    TEST_ASSERT_EQUAL_UINT16(16842u, tfw_h5_pwm_psc(0xFFFFFFFFu));
}

void test_duty_0_is_never_high(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, hal_write(1u, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_H5_TIM_CCR(1u));
    TEST_ASSERT_EQUAL_UINT32(0u, high_counts(1u));
}

void test_duty_128_is_half(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, hal_write(1u, 128u));
    /* 64 MHz, the board's core clock (boards/stm32h573i-dk/board_profile.h). */
    TEST_ASSERT_EQUAL_UINT32(tfw_h5_pwm_psc(64000000uL), TFW_H5_TIM_PSC);
    TEST_ASSERT_EQUAL_UINT32(254u, TFW_H5_TIM_ARR);
    TEST_ASSERT_EQUAL_UINT32(128u, TFW_H5_TIM_CCR(1u));
    TEST_ASSERT_EQUAL_UINT32(128u, high_counts(1u));
}

void test_duty_255_is_always_high(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, hal_write(1u, 255u));
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_PWM_STEPS, high_counts(1u));
}

void test_the_base_is_set_once_with_preload_and_update(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, hal_write(2u, 10u));
    TEST_ASSERT_BITS_HIGH(TFW_H5_RCC_APB1LENR_TIM5EN, TFW_H5_RCC_APB1LENR);
    TEST_ASSERT_EQUAL_UINT32(250u, TFW_H5_TIM_PSC);
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_PWM_ARR, TFW_H5_TIM_ARR);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_TIM_CR1_CEN | TFW_H5_TIM_CR1_ARPE, TFW_H5_TIM_CR1);
    TEST_ASSERT_EQUAL_HEX32(TFW_H5_TIM_EGR_UG, TFW_H5_TIM_EGR);
    /* A running timer is left alone: no second update, no new prescaler. */
    TFW_H5_TIM_EGR = 0u;
    TFW_H5_TIM_PSC = 0u;
    TEST_ASSERT_EQUAL(TFW_OK, hal_write(2u, 20u));
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_TIM_EGR);
    TEST_ASSERT_EQUAL_UINT32(0u, TFW_H5_TIM_PSC);
    TEST_ASSERT_EQUAL_UINT32(20u, TFW_H5_TIM_CCR(2u));
}

void test_each_channels_ccmr_bits(void)
{
    /* OCxM 0110 and OCxPE: 0x68 in the channel's byte, CCxS 00. */
    TEST_ASSERT_EQUAL_HEX32(0x00000068u, tfw_h5_pwm_ccmr(0u, 1u));
    TEST_ASSERT_EQUAL_HEX32(0x00006800u, tfw_h5_pwm_ccmr(0u, 2u));
    TEST_ASSERT_EQUAL_HEX32(0x00000068u, tfw_h5_pwm_ccmr(0u, 3u));
    TEST_ASSERT_EQUAL_HEX32(0x00006800u, tfw_h5_pwm_ccmr(0u, 4u));
    /* The other channel's half is kept; this one's OCxM_3 and CCxS cleared. */
    TEST_ASSERT_EQUAL_HEX32(0x0100FF68u, tfw_h5_pwm_ccmr(0x0101FFFFu, 1u));
    TEST_ASSERT_EQUAL_HEX32(0x000168FFu, tfw_h5_pwm_ccmr(0x0101FFFFu, 2u));
}

void test_channels_one_and_two_use_ccmr1(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, hal_write(1u, 1u));
    TEST_ASSERT_EQUAL_HEX32(0x00000068u, TFW_H5_TIM_CCMR1);
    TEST_ASSERT_EQUAL(TFW_OK, hal_write(2u, 1u));
    TEST_ASSERT_EQUAL_HEX32(0x00006868u, TFW_H5_TIM_CCMR1);
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(TIM5_CCMR2_ADDR));
}

void test_channels_three_and_four_use_ccmr2(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, hal_write(3u, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, hal_write(4u, 1u));
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(TIM5_CCMR1_ADDR));
    TEST_ASSERT_EQUAL_HEX32(0x00006868u, TFW_H5_TIM_CCMR2);
}

void test_each_channels_ccer_bit(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x0001u, tfw_h5_pwm_ccer(0u, 1u, 1u));
    TEST_ASSERT_EQUAL_HEX32(0x0010u, tfw_h5_pwm_ccer(0u, 2u, 1u));
    TEST_ASSERT_EQUAL_HEX32(0x0100u, tfw_h5_pwm_ccer(0u, 3u, 1u));
    TEST_ASSERT_EQUAL_HEX32(0x1000u, tfw_h5_pwm_ccer(0u, 4u, 1u));
    /* Active high: CCxP cleared. */
    TEST_ASSERT_EQUAL_HEX32(0x0001u, tfw_h5_pwm_ccer(0x0002u, 1u, 1u));
}

void test_each_write_enables_its_channel(void)
{
    uint8_t  ch;
    uint32_t want = 0u;

    for (ch = 1u; ch <= 4u; ch++) {
        TEST_ASSERT_EQUAL(TFW_OK, hal_write(ch, (uint8_t)(10u * ch)));
        want |= 1u << (4u * (ch - 1u));
        TEST_ASSERT_EQUAL_HEX32(want, TFW_H5_TIM_CCER);
    }
}

void test_a_write_leaves_the_other_channels_alone(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, hal_write(1u, 100u));
    TEST_ASSERT_EQUAL(TFW_OK, hal_write(2u, 50u));
    TEST_ASSERT_EQUAL(TFW_OK, hal_write(1u, 200u));
    TEST_ASSERT_EQUAL_UINT32(200u, TFW_H5_TIM_CCR(1u));
    TEST_ASSERT_EQUAL_UINT32(50u, TFW_H5_TIM_CCR(2u));
    TEST_ASSERT_EQUAL_HEX32(0x0011u, TFW_H5_TIM_CCER);
    TEST_ASSERT_EQUAL_HEX32(0u, regfile_model_peek(TIM5_CCR_ADDR(3u)));
}

void test_stop_clears_only_its_channel(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, hal_write(1u, 100u));
    TEST_ASSERT_EQUAL(TFW_OK, hal_write(2u, 80u));
    TEST_ASSERT_EQUAL(TFW_OK, hal_stop(1u));
    TEST_ASSERT_EQUAL_HEX32(0x0010u, TFW_H5_TIM_CCER);
    TEST_ASSERT_EQUAL_UINT32(80u, TFW_H5_TIM_CCR(2u));
    assert_low_output(10u);
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_MODE_AF, mode_of(11u));
}

void test_a_wire_write_of_duty_0_leaves_the_pin_low(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, wire_write(D6, 0u));
    TEST_ASSERT_EQUAL_HEX32(0u, TFW_H5_TIM_CCER);
    assert_low_output(10u);
}

void test_a_wire_write_of_duty_0_stops_a_running_channel(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, wire_write(D6, 100u));
    TEST_ASSERT_EQUAL(TFW_OK, wire_write(D5, 60u));
    TEST_ASSERT_EQUAL_HEX32(0x0011u, TFW_H5_TIM_CCER);
    TEST_ASSERT_EQUAL(TFW_OK, wire_write(D6, 0u));
    TEST_ASSERT_EQUAL_HEX32(0x0010u, TFW_H5_TIM_CCER);
    assert_low_output(10u);
    TEST_ASSERT_EQUAL_UINT32(TFW_H5_MODE_AF, mode_of(11u));
    TEST_ASSERT_EQUAL_UINT32(60u, TFW_H5_TIM_CCR(2u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_pwm_psc_1khz);
    RUN_TEST(test_pwm_psc_holds_its_range);
    RUN_TEST(test_duty_0_is_never_high);
    RUN_TEST(test_duty_128_is_half);
    RUN_TEST(test_duty_255_is_always_high);
    RUN_TEST(test_the_base_is_set_once_with_preload_and_update);
    RUN_TEST(test_each_channels_ccmr_bits);
    RUN_TEST(test_channels_one_and_two_use_ccmr1);
    RUN_TEST(test_channels_three_and_four_use_ccmr2);
    RUN_TEST(test_each_channels_ccer_bit);
    RUN_TEST(test_each_write_enables_its_channel);
    RUN_TEST(test_a_write_leaves_the_other_channels_alone);
    RUN_TEST(test_stop_clears_only_its_channel);
    RUN_TEST(test_a_wire_write_of_duty_0_leaves_the_pin_low);
    RUN_TEST(test_a_wire_write_of_duty_0_stops_a_running_channel);
    RUN_TEST(test_each_wire_write_gives_the_pin_back_to_the_timer);
    return UNITY_END();
}
