/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ATTEST_CSR: possession at ACME finalize -- issue #30.
 *
 * ATTEST_CSR(b[32])  returns sig[64], r||s
 *
 * WHAT IT SIGNS. A PKCS#10 CertificationRequestInfo (RFC 2986 section 4.1) the
 * device fills from its own state: version 0, an empty subject, the DeviceID
 * key's SubjectPublicKeyInfo, and one extensionRequest attribute holding two
 * extensions -- a subjectAltName naming the device by an otherName of type
 * id-on-permanentIdentifier (RFC 4043) whose identifierValue is its whole UEID
 * -- type byte and DeviceID, 34 digits -- in lowercase hex, as go-xkms#1441
 * profiles it, and the same tcg-dice-UCCS-evidence extension the alias
 * certificate carries, with b as the nonce. The host knows all four values --
 * b because it chose it, and the key, UEID and measurement from the alias
 * certificate -- so it rebuilds the same 365 bytes, wraps them with this
 * signature, and has a CSR for the ACME finalize the device itself never sees.
 * docs/dice-attestation/adr-possession-at-acme-finalize.md is the decision.
 *
 * WHY NOT A DIGEST FROM THE HOST. The same reason the alias certificate is
 * built here (see src/caps/alias.c): a signature over 32 opaque bytes is a
 * signature over anything the holder of those bytes says it is. The host
 * chooses b and nothing else. b is a fixed-width field of a fixed template,
 * inside a CBOR byte string inside an OCTET STRING, so no value of it changes
 * a length, a tag or the document's shape.
 *
 * WHY IT CANNOT BE MISTAKEN FOR THE OTHER TWO. The DeviceID key signs three
 * structures, and each is unparseable as the others: the evidence frame opens
 * 0x01, not a SEQUENCE; the alias TBS has [0] EXPLICIT version at offset 4,
 * where this one has INTEGER 0; and this one's `30 00` at offset 7 sits where a
 * TBSCertificate needs its signature AlgorithmIdentifier, whose OID is
 * mandatory. test/unit/test_csr.c pins all six directions.
 *
 * THE SUBJECT IS EMPTY, so the subjectAltName is critical: RFC 5280 section
 * 4.2.1.6 requires it whenever the subject is an empty sequence. The evidence
 * extension is critical for the reason given in alias.c.
 */

#include <string.h>
#include "csr.h"
#include "alias.h"
#include "attest.h"
#include "tmpl.h"
#include "uccs.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/progmem.h"
#include "tfw/protocol.h"

/*
 * The identifier's width, and every DER length that encloses it, derived from
 * the holes' widths -- each a content length, header excluded. They are
 * constant expressions, so flash holds the same literals it would if they were
 * written out; what changes is that a hole that grows moves every length around
 * it at once, and one that grows past a length form's range is a compile error
 * below rather than a document that misstates its own size.
 */
#define ID_HEX_LEN   (2u * TFW_UCCS_UEID_LEN)       /* identifierValue, 34 digits */
#define PERM_ID_LEN  (2u + ID_HEX_LEN)              /* PermanentIdentifier */
#define PERM_EXP_LEN (2u + PERM_ID_LEN)             /* otherName's [0] EXPLICIT */
#define OTHER_LEN    (10u + 2u + PERM_EXP_LEN)      /* otherName: type-id, value */
#define GNAMES_LEN   (2u + OTHER_LEN)               /* GeneralNames */
#define SAN_OCT_LEN  (2u + GNAMES_LEN)              /* the SAN's extnValue */
#define SAN_EXT_LEN  (5u + 3u + 2u + SAN_OCT_LEN)   /* extnID, critical, extnValue */
#define EXTS_LEN     (2u + SAN_EXT_LEN + TFW_ALIAS_TBS_UCCS_EXT_LEN + TFW_UCCS_LEN)
#define EXT_SET_LEN  (3u + EXTS_LEN)                /* the attribute's SET OF */
#define ATTR_LEN     (11u + 3u + EXT_SET_LEN)       /* extensionRequest Attribute */
#define ATTRS_LEN    (4u + ATTR_LEN)                /* attributes [0] */
#define CRI_BODY_LEN (3u + 2u + TFW_ALIAS_TBS_SPKI_HDR_LEN + TFW_ALIAS_POINT_LEN \
                      + 4u + ATTRS_LEN)

/* Each length's header form is written into the template, so each must stay in
 * its form's range: one byte below 0x80, 0x81 and one byte to 0xFF, 0x82 and two
 * beyond. Every length is larger than the ones it encloses, so bounding the
 * outermost of a form and the innermost of the next bounds them all. */
_Static_assert(SAN_EXT_LEN < 0x80u, "the SAN outgrew the short length form");
_Static_assert(EXTS_LEN >= 0x80u && EXT_SET_LEN <= 0xFFu,
               "the Extensions left the 0x81 length form");
_Static_assert(ATTR_LEN > 0xFFu && CRI_BODY_LEN <= 0xFFFFu,
               "the attribute left the 0x82 length form");

#define HI(n) ((uint8_t)((n) >> 8))
#define LO(n) ((uint8_t)((n) & 0xFFu))

/*
 * The request's own fixed bytes. Everything else it reads from the alias
 * certificate's template -- the SubjectPublicKeyInfo header and the evidence
 * Extension header -- or from the claims set, so the two documents cannot
 * describe the key, the evidence or the device differently. Read it against
 * RFC 2986 section 4.1.
 */
static const uint8_t CRI[] TFW_PROGMEM = {
    /* CertificationRequestInfo SEQUENCE */
    0x30, 0x82, HI(CRI_BODY_LEN), LO(CRI_BODY_LEN),
    /* version INTEGER 0 -- v1, the only version */
    0x02, 0x01, 0x00,
    /* subject Name: the empty RDNSequence. The device's name is the SAN. */
    0x30, 0x00,
    /* subjectPKInfo: read from the alias template, then the point. */

    /* attributes [0] IMPLICIT SET OF Attribute, holding exactly one */
    0xA0, 0x82, HI(ATTRS_LEN), LO(ATTRS_LEN),
    /* Attribute: extensionRequest, 1.2.840.113549.1.9.14 */
    0x30, 0x82, HI(ATTR_LEN), LO(ATTR_LEN),
    0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x09, 0x0E,
    /* SET OF, holding one Extensions, holding two Extension */
    0x31, 0x81, LO(EXT_SET_LEN),
    0x30, 0x81, LO(EXTS_LEN),
    /* Extension: subjectAltName, 2.5.29.17, critical because the subject is
     * empty, its extnValue an OCTET STRING holding GeneralNames */
    0x30, LO(SAN_EXT_LEN), 0x06, 0x03, 0x55, 0x1D, 0x11, 0x01, 0x01, 0xFF,
    0x04, LO(SAN_OCT_LEN), 0x30, LO(GNAMES_LEN),
    /* otherName [0]: id-on-permanentIdentifier, 1.3.6.1.5.5.7.8.3 */
    0xA0, LO(OTHER_LEN),
    0x06, 0x08, 0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x08, 0x03,
    /* [0] EXPLICIT PermanentIdentifier { identifierValue UTF8String }, no
     * assigner -- THE HOLE, the UEID in hex, then the evidence Extension */
    0xA0, LO(PERM_EXP_LEN), 0x30, LO(PERM_ID_LEN), 0x0C, LO(ID_HEX_LEN)
};

#define CRI_OFF_ATTRS 9u
#define CRI_HEAD_LEN  CRI_OFF_ATTRS
#define CRI_ATTRS_LEN 57u
_Static_assert(sizeof CRI == CRI_HEAD_LEN + CRI_ATTRS_LEN,
               "the CertificationRequestInfo template changed size");

#define S_OWN(k)  ((uint8_t)((k) | TFW_ALIAS_SRC_OWN))
#define S_TBS(k)  ((uint8_t)((k) | TFW_ALIAS_SRC_TBS))

static const tfw_tmpl_step_t STEPS[] TFW_PROGMEM = {
    { S_OWN(TFW_TMPL_PGM), 0u, CRI_HEAD_LEN },
    { S_TBS(TFW_TMPL_PGM), TFW_ALIAS_TBS_OFF_SPKI_HDR, TFW_ALIAS_TBS_SPKI_HDR_LEN },
    { TFW_TMPL_RAM | TFW_ALIAS_SRC_PUBKEY, 0u, TFW_ALIAS_POINT_LEN },
    { S_OWN(TFW_TMPL_PGM), CRI_OFF_ATTRS, CRI_ATTRS_LEN },
    /* The UEID as the claims set carries it, type byte and all: go-xkms#1441
     * names a DICE device by the hex of its whole UEID, which is what
     * device-attest-01 recorded from the alias certificate. */
    { TFW_TMPL_HEX | TFW_ALIAS_SRC_UCCS, TFW_UCCS_OFF_UEID, TFW_UCCS_UEID_LEN },
    { S_TBS(TFW_TMPL_PGM), TFW_ALIAS_TBS_OFF_UCCS_EXT, TFW_ALIAS_TBS_UCCS_EXT_LEN },
    { TFW_TMPL_RAM | TFW_ALIAS_SRC_UCCS, 0u, TFW_UCCS_LEN }
};

#define CRI_STEPS ((uint8_t)(sizeof STEPS / sizeof STEPS[0]))

/* The widths add up to the document, and the document is its body and the
 * body's four-byte header. Both are against TFW_CSR_CRI_LEN, a literal the
 * vectors check as well. */
_Static_assert(CRI_HEAD_LEN + TFW_ALIAS_TBS_SPKI_HDR_LEN + TFW_ALIAS_POINT_LEN
                   + CRI_ATTRS_LEN + ID_HEX_LEN
                   + TFW_ALIAS_TBS_UCCS_EXT_LEN + TFW_UCCS_LEN
                   == TFW_CSR_CRI_LEN,
               "the CertificationRequestInfo is no longer 365 bytes");
_Static_assert(4u + CRI_BODY_LEN == TFW_CSR_CRI_LEN,
               "the CertificationRequestInfo's outer length is stale");

tfw_status_t tfw_csr_cri_digest(const tfw_alias_parts_t *p,
                              uint8_t out[TFW_SHA256_LEN])
{
    uint8_t        uccs[TFW_UCCS_LEN];
    const uint8_t *src[TFW_ALIAS_SRC_COUNT];
    tfw_status_t   st;

    st = tfw_alias_sources(p, CRI, uccs, src);
    if (st != TFW_OK) {
        return st;
    }
    tfw_tmpl_digest(STEPS, CRI_STEPS, src, out);
    return TFW_OK;
}

tfw_status_t tfw_csr_cri(const tfw_alias_parts_t *p, uint16_t off,
                       uint8_t *out, uint8_t cap,
                       uint16_t *total, uint8_t *got)
{
    uint8_t        uccs[TFW_UCCS_LEN];
    const uint8_t *src[TFW_ALIAS_SRC_COUNT];
    tfw_emit_t     e;
    tfw_status_t   st;

    st = tfw_alias_sources(p, CRI, uccs, src);
    if (st != TFW_OK) {
        return st;
    }
    memset(&e, 0, sizeof e);
    e.out  = out;
    e.from = off;
    e.cap  = cap;
    tfw_tmpl_emit(&e, STEPS, CRI_STEPS, src);
    if (off >= e.pos) {
        return TFW_ERR_RANGE;
    }
    *total = e.pos;
    *got   = e.got;
    return TFW_OK;
}

/*
 * One exchange, no pages and no state: the response is the signature alone,
 * which fits a frame, and nothing is kept for a later command to read. The key
 * buffer is the signature buffer too -- the point is consumed by the digest
 * before the element writes r||s over it -- so the frame is no deeper than the
 * alias certificate's mint.
 */
tfw_status_t tfw_csr_cmd(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t          digest[TFW_SHA256_LEN];
    uint8_t          key[TFW_ALIAS_POINT_LEN];
    uint8_t          alg = TFW_ALG_NONE;
    const uint8_t   *b;
    tfw_alias_parts_t p;
    tfw_status_t      st;

    if (tfw_reader_remaining(r) != TFW_ALIAS_NONCE_LEN) {
        return TFW_ERR_BAD_ARGC;
    }
    b = tfw_read_blob(r, TFW_ALIAS_NONCE_LEN);
    if (!b) {
        return TFW_ERR_BAD_ARGC;
    }

    /* The subject key, refused exactly as ATTEST_CERT's mint refuses it: no
     * asymmetric key on this tier, or an element with none provisioned. */
    st = tfw_crypto_pubkey(key);
    if (st != TFW_OK) {
        return st;
    }

    p.nonce   = b;
    p.ueid    = tfw_attest_device_id();
    p.fw_hash = tfw_attest_fw_hash();
    p.pubkey  = key;
    p.sig     = NULL;
    st = tfw_csr_cri_digest(&p, digest);
    if (st != TFW_OK) {
        return st;
    }
    st = tfw_crypto_sign(digest, key, &alg);
    if (st != TFW_OK) {
        return st;
    }
    /* The host wraps this in a CertificationRequest naming ecdsa-with-SHA256,
     * and a signature under anything else would make that a lie. */
    if (alg != TFW_ALG_ECDSA_P256_SHA256) {
        return TFW_ERR_UNSUPPORTED;
    }

    tfw_write_blob(w, key, TFW_SIG_LEN);
    return TFW_OK;
}
