/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The host stand-in for FreeRTOS's task.h as ESP-IDF v5.5.5 ships it, for
 * test/unit/test_esp_bus_mutate.c alone: the interrupt mask
 * src/hal/esp32/hal_bus.c holds around a 1-Wire slot, as calls the test
 * implements and logs.
 */
#ifndef TFW_MOCK_ESP32_BUS_FREERTOS_TASK_H
#define TFW_MOCK_ESP32_BUS_FREERTOS_TASK_H

void mock_irq_disable(void);
void mock_irq_enable(void);

#define taskDISABLE_INTERRUPTS() mock_irq_disable()
#define taskENABLE_INTERRUPTS()  mock_irq_enable()

#endif
