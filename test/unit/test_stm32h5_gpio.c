/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The STM32H5 gpio HAL's register values (src/hal/cortex-m/stm32h5/hal_gpio.c)
 * through tfw_hal_gpio_mode, tfw_hal_gpio_write and tfw_hal_gpio_read on the
 * register file (test/model/cortex-m/regfile_model.c): the bank's clock
 * enable, MODER, PUPDR and OTYPER for each mode, BSRR on a write, IDR on a
 * read, and a bank or pin past the part refused with nothing written. The
 * addresses and field values are ST's CMSIS header for the part, as
 * soc_regs.h cites it. A board has not run them.
 */
#include <stdint.h>

#include "unity.h"
#include "tfw/hal.h"
#include "soc_regs.h"
#include "regfile_model.h"

/* GPIOx_OTYPER, GPIO_TypeDef's second word: 0 push-pull, the reset value.
 * soc_regs.h names no OTYPER, as nothing in the port writes one. */
#define GPIO_OTYPER_OFF 0x04u

#define GPIOA 0x42020000u
#define GPIOF 0x42021400u   /* bank 5, LD3 and LD4 */
#define GPIOI 0x42022000u   /* bank 8, LD1 and LD2 */
#define AHB2ENR (0x44020C00u + 0x8Cu)

/* Every pin analog, the reset state of the banks the board uses. */
#define MODER_RESET 0xFFFFFFFFu

static tfw_pinref_t pin(uint8_t port, uint8_t bit)
{
    tfw_pinref_t p = { .port = port, .bit = bit };

    return p;
}

static uint32_t field2(uint32_t reg, uint8_t bit)
{
    return (reg >> (2u * bit)) & 0x3u;
}

/* reg with the pin's two-bit field cleared, for "the other pins kept". */
static uint32_t others2(uint32_t reg, uint8_t bit)
{
    return reg & ~(0x3u << (2u * bit));
}

void setUp(void)
{
    regfile_model_reset();
}

void tearDown(void)
{
}

void test_input_is_moder_00_and_no_pull(void)
{
    tfw_pinref_t p = pin(8u, 9u);   /* PI9, LD1 */

    TFW_H5_GPIO_MODER(GPIOI) = MODER_RESET;
    TFW_H5_GPIO_PUPDR(GPIOI) = 0xAAAAAAAAu;   /* every pin pull-down */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&p, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_HEX32(0x0u, field2(regfile_model_peek(GPIOI + 0x00u), 9u));
    TEST_ASSERT_EQUAL_HEX32(0x0u, field2(regfile_model_peek(GPIOI + 0x0Cu), 9u));
}

void test_input_pullup_is_moder_00_and_pupdr_01(void)
{
    tfw_pinref_t p = pin(5u, 1u);   /* PF1, LD3 */

    TFW_H5_GPIO_MODER(GPIOF) = MODER_RESET;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&p, TFW_PIN_INPUT_PULLUP));
    TEST_ASSERT_EQUAL_HEX32(0x0u, field2(regfile_model_peek(GPIOF + 0x00u), 1u));
    TEST_ASSERT_EQUAL_HEX32(0x1u, field2(regfile_model_peek(GPIOF + 0x0Cu), 1u));
}

void test_output_is_moder_01_no_pull_and_push_pull(void)
{
    tfw_pinref_t p = pin(5u, 4u);   /* PF4, LD4 */

    TFW_H5_GPIO_MODER(GPIOF) = MODER_RESET;
    TFW_H5_GPIO_PUPDR(GPIOF) = 0x1u << (2u * 4u);   /* a pull-up left from input */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&p, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL_HEX32(0x1u, field2(regfile_model_peek(GPIOF + 0x00u), 4u));
    TEST_ASSERT_EQUAL_HEX32(0x0u, field2(regfile_model_peek(GPIOF + 0x0Cu), 4u));
    TEST_ASSERT_EQUAL_HEX32(0x0u, regfile_model_peek(GPIOF + GPIO_OTYPER_OFF));
}

void test_a_mode_keeps_the_banks_other_pins(void)
{
    tfw_pinref_t p = pin(8u, 8u);   /* PI8, LD2 */

    TFW_H5_GPIO_MODER(GPIOI) = MODER_RESET;
    TFW_H5_GPIO_PUPDR(GPIOI) = 0xAAAAAAAAu;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&p, TFW_PIN_INPUT_PULLUP));
    TEST_ASSERT_EQUAL_HEX32(others2(MODER_RESET, 8u),
                            others2(regfile_model_peek(GPIOI + 0x00u), 8u));
    TEST_ASSERT_EQUAL_HEX32(others2(0xAAAAAAAAu, 8u),
                            others2(regfile_model_peek(GPIOI + 0x0Cu), 8u));
}

void test_a_mode_enables_its_banks_clock_and_keeps_the_others(void)
{
    tfw_pinref_t p = pin(5u, 1u);

    TFW_H5_RCC_AHB2ENR = 0x1u;   /* GPIOAEN, the UART's bank */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&p, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL_HEX32(0x1u | (0x1u << 5u), regfile_model_peek(AHB2ENR));
}

void test_each_bank_is_its_own_block(void)
{
    uint8_t b;

    for (b = 0u; b < 9u; b++) {
        tfw_pinref_t p = pin(b, 3u);

        regfile_model_reset();
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&p, TFW_PIN_OUTPUT));
        TEST_ASSERT_EQUAL_HEX32(0x1u << (2u * 3u), regfile_model_peek(GPIOA + 0x400u * b));
        TEST_ASSERT_EQUAL_HEX32(0x1u << b, regfile_model_peek(AHB2ENR));
    }
}

void test_a_high_write_sets_bsrr_bs(void)
{
    tfw_pinref_t p = pin(8u, 9u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&p, 1u));
    TEST_ASSERT_EQUAL_HEX32(0x1u << 9u, regfile_model_peek(GPIOI + 0x18u));
}

void test_a_low_write_sets_bsrr_br(void)
{
    tfw_pinref_t p = pin(8u, 9u);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&p, 0u));
    TEST_ASSERT_EQUAL_HEX32(0x1u << (9u + 16u), regfile_model_peek(GPIOI + 0x18u));
}

void test_a_write_is_bsrr_alone(void)
{
    tfw_pinref_t p = pin(5u, 15u);

    TFW_H5_GPIO_MODER(GPIOF) = MODER_RESET;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&p, 1u));
    TEST_ASSERT_EQUAL_HEX32(0x1u << 15u, regfile_model_peek(GPIOF + 0x18u));
    TEST_ASSERT_EQUAL_HEX32(MODER_RESET, regfile_model_peek(GPIOF + 0x00u));
    TEST_ASSERT_EQUAL_HEX32(0x0u, regfile_model_peek(AHB2ENR));
}

void test_a_read_is_the_pins_idr_bit(void)
{
    tfw_pinref_t p = pin(7u, 10u);   /* PH10, ARD_D6 */
    uint8_t level = 0xFFu;

    TFW_H5_GPIO_IDR(0x42021C00u) = 0x1u << 10u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&p, &level));
    TEST_ASSERT_EQUAL_UINT8(1u, level);

    TFW_H5_GPIO_IDR(0x42021C00u) = ~(0x1u << 10u);   /* every other pin high */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&p, &level));
    TEST_ASSERT_EQUAL_UINT8(0u, level);
}

void test_a_bank_past_the_part_is_refused(void)
{
    tfw_pinref_t p = pin(9u, 0u);
    uint8_t level = 0xA5u;

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(&p, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_write(&p, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_read(&p, &level));
    TEST_ASSERT_EQUAL_UINT8(0xA5u, level);
    TEST_ASSERT_EQUAL_HEX32(0x0u, regfile_model_peek(AHB2ENR));
    TEST_ASSERT_EQUAL_HEX32(0x0u, regfile_model_peek(GPIOA + 0x400u * 9u));
    TEST_ASSERT_EQUAL_HEX32(0x0u, regfile_model_peek(GPIOA + 0x400u * 9u + 0x18u));
}

void test_a_pin_past_the_bank_is_refused(void)
{
    tfw_pinref_t p = pin(5u, 16u);
    uint8_t level = 0xA5u;

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(&p, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_write(&p, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_read(&p, &level));
    TEST_ASSERT_EQUAL_UINT8(0xA5u, level);
    TEST_ASSERT_EQUAL_HEX32(0x0u, regfile_model_peek(AHB2ENR));
    TEST_ASSERT_EQUAL_HEX32(0x0u, regfile_model_peek(GPIOF + 0x00u));
    TEST_ASSERT_EQUAL_HEX32(0x0u, regfile_model_peek(GPIOF + 0x18u));
}

void test_a_mode_outside_the_three_is_refused(void)
{
    tfw_pinref_t p = pin(5u, 1u);

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_gpio_mode(&p, (tfw_pin_mode_t)3));
    TEST_ASSERT_EQUAL_HEX32(0x0u, regfile_model_peek(AHB2ENR));
    TEST_ASSERT_EQUAL_HEX32(0x0u, regfile_model_peek(GPIOF + 0x00u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_input_is_moder_00_and_no_pull);
    RUN_TEST(test_input_pullup_is_moder_00_and_pupdr_01);
    RUN_TEST(test_output_is_moder_01_no_pull_and_push_pull);
    RUN_TEST(test_a_mode_keeps_the_banks_other_pins);
    RUN_TEST(test_a_mode_enables_its_banks_clock_and_keeps_the_others);
    RUN_TEST(test_each_bank_is_its_own_block);
    RUN_TEST(test_a_high_write_sets_bsrr_bs);
    RUN_TEST(test_a_low_write_sets_bsrr_br);
    RUN_TEST(test_a_write_is_bsrr_alone);
    RUN_TEST(test_a_read_is_the_pins_idr_bit);
    RUN_TEST(test_a_bank_past_the_part_is_refused);
    RUN_TEST(test_a_pin_past_the_bank_is_refused);
    RUN_TEST(test_a_mode_outside_the_three_is_refused);
    return UNITY_END();
}
