/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SUIT reader's primitives, shared by the image reader (suit.c) and the
 * integrated-payload reader (suit_integrated.c), and the head under the policy
 * payload reader (hsm_payload.c). Internal to src/core: nothing outside the
 * three readers includes this.
 *
 * Every primitive reads one member at a cursor and moves the cursor past it,
 * checking each length against what remains before reading past it.
 */
#ifndef TFW_CORE_SUIT_CBOR_H
#define TFW_CORE_SUIT_CBOR_H

#include <stdint.h>

#include "suit.h"
#include "tfw/crypto.h"

#define TFW_SUIT_MT_UINT   0u
#define TFW_SUIT_MT_NINT   1u
#define TFW_SUIT_MT_BSTR   2u
#define TFW_SUIT_MT_TSTR   3u
#define TFW_SUIT_MT_ARRAY  4u
#define TFW_SUIT_MT_MAP    5u
#define TFW_SUIT_MT_TAG    6u
#define TFW_SUIT_MT_SIMPLE 7u

#define TFW_SUIT_TAG_ENVELOPE 107u
#define TFW_SUIT_ENV_AUTH     2u
#define TFW_SUIT_ENV_MANIFEST 3u
#define TFW_SUIT_MAN_VERSION  1u
#define TFW_SUIT_MAN_SEQUENCE 2u
#define TFW_SUIT_MAN_COMMON   3u

typedef struct {
    const uint8_t *p;
    uint16_t       n;
} tfw_suit_cur_t;

/* One CBOR head: major type and argument. */
tfw_status_t tfw_suit_head(tfw_suit_cur_t *c, uint8_t *major, uint32_t *val);
/* Exactly this head, or TFW_ERR_UNSUPPORTED. */
tfw_status_t tfw_suit_expect(tfw_suit_cur_t *c, uint8_t major, uint32_t val);
/* An unsigned integer that fits 32 bits. */
tfw_status_t tfw_suit_uint32(tfw_suit_cur_t *c, uint32_t *out);
/* A string of major type `major` of at most `max` bytes: `sub` spans its
 * content and `c` moves past it. */
tfw_status_t tfw_suit_string(tfw_suit_cur_t *c, uint8_t major, uint16_t max, tfw_suit_cur_t *sub);
/* TFW_OK when the container is read to its end. */
tfw_status_t tfw_suit_done(const tfw_suit_cur_t *c);

/* The authentication wrapper `auth` over the bstr-wrapped manifest `wrapped`:
 * reads the manifest digest and the one COSE_Sign1, checks the digest against
 * `wrapped`, and writes the SHA-256 of the COSE Sig_structure to `tbs_hash`
 * and the r||s signature to `sig`. Whose key verifies them is the caller's. */
tfw_status_t tfw_suit_auth(tfw_suit_cur_t auth, const tfw_suit_cur_t *wrapped,
                           uint8_t tbs_hash[TFW_SHA256_LEN], uint8_t sig[TFW_SIG_LEN]);

/* suit-common: one component and the shared sequence's four parameters. */
tfw_status_t tfw_suit_common(tfw_suit_cur_t c, tfw_suit_manifest_t *m);

#endif
