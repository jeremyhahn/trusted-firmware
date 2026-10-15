/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A recording application for the host build. It defines every hook of
 * include/tfw/app.h and the descriptor, and writes one letter per hook call to
 * a log, so a test asserts the order the runner called them in as one string:
 *
 *   i init   r run   s shutdown   b before_update   a after_update
 */
#ifndef TFW_APP_MOCK_H
#define TFW_APP_MOCK_H

#include "tfw/app.h"

/* A valid descriptor (name "mock", no capabilities, no update path, 16 bytes of
 * configuration), init answering TFW_OK, run taking no time, an empty log. */
void tfw_mock_app_reset(void);

/* The descriptor the runner reads, writable here so one binary can present
 * each one the runner must refuse. */
tfw_app_descriptor_t *tfw_mock_app_descriptor(void);

void        tfw_mock_app_set_init(tfw_status_t st);
/* Each run advances the mock clock by ms: a slow application, on demand. */
void        tfw_mock_app_set_run_ms(tfw_ms_t ms);
const char *tfw_mock_app_log(void);
tfw_ms_t    tfw_mock_app_last_now(void);

#endif /* TFW_APP_MOCK_H */
