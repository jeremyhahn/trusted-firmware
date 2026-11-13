/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 FPGA I/O block's LED register (FPGAIO +0x00, AN505 Table 4-5,
 * AN500 Table 3-6) as a host register file, for a suite that links
 * src/hal/cortex-m/mps2/hal_gpio.c with boot/hal/cortex-m/rp2350/test/cm_regs.h
 * on its include path. The model holds that one register; an access to any
 * other address is counted as stray and lands in a scratch cell.
 */
#ifndef TFW_MPS2_FPGAIO_MODEL_H
#define TFW_MPS2_FPGAIO_MODEL_H

#include <stdint.h>

/* The LED register's value; a suite presets it and reads it back. */
extern uint32_t tfw_mps2_model_led;
/* Accesses to the LED register, and to any address the model does not hold. */
extern unsigned tfw_mps2_model_led_accesses;
extern unsigned tfw_mps2_model_stray;

/* The LED register's address on the profile the model is compiled with. */
uint32_t tfw_mps2_model_led_addr(void);

/* Both counts to zero and the LED register to led. */
void tfw_mps2_model_reset(uint32_t led);

#endif /* TFW_MPS2_FPGAIO_MODEL_H */
