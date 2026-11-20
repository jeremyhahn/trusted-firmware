/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A register model of the nrf54l registers the PWM and tone HALs touch,
 * for host tests (test/unit/test_nrf54l_pwm.c), as test/model/imx95_model.h is for
 * the i.MX 95. Every register is a plain word that holds what was last
 * written and reads 0 from reset: a task register reading 1 is a task
 * triggered since the test cleared it.
 */
#ifndef TFW_NRF54L_MODEL_H
#define TFW_NRF54L_MODEL_H

#include <stdint.h>

/* The word at addr. Fails the test run (abort) past the model's capacity. */
volatile uint32_t *tfw_nrf54l_model_reg(uintptr_t addr);

/* Every register back to 0, and no late task. */
void tfw_nrf54l_model_reset(void);

/*
 * A task that completes late, as TASKS_STOP does at the end of the PWM
 * period it was triggered in: while *task reads nonzero, *event reads 0 for
 * `reads` more lookups of it, then 1, and *task is cleared (consumed). A
 * lookup of *blocked while *task is still pending is counted: a task that
 * must not be triggered before the late one completes. The words are the
 * model's own, &TFW_NRF_PWM_STOP and the like. Resets the count.
 */
void tfw_nrf54l_model_late_task(volatile uint32_t *task, volatile uint32_t *event,
                                unsigned reads, volatile uint32_t *blocked);

/* Lookups of the late task's blocked word while the task was pending. */
unsigned tfw_nrf54l_model_blocked(void);

#endif /* TFW_NRF54L_MODEL_H */
