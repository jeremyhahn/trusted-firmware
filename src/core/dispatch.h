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
#ifndef AG_DISPATCH_H
#define AG_DISPATCH_H

#include <stdint.h>
#include "ag/status.h"
#include "reader.h"
#include "writer.h"

/* A handler reads its arguments from r and writes its reply into w. The
 * dispatcher has already checked that the payload length is within the bounds
 * the table declares, so a handler validates VALUES, never arity. */
typedef ag_status_t (*ag_cmd_handler_t)(ag_reader_t *r, ag_writer_t *w);

/* Runs one command. Returns the status to put in the response frame; anything
 * the handler wrote is in w.
 *
 * On any non-OK status the response payload is deliberately whatever the
 * handler had emitted before failing -- the transport sends the status and
 * ignores the length, because a partial reply with a failure status is not
 * something a host should parse. */
ag_status_t ag_dispatch(uint8_t cmd, ag_reader_t *r, ag_writer_t *w);

/* True if this build has a handler for cmd. Used by CAPS and by the tests. */
uint8_t ag_dispatch_knows(uint8_t cmd);

#endif /* AG_DISPATCH_H */
