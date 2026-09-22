/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "transport.h"
#include "ag/hal.h"
#include "ag/config.h"
#include "../core/dispatch.h"
#include "../core/reader.h"
#include "../core/writer.h"

void ag_transport_init(ag_transport_t *t)
{
    ag_framer_init(&t->framer);
    t->out_len    = 0u;
    t->out_pos    = 0u;
    t->last_rx_ms = 0u;
    t->in_frame   = 0u;
}

/* Pushes as much of the pending response as the ring will take. Returns 1 if
 * bytes are still owed. */
static uint8_t drain(ag_transport_t *t)
{
    while (t->out_pos < t->out_len) {
        if (!ag_hal_uart_tx_try(t->out[t->out_pos])) {
            return 1u;                    /* ring full; resume next poll */
        }
        t->out_pos++;
    }
    t->out_len = 0u;
    t->out_pos = 0u;
    return 0u;
}

/* Runs one decoded request through dispatch and leaves the encoded response in
 * t->out. The payload buffer is a local: it lives only for this call, and the
 * frame it produces is copied into t->out before it goes out of scope. */
static void handle(ag_transport_t *t)
{
    uint8_t     payload[AG_MAX_PAYLOAD];
    ag_reader_t r;
    ag_writer_t w;
    ag_status_t st;
    uint8_t     seq = ag_frame_seq(&t->framer);
    uint8_t     op  = ag_frame_op(&t->framer);

    /* A response arriving at a device is not a request. Silently ignored: it
     * is either a host bug or the echo of our own line, and answering it would
     * make two devices on one bus argue forever. */
    if (op & AG_OP_RESPONSE_BIT) {
        return;
    }

    ag_reader_init(&r, ag_frame_payload(&t->framer), ag_frame_payload_len(&t->framer));
    ag_writer_init(&w, payload, (uint8_t)sizeof payload);

    st = ag_dispatch(op, &r, &w);

    /* On failure the reply carries the status and no payload. A partial reply
     * beside a failure status is something a host would have to guess about. */
    t->out_len = ag_frame_encode_response(t->out, (uint16_t)sizeof t->out,
                                          seq, op, st,
                                          payload,
                                          (st == AG_OK) ? ag_writer_len(&w) : 0u);
    t->out_pos = 0u;
}

static void reply_status(ag_transport_t *t, uint8_t seq, uint8_t cmd, ag_status_t st)
{
    t->out_len = ag_frame_encode_response(t->out, (uint16_t)sizeof t->out,
                                          seq, cmd, st, 0, 0u);
    t->out_pos = 0u;
}

void ag_transport_poll(ag_transport_t *t)
{
    uint8_t byte;

    /* Finish what is owed before reading more. */
    if (t->out_len && drain(t)) {
        return;
    }

    /* A frame that stopped arriving mid-way is abandoned rather than left to
     * absorb the next good frame's opening bytes. */
    if (t->in_frame &&
        (ag_ms_t)(ag_hal_millis() - t->last_rx_ms) > (ag_ms_t)AG_FRAME_IDLE_MS) {
        ag_framer_abort(&t->framer);
        t->in_frame = 0u;
    }

    while (ag_hal_uart_rx(&byte)) {
        ag_frame_event_t e = ag_framer_push(&t->framer, byte);

        t->last_rx_ms = ag_hal_millis();
        t->in_frame   = 1u;

        switch (e) {
        case AG_FRAME_READY:
            t->in_frame = 0u;
            handle(t);
            (void)drain(t);
            return;                       /* one frame per poll: stop-and-wait */

        case AG_FRAME_BAD_CRC:
            /* Answered so the host learns promptly rather than by timeout. The
             * SEQ is taken from a frame whose integrity just failed, so it may
             * be wrong -- but a reply against the wrong sequence is still more
             * informative than silence, and the host's own timeout covers the
             * case where it is unusable. */
            t->in_frame = 0u;
            reply_status(t, ag_frame_seq(&t->framer), ag_frame_op(&t->framer) & 0x7Fu,
                         AG_ERR_CRC);
            (void)drain(t);
            return;

        case AG_FRAME_BAD_FORMAT:
            /* No trustworthy sequence number exists here -- the framer never
             * got far enough to have one -- so nothing is sent. The counters
             * in SYS_INFO are how a host sees this happening. */
            t->in_frame = 0u;
            break;

        case AG_FRAME_NONE:
        default:
            break;
        }
    }
}

uint8_t ag_transport_send_event(ag_transport_t *t, uint8_t event_id)
{
    if (t->out_len) {
        return 0u;
    }
    t->out_len = ag_frame_encode_event(t->out, (uint16_t)sizeof t->out,
                                       AG_OK, &event_id, 1u);
    t->out_pos = 0u;
    (void)drain(t);
    return 1u;
}
