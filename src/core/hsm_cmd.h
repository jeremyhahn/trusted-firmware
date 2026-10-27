/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The channel's five commands (docs/hsm-self-defence/ slice 4): the A55's
 * messages to the response engine, carried by the wire protocol. Linked only
 * where TFW_FEATURE_HSM is built; commands.def is the one place that reads it.
 */
#ifndef TFW_CORE_HSM_CMD_H
#define TFW_CORE_HSM_CMD_H

#include <stdint.h>

#include "tfw/status.h"
#include "hsm.h"
#include "reader.h"
#include "writer.h"

tfw_status_t tfw_hsm_cmd_heartbeat(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_hsm_cmd_report(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_hsm_cmd_verdict(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_hsm_cmd_select(tfw_reader_t *r, tfw_writer_t *w);
tfw_status_t tfw_hsm_cmd_load(tfw_reader_t *r, tfw_writer_t *w);

/* What a reset does to the engine and the pending envelope: no P, nothing
 * buffered, the floor back at TFW_HSM_FLOOR_MIN. The first command after a
 * reset does it unasked; a test calls it between cases. */
void tfw_hsm_cmd_reset(void);

/* The one engine the commands and the loop (hsm_loop.c) share, ticked to now
 * first, so R5's heartbeat deadline is checked before anything else is. */
tfw_hsm_t *tfw_hsm_cmd_engine(uint32_t now);

#endif
