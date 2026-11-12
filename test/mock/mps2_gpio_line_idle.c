/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * gpio_line.h for the host suites that link the MPS2 bus driver for its I2C
 * or SPI master (test_mps2_bus, test_mps2_spi_row, test_mps2_optiga): an idle
 * line, high, that nothing drives. test_mps2_onewire models the line itself.
 */
#include <stdint.h>
#include "gpio_line.h"

uint32_t tfw_mps2_gpio_lines(void)               { return 1u << TFW_MPS2_OW_BIT; }
void     tfw_mps2_gpio_drive_low(uint32_t mask)  { (void)mask; }
void     tfw_mps2_gpio_release(uint32_t mask)    { (void)mask; }
uint32_t tfw_mps2_gpio_irq_save(void)            { return 0u; }
void     tfw_mps2_gpio_irq_restore(uint32_t s)   { (void)s; }
