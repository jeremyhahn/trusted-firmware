/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5's freertos/task.h: the task handle, the
 * idle priority and the priority setter src/hal/esp32/hal_sys.c's app_main
 * calls. test/mock/esp_sys_mock.c defines the setter so hal_sys.c links; no
 * host suite runs app_main, which calls the firmware's main() -- on the host,
 * the test runner's own.
 */
#ifndef TFW_MOCK_FREERTOS_TASK_H
#define TFW_MOCK_FREERTOS_TASK_H

#include "freertos/FreeRTOS.h"

typedef struct tskTaskControlBlock *TaskHandle_t;

#define tskIDLE_PRIORITY ((UBaseType_t)0U)

void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority);

/* hal_bus.c's 1-Wire slots mask interrupts; nothing to mask on the host. */
#define taskDISABLE_INTERRUPTS() ((void)0)
#define taskENABLE_INTERRUPTS()  ((void)0)

#endif
