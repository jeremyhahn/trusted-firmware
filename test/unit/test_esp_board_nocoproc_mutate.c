/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ESP32 board bring-up, src/hal/esp32/hal_board.c,
 * built for a board with no co-processor: TFW_ESP_COPROC_EN_GPIO undefined.
 * The bring-up touches no pin and succeeds. test_esp_board_mutate.c holds the
 * build that names one.
 */

#include "unity.h"
#include "driver/gpio.h"
#include "hal_esp32.h"

static unsigned ncalls;

esp_err_t gpio_config(const gpio_config_t *pGPIOConfig)
{
    (void)pGPIOConfig;
    ncalls++;
    return ESP_FAIL;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    (void)gpio_num;
    (void)level;
    ncalls++;
    return ESP_FAIL;
}

esp_err_t gpio_hold_en(gpio_num_t gpio_num)
{
    (void)gpio_num;
    ncalls++;
    return ESP_FAIL;
}

void setUp(void) { ncalls = 0u; }
void tearDown(void) {}

static void test_a_board_without_a_coprocessor_touches_no_pin(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_esp_board_init());
    TEST_ASSERT_EQUAL_UINT(0u, ncalls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_board_without_a_coprocessor_touches_no_pin);
    return UNITY_END();
}
