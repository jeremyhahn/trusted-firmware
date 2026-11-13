/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/cortex-m/mps2/hal_gpio.c on the host, against a register file the
 * suite holds (boot/hal/cortex-m/rp2350/test/cm_regs.h sends TFW_CM_REG
 * here), on mps2-an505's profile. The MPS2 digital pins are the FPGA I/O
 * block's LED register at FPGAIO +0x00 (AN505 Table 4-5, AN500 Table 3-6),
 * bit n user LED n, two LEDs, port index 0; the port drives no CMSDK GPIO.
 * Each test checks the register value the code leaves:
 *
 *   tfw_hal_gpio_mode   output accepted, both input modes TFW_ERR_UNSUPPORTED,
 *                       and no register written either way
 *   tfw_hal_gpio_write  sets or clears its own LED bit, every other bit kept
 *   tfw_hal_gpio_read   the LED register's bit for the pin
 *   any pin outside the table (another port, a bit past the LEDs) is
 *   TFW_ERR_BAD_PIN, and the LED register is not touched
 *
 * Any address other than the LED register fails the test. A board has not
 * run them.
 */

#include <stdint.h>
#include "unity.h"
#include "tfw/hal.h"
#include "soc_regs.h"

#define LED_ADDR  (TFW_MPS2_FPGAIO_BASE + 0x00u)
#define UNTOUCHED 0xA5A5A5A4u   /* LED0 clear, LED1 clear, bits 2-31 a pattern */

static uint32_t led_reg;
static unsigned led_accesses;

volatile uint32_t *tfw_cm_model_reg(uint32_t addr)
{
    if (addr != (uint32_t)LED_ADDR) {
        TEST_FAIL_MESSAGE("hal_gpio.c reached a register other than FPGAIO LED");
    }
    led_accesses++;
    return (volatile uint32_t *)&led_reg;
}

static const tfw_pinref_t LED0 = { 0u, 0u };
static const tfw_pinref_t LED1 = { 0u, 1u };

void setUp(void)
{
    led_reg      = UNTOUCHED;
    led_accesses = 0u;
}

void tearDown(void) {}

void test_led_register_is_fpgaio_plus_zero(void)
{
    uint8_t level = 0u;

    TEST_ASSERT_EQUAL_HEX32(0x40302000u, LED_ADDR);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&LED0, &level));
    TEST_ASSERT_EQUAL_UINT(1u, led_accesses);
}

void test_mode_output_is_accepted_and_writes_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&LED0, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&LED1, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL_UINT(0u, led_accesses);
    TEST_ASSERT_EQUAL_HEX32(UNTOUCHED, led_reg);
}

void test_mode_input_is_unsupported_on_an_led(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_gpio_mode(&LED0, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_hal_gpio_mode(&LED1, TFW_PIN_INPUT_PULLUP));
    TEST_ASSERT_EQUAL_UINT(0u, led_accesses);
    TEST_ASSERT_EQUAL_HEX32(UNTOUCHED, led_reg);
}

void test_write_high_sets_only_its_bit(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&LED0, 1u));
    TEST_ASSERT_EQUAL_HEX32(UNTOUCHED | 0x1u, led_reg);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&LED1, 1u));
    TEST_ASSERT_EQUAL_HEX32(UNTOUCHED | 0x3u, led_reg);
}

void test_write_low_clears_only_its_bit(void)
{
    led_reg = 0xFFFFFFFFu;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&LED1, 0u));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFDu, led_reg);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&LED0, 0u));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFCu, led_reg);
}

void test_write_any_nonzero_level_is_high(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&LED1, 0x80u));
    TEST_ASSERT_EQUAL_HEX32(UNTOUCHED | 0x2u, led_reg);
}

void test_read_returns_the_pins_bit(void)
{
    uint8_t level = 0xFFu;

    led_reg = 0x2u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&LED0, &level));
    TEST_ASSERT_EQUAL_UINT8(0u, level);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&LED1, &level));
    TEST_ASSERT_EQUAL_UINT8(1u, level);

    led_reg = 0xFFFFFFFDu;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&LED0, &level));
    TEST_ASSERT_EQUAL_UINT8(1u, level);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&LED1, &level));
    TEST_ASSERT_EQUAL_UINT8(0u, level);
}

void test_read_returns_the_level_last_written(void)
{
    uint8_t level = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&LED1, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&LED1, &level));
    TEST_ASSERT_EQUAL_UINT8(1u, level);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&LED1, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&LED1, &level));
    TEST_ASSERT_EQUAL_UINT8(0u, level);
}

static void check_refused(const tfw_pinref_t *pin)
{
    uint8_t level = 0x5Au;

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(pin, TFW_PIN_OUTPUT));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(pin, TFW_PIN_INPUT));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_write(pin, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_write(pin, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_read(pin, &level));
    TEST_ASSERT_EQUAL_UINT8(0x5Au, level);
    TEST_ASSERT_EQUAL_UINT(0u, led_accesses);
    TEST_ASSERT_EQUAL_HEX32(UNTOUCHED, led_reg);
}

void test_a_bit_past_the_leds_is_refused(void)
{
    const tfw_pinref_t past = { 0u, (uint8_t)TFW_MPS2_LED_COUNT };
    const tfw_pinref_t top  = { 0u, 31u };

    check_refused(&past);
    check_refused(&top);
}

void test_another_port_is_refused(void)
{
    const tfw_pinref_t port1 = { 1u, 0u };
    const tfw_pinref_t last  = { 0xFFu, 1u };

    check_refused(&port1);
    check_refused(&last);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_led_register_is_fpgaio_plus_zero);
    RUN_TEST(test_mode_output_is_accepted_and_writes_nothing);
    RUN_TEST(test_mode_input_is_unsupported_on_an_led);
    RUN_TEST(test_write_high_sets_only_its_bit);
    RUN_TEST(test_write_low_clears_only_its_bit);
    RUN_TEST(test_write_any_nonzero_level_is_high);
    RUN_TEST(test_read_returns_the_pins_bit);
    RUN_TEST(test_read_returns_the_level_last_written);
    RUN_TEST(test_a_bit_past_the_leds_is_refused);
    RUN_TEST(test_another_port_is_refused);
    return UNITY_END();
}
