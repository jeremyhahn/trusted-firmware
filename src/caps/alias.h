/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef AG_CAPS_ALIAS_H
#define AG_CAPS_ALIAS_H

#include <stdint.h>
#include "ag/status.h"
#include "ag/crypto.h"
#include "../core/reader.h"
#include "../core/writer.h"

/* The raw uncompressed EC point, X||Y, and the raw signature, r||s. Both are
 * 64 bytes on P-256, which is why AG_SIG_LEN serves for both. */
#define AG_ALIAS_POINT_LEN AG_SIG_LEN

/*
 * The parts a certificate is made of, gathered so the builder can be called
 * with values a test chose rather than only with values a crypto provider
 * produced. That is what lets the DER vectors be pinned in a link with no
 * provider in it at all.
 */
typedef struct {
    const uint8_t *nonce;    /* AG_ALIAS_NONCE_LEN -- 32, not the frame's 16 */
    const uint8_t *ueid;     /* AG_ATTEST_ID_LEN    */
    const uint8_t *fw_hash;  /* AG_SHA256_LEN       */
    const uint8_t *pubkey;   /* AG_ALIAS_POINT_LEN, X||Y */
    const uint8_t *sig;      /* AG_SIG_LEN, raw r||s     */
} ag_alias_parts_t;

/* SHA-256 over the TBSCertificate: the bytes the DeviceID key signs. `sig` is
 * not read, because the digest is what produces it. */
ag_status_t ag_alias_tbs_digest(const ag_alias_parts_t *p,
                                uint8_t out[AG_SHA256_LEN]);

/*
 * Emits the certificate and copies the window [off, off+cap) of it into out.
 *
 * *total is always the WHOLE certificate's length, whichever window was asked
 * for, so a host learns on its first page how many it will need. *got is how
 * many bytes the window actually yielded, which is shorter than cap on the last
 * page. An offset at or past the end is AG_ERR_RANGE rather than an empty
 * success, because an empty success is indistinguishable from a page of nothing.
 */
ag_status_t ag_alias_cert(const ag_alias_parts_t *p, uint16_t off,
                          uint8_t *out, uint8_t cap,
                          uint16_t *total, uint8_t *got);

ag_status_t ag_alias_cmd(ag_reader_t *r, ag_writer_t *w);
void        ag_alias_reset_state(void);   /* test seam */

#endif /* AG_CAPS_ALIAS_H */
