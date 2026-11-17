/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * One open-drain line on the CMSDK GPIO block 0 the MPS2 FPGA images carry on
 * the shield header (Arm DDI 0479, Cortex-M System Design Kit, 4.6 "AHB GPIO"):
 *
 *   DATA       read  +0x00  the pin levels
 *   DATAOUT    write +0x04  the level each output drives: left 0
 *   OUTENSET   write +0x10  a 1 makes the bit an output, so the line is low
 *   OUTENCLR   write +0x14  a 1 makes the bit an input, so the line floats
 *
 * The line is never driven high: low is output-low, high is input with the
 * bus's pull-up, as the AVR's 1-Wire master switches DDR. hal_bus.c times the
 * 1-Wire slots over these; hal_gpio.c is the only file that touches the
 * registers, so the host suite (test/unit/test_mps2_onewire.c) links hal_bus.c
 * against a modelled pin in its place.
 */
#ifndef TFW_CM_MPS2_GPIO_LINE_H
#define TFW_CM_MPS2_GPIO_LINE_H

#include <stdint.h>

/* The board table's port for GPIO block 0, and the 1-Wire row's bit on it
 * (board.c, TFW_CM_ONEWIRE). */
#define TFW_MPS2_PORT_GPIO0 2u
#define TFW_MPS2_OW_BIT     0u

/* DATA: the levels on the block's pins, a 1 where high. */
uint32_t tfw_mps2_gpio_lines(void);
/* DATAOUT cleared and OUTENSET: the lines in mask are pulled low. */
void     tfw_mps2_gpio_drive_low(uint32_t mask);
/* OUTENCLR: the lines in mask float, high unless a device holds them low. */
void     tfw_mps2_gpio_release(uint32_t mask);
/* The port's critical section (tfw_cm_irq_save and tfw_cm_irq_restore in
 * cm_regs.h): a 1-Wire slot runs with interrupts masked, as on AVR. */
uint32_t tfw_mps2_gpio_irq_save(void);
void     tfw_mps2_gpio_irq_restore(uint32_t saved);

#endif /* TFW_CM_MPS2_GPIO_LINE_H */
