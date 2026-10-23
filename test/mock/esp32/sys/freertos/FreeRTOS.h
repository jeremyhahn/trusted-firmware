/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for FreeRTOS.h as ESP-IDF v5.5.5 ships it, for
 * test/unit/test_esp_sys_mutate.c alone: the two types task.h names.
 */
#ifndef TFW_MOCK_ESP32_SYS_FREERTOS_H
#define TFW_MOCK_ESP32_SYS_FREERTOS_H

typedef void         *TaskHandle_t;
typedef unsigned int  UBaseType_t;

#endif
