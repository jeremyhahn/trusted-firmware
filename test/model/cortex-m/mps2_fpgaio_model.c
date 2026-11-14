/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MPS2 FPGAIO LED register file (mps2_fpgaio_model.h): TFW_CM_REG, through
 * the stand-in cm_regs.h, reaches tfw_cm_model_reg here. That cm_regs.h is
 * taken from the include path, not this directory's i.MX95 one beside it.
 */
#include <stdint.h>

#include "tfw/config.h"
#include <cm_regs.h>
#include "mps2_fpgaio_model.h"

uint32_t tfw_mps2_model_led;
unsigned tfw_mps2_model_led_accesses;
unsigned tfw_mps2_model_stray;

static uint32_t scratch;

uint32_t tfw_mps2_model_led_addr(void)
{
    return (uint32_t)(TFW_MPS2_FPGAIO_BASE + 0x00u);
}

void tfw_mps2_model_reset(uint32_t led)
{
    tfw_mps2_model_led          = led;
    tfw_mps2_model_led_accesses = 0u;
    tfw_mps2_model_stray        = 0u;
}

volatile uint32_t *tfw_cm_model_reg(uint32_t addr)
{
    if (addr == tfw_mps2_model_led_addr()) {
        tfw_mps2_model_led_accesses++;
        return (volatile uint32_t *)&tfw_mps2_model_led;
    }
    tfw_mps2_model_stray++;
    return (volatile uint32_t *)&scratch;
}

/* hal_gpio.c's read-modify-write of the LED word runs with interrupts
 * masked (row/mps2-tone: the tone's TIMER0 handler writes the same word).
 * The host has no PRIMASK: a flag stands in for it. */
static uint32_t irq_masked;

uint32_t tfw_cm_irq_save(void)
{
    uint32_t was = irq_masked;

    irq_masked = 1u;
    return was;
}

void tfw_cm_irq_restore(uint32_t primask)
{
    irq_masked = primask;
}
