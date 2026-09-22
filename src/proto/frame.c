/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include "frame.h"
#include "crc16.h"

void ag_framer_init(ag_framer_t *f)
{
    f->state      = (uint8_t)AG_FRAME_WAIT_SOF;
    f->want       = 0u;
    f->got        = 0u;
    f->crc        = AG_CRC16_INIT;
    f->crc_rx     = 0u;
    f->err_crc    = 0u;
    f->err_format = 0u;
}

void ag_framer_abort(ag_framer_t *f)
{
    f->state = (uint8_t)AG_FRAME_WAIT_SOF;
    f->want  = 0u;
    f->got   = 0u;
}

/* Saturating, because a counter that wraps to zero reads as "healthy" to a
 * host polling SYS_INFO, which is the one thing it must never say. */
static void bump(uint16_t *counter)
{
    if (*counter != 0xFFFFu) {
        (*counter)++;
    }
}

/* Responses and events carry a STATUS byte ahead of their payload; requests do
 * not. OP says which: the response bit is set on a response, and the event
 * opcode is 0xFF, which has it set too. */
static uint8_t has_status(const ag_framer_t *f)
{
    return (uint8_t)((f->body[1] & AG_OP_RESPONSE_BIT) != 0u);
}

ag_frame_event_t ag_framer_push(ag_framer_t *f, uint8_t byte)
{
    switch ((ag_frame_state_t)f->state) {

    case AG_FRAME_WAIT_SOF:
        if (byte == AG_SOF) {
            f->state = (uint8_t)AG_FRAME_WAIT_LEN;
        }
        /* Anything else is line noise between frames and is dropped in
         * silence. Counting it would turn an idle noisy line into a stream of
         * diagnostics that says nothing about whether frames are getting
         * through. */
        return AG_FRAME_NONE;

    case AG_FRAME_WAIT_LEN:
        if (byte < AG_LEN_MIN || byte > AG_MAX_LEN) {
            /* Not a plausible length, so the SOF that preceded it was not a
             * real frame start. Fail, then REPROCESS this byte as a candidate
             * SOF: if the false SOF was payload and a genuine frame begins
             * right here, this is what saves it. */
            bump(&f->err_format);
            f->state = (uint8_t)AG_FRAME_WAIT_SOF;
            if (byte == AG_SOF) {
                f->state = (uint8_t)AG_FRAME_WAIT_LEN;
            }
            return AG_FRAME_BAD_FORMAT;
        }
        f->want  = byte;
        f->got   = 0u;
        /* The CRC covers LEN. A corrupted length byte is otherwise undetectable
         * until it has already mis-framed whatever follows. */
        f->crc   = ag_crc16_update(AG_CRC16_INIT, byte);
        f->state = (uint8_t)AG_FRAME_BODY;
        return AG_FRAME_NONE;

    case AG_FRAME_BODY:
        f->body[f->got] = byte;
        f->got          = (uint8_t)(f->got + 1u);
        f->crc          = ag_crc16_update(f->crc, byte);
        if (f->got == f->want) {
            f->state = (uint8_t)AG_FRAME_CRC_LO;
        }
        return AG_FRAME_NONE;

    case AG_FRAME_CRC_LO:
        f->crc_rx = byte;
        f->state  = (uint8_t)AG_FRAME_CRC_HI;
        return AG_FRAME_NONE;

    case AG_FRAME_CRC_HI:
        f->crc_rx = (uint16_t)(f->crc_rx | ((uint16_t)byte << 8));
        f->state  = (uint8_t)AG_FRAME_WAIT_SOF;
        if (f->crc_rx != f->crc) {
            bump(&f->err_crc);
            return AG_FRAME_BAD_CRC;
        }
        /* Structural check, after integrity and before the accessors are let
         * near it. A frame whose OP claims a STATUS byte while LEN says the
         * body holds only SEQ and OP is malformed: ag_frame_payload_len would
         * compute want - 3 on a want of 2 and wrap to 255, handing the
         * dispatcher a length longer than the buffer. The CRC does not catch
         * this, because a sender can be internally consistent and still wrong. */
        if (has_status(f) && f->want < 3u) {
            bump(&f->err_format);
            return AG_FRAME_BAD_FORMAT;
        }
        return AG_FRAME_READY;

    default:
        /* Unreachable unless the struct was corrupted. Recover rather than
         * spin: a framer stuck in an impossible state answers nothing forever,
         * which looks exactly like a dead device. */
        ag_framer_abort(f);
        return AG_FRAME_BAD_FORMAT;
    }
}

uint8_t ag_frame_seq(const ag_framer_t *f)
{
    return f->body[0];
}

uint8_t ag_frame_op(const ag_framer_t *f)
{
    return f->body[1];
}

ag_status_t ag_frame_status(const ag_framer_t *f)
{
    return has_status(f) ? (ag_status_t)f->body[2] : AG_OK;
}

const uint8_t *ag_frame_payload(const ag_framer_t *f)
{
    return &f->body[2u + has_status(f)];
}

uint8_t ag_frame_payload_len(const ag_framer_t *f)
{
    return (uint8_t)(f->want - 2u - has_status(f));
}

/* ------------------------------------------------------------------- TX */

/* The single emitter. Everything that puts a frame on the wire goes through
 * here, so the header layout and the CRC span are written down once. */
static uint16_t emit(uint8_t *out, uint16_t cap,
                     uint8_t seq, uint8_t op,
                     const uint8_t *status, /* NULL for a request */
                     const uint8_t *payload, uint8_t plen)
{
    uint16_t crc;
    uint16_t n;
    uint8_t  len;
    uint8_t  i;

    if (plen > AG_MAX_PAYLOAD) {
        return 0u;
    }
    len = (uint8_t)(2u + (status ? 1u : 0u) + plen);
    n   = (uint16_t)(len + AG_FRAME_OVERHEAD);
    if (cap < n) {
        return 0u;
    }

    out[0] = AG_SOF;
    out[1] = len;
    out[2] = seq;
    out[3] = op;
    i = 4u;
    if (status) {
        out[i++] = *status;
    }
    for (uint8_t k = 0u; k < plen; k++) {
        out[i++] = payload[k];
    }

    crc = ag_crc16(&out[1], (uint16_t)(len + 1u));  /* LEN through end of body */
    out[i++] = (uint8_t)(crc & 0xFFu);
    out[i]   = (uint8_t)((crc >> 8) & 0xFFu);
    return n;
}

uint16_t ag_frame_encode_response(uint8_t *out, uint16_t cap,
                                  uint8_t seq, uint8_t cmd, ag_status_t status,
                                  const uint8_t *payload, uint8_t plen)
{
    uint8_t s = (uint8_t)status;
    return emit(out, cap, seq, (uint8_t)(cmd | AG_OP_RESPONSE_BIT), &s, payload, plen);
}

uint16_t ag_frame_encode_event(uint8_t *out, uint16_t cap,
                               ag_status_t status,
                               const uint8_t *payload, uint8_t plen)
{
    uint8_t s = (uint8_t)status;
    return emit(out, cap, AG_SEQ_NONE, AG_OP_EVENT, &s, payload, plen);
}

uint16_t ag_frame_encode_request(uint8_t *out, uint16_t cap,
                                 uint8_t seq, uint8_t cmd,
                                 const uint8_t *payload, uint8_t plen)
{
    return emit(out, cap, seq, cmd, 0, payload, plen);
}
