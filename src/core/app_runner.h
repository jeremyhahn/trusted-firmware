/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The firmware's side of the application interface (include/tfw/app.h): the
 * one place that calls an application hook. It checks the descriptor before the
 * first call and keeps the state that decides whether there is a next one, so
 * an application is never called in a state it was not put in.
 *
 * Internal. An application includes include/tfw/app.h, never this.
 */
#ifndef TFW_CORE_APP_RUNNER_H
#define TFW_CORE_APP_RUNNER_H

#include <stdint.h>

/* The runner's state. Numbered now because an APP_INFO command will report it,
 * and on the wire values extend and never renumber. */
typedef uint8_t tfw_app_state_t;
#define TFW_APP_STATE_IDLE        0u   /* not started */
#define TFW_APP_STATE_RUNNING     1u   /* init returned TFW_OK */
#define TFW_APP_STATE_FAILED      2u   /* init refused; firmware-only */
#define TFW_APP_STATE_UNSUPPORTED 3u   /* the descriptor check failed; init never called */
#define TFW_APP_STATE_STOPPED     4u   /* the stop hooks ran */

/* At the end of startup, before interrupts. Checks the descriptor, then calls
 * tfw_app_init. Clears the overrun count. */
void tfw_app_runner_start(void);

/* Once per superloop pass. Calls tfw_app_run while RUNNING and counts a call
 * that took longer than TFW_APP_RUN_BUDGET_MS. */
void tfw_app_runner_poll(void);

/* Before a commanded reset. While RUNNING: tfw_app_before_update when
 * for_update, then tfw_app_shutdown. In any other state, nothing. */
void tfw_app_runner_stop(uint8_t for_update);

tfw_app_state_t tfw_app_runner_state(void);

/* Calls of tfw_app_run over budget since start, saturating at 0xFFFF. */
uint16_t tfw_app_runner_overruns(void);

#endif /* TFW_CORE_APP_RUNNER_H */
