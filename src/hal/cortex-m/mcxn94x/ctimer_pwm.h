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

#endif /* TFW_CM_MCXN94X_CTIMER_PWM_H */
