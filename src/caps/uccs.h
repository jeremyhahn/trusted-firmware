/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef AG_CAPS_UCCS_H
#define AG_CAPS_UCCS_H

#include <stdint.h>
#include "ag/status.h"
#include "../core/writer.h"

/*
 * The exact size of the claims set this encoder emits, in bytes.
 *
 * It is a CONSTANT, not a bound, and that is what makes the certificate
 * template possible: every claim carries a fixed-width value, so the DER
 * length fields around the extension can be baked into flash instead of
 * computed. A change here moves the template with it, and both are pinned by
 * vectors so neither can move quietly.
 */
#define AG_UCCS_LEN 152u

/* The evidence CoSWID nested inside the measurements claim. Its length is a
 * field of the claims set -- the byte string that carries it is headed with
 * this number -- so it has to be known before the CoSWID is emitted, which is
 * why it is a constant here rather than something the encoder discovers. */
#define AG_UCCS_COSWID_LEN 85u

/*
 * Encodes the claims set into w.
 *
 * nonce_len is taken rather than assumed so a caller cannot hand over a short
 * buffer and get a claims set that reads past it. RFC 9711 permits 8..64 bytes;
 * this encoder accepts exactly AG_ALIAS_NONCE_LEN, because a variable-width
 * nonce is a variable-width claims set and the template has no room to move.
 *
 * AG_ALIAS_NONCE_LEN is 32 and is NOT the evidence frame's 16-byte nonce. See
 * the note beside it in include/ag/protocol.h: one is enrolment, the other is
 * runtime freshness, and merging them would either truncate an ACME challenge
 * or widen a frame every deployed host parses at a fixed offset.
 *
 * Returns AG_ERR_RANGE for a nonce of any other length and AG_ERR_TOO_LONG if
 * the writer could not take all AG_UCCS_LEN bytes. Nothing partial is ever
 * reported as success: the writer's overflow is sticky and is asked once, at
 * the end.
 */
ag_status_t ag_uccs_encode(ag_writer_t *w,
                           const uint8_t *nonce, uint8_t nonce_len,
                           const uint8_t *ueid,
                           const uint8_t *fw_hash);

#endif /* AG_CAPS_UCCS_H */
