/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for FreeRTOS's task.h, for test/unit/test_esp_sys_mutate.c
 * alone: the one call src/hal/esp32/hal_sys.c makes, with FreeRTOS's idle
 * priority. The test implements it.
 */
#ifndef TFW_MOCK_ESP32_SYS_TASK_H
#define TFW_MOCK_ESP32_SYS_TASK_H

#include "freertos/FreeRTOS.h"

#define tskIDLE_PRIORITY ((UBaseType_t)0U)

void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority);

#endif
