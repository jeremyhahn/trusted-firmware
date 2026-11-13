/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The tone on the RP2350: TIMER1 alarm 0 toggles a SIO pin (row rp2350).
 *
 * The pin is a plain digital pin (the Pico 2 W's TFW_TONE_PIN, GP7), so the
 * tone needs no pin function other than SIO: each alarm XORs the pin through
 * GPIO_OUT_XOR and re-arms the alarm half a period on. The step is
 * soc_calc.h's tfw_rp_tone_half_period on TIMER1's 1 us tick; a frequency it
 * refuses is TFW_ERR_RANGE.
 *
 * The alarm compares TIMELR for equality ([RP-DS] §12.8.4), so the interrupt
 * re-arms from the previous target, not from now: a late interrupt does not
 * stretch the period, and the tone does not drift.
 *
 * A start while a tone plays retargets: the interrupt is masked and the old
 * pin driven low before the new pin and step take over. A stop masks the
 * interrupt, disarms the alarm and drives the pin low.
 */

#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"
#include "soc_calc.h"

#define PORT_GPIO   0u
#define GPIO_COUNT  30u
#define ALARM0      0x1u

void tfw_cm_irq4(void);

static uint32_t tone_bit;       /* SIO mask of the playing pin, 0 if none */
static uint32_t tone_step;      /* ticks between toggles */
static uint32_t tone_target;    /* TIMELR value ALARM0 was last armed for */

/* Mask and disarm the alarm, and drive the playing pin (if any) low. */
static void tone_halt(void)
{
    TFW_RP_TIMER1_INTE  = 0u;
    TFW_RP_TIMER1_ARMED = ALARM0;
    TFW_RP_TIMER1_INTR  = ALARM0;
    if (tone_bit != 0u) {
        TFW_RP_SIO_GPIO_OUT_CLR = tone_bit;
    }
    tone_bit = 0u;
}

/* TIMER1 out of reset, and its TICKS generator dividing clk_ref to 1 us. */
static void tone_timer_on(void)
{
    if ((TFW_RP_RESETS_DONE & TFW_RP_RESET_TIMER1) == 0u) {
        TFW_RP_CLR(TFW_RP_RESETS_RESET) = TFW_RP_RESET_TIMER1;
        while ((TFW_RP_RESETS_DONE & TFW_RP_RESET_TIMER1) == 0u) {
        }
    }
    if ((TFW_RP_TICKS_TIMER1_CTRL & TFW_RP_TICKS_ENABLE) == 0u) {
        TFW_RP_TICKS_TIMER1_CYCLES = TFW_CM_CPU_HZ / TFW_RP_TONE_TICK_HZ;
        TFW_RP_TICKS_TIMER1_CTRL   = TFW_RP_TICKS_ENABLE;
    }
}

tfw_status_t tfw_hal_tone_start(const tfw_pinref_t *p, uint16_t hz)
{
    uint32_t step;
    uint32_t bit;

    if (p->port != PORT_GPIO || p->bit >= GPIO_COUNT) {
        return TFW_ERR_BAD_PIN;
    }
    step = tfw_rp_tone_half_period(TFW_RP_TONE_TICK_HZ, hz);
    if (step == 0u) {
        return TFW_ERR_RANGE;
    }
    bit = 1u << p->bit;

    tone_timer_on();
    tone_halt();

    TFW_RP_SIO_GPIO_OUT_CLR = bit;
    TFW_RP_SIO_GPIO_OE_SET  = bit;
    TFW_CM_REG(TFW_RP_PADS_GPIO(p->bit)) = tfw_rp_pad_value(0u);
    TFW_RP_IO_CTRL(p->bit) = TFW_RP_FUNC_SIO;

    tone_bit    = bit;
    tone_step   = step;
    tone_target = TFW_RP_TIMER1_TIMELR + step;
    TFW_RP_TIMER1_ALARM0 = tone_target;
    TFW_RP_TIMER1_INTE   = ALARM0;
    tfw_cm_nvic_enable(TFW_RP_TIMER1_IRQ_0);
    return TFW_OK;
}

tfw_status_t tfw_hal_tone_stop(void)
{
    tone_halt();
    return TFW_OK;
}

/* Alarm 0 fired (it disarms itself): toggle, and re-arm from the target. */
void tfw_cm_irq4(void)
{
    TFW_RP_TIMER1_INTR = ALARM0;
    if (tone_bit == 0u) {
        return;
    }
    TFW_RP_SIO_GPIO_OUT_XOR = tone_bit;
    tone_target += tone_step;
    TFW_RP_TIMER1_ALARM0 = tone_target;
}
