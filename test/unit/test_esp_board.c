/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * src/hal/esp32/hal_board.c on the host, compiled against the esp32p4-poe
 * profile: the C6 co-processor's enable is driven low before it becomes an
 * output, pulled down, driven low again and held, so the C6 stays in reset and
 * its radio never comes up. The configuration and each refusal are pinned by
 * test_esp_board_mutate.c; what stays here is the sequence on the real
 * profile's enable line.
 *
 * The profile is the board's own, not a copy: the rule puts boards/esp32p4-poe
 * ahead of the host board, and a profile without TFW_ESP_COPROC_EN_GPIO does not
 * compile this suite.
 */
#include <string.h>

#include "unity.h"
#include "driver/gpio.h"
#include "tfw/config.h"
#include "hal_esp32.h"

enum call { SET_LEVEL, CONFIG, HOLD };

static struct {
    enum call     what;
    gpio_num_t    pin;
    uint32_t      level;
    gpio_config_t cfg;
} calls[8];
static unsigned n_calls;
static unsigned refuse_at; /* 1-based call index that fails; 0: none */

static esp_err_t record(enum call what, gpio_num_t pin, uint32_t level,
                        const gpio_config_t *cfg)
{
    TEST_ASSERT_TRUE(n_calls < sizeof calls / sizeof calls[0]);
    calls[n_calls].what  = what;
    calls[n_calls].pin   = pin;
    calls[n_calls].level = level;
    if (cfg != NULL) {
        calls[n_calls].cfg = *cfg;
    }
    n_calls++;
    return n_calls == refuse_at ? ESP_FAIL : ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    return record(SET_LEVEL, gpio_num, level, NULL);
}

esp_err_t gpio_config(const gpio_config_t *cfg)
{
    return record(CONFIG, -1, 0u, cfg);
}

esp_err_t gpio_hold_en(gpio_num_t gpio_num)
{
    return record(HOLD, gpio_num, 0u, NULL);
}

void setUp(void)
{
    memset(calls, 0, sizeof calls);
    n_calls   = 0u;
    refuse_at = 0u;
}

void tearDown(void) {}

static void test_the_enable_is_low_before_it_is_an_output_and_then_held(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_esp_board_init());
    TEST_ASSERT_EQUAL_UINT(4u, n_calls);

    TEST_ASSERT_EQUAL(SET_LEVEL, calls[0].what);
    TEST_ASSERT_EQUAL(TFW_ESP_COPROC_EN_GPIO, calls[0].pin);
    TEST_ASSERT_EQUAL_UINT32(0u, calls[0].level);

    TEST_ASSERT_EQUAL(CONFIG, calls[1].what);

    TEST_ASSERT_EQUAL(SET_LEVEL, calls[2].what);
    TEST_ASSERT_EQUAL(TFW_ESP_COPROC_EN_GPIO, calls[2].pin);
    TEST_ASSERT_EQUAL_UINT32(0u, calls[2].level);

    TEST_ASSERT_EQUAL(HOLD, calls[3].what);
    TEST_ASSERT_EQUAL(TFW_ESP_COPROC_EN_GPIO, calls[3].pin);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_enable_is_low_before_it_is_an_output_and_then_held);
    return UNITY_END();
}
