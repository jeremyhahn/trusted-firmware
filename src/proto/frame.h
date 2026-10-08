/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Byte stream <-> frame. A pure function of bytes: this layer performs no I/O,
 * touches no register and calls nothing but the CRC, which is what lets the
 * whole of it run on the host against a fuzzer.
 *
 * RX is a five-state machine fed one byte at a time, so the caller never needs
 * to buffer a partial frame itself and a frame may arrive split across any
 * number of reads.
 *
 * RESYNC. The payload is not escaped, so the SOF byte can occur inside one. The
 * framer recovers by construction rather than by scanning: an implausible LEN
 * is rejected immediately and the byte that failed is REPROCESSED as a possible
 * SOF, so a real frame starting one byte after a false SOF is not swallowed by
 * the false one. Without that reprocess a 0xA5 payload byte followed
 * immediately by a genuine frame costs two frames instead of one.
 *
 * The framer retains at most TFW_MAX_FRAME bytes and every transition is
 * bounded, so an arbitrary byte stream can waste CPU but cannot overrun,
 * allocate, or wedge the machine. test_frame_fuzz asserts exactly that.
 */
#ifndef TFW_FRAME_H
#define TFW_FRAME_H

#include <stdint.h>
#include "tfw/status.h"
#include "tfw/protocol.h"
#include "tfw/config.h"

typedef enum {
    TFW_FRAME_WAIT_SOF = 0,
    TFW_FRAME_WAIT_LEN,
    TFW_FRAME_BODY,
    TFW_FRAME_CRC_LO,
    TFW_FRAME_CRC_HI
} tfw_frame_state_t;

/* What a single pushed byte did. */
typedef enum {
    TFW_FRAME_NONE = 0,   /* absorbed; nothing complete yet */
    TFW_FRAME_READY,      /* a whole frame passed its CRC and is in .body */
    TFW_FRAME_BAD_CRC,    /* a whole frame arrived and its CRC did not match */
    TFW_FRAME_BAD_FORMAT  /* implausible LEN or an aborted partial frame */
} tfw_frame_event_t;

typedef struct {
    uint8_t  body[TFW_MAX_LEN]; /* SEQ, OP, then STATUS and payload as applicable */
    uint8_t  want;             /* body bytes the LEN field promised */
    uint8_t  got;              /* body bytes received so far */
    uint16_t crc;              /* running CRC over LEN and the body */
    uint16_t crc_rx;           /* the CRC the sender claimed */
    uint8_t  state;

    /* Diagnostics, reported by SYS_INFO (appended after reset_cause, #5 --
     * see src/caps/sys.c and sys.h for how a command handler that may not
     * touch the framer directly gets these numbers). A host that sees these
     * climbing knows the line is noisy before it starts blaming the
     * firmware's logic. */
    uint16_t err_crc;
    uint16_t err_format;
} tfw_framer_t;

void tfw_framer_init(tfw_framer_t *f);

/* Abandons any partial frame and returns to hunting for SOF. The transport
 * calls this when the line has been idle mid-frame, so a truncated frame
 * cannot hold the machine until the next byte arrives -- which might be the
 * start of a good frame that then gets eaten as someone else's tail. */
void tfw_framer_abort(tfw_framer_t *f);

tfw_frame_event_t tfw_framer_push(tfw_framer_t *f, uint8_t byte);

/* Valid only immediately after TFW_FRAME_READY.
 *
 * A request body is [SEQ][OP][payload...]; a response or event body is
 * [SEQ][OP][STATUS][payload...]. The accessors work out which from OP rather
 * than making every caller remember, because a caller that gets it wrong reads
 * the status byte as the first payload byte and the mistake is silent. A frame
 * whose OP claims a status it is too short to contain is rejected as malformed
 * at parse time, so these never index past the body. */
uint8_t        tfw_frame_seq(const tfw_framer_t *f);
uint8_t        tfw_frame_op(const tfw_framer_t *f);
tfw_status_t    tfw_frame_status(const tfw_framer_t *f); /* TFW_OK for a request */
const uint8_t *tfw_frame_payload(const tfw_framer_t *f);
uint8_t        tfw_frame_payload_len(const tfw_framer_t *f);

/* ------------------------------------------------------------------- TX */
/* Each returns the number of bytes written, or 0 if the buffer is too small or
 * the payload exceeds TFW_MAX_PAYLOAD. Zero is never a valid frame length, so
 * the return doubles as the error signal without a second out-parameter. */

uint16_t tfw_frame_encode_response(uint8_t *out, uint16_t cap,
                                  uint8_t seq, uint8_t cmd, tfw_status_t status,
                                  const uint8_t *payload, uint8_t plen);

uint16_t tfw_frame_encode_event(uint8_t *out, uint16_t cap,
                               tfw_status_t status,
                               const uint8_t *payload, uint8_t plen);

/* Used by the host SDK, the simulator and the tests. Compiled into firmware
 * too, where nothing references it and --gc-sections drops it; keeping one
 * encoder for both directions is what stops request and response framing from
 * drifting apart. */
uint16_t tfw_frame_encode_request(uint8_t *out, uint16_t cap,
                                 uint8_t seq, uint8_t cmd,
                                 const uint8_t *payload, uint8_t plen);

#endif /* TFW_FRAME_H */
