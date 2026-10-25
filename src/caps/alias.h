/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
#ifndef TFW_CAPS_ALIAS_H
#define TFW_CAPS_ALIAS_H

#include <stdint.h>
#include "tfw/status.h"
#include "tfw/crypto.h"
#include "uccs.h"
#include "../core/reader.h"
#include "../core/writer.h"

/* The raw uncompressed EC point, X||Y, and the raw signature, r||s. Both are
 * 64 bytes on P-256, which is why TFW_SIG_LEN serves for both. */
#define TFW_ALIAS_POINT_LEN TFW_SIG_LEN

/*
 * The parts a certificate is made of, gathered so the builder can be called
 * with values a test chose rather than only with values a crypto provider
 * produced. That is what lets the DER vectors be pinned in a link with no
 * provider in it at all.
 */
typedef struct {
    const uint8_t *nonce;    /* TFW_ALIAS_NONCE_LEN -- 32, not the frame's 16 */
    const uint8_t *ueid;     /* TFW_ATTEST_ID_LEN    */
    const uint8_t *fw_hash;  /* TFW_SHA256_LEN, 273's value: tfw_attest_measurement */
    const uint8_t *pubkey;   /* TFW_ALIAS_POINT_LEN, X||Y */
    const uint8_t *sig;      /* TFW_SIG_LEN, raw r||s     */
    /* The root the subject key and the signature come from: tfw_crypto_tier()
     * at the mint. TFW_CRYPTO_SOFTWARE sets notSecure in the DiceTcbInfo flags;
     * every other tier leaves them clear. */
    tfw_crypto_tier_t tier;
} tfw_alias_parts_t;

/*
 * The sources every template in this family reads, by index -- see
 * src/caps/tmpl.h. The slots are shared so that one function fills them for
 * both documents: the alias TBS and the possession CSR (src/caps/csr.c) carry
 * the same key, the same claims set and the same fixed headers, and a value
 * gathered twice is a value that can be gathered two different ways.
 */
#define TFW_ALIAS_SRC_TBS    0u  /* the TBS template, flash */
#define TFW_ALIAS_SRC_OWN    1u  /* another document's own template, or the
                                  * software root's, flash */
#define TFW_ALIAS_SRC_NONCE  2u
#define TFW_ALIAS_SRC_PUBKEY 3u
#define TFW_ALIAS_SRC_UCCS   4u  /* the encoded claims set, the UEID inside it */
#define TFW_ALIAS_SRC_COUNT  5u

/* Spans of the TBS template another document reads verbatim. Literals, like the
 * holes beside them in alias.c, and asserted there against the template. */
#define TFW_ALIAS_TBS_OFF_SPKI_HDR 112u  /* SubjectPublicKeyInfo up to the point */
#define TFW_ALIAS_TBS_SPKI_HDR_LEN  27u
#define TFW_ALIAS_TBS_OFF_UCCS_EXT 226u  /* the evidence Extension up to the claims */
#define TFW_ALIAS_TBS_UCCS_EXT_LEN  23u

/*
 * Encodes the claims set into uccs and fills src. `own` is the caller's own
 * flash template, or NULL for a document that has none.
 *
 * The claims set is exactly TFW_UCCS_LEN bytes or this refuses with
 * TFW_ERR_INTERNAL: every template's length fields were computed for that width,
 * and a claims set of any other length would produce a document whose DER
 * lengths describe a different document than the one that follows them -- the
 * failure mode that parses somewhere and means nothing.
 */
tfw_status_t tfw_alias_sources(const tfw_alias_parts_t *p, const uint8_t *own,
                             uint8_t uccs[TFW_UCCS_LEN],
                             const uint8_t *src[TFW_ALIAS_SRC_COUNT]);

/* SHA-256 over the TBSCertificate: the bytes the DeviceID key signs. `sig` is
 * not read, because the digest is what produces it. */
tfw_status_t tfw_alias_tbs_digest(const tfw_alias_parts_t *p,
                                uint8_t out[TFW_SHA256_LEN]);

/*
 * Emits the certificate and copies the window [off, off+cap) of it into out.
 *
 * *total is always the WHOLE certificate's length, whichever window was asked
 * for, so a host learns on its first page how many it will need. *got is how
 * many bytes the window actually yielded, which is shorter than cap on the last
 * page. An offset at or past the end is TFW_ERR_RANGE rather than an empty
 * success, because an empty success is indistinguishable from a page of nothing.
 */
tfw_status_t tfw_alias_cert(const tfw_alias_parts_t *p, uint16_t off,
                          uint8_t *out, uint8_t cap,
                          uint16_t *total, uint8_t *got);

tfw_status_t tfw_alias_cmd(tfw_reader_t *r, tfw_writer_t *w);
void        tfw_alias_reset_state(void);   /* test seam */

/*
 * The 32 bytes of claim 273 in the certificate last minted, if it was minted
 * for the 32-byte nonce b. ATTEST_CSR signs these and never asks the application again: the
 * request go-xkms rebuilds is built from that certificate, so it must carry the
 * value the certificate carries, even when the application's evidence has
 * changed since. No certificate, a failed mint, a reset or a later mint for
 * another nonce is TFW_ERR_BAD_ARGC, as a page request with no certificate
 * behind it is.
 */
tfw_status_t tfw_alias_measurement_for(const uint8_t *b,
                                       uint8_t out[TFW_SHA256_LEN]);

#endif /* TFW_CAPS_ALIAS_H */
