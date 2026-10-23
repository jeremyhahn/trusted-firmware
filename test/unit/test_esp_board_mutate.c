/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 board bring-up, src/hal/esp32/hal_board.c,
 * built as the PoE board builds it: TFW_ESP_COPROC_EN_GPIO names the
 * co-processor's enable line, here GPIO 54 so that a mask built in 32 bits is
 * seen. Over a stand-in gpio driver (test/mock/esp32/board/) it holds that the
 * line is driven low before it is made an output, configured as a pulled-down
 * output with no interrupt, driven low again and held; and that any of those
 * calls failing stops the bring-up there with TFW_ERR_INTERNAL.
 * test_esp_board_nocoproc_mutate.c holds the build without one.
 */

#include <stdint.h>
#include "unity.h"
#include "driver/gpio.h"
#include "hal_esp32.h"

enum { E_SET = 1, E_CONFIG, E_HOLD };

static int           ops[8];
static long          args[8][2];
static unsigned      nops;
static unsigned      fail_at;   /* 1-based call to fail; 0 none */
static gpio_config_t cfg_seen;

static esp_err_t log_call(int op, long a, long b)
{
    TEST_ASSERT_LESS_THAN_UINT(8u, nops);
    ops[nops]     = op;
    args[nops][0] = a;
    args[nops][1] = b;
    nops++;
    return nops == fail_at ? ESP_FAIL : ESP_OK;
}

esp_err_t gpio_config(const gpio_config_t *pGPIOConfig)
{
    cfg_seen = *pGPIOConfig;
    return log_call(E_CONFIG, 0, 0);
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    return log_call(E_SET, gpio_num, (long)level);
}

esp_err_t gpio_hold_en(gpio_num_t gpio_num)
{
    return log_call(E_HOLD, gpio_num, 0);
}

void setUp(void)
{
    nops    = 0u;
    fail_at = 0u;
    cfg_seen.pin_bit_mask = 0u;
    cfg_seen.mode         = GPIO_MODE_DISABLE;
    cfg_seen.pull_up_en   = GPIO_PULLUP_ENABLE;
    cfg_seen.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg_seen.intr_type    = GPIO_INTR_ANYEDGE;
}

void tearDown(void) {}

static void assert_call(unsigned i, int op, long a, long b)
{
    TEST_ASSERT_EQUAL_INT(op, ops[i]);
    TEST_ASSERT_EQUAL_INT(a, args[i][0]);
    TEST_ASSERT_EQUAL_INT(b, args[i][1]);
}

static void test_the_coprocessor_is_driven_low_configured_and_held(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_esp_board_init());
    TEST_ASSERT_EQUAL_UINT(4u, nops);
    assert_call(0u, E_SET, 54, 0);
    assert_call(1u, E_CONFIG, 0, 0);
    assert_call(2u, E_SET, 54, 0);
    assert_call(3u, E_HOLD, 54, 0);
    TEST_ASSERT_EQUAL_HEX64(1ULL << 54, cfg_seen.pin_bit_mask);
    TEST_ASSERT_EQUAL(GPIO_MODE_OUTPUT, cfg_seen.mode);
    TEST_ASSERT_EQUAL(GPIO_PULLUP_DISABLE, cfg_seen.pull_up_en);
    TEST_ASSERT_EQUAL(GPIO_PULLDOWN_ENABLE, cfg_seen.pull_down_en);
    TEST_ASSERT_EQUAL(GPIO_INTR_DISABLE, cfg_seen.intr_type);
}

static void test_a_failed_call_stops_the_bring_up(void)
{
    unsigned i;

    for (i = 1u; i <= 4u; i++) {
        setUp();
        fail_at = i;
        TEST_ASSERT_EQUAL(TFW_ERR_INTERNAL, tfw_esp_board_init());
        TEST_ASSERT_EQUAL_UINT(i, nops);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_coprocessor_is_driven_low_configured_and_held);
    RUN_TEST(test_a_failed_call_stops_the_bring_up);
    return UNITY_END();
}
