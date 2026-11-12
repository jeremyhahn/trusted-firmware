/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The nRF54L15 DK's pin table, checked on x86.
 *
 * The table is the DK's four LEDs and four buttons on the SoC's three GPIO
 * banks, and no pin offers a converter or a compare unit yet. The pins are
 * the board's (Zephyr's nrf54l15dk_common.dtsi); a slip here lights the wrong
 * LED on a board nobody is watching.
 *
 * Runs under `make board-sweep`, against this board's own profile.
 */

#include "unity.h"
#include "tfw/board.h"
#include "board_profile.h"
#include "tfw/config.h"
#if defined(TFW_TEST_BOARD_CAPS)
#include "gpio.h"
#include "reader.h"
#include "sched.h"
#include "writer.h"
#include "tfw/hal.h"
#endif

#if defined(TFW_TEST_BOARD_CAPS)
/* The GPIO and scheduler commands over this board table: mk/host.mk's
 * test_board_nrf54l15_i2c0 and test_board_nrf54l15_i2c1 link src/caps/gpio.c
 * and src/caps/sched.c with the GPIO HAL below, once per I2C row. */
static uint32_t s_hal_calls;

tfw_status_t tfw_hal_gpio_mode(const tfw_pinref_t *p, tfw_pin_mode_t mode)
{
    (void)p;
    (void)mode;
    s_hal_calls++;
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_write(const tfw_pinref_t *p, uint8_t level)
{
    (void)p;
    (void)level;
    s_hal_calls++;
    return TFW_OK;
}

tfw_status_t tfw_hal_gpio_read(const tfw_pinref_t *p, uint8_t *out)
{
    (void)p;
    *out = 0u;
    return TFW_OK;
}

tfw_ms_t tfw_hal_millis(void)
{
    return 0u;
}

static tfw_status_t run(tfw_status_t (*cmd)(tfw_reader_t *, tfw_writer_t *),
                        const uint8_t *req, uint8_t len)
{
    static uint8_t reply[16];
    tfw_reader_t   r;
    tfw_writer_t   w;

    tfw_reader_init(&r, req, len);
    tfw_writer_init(&w, reply, sizeof reply);
    return cmd(&r, &w);
}

void setUp(void)
{
    tfw_sched_init();
    s_hal_calls = 0u;
}
#else
void setUp(void) {}
#endif

/* The I2C row's SCL and SDA, P1.10 (LED1, pin 1) and P1.8 (BTN2, pin 6). */
static int i2c_pin(uint8_t pin)
{
    return pin == 1u || pin == 6u;
}

static void test_the_i2c_pins_are_in_use_only_with_the_i2c_row(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        uint8_t in_use = (uint8_t)(tfw_board_pin_flags(i) & TFW_PIN_IN_USE);

        if (TFW_FEATURE_I2C && i2c_pin(i)) {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(TFW_PIN_IN_USE, in_use, "I2C pin free with I2C=1");
        } else {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(0u, in_use, "pin taken");
        }
        TEST_ASSERT_EQUAL_HEX8(0u, tfw_board_pin_flags(i) & TFW_PIN_RESERVED);
    }
}

#if defined(TFW_TEST_BOARD_CAPS)
static void test_gpio_and_the_scheduler_refuse_the_i2c_pins_only_with_the_row(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        const uint8_t mode[2]  = { i, (uint8_t)TFW_PIN_OUTPUT };
        const uint8_t level[2] = { i, 1u };
        const uint8_t sched[6] = { i, 1u, 10u, 0u, 0u, 0u };
        tfw_status_t  want     = (TFW_FEATURE_I2C && i2c_pin(i))
                                     ? TFW_ERR_RESERVED_PIN : TFW_OK;
        uint32_t      calls    = s_hal_calls;

        TEST_ASSERT_EQUAL(want, run(tfw_gpio_cmd_mode, mode, sizeof mode));
        TEST_ASSERT_EQUAL(want, run(tfw_gpio_cmd_write, level, sizeof level));
        TEST_ASSERT_EQUAL(want, run(tfw_sched_cmd_set, sched, sizeof sched));
        /* A refusal reaches no HAL call; an accepted pin reaches four. */
        TEST_ASSERT_EQUAL_UINT32(want == TFW_OK ? 4u : 0u, s_hal_calls - calls);
    }
}
#endif
void tearDown(void) {}

static void test_the_board_is_its_registry_id(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x45u, TFW_BOARD_ID);
    TEST_ASSERT_EQUAL_STRING("nrf54l15-dk", tfw_board_name());
}

static void test_the_pins_are_the_four_leds_then_the_four_buttons(void)
{
    static const uint8_t port[8] = { 2u, 1u, 2u, 1u, 1u, 1u, 1u, 0u };
    static const uint8_t bit[8]  = { 9u, 10u, 7u, 14u, 13u, 9u, 8u, 4u };

    TEST_ASSERT_EQUAL_UINT8(8u, TFW_PIN_COUNT);
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pinref_t r;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        TEST_ASSERT_EQUAL_UINT8(port[i], r.port);
        TEST_ASSERT_EQUAL_UINT8(bit[i], r.bit);
        TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_DIGITAL);
        if (i < 4u) {
            TEST_ASSERT_TRUE(tfw_board_pin_flags(i) & TFW_PIN_LED);
        } else {
            TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & TFW_PIN_LED);
        }
    }
}

static void test_no_pin_is_the_console(void)
{
    /* P1.4 and P1.5 are the UARTE's; a pin table entry would let a host
     * command drive the console's own line. */
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        tfw_pinref_t r;

        TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin(i, &r));
        TEST_ASSERT_FALSE(r.port == 1u && (r.bit == 4u || r.bit == 5u));
    }
}

static void test_no_pin_offers_an_adc_or_a_pwm(void)
{
    for (uint8_t i = 0u; i < TFW_PIN_COUNT; i++) {
        uint8_t ch;
        tfw_pwmref_t p;

        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_adc_channel(i, &ch));
        TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_board_pwm(i, &p));
        TEST_ASSERT_FALSE(tfw_board_pin_flags(i) & (TFW_PIN_ADC | TFW_PIN_PWM));
    }
}

static void test_a_pin_beyond_the_board_is_refused(void)
{
    tfw_pinref_t r;

    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_board_pin(TFW_PIN_COUNT, &r));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_board_is_its_registry_id);
    RUN_TEST(test_the_pins_are_the_four_leds_then_the_four_buttons);
    RUN_TEST(test_no_pin_is_the_console);
    RUN_TEST(test_no_pin_offers_an_adc_or_a_pwm);
    RUN_TEST(test_a_pin_beyond_the_board_is_refused);
    RUN_TEST(test_the_i2c_pins_are_in_use_only_with_the_i2c_row);
#if defined(TFW_TEST_BOARD_CAPS)
    RUN_TEST(test_gpio_and_the_scheduler_refuse_the_i2c_pins_only_with_the_row);
#endif
    return UNITY_END();
}
