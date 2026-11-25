/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * A recording application for the host build. It defines every hook of
 * include/tfw/app.h and the descriptor, and writes one letter per hook call to
 * a log, so a test asserts the order the runner called them in as one string:
 *
 *   i init   r run   s shutdown   b before_update   a after_update   e evidence
 *   c command
 */
#ifndef TFW_APP_MOCK_H
#define TFW_APP_MOCK_H

#include "tfw/app.h"

/* A valid descriptor (name "mock", no capabilities, no update path, 16 bytes of
 * configuration), init and after_update answering TFW_OK, run taking no time,
 * an empty log. */
void tfw_mock_app_reset(void);

/* The descriptor the runner reads, writable here so one binary can present
 * each one the runner must refuse. */
tfw_app_descriptor_t *tfw_mock_app_descriptor(void);

void        tfw_mock_app_set_init(tfw_status_t st);
/* What tfw_app_after_update answers; reset makes it TFW_OK. */
void        tfw_mock_app_set_after_update(tfw_status_t st);
/* Each run advances the mock clock by ms: a slow application, on demand. */
void        tfw_mock_app_set_run_ms(tfw_ms_t ms);
const char *tfw_mock_app_log(void);
tfw_ms_t    tfw_mock_app_last_now(void);
/* What tfw_app_evidence answers and writes; reset makes it TFW_ERR_UNSUPPORTED
 * over zeros, an application that supplies none. */
void        tfw_mock_app_set_evidence(tfw_status_t st, const uint8_t ev[TFW_APP_EVIDENCE_LEN]);

/* tfw_app_command (log letter c): what it answers and the reply it writes,
 * whatever it answers; reset makes it TFW_OK with an empty reply. */
void        tfw_mock_app_set_command(tfw_status_t st, const uint8_t *reply, uint8_t len);
/* The hook reads n bytes of arguments, past the end when n exceeds them; reset
 * reads all that remain. */
void        tfw_mock_app_set_command_reads(uint8_t n);
/* Each command call advances the mock clock by ms, as set_run_ms does for run. */
void        tfw_mock_app_set_command_ms(tfw_ms_t ms);
/* The last call's op and the argument bytes it read. */
uint8_t        tfw_mock_app_command_op(void);
uint8_t        tfw_mock_app_command_args_len(void);
const uint8_t *tfw_mock_app_command_args(void);

#endif /* TFW_APP_MOCK_H */
