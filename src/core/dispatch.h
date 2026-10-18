/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Command dispatch: request payload in, response payload out.
 *
 * This layer may call the reader, the writer and the capability modules. It may
 * NOT call the HAL, the board descriptor, the framer or the transport. That is
 * the "never reach through the service layer" rule made structural: dispatch.c
 * contains no register access and no I/O, so it is entirely host-testable, and
 * a capability that wants hardware has to go through its own module to get it.
 */
#ifndef TFW_DISPATCH_H
#define TFW_DISPATCH_H

#include <stdint.h>
#include "tfw/status.h"
#include "reader.h"
#include "writer.h"

/* A handler reads its arguments from r and writes its reply into w. The
 * dispatcher has already checked that the payload length is within the bounds
 * the table declares, so a handler validates VALUES, never arity. */
typedef tfw_status_t (*tfw_cmd_handler_t)(tfw_reader_t *r, tfw_writer_t *w);

/* Runs one command. Returns the status to put in the response frame; anything
 * the handler wrote is in w.
 *
 * On any non-OK status the response payload is deliberately whatever the
 * handler had emitted before failing -- the transport sends the status and
 * ignores the length, because a partial reply with a failure status is not
 * something a host should parse. */
tfw_status_t tfw_dispatch(uint8_t cmd, tfw_reader_t *r, tfw_writer_t *w);

/* A request the transport answered with a refusal without dispatching it -- a
 * command the link does not admit, a frame whose CRC failed -- finishes here,
 * as a refused cmd: tfw_dispatch_last() reads cmd, tfw_dispatch_last_ok() 0.
 * The device answered something in between, so a held OTA_AUTH is gone (#137). */
void tfw_dispatch_refused(uint8_t cmd);

/* The id of the last command tfw_dispatch finished, known or not, refused or
 * not; 0 before the first. Inside a handler it is the command before this one:
 * OTA_AUTH's signature counts only for the command right after it (#137). */
uint8_t tfw_dispatch_last(void);

/* 1 if that command finished TFW_OK, 0 if it was refused, whether by the table
 * before its handler ran or by the handler; 0 before the first. A refused
 * OTA_AUTH holds nothing for the command after it (#137). */
uint8_t tfw_dispatch_last_ok(void);

/* True if this build has a handler for cmd. Used by CAPS and by the tests. */
uint8_t tfw_dispatch_knows(uint8_t cmd);

#endif /* TFW_DISPATCH_H */
