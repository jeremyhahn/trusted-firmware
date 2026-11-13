/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MCXN94x gpio HAL (src/hal/cortex-m/mcxn94x/hal_gpio.c) on the register
 * file: the PORT PCR each mode writes (MUX function 0, the pull, the input
 * buffer) and the GPIO PDDR bit it sets or clears, the PSOR or PCOR store of
 * a write, the PDIR bit a read returns, and a coordinate the part has no pin
 * at refused with nothing written. The addresses and bits are the MCXNx4x
 * Reference Manual's (the PORT and RGPIO chapters, through soc_regs.h's
 * citation), written out here for the checks so a drift in soc_regs.h shows
 * too; a register the test presets goes through soc_regs.h's name. The pins
 * are the FRDM-MCXN947's (boards/frdm-mcxn947/board.c). A board has not run
 * them.
 */
#include <stdint.h>

#include "unity.h"
#include "tfw/hal.h"
#include "soc_regs.h"
#include "regfile_model.h"

/* PORTn and GPIOn, the secure aliases soc_regs.h uses. */
#define PORT_PCR(n, b) (0x50116000u + 0x1000u * (n) + 0x80u + 4u * (b))
#define GPIO_BASE(n)   (0x50096000u + 0x2000u * (n))
#define GPIO_PSOR(n)   (GPIO_BASE(n) + 0x44u)
#define GPIO_PCOR(n)   (GPIO_BASE(n) + 0x48u)
#define GPIO_PDIR(n)   (GPIO_BASE(n) + 0x50u)
#define GPIO_PDDR(n)   (GPIO_BASE(n) + 0x54u)

#define PCR_PS   (1u << 0)    /* pull select: 1 = up */
#define PCR_PE   (1u << 1)    /* pull enable */
#define PCR_MUX  (0xFu << 8)  /* pin function */
#define PCR_IBE  (1u << 12)   /* input buffer enable */

static const tfw_pinref_t led_red  = TFW_PB(0u, 10u);  /* P0_10 */
static const tfw_pinref_t led_blue = TFW_PB(1u, 2u);   /* P1_2 */
static const tfw_pinref_t sw2      = TFW_PB(0u, 23u);  /* P0_23 */

static uint32_t peek(uint32_t addr)
{
    return regfile_model_peek((uintptr_t)addr);
}

void setUp(void)
{
    regfile_model_reset();
}

void tearDown(void) {}

void test_output_is_function_0_no_pull_input_buffer_on(void)
{
    /* A PCR left at another function with a pull: the mode replaces it. */
    TFW_MCXN_PORT_PCR(0u, 10u) = (4u << 8) | PCR_PE | PCR_PS;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&led_red, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL_HEX32(PCR_IBE, peek(PORT_PCR(0u, 10u)));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(PORT_PCR(0u, 10u)) & PCR_MUX);
}

void test_output_sets_only_its_pddr_bit(void)
{
    TFW_MCXN_GPIO_PDDR(0u) = 1u << 3;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&led_red, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL_HEX32((1u << 3) | (1u << 10), peek(GPIO_PDDR(0u)));
}

void test_output_on_port_1_uses_port1_and_gpio1(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&led_blue, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL_HEX32(PCR_IBE, peek(PORT_PCR(1u, 2u)));
    TEST_ASSERT_EQUAL_HEX32(1u << 2, peek(GPIO_PDDR(1u)));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(PORT_PCR(0u, 2u)));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(GPIO_PDDR(0u)));
}

void test_input_clears_only_its_pddr_bit_no_pull(void)
{
    TFW_MCXN_GPIO_PDDR(0u) = 0xFFFFFFFFu;
    TFW_MCXN_PORT_PCR(0u, 23u) = (4u << 8) | PCR_PE | PCR_PS;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&sw2, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL_HEX32(~(1u << 23), peek(GPIO_PDDR(0u)));
    TEST_ASSERT_EQUAL_HEX32(PCR_IBE, peek(PORT_PCR(0u, 23u)));
}

void test_input_pullup_enables_the_pull_up(void)
{
    TFW_MCXN_GPIO_PDDR(0u) = 0xFFFFFFFFu;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&sw2, TFW_PIN_INPUT_PULLUP));
    TEST_ASSERT_EQUAL_HEX32(PCR_IBE | PCR_PE | PCR_PS, peek(PORT_PCR(0u, 23u)));
    TEST_ASSERT_EQUAL_HEX32(~(1u << 23), peek(GPIO_PDDR(0u)));
}

void test_an_unknown_mode_is_unsupported_and_writes_nothing(void)
{
    TFW_MCXN_GPIO_PDDR(0u) = 1u << 10;
    TFW_MCXN_PORT_PCR(0u, 10u) = 4u << 8;
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_gpio_mode(&led_red, (tfw_pin_mode_t)3));
    TEST_ASSERT_EQUAL_HEX32(1u << 10, peek(GPIO_PDDR(0u)));
    TEST_ASSERT_EQUAL_HEX32(4u << 8, peek(PORT_PCR(0u, 10u)));
}

void test_write_1_is_one_psor_store(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&led_red, 1u));
    TEST_ASSERT_EQUAL_HEX32(1u << 10, peek(GPIO_PSOR(0u)));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(GPIO_PCOR(0u)));
}

void test_write_0_is_one_pcor_store(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&led_red, 0u));
    TEST_ASSERT_EQUAL_HEX32(1u << 10, peek(GPIO_PCOR(0u)));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(GPIO_PSOR(0u)));
}

void test_write_any_nonzero_level_sets(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&led_blue, 0x80u));
    TEST_ASSERT_EQUAL_HEX32(1u << 2, peek(GPIO_PSOR(1u)));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(GPIO_PCOR(1u)));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(GPIO_PSOR(0u)));
}

void test_read_is_the_pins_pdir_bit(void)
{
    uint8_t level = 0xAAu;

    TFW_MCXN_GPIO_PDIR(0u) = 1u << 23;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&sw2, &level));
    TEST_ASSERT_EQUAL_UINT8(1u, level);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&led_red, &level));
    TEST_ASSERT_EQUAL_UINT8(0u, level);

    /* The other bits of the bank do not leak into the pin's level. */
    TFW_MCXN_GPIO_PDIR(0u) = ~(1u << 23);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&sw2, &level));
    TEST_ASSERT_EQUAL_UINT8(0u, level);
}

void test_read_on_port_1_reads_gpio1(void)
{
    uint8_t level = 0xAAu;

    TFW_MCXN_GPIO_PDIR(0u) = 1u << 2;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&led_blue, &level));
    TEST_ASSERT_EQUAL_UINT8(0u, level);
    TFW_MCXN_GPIO_PDIR(1u) = 1u << 2;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&led_blue, &level));
    TEST_ASSERT_EQUAL_UINT8(1u, level);
}

void test_read_of_bit_31(void)
{
    const tfw_pinref_t p4_31 = TFW_PB(4u, 31u);
    uint8_t level = 0u;

    TFW_MCXN_GPIO_PDIR(4u) = 1u << 31;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&p4_31, &level));
    TEST_ASSERT_EQUAL_UINT8(1u, level);
}

void test_a_port_the_part_lacks_is_refused(void)
{
    const tfw_pinref_t p5 = TFW_PB(5u, 0u);
    uint8_t level = 0xAAu;

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(&p5, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_write(&p5, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_read(&p5, &level));
    TEST_ASSERT_EQUAL_UINT8(0xAAu, level);
    TEST_ASSERT_EQUAL_HEX32(0u, peek(PORT_PCR(5u, 0u)));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(GPIO_PDDR(5u)));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(GPIO_PSOR(5u)));
}

void test_a_bit_past_31_is_refused(void)
{
    const tfw_pinref_t p0_32 = TFW_PB(0u, 32u);
    uint8_t level = 0xAAu;

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(&p0_32, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_write(&p0_32, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_read(&p0_32, &level));
    TEST_ASSERT_EQUAL_UINT8(0xAAu, level);
    TEST_ASSERT_EQUAL_HEX32(0u, peek(PORT_PCR(0u, 32u)));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(GPIO_PDDR(0u)));
    TEST_ASSERT_EQUAL_HEX32(0u, peek(GPIO_PCOR(0u)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_output_is_function_0_no_pull_input_buffer_on);
    RUN_TEST(test_output_sets_only_its_pddr_bit);
    RUN_TEST(test_output_on_port_1_uses_port1_and_gpio1);
    RUN_TEST(test_input_clears_only_its_pddr_bit_no_pull);
    RUN_TEST(test_input_pullup_enables_the_pull_up);
    RUN_TEST(test_an_unknown_mode_is_unsupported_and_writes_nothing);
    RUN_TEST(test_write_1_is_one_psor_store);
    RUN_TEST(test_write_0_is_one_pcor_store);
    RUN_TEST(test_write_any_nonzero_level_sets);
    RUN_TEST(test_read_is_the_pins_pdir_bit);
    RUN_TEST(test_read_on_port_1_reads_gpio1);
    RUN_TEST(test_read_of_bit_31);
    RUN_TEST(test_a_port_the_part_lacks_is_refused);
    RUN_TEST(test_a_bit_past_31_is_refused);
    return UNITY_END();
}
