/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "transport.h"
#include "tfw/hal.h"
#include "tfw/config.h"
#include "tfw/link.h"
#include "../core/dispatch.h"
#include "../core/reader.h"
#include "../core/writer.h"

void tfw_transport_init(tfw_transport_t *t)
{
    tfw_framer_init(&t->framer);
    t->out_len    = 0u;
    t->out_pos    = 0u;
    t->last_rx_ms = 0u;
    t->in_frame   = 0u;
    t->out_event  = 0u;
}

/* Pushes as much of the pending response as the link will take. Returns 1 if
 * bytes are still owed. */
static uint8_t drain(tfw_transport_t *t)
{
    while (t->out_pos < t->out_len) {
        if (!tfw_link_tx_try(t->out[t->out_pos], t->out_event)) {
            return 1u;                    /* ring full; resume next poll */
        }
        t->out_pos++;
    }
    tfw_link_tx_done(t->out_event);
    t->out_event = 0u;
    t->out_len = 0u;
    t->out_pos = 0u;
    return 0u;
}

/* Runs one decoded request through dispatch and leaves the encoded response in
 * t->out. The payload buffer is a local: it lives only for this call, and the
 * frame it produces is copied into t->out before it goes out of scope. */
static void handle(tfw_transport_t *t)
{
    uint8_t     payload[TFW_MAX_PAYLOAD];
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_status_t st;
    uint8_t     seq = tfw_frame_seq(&t->framer);
    uint8_t     op  = tfw_frame_op(&t->framer);

    /* A response arriving at a device is not a request. Silently ignored: it
     * is either a host bug or the echo of our own line, and answering it would
     * make two devices on one bus argue forever. */
    if (op & TFW_OP_RESPONSE_BIT) {
        return;
    }

    tfw_reader_init(&r, tfw_frame_payload(&t->framer), tfw_frame_payload_len(&t->framer));
    tfw_writer_init(&w, payload, (uint8_t)sizeof payload);

    /* A command the link does not carry gets the answer a command absent from
     * the image gets: from that wire's side, it is absent. */
    st = tfw_link_admits(op) ? tfw_dispatch(op, &r, &w) : TFW_ERR_UNKNOWN_CMD;

    /* On failure the reply carries the status and no payload. A partial reply
     * beside a failure status is something a host would have to guess about. */
    t->out_len = tfw_frame_encode_response(t->out, (uint16_t)sizeof t->out,
                                          seq, op, st,
                                          payload,
                                          (st == TFW_OK) ? tfw_writer_len(&w) : 0u);
    t->out_pos = 0u;
}

static void reply_status(tfw_transport_t *t, uint8_t seq, uint8_t cmd, tfw_status_t st)
{
    t->out_len = tfw_frame_encode_response(t->out, (uint16_t)sizeof t->out,
                                          seq, cmd, st, 0, 0u);
    t->out_pos = 0u;
}

void tfw_transport_poll(tfw_transport_t *t)
{
    uint8_t byte;

    tfw_link_poll();

    /* Finish what is owed before reading more. */
    if (t->out_len && drain(t)) {
        return;
    }

    /* A frame that stopped arriving mid-way is abandoned rather than left to
     * absorb the next good frame's opening bytes. */
    if (t->in_frame &&
        (tfw_ms_t)(tfw_hal_millis() - t->last_rx_ms) > (tfw_ms_t)TFW_FRAME_IDLE_MS) {
        tfw_framer_abort(&t->framer);
        t->in_frame = 0u;
    }

    while (tfw_link_rx(&byte, t->in_frame)) {
        tfw_frame_event_t e = tfw_framer_push(&t->framer, byte);

        t->last_rx_ms = tfw_hal_millis();
        t->in_frame   = 1u;

        switch (e) {
        case TFW_FRAME_READY:
            t->in_frame = 0u;
            handle(t);
            (void)drain(t);
            return;                       /* one frame per poll: stop-and-wait */

        case TFW_FRAME_BAD_CRC:
            /* Answered so the host learns promptly rather than by timeout. The
             * SEQ is taken from a frame whose integrity just failed, so it may
             * be wrong -- but a reply against the wrong sequence is still more
             * informative than silence, and the host's own timeout covers the
             * case where it is unusable. */
            t->in_frame = 0u;
            reply_status(t, tfw_frame_seq(&t->framer), tfw_frame_op(&t->framer) & 0x7Fu,
                         TFW_ERR_CRC);
            (void)drain(t);
            return;

        case TFW_FRAME_BAD_FORMAT:
            /* No trustworthy sequence number exists here -- the framer never
             * got far enough to have one -- so nothing is sent. The counters
             * in SYS_INFO are how a host sees this happening.
             *
             * Returns, matching READY and BAD_CRC below, rather than looping
             * straight on to whatever the ring holds next. Without that, a
             * BAD_FORMAT byte immediately followed in the same buffered burst
             * by a valid SYS_INFO request would dispatch and encode that
             * reply inside THIS poll() call -- before main.c's one-per-pass
             * tfw_sys_set_framer_errors() has had a chance to run again and
             * mirror the bump this event just made, so the reply would read
             * one event stale. Any bytes still in the ring are simply seen on
             * the next poll(), microseconds later. */
            t->in_frame = 0u;
            return;

        case TFW_FRAME_NONE:
        default:
            break;
        }
    }
}

uint8_t tfw_transport_send_event(tfw_transport_t *t, uint8_t event_id)
{
    if (t->out_len) {
        return 0u;
    }
    t->out_len = tfw_frame_encode_event(t->out, (uint16_t)sizeof t->out,
                                       TFW_OK, &event_id, 1u);
    t->out_pos   = 0u;
    t->out_event = 1u;
    (void)drain(t);
    return 1u;
}
