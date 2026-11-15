/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5.5.5's driver/ledc.h: the duty readback
 * src/hal/esp32/hal_selfcheck.c makes (test/unit/test_esp_selfcheck.c keeps it).
 */
#ifndef TFW_MOCK_SELFCHECK_LEDC_H
#define TFW_MOCK_SELFCHECK_LEDC_H

#include <stdint.h>

typedef int ledc_mode_t;
typedef int ledc_channel_t;

uint32_t ledc_get_duty(ledc_mode_t speed_mode, ledc_channel_t channel);

#endif
