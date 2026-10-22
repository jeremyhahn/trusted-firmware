/* SPDX-License-Identifier: commercial */
/*
 * The runner's two entries the trial calls, tfw_app_runner_state and
 * tfw_app_runner_after_update, as a fake for the trial's own link
 * (test_ota_trial, test_ota_trial_mutate), which carries no runner and no
 * application. It reports the state the test set, counts after_update's calls
 * and answers what the test set; the runner's half is pinned in test_app.c on the real runner.
 */
#ifndef TFW_RUNNER_FAKE_H
#define TFW_RUNNER_FAKE_H

#include "tfw/protocol.h"
#include "app_runner.h"

/* Zero calls, answering TFW_OK, in TFW_APP_STATE_RUNNING. */
void         tfw_fake_runner_reset(void);
void         tfw_fake_runner_set_state(tfw_app_state_t st);
void         tfw_fake_runner_set_after_update(tfw_status_t st);
unsigned     tfw_fake_runner_after_update_calls(void);

#endif /* TFW_RUNNER_FAKE_H */
