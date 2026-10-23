/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 GPIO block, src/hal/esp32/hal_gpio.c.
 *
 * The unit had no host build; test_gpio_mutate builds the core layer
 * (src/caps/gpio.c) over the mock HAL, not this. This builds it over a
 * stand-in for ESP-IDF's gpio driver (test/mock/esp32/gpio/), each call
 * logged here and able to fail, and holds:
 *
 *   - a pin the part does not have refused before any call is made;
 *   - each mode as its reset, direction and pull, in that order, an output
 *     configured INPUT_OUTPUT so that a read returns the pin;
 *   - any one of those calls failing the mode, the rest still made;
 *   - a mode it does not know refused as out of range;
 *   - a write as 0 or 1, a failed one a bad pin; a read as 0 or 1.
 */

#include <stdint.h>
#include "unity.h"
#include "driver/gpio.h"
#include "tfw/hal.h"

uint64_t mock_gpio_valid_mask;

enum { RESET = 'R', DIR = 'D', PULL = 'P', SET = 'S', GET = 'G' };

static struct {
    char fn;
    int  n;
    int  arg;
} calls[8];
static unsigned  ncalls;
static esp_err_t reset_err, dir_err, pull_err, set_err;
static int       level_in;

static void log_call(char fn, int n, int arg)
{
    TEST_ASSERT_LESS_THAN_UINT(8u, ncalls);
    calls[ncalls].fn  = fn;
    calls[ncalls].n   = n;
    calls[ncalls].arg = arg;
    ncalls++;
}

esp_err_t gpio_reset_pin(gpio_num_t gpio_num)
{
    log_call(RESET, gpio_num, 0);
    return reset_err;
}

esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
    log_call(DIR, gpio_num, (int)mode);
    return dir_err;
}

esp_err_t gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull)
{
    log_call(PULL, gpio_num, (int)pull);
    return pull_err;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    log_call(SET, gpio_num, (int)level);
    return set_err;
}

int gpio_get_level(gpio_num_t gpio_num)
{
    log_call(GET, gpio_num, 0);
    return level_in;
}

void setUp(void)
{
    mock_gpio_valid_mask = (1ULL << 0) | (1ULL << 5) | (1ULL << 33) | (1ULL << 54);
    ncalls    = 0u;
    reset_err = dir_err = pull_err = set_err = ESP_OK;
    level_in  = 0;
}

void tearDown(void) {}

static void assert_call(unsigned i, char fn, int n, int arg)
{
    TEST_ASSERT_LESS_THAN_UINT(ncalls, i);
    TEST_ASSERT_EQUAL_CHAR(fn, calls[i].fn);
    TEST_ASSERT_EQUAL_INT(n, calls[i].n);
    TEST_ASSERT_EQUAL_INT(arg, calls[i].arg);
}

static void test_a_pin_the_part_lacks_is_refused_untouched(void)
{
    static const uint8_t bad[] = { 1u, 4u, 6u, 32u, 34u, 53u, 55u, 63u, 64u, 255u };
    unsigned i;

    for (i = 0u; i < sizeof bad; i++) {
        tfw_pinref_t p = { bad[i] };
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(&p, TFW_PIN_OUTPUT));
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_mode(&p, TFW_PIN_INPUT));
    }
    TEST_ASSERT_EQUAL_UINT(0u, ncalls);
}

static void test_each_mode_is_its_reset_direction_and_pull(void)
{
    static const struct {
        uint8_t        pin;
        tfw_pin_mode_t mode;
        int            dir;
        int            pull;   /* -1: none */
    } t[] = {
        { 0u,  TFW_PIN_OUTPUT,       GPIO_MODE_INPUT_OUTPUT, -1               },
        { 5u,  TFW_PIN_INPUT,        GPIO_MODE_INPUT,        GPIO_FLOATING    },
        { 33u, TFW_PIN_INPUT_PULLUP, GPIO_MODE_INPUT,        GPIO_PULLUP_ONLY },
        { 54u, TFW_PIN_OUTPUT,       GPIO_MODE_INPUT_OUTPUT, -1               },
    };
    unsigned i;

    for (i = 0u; i < sizeof t / sizeof t[0]; i++) {
        tfw_pinref_t p = { t[i].pin };

        ncalls = 0u;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_mode(&p, t[i].mode));
        TEST_ASSERT_EQUAL_UINT(t[i].pull < 0 ? 2u : 3u, ncalls);
        assert_call(0u, RESET, t[i].pin, 0);
        assert_call(1u, DIR, t[i].pin, t[i].dir);
        if (t[i].pull >= 0) {
            assert_call(2u, PULL, t[i].pin, t[i].pull);
        }
    }
}

static void test_any_failed_call_fails_the_mode_the_rest_still_made(void)
{
    static const tfw_pin_mode_t modes[] = { TFW_PIN_OUTPUT, TFW_PIN_INPUT, TFW_PIN_INPUT_PULLUP };
    unsigned m, which;

    for (m = 0u; m < 3u; m++) {
        for (which = 0u; which < 3u; which++) {
            tfw_pinref_t p   = { 5u };
            unsigned     all = modes[m] == TFW_PIN_OUTPUT ? 2u : 3u;

            if (which == 2u && all == 2u) {
                continue;   /* an output sets no pull */
            }
            setUp();
            reset_err = which == 0u ? ESP_FAIL : ESP_OK;
            dir_err   = which == 1u ? ESP_ERR_INVALID_ARG : ESP_OK;
            pull_err  = which == 2u ? ESP_ERR_INVALID_SIZE : ESP_OK;
            TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_hal_gpio_mode(&p, modes[m]));
            TEST_ASSERT_EQUAL_UINT(all, ncalls);
        }
    }
}

static void test_a_mode_it_does_not_know_is_out_of_range(void)
{
    tfw_pinref_t p = { 5u };

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_gpio_mode(&p, (tfw_pin_mode_t)3));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hal_gpio_mode(&p, (tfw_pin_mode_t)99));
    TEST_ASSERT_EQUAL_UINT(2u, ncalls);   /* the resets, no direction */
    TEST_ASSERT_EQUAL_CHAR(RESET, calls[0].fn);
    TEST_ASSERT_EQUAL_CHAR(RESET, calls[1].fn);
}

static void test_a_write_is_0_or_1(void)
{
    static const struct {
        uint8_t level;
        int     want;
    } t[] = { { 0u, 0 }, { 1u, 1 }, { 2u, 1 }, { 0x80u, 1 }, { 255u, 1 } };
    tfw_pinref_t p = { 33u };
    unsigned     i;

    for (i = 0u; i < sizeof t / sizeof t[0]; i++) {
        ncalls = 0u;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_write(&p, t[i].level));
        TEST_ASSERT_EQUAL_UINT(1u, ncalls);
        assert_call(0u, SET, 33, t[i].want);
    }
}

static void test_a_failed_write_is_a_bad_pin(void)
{
    tfw_pinref_t p = { 33u };

    set_err = ESP_ERR_INVALID_ARG;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_write(&p, 1u));
    set_err = ESP_FAIL;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_hal_gpio_write(&p, 0u));
}

static void test_a_read_is_0_or_1(void)
{
    static const struct {
        int     level;
        uint8_t want;
    } t[] = { { 0, 0u }, { 1, 1u }, { 2, 1u }, { 256, 1u }, { -1, 1u } };
    tfw_pinref_t p = { 54u };
    unsigned     i;

    for (i = 0u; i < sizeof t / sizeof t[0]; i++) {
        uint8_t out = 0xA5u;

        ncalls   = 0u;
        level_in = t[i].level;
        TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_gpio_read(&p, &out));
        TEST_ASSERT_EQUAL_UINT8(t[i].want, out);
        TEST_ASSERT_EQUAL_UINT(1u, ncalls);
        assert_call(0u, GET, 54, 0);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_pin_the_part_lacks_is_refused_untouched);
    RUN_TEST(test_each_mode_is_its_reset_direction_and_pull);
    RUN_TEST(test_any_failed_call_fails_the_mode_the_rest_still_made);
    RUN_TEST(test_a_mode_it_does_not_know_is_out_of_range);
    RUN_TEST(test_a_write_is_0_or_1);
    RUN_TEST(test_a_failed_write_is_a_bad_pin);
    RUN_TEST(test_a_read_is_0_or_1);
    return UNITY_END();
}
