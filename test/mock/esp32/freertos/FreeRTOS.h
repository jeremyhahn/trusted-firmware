/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for ESP-IDF v5's freertos/FreeRTOS.h: the port's
 * unsigned base type, which a task priority is (portmacro.h: unsigned int).
 */
#ifndef TFW_MOCK_FREERTOS_H
#define TFW_MOCK_FREERTOS_H

typedef unsigned int UBaseType_t;

#endif
