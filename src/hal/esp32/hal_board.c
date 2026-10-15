/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The board's own bring-up, run first from tfw_hal_init.
 *
 * TFW_ESP_COPROC_EN_GPIO names the enable line of a co-processor the board
 * carries and this firmware never uses -- the ESP32-C6 beside the P4 on the
 * PoE board. It is driven low here and held there: the C6 stays in reset, so
 * no firmware runs on it, ours or the factory's, and its radio is never
 * enabled. That is a property of this function and not of what the C6 was
 * flashed with, which is why it is enforced by holding reset rather than by
 * trusting the C6 to stay quiet. A board without one leaves the name
 * undefined.
 */

#include "driver/gpio.h"

#include "tfw/config.h"
#include "hal_esp32.h"

tfw_status_t tfw_esp_board_init(void)
{
#ifdef TFW_ESP_COPROC_EN_GPIO
    const gpio_config_t en = {
        .pin_bit_mask = 1ULL << TFW_ESP_COPROC_EN_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    if (gpio_set_level((gpio_num_t)TFW_ESP_COPROC_EN_GPIO, 0u) != ESP_OK ||
        gpio_config(&en) != ESP_OK ||
        gpio_set_level((gpio_num_t)TFW_ESP_COPROC_EN_GPIO, 0u) != ESP_OK ||
        gpio_hold_en((gpio_num_t)TFW_ESP_COPROC_EN_GPIO) != ESP_OK) {
        return TFW_ERR_INTERNAL;
    }
#endif
    return TFW_OK;
}
