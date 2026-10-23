/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's driver/gpio.h, for
 * test/unit/test_esp_pulse_mutate.c alone: the one call
 * src/hal/esp32/hal_pulse.c makes. The test implements it.
 */
#ifndef TFW_MOCK_ESP32_PULSE_DRIVER_GPIO_H
#define TFW_MOCK_ESP32_PULSE_DRIVER_GPIO_H

typedef int gpio_num_t;

int gpio_get_level(gpio_num_t gpio_num);

#endif
