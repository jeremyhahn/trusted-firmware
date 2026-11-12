/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * PWM on the MPS2 boards, which have no PWM peripheral: software PWM on the
 * CMSDK APB dual timer's first counter (soc_regs.h), built in the PWM=1 row
 * only (profile.mk's TFW_CM_PWM). Timer 2 and the APB timers are left alone.
 *
 * One fixed period of 256 steps, one per duty count (TFW_MPS2_PWM_STEP timer
 * ticks each, TFW_MPS2_PWM_HZ periods a second). At the period's start every
 * running channel's pin goes high; at a channel's duty its pin goes low. The
 * timer is not left free-running at the step rate: each interrupt loads the
 * distance to the next edge, so a period costs one interrupt per distinct
 * duty plus one, three at most with the board's two channels (board.c).
 *
 * The pins are the board's LEDs, driven through hal_gpio.c from the
 * interrupt. Duty 0 and 255 drive the pin low and high and take the channel
 * off the timer, as src/hal/avr/hal_pwm.c's compare unit would leave it; the
 * timer stops when no channel is left on it. A new duty for a running channel
 * takes effect at the next period start, so a period is never cut short.
 */
#include <stdint.h>

#include "tfw/hal.h"
#include "tfw/config.h"
#include "soc_regs.h"

/* Only the PWM=1 row links this file; outside it TFW_PWM_COUNT is 0 and
 * there is nothing to drive, so the body is that row's alone. */
#if TFW_FEATURE_PWM

#define PWM_PERIOD 256u   /* steps; the period's end, the next one's start */

typedef struct {
    tfw_pinref_t pin;
    uint8_t duty;   /* this period's, 0 when the channel is off the timer */
    uint8_t next;   /* the next period's, 0 to come off the timer */
} pwm_ch_t;

static struct {
    pwm_ch_t ch[TFW_PWM_COUNT];
    uint16_t at;        /* the step the running interval ends at */
    uint8_t  running;
} s_pwm;

void TFW_MPS2_IRQ_HANDLER(TFW_MPS2_DUALTIMER_IRQ)(void);

static void pin_set(const pwm_ch_t *c, uint8_t level)
{
    (void)tfw_hal_gpio_write(&c->pin, level);
}

/* Starts a period: the pending duties apply and their pins go high. */
static void period_start(void)
{
    uint8_t i;

    for (i = 0u; i < TFW_PWM_COUNT; i++) {
        s_pwm.ch[i].duty = s_pwm.ch[i].next;
        if (s_pwm.ch[i].duty != 0u) {
            pin_set(&s_pwm.ch[i], 1u);
        }
    }
}

/* Loads the distance from step `from` to the next edge. */
static void arm_next(uint16_t from)
{
    uint16_t next = PWM_PERIOD;
    uint8_t  i;

    for (i = 0u; i < TFW_PWM_COUNT; i++) {
        uint16_t d = s_pwm.ch[i].duty;

        if (d > from && d < next) {
            next = d;
        }
    }
    s_pwm.at = next;
    /* A Load write restarts the count from it (cmsdk-apb-dualtimer.c). */
    TFW_MPS2_DT1(TFW_MPS2_DT1_LOAD) = (uint32_t)(next - from) * TFW_MPS2_PWM_STEP;
}

static void timer_stop(void)
{
    TFW_MPS2_DT1(TFW_MPS2_DT1_CONTROL) = 0u;
    TFW_MPS2_DT1(TFW_MPS2_DT1_INTCLR)  = 1u;
    tfw_cm_nvic_disable((uint32_t)TFW_MPS2_DUALTIMER_IRQ);
    s_pwm.running = 0u;
}

static uint8_t any_next(void)
{
    uint8_t i;

    for (i = 0u; i < TFW_PWM_COUNT; i++) {
        if (s_pwm.ch[i].next != 0u) {
            return 1u;
        }
    }
    return 0u;
}

void TFW_MPS2_IRQ_HANDLER(TFW_MPS2_DUALTIMER_IRQ)(void)
{
    uint16_t from;

    TFW_MPS2_DT1(TFW_MPS2_DT1_INTCLR) = 1u;
    if (!s_pwm.running) {
        return;
    }
    if (s_pwm.at >= PWM_PERIOD) {
        period_start();
        from = 0u;
    } else {
        uint8_t i;

        from = s_pwm.at;
        for (i = 0u; i < TFW_PWM_COUNT; i++) {
            if (s_pwm.ch[i].duty == from) {
                pin_set(&s_pwm.ch[i], 0u);
            }
        }
    }
    arm_next(from);
}

/* Takes channel c off the timer with its pin at `level`; the timer stops when
 * it was the last. */
static void ch_static(pwm_ch_t *c, uint8_t level)
{
    c->duty = 0u;
    c->next = 0u;
    pin_set(c, level);
    if (s_pwm.running && !any_next()) {
        timer_stop();
    }
}

static pwm_ch_t *ch_of(const tfw_pwmref_t *p)
{
    pwm_ch_t *c;

    if (p->channel >= TFW_PWM_COUNT) {
        return (pwm_ch_t *)0;
    }
    c = &s_pwm.ch[p->channel];
    c->pin.port = p->port;
    c->pin.bit  = p->bit;
    return c;
}

tfw_status_t tfw_hal_pwm_write(const tfw_pwmref_t *p, uint8_t duty)
{
    pwm_ch_t *c = ch_of(p);
    uint32_t  key;

    if (c == (pwm_ch_t *)0) {
        return TFW_ERR_BAD_PIN;
    }
    key = tfw_cm_irq_save();
    if (duty == 0u || duty == 255u) {
        ch_static(c, duty != 0u);
    } else if (!s_pwm.running) {
        c->next = duty;
        period_start();
        s_pwm.running = 1u;
        TFW_MPS2_DT1(TFW_MPS2_DT1_INTCLR) = 1u;
        arm_next(0u);
        TFW_MPS2_DT1(TFW_MPS2_DT1_CONTROL) = TFW_MPS2_DT_CTRL_RUN;
        tfw_cm_nvic_enable((uint32_t)TFW_MPS2_DUALTIMER_IRQ);
    } else {
        c->next = duty;
    }
    tfw_cm_irq_restore(key);
    return TFW_OK;
}

tfw_status_t tfw_hal_pwm_stop(const tfw_pwmref_t *p)
{
    pwm_ch_t *c = ch_of(p);
    uint32_t  key;

    if (c == (pwm_ch_t *)0) {
        return TFW_ERR_BAD_PIN;
    }
    key = tfw_cm_irq_save();
    ch_static(c, 0u);
    tfw_cm_irq_restore(key);
    return TFW_OK;
}

#endif /* TFW_FEATURE_PWM */
