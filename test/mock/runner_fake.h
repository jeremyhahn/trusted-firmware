/* SPDX-License-Identifier: commercial */
/*
 * The runner's one entry the trial calls, tfw_app_runner_after_update, as a
 * fake for the trial's own link (test_ota_trial, test_ota_trial_mutate), which
 * carries no runner and no application. It counts its calls and answers what
 * the test set; the runner's half is pinned in test_app.c on the real runner.
 */
#ifndef TFW_RUNNER_FAKE_H
#define TFW_RUNNER_FAKE_H

#include "tfw/protocol.h"

/* Zero calls, answering TFW_OK. */
void         tfw_fake_runner_reset(void);
void         tfw_fake_runner_set_after_update(tfw_status_t st);
unsigned     tfw_fake_runner_after_update_calls(void);

#endif /* TFW_RUNNER_FAKE_H */
