/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The build's SUIT manifest emitter (issue #78): the encoder half of
 * src/core/suit.h's profile. It emits the manifest and its to-be-signed bytes,
 * the COSE Sig_structure, for go-xkms to sign, and assembles the envelope from
 * the manifest and the signature go-xkms returns. NO SIGNING CODE: a key never
 * reaches this tree (ADR decision 10).
 *
 * A host tool, built per board, so the vendor and class ids come from the same
 * include/tfw/config.h and board_profile.h the device's gate compares against
 * -- there is no second copy of either to drift.
 *
 * Each function returns the bytes written, or 0 when `cap` is too small.
 */
#ifndef TFW_TOOLS_SUIT_EMIT_H
#define TFW_TOOLS_SUIT_EMIT_H

#include <stddef.h>
#include <stdint.h>

/* The bstr-wrapped manifest: the bytes the manifest digest covers and the
 * bytes that travel in the envelope's member 3. */
size_t tfw_suit_emit_manifest(uint8_t *out, size_t cap,
                              const uint8_t image_digest[32], uint32_t image_size,
                              uint32_t seq, const uint8_t vendor[16],
                              const uint8_t class_id[16]);

/* ["Signature1", h'A10126', h'', bstr .cbor [-16, SHA-256(manifest)]]: what
 * go-xkms signs with the board id's ES256 key. */
size_t tfw_suit_emit_tbs(uint8_t *out, size_t cap,
                         const uint8_t *manifest, size_t manifest_len);

/* 107({2: auth, 3: manifest}) around a 64-byte r||s signature over the TBS. */
size_t tfw_suit_emit_envelope(uint8_t *out, size_t cap,
                              const uint8_t *manifest, size_t manifest_len,
                              const uint8_t sig[64]);

#endif
