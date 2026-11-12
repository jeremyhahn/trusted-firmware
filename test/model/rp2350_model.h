/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A register model of the RP2350 blocks the port's converter driver touches
 * (SPEC row `rp2350`), for test/unit/test_rp2350_adc.c: clk_adc in CLOCKS,
 * the ADC bit of RESETS, IO_BANK0's and PADS_BANK0's GPIO26-29, and the ADC's
 * CS and RESULT.
 *
 * The SoC code reaches a register only through TFW_CM_REG; mk/host.mk puts
 * test/model/cortex-m/rp2350/ ahead of src/hal/cortex-m/ so its cm_regs.h
 * sends every access here, as test/model/imx95_model.h does for the i.MX 95.
 * A read and a write are told apart the same way: each access gets a scratch
 * word holding what a read would see, and at the next access, or at
 * tfw_rp2350_model_get(), a changed word was a write. No write the driver
 * makes equals the read view (CS never reads START_ONCE; the RESETS aliases
 * read zero), so none is taken for a read. An access to any other address is
 * counted in `unmodelled`, and every test asserts it is zero.
 */
#ifndef TFW_TEST_RP2350_MODEL_H
#define TFW_TEST_RP2350_MODEL_H

#include <stdint.h>

#define TFW_RP2350_MODEL_GPIO0 26u   /* the first GPIO modelled: ADC0 */
#define TFW_RP2350_MODEL_GPIOS 4u    /* GPIO26-29 */
#define TFW_RP2350_MODEL_INPUTS 5u   /* AINSEL 0-4 on the RP2350A */

struct tfw_rp2350_model {
    uint32_t clk_adc_ctrl, clk_adc_div;
    uint32_t reset;                          /* RESETS.RESET; DONE is its inverse */
    uint32_t io_ctrl[TFW_RP2350_MODEL_GPIOS];
    uint32_t pad[TFW_RP2350_MODEL_GPIOS];
    struct {
        uint8_t  en, ready, err;
        uint32_t ainsel;
        uint32_t result;
        uint32_t conversions;                /* START_ONCE taken */
        uint32_t ignored;                    /* CS written in reset or unclocked */
        uint32_t pad_at_start[TFW_RP2350_MODEL_INPUTS];   /* the pad when it converted */
        uint32_t io_at_start[TFW_RP2350_MODEL_INPUTS];
        uint16_t level[TFW_RP2350_MODEL_INPUTS];          /* set by a test */
        uint8_t  stuck;                      /* set by a test: READY never rises */
        uint8_t  fail;                       /* set by a test: a conversion sets ERR */
    } adc;
    uint32_t unmodelled;
    uintptr_t unmodelled_addr;
};

/* Out of reset: every block held in reset, clk_adc off, the pads at their
 * reset value (isolated, input off, pulled down). */
void tfw_rp2350_model_reset(void);

/* The model, with the last access settled. Tests read and set it here. */
struct tfw_rp2350_model *tfw_rp2350_model_get(void);

/* The substitute test/model/cortex-m/rp2350/cm_regs.h links in. */
volatile uint32_t *tfw_rp2350_model_reg(uintptr_t addr);

#endif /* TFW_TEST_RP2350_MODEL_H */
