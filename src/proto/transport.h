/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The only layer that touches the byte link, through tfw/link.h, which picks
 * the wire at link time (the UART alone, or the UART and Ethernet). Everything
 * above it -- framing,
 * dispatch, capabilities -- is a pure function of bytes and state, which is
 * what lets all of it run on the host.
 *
 * Strict stop-and-wait, deliberately. One request is decoded, dispatched and
 * its response fully emitted before the next byte of input is looked at. The
 * device has one response buffer, so there is nothing to gain from overlapping
 * and a great deal to lose: interleaved responses would need correlation state
 * on a part with two kilobytes of RAM.
 *
 * Nothing here blocks. A full TX ring is not an error; the emit cursor simply
 * stays where it is and the next poll continues from there, so the scheduler
 * keeps running while a long response drains.
 */
#ifndef TFW_TRANSPORT_H
#define TFW_TRANSPORT_H

#include <stdint.h>
#include "tfw/types.h"
#include "frame.h"

typedef struct {
    tfw_framer_t framer;
    uint8_t     out[TFW_MAX_FRAME];
    uint16_t    out_len;      /* bytes the pending response occupies */
    uint16_t    out_pos;      /* how many of them have reached the ring */
    tfw_ms_t     last_rx_ms;   /* for the partial-frame idle guard */
    uint8_t     in_frame;     /* a frame is partially received */
    uint8_t     out_event;    /* the pending bytes are an event: serial only */
} tfw_transport_t;

void tfw_transport_init(tfw_transport_t *t);

/* One pass. Emits what it can of any pending response, then consumes input up
 * to and including one complete frame -- READY, BAD_CRC or BAD_FORMAT all stop
 * it, so a diagnostic event (a malformed frame) gets exactly the same one-per-
 * poll treatment a real frame does, and never shares a poll with the frame
 * after it. */
void tfw_transport_poll(tfw_transport_t *t);

/* Queues an unsolicited event. Refused, returning 0, while a response is still
 * draining: events are emitted only when nothing else is in flight, which is
 * what keeps the host a strict request/response state machine instead of a
 * reassembler. The caller retries on a later poll. */
uint8_t tfw_transport_send_event(tfw_transport_t *t, uint8_t event_id);

#endif /* TFW_TRANSPORT_H */
