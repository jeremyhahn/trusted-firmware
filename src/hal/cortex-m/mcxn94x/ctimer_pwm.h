/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The MCXN94x CTIMER in PWM mode, the part of hal_pwm.c that is arithmetic and
 * register sequence on one timer: it takes the timer's base, so its host test
 * hands it a register model (test/unit/test_mcxn_ctimer_pwm.c). The SYSCON
 * gate, reset and clock select and the pin's function stay in hal_pwm.c.
 *
 * Match register 3 is the period: the counter resets on it (MCR MR3R), so a
 * cycle is MR3 + 1 = 255 counts. A match channel n (0-2) in PWM mode (PWMC
 * PWMENn) is low from the start of the cycle until the counter reaches MRn and
 * high from there to the cycle's end: the output is the inverse of a "high
 * until the match" PWM. The duty is therefore written as MRn = 255 - duty:
 * duty 0 puts MRn at 255, past MR3, so it never matches and the pin stays low;
 * duty 255 puts MRn at 0 and the pin is high for the whole cycle. Both ends are
 * exact, and duty d is high for d of the 255 counts.
 */
#ifndef TFW_CM_MCXN94X_CTIMER_PWM_H
#define TFW_CM_MCXN94X_CTIMER_PWM_H

#include <stdint.h>

#include "tfw/status.h"

#define TFW_MCXN_CTIMER_PWM_HZ      1000u  /* the period the HAL asks for */
#define TFW_MCXN_CTIMER_PWM_MR3     254u   /* 255 counts a cycle */
#define TFW_MCXN_CTIMER_PWM_CHANNELS 3u    /* match 0-2; match 3 is the period */

/* The prescale (PR: the counter advances every PR + 1 clocks) and the period
 * match (MR3) for TFW_MCXN_CTIMER_PWM_HZ from a timer clock of clk_hz. A clock
 * of 0, or one too slow for a 255-count cycle at that rate, is refused. */
tfw_status_t mcxn_ctimer_pwm_period(uint32_t clk_hz, uint32_t *pr, uint32_t *mr3);

/* The CTIMER's registers as 32-bit words from its base (MCXN947_cm33_core0.h,
 * CTIMER_Type). */
#define TFW_MCXN_CT_TCR      (0x04u / 4u)
#define TFW_MCXN_CT_PR       (0x0Cu / 4u)
#define TFW_MCXN_CT_MCR      (0x14u / 4u)
#define TFW_MCXN_CT_MR(n)    (0x18u / 4u + (n))
#define TFW_MCXN_CT_CTCR     (0x70u / 4u)
#define TFW_MCXN_CT_PWMC     (0x74u / 4u)
#define TFW_MCXN_CT_WORDS    (0x88u / 4u)  /* through MSR[3] */

#define TFW_MCXN_CT_TCR_CEN  (1u << 0)
#define TFW_MCXN_CT_TCR_CRST (1u << 1)
#define TFW_MCXN_CT_MCR_MR3R (1u << 10)

/* The match value a duty is written as: 255 - duty (see above). */
uint32_t mcxn_ctimer_pwm_match(uint8_t duty);

/* Stop and reset the timer, program the period from clk_hz with every match
 * channel off (low), and start it counting. */
tfw_status_t mcxn_ctimer_pwm_start(volatile uint32_t *ct, uint32_t clk_hz);

/* Put match channel 0-2 in PWM mode at the given duty. */
tfw_status_t mcxn_ctimer_pwm_duty(volatile uint32_t *ct, uint8_t channel, uint8_t duty);

/* Take match channel 0-2 out of PWM mode; the last channel out stops the
 * counter and holds it in reset. Returns 1 when the timer was stopped. */
uint8_t mcxn_ctimer_pwm_halt(volatile uint32_t *ct, uint8_t channel);

#endif /* TFW_CM_MCXN94X_CTIMER_PWM_H */
