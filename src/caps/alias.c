/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The alias certificate: a DICE Evidence carrier the device mints for itself.
 *
 * ATTEST_CERT(nonce[32])  mints a fresh certificate and returns page 0
 * ATTEST_CERT(page)       returns a later page of the certificate just minted
 *
 * WHY A CERTIFICATE AT ALL. TCG DICE Attestation Architecture r23 section 6.1.4
 * puts the verifier's challenge in tcg-dice-UCCS-evidence, an extension that
 * carries no signature of its own -- "the certificate signature authenticates,
 * and integrity protects UCCS contents". So there has to be a certificate, and
 * its subject has to be this device, and the DeviceID key in the element has to
 * sign it. That is an alias certificate, which is what DICE layering calls the
 * thing an ECA key issues on behalf of the layer above it.
 *
 * WHY THE DEVICE BUILDS THE WHOLE TBS AND NOT JUST A SIGNATURE. A gateway could
 * assemble a TBSCertificate and hand the device a digest to sign, and it would
 * be less code here. It would also mean the device cannot know what it attested
 * to: a signature over 32 opaque bytes is a signature over anything the holder
 * of those bytes says it is. docs/security/threat-model.md draws the
 * containment boundary at "the gateway relays device-signed evidence verbatim
 * and never re-signs it", and that boundary is only real if the device
 * determines the content. So the TBS is built here, byte for byte, and the only
 * thing that crosses the bus into the element is a digest of bytes this file
 * produced.
 *
 * WHY A TEMPLATE. Every field of the TBS except three is the same in every
 * certificate this firmware will ever issue -- the algorithm identifiers, the
 * names, the validity, the extension's OID and every DER length around it. A
 * template is 249 bytes of flash and a table of steps for src/caps/tmpl.c to
 * walk; a general DER writer is a length stack, a two-pass size computation and
 * a class of bug where the lengths and the contents disagree. The builder is
 * already the largest thing this firmware gained; a general writer is not a
 * close call.
 *
 * THE THREE HOLES are the serial number, the subject public key and the claims
 * set. The first two sit inside the prefix; the claims set is the last thing in
 * the TBS, so it is appended rather than patched, which is why the template
 * stops at 249 bytes and carries no room for it.
 *
 * WHAT THE SPEC ASKS FOR THAT THIS CANNOT GIVE. Section 6.1.4 says the issuer
 * MUST ensure that non-constant elements of the extension contributed to the
 * CDI that generated the subject key. The ueid and the firmware measurement do:
 * they are DICE inputs, fixed before the CDI exists. THE NONCE DOES NOT, and on
 * this class of part it cannot -- the CDI is computed at boot and the challenge
 * arrives afterwards, and satisfying the letter would need a subject key
 * derived per challenge, which an ATECC608A will not do: there is no command
 * that turns an arbitrary secret into a P-256 private key inside the part, and
 * a key derived outside it is a key that left it. What binds the nonce here is
 * the certificate signature over a TBS that contains it, which is the
 * protection section 6.1.4 itself names for UCCS contents. The deviation is
 * stated in docs/dice-attestation/architecture.md rather than buried.
 */

#include <string.h>
#include "alias.h"
#include "attest.h"
#include "tmpl.h"
#include "uccs.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/progmem.h"
#include "tfw/protocol.h"
#include "../crypto/sha256.h"

/* Offsets of the two holes inside the prefix, and the widths that bound them.
 * They are literals checked by the vectors, not computed from the template --
 * a computed offset agrees with a broken template as readily as with a right
 * one. */
#define TBS_OFF_SERIAL 11u
#define TBS_OFF_SPKI  139u

/*
 * The TBSCertificate prefix: everything up to and including the OCTET STRING
 * header that the claims set fills.
 *
 * Read it against RFC 5280 section 4.1. The lengths are baked in, so a change
 * to any constant below -- a longer common name, a different validity -- moves
 * three or four of them at once, and the vectors are what catch a move that was
 * not carried all the way through.
 */
static const uint8_t TBS_PREFIX[] TFW_PROGMEM = {
    /* TBSCertificate SEQUENCE, 0x018C = 396 bytes of content */
    0x30, 0x82, 0x01, 0x8C,
    /* version [0] EXPLICIT INTEGER 2 -- v3, which extensions require */
    0xA0, 0x03, 0x02, 0x01, 0x02,
    /* serialNumber INTEGER, 8 bytes. THE HOLE: see SERIAL_LEN for why the
     * leading byte is forced into 0x40..0x7F. */
    0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* signature AlgorithmIdentifier: ecdsa-with-SHA256, 1.2.840.10045.4.3.2.
     * No parameters, per RFC 5758 section 3.2. */
    0x30, 0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02,
    /* issuer RDNSequence: CN=tfw DeviceID. A product constant: the DeviceID
     * certificate is issued off-device at provisioning, so the device knows its
     * issuer's name and not its key. */
    0x30, 0x17, 0x31, 0x15, 0x30, 0x13, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13,
    0x0C, 0x74, 0x66, 0x77, 0x20, 0x44, 0x65, 0x76, 0x69, 0x63, 0x65, 0x49,
    0x44,
    /* validity: notBefore 20200101000000Z as UTCTime, notAfter
     * 99991231235959Z as GeneralizedTime. RFC 5280 section 4.1.2.5 gives that
     * value for "no well-defined expiration", and section 4.1.2.5.1 is why the
     * two use different types: through 2049 UTCTime, from 2050 GeneralizedTime.
     * NO PART IN THIS FAMILY HAS A CLOCK, so these dates carry no information
     * and must not be read as any. Freshness is the nonce, always. */
    0x30, 0x20, 0x17, 0x0D, 0x32, 0x30, 0x30, 0x31, 0x30, 0x31, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0x5A, 0x18, 0x0F, 0x39, 0x39, 0x39, 0x39, 0x31,
    0x32, 0x33, 0x31, 0x32, 0x33, 0x35, 0x39, 0x35, 0x39, 0x5A,
    /* subject RDNSequence: CN=tfw Alias. The per-device identity is the ueid
     * claim, not a name -- a name assembled from a DeviceID would have to be
     * hex-formatted on the device for no verifier's benefit. */
    0x30, 0x14, 0x31, 0x12, 0x30, 0x10, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13,
    0x09, 0x74, 0x66, 0x77, 0x20, 0x41, 0x6C, 0x69, 0x61, 0x73,
    /* subjectPublicKeyInfo: id-ecPublicKey with prime256v1, then a BIT STRING
     * of 0 unused bits holding 0x04 and the uncompressed point. THE HOLE. */
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02,
    0x01, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07, 0x03,
    0x42, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* extensions [3] EXPLICIT SEQUENCE OF Extension, holding exactly two.
     *
     * The first is the TCG DICE Attestation Architecture r23 section 6.1.1
     * DiceTcbInfo, non-critical, carrying only flags:
     *   06 06 67 81 05 05 04 01   tcg-dice-TcbInfo, 2.23.133.5.4.1
     *   04 05                     extnValue OCTET STRING, wrapping
     *   30 03                       DiceTcbInfo ::= SEQUENCE {
     *   87 01 00                      flags [7] IMPLICIT OperationalFlags }
     * flags is a BIT STRING with no bit set: this template is the element
     * root's, which does not set notSecure (bit 1). A software root sets it --
     * 87 02 06 40 -- and SOFT_PREFIX below carries that and the six lengths it
     * lengthens; the auto provider without an element mints one. There is
     * no vendorInfo: its content is go-xkms's to define in its DICE profile
     * (owner ruling 76, CA m502), so the device writes none until it does.
     *
     * The second is the evidence:
     *   06 06 67 81 05 05 04 06   tcg-dice-UCCS-evidence, 2.23.133.5.4.6
     *   01 01 FF                  critical
     *   04 81 9D ...              extnValue OCTET STRING, wrapping
     *   30 81 9A                    UccsEvidence ::= SEQUENCE {
     *   04 81 97                      uccs OCTET STRING }  -- THE HOLE
     *
     * CRITICAL on purpose. This is not a general-purpose identity certificate
     * and must not be mistaken for one: a relying party that cannot read the
     * evidence has no business accepting the key it certifies, and a
     * non-critical extension is one a parser may silently drop. */
    0xA3, 0x81, 0xC2, 0x30, 0x81, 0xBF,
    0x30, 0x0F, 0x06, 0x06, 0x67, 0x81, 0x05, 0x05, 0x04, 0x01, 0x04, 0x05,
    0x30, 0x03, 0x87, 0x01, 0x00,
    0x30, 0x81, 0xAB, 0x06, 0x06, 0x67,
    0x81, 0x05, 0x05, 0x04, 0x06, 0x01, 0x01, 0xFF, 0x04, 0x81, 0x9D, 0x30,
    0x81, 0x9A, 0x04, 0x81, 0x97
};

#define TBS_PREFIX_LEN ((uint16_t)sizeof TBS_PREFIX)
#define TBS_LEN        ((uint16_t)(TBS_PREFIX_LEN + TFW_UCCS_LEN))

/* The claims set is the tail of the TBS, not a patch into it. If that ever
 * stopped being true the template would need room for it and this file would
 * need a third hole, so it is asserted rather than remembered. */
_Static_assert(TBS_PREFIX_LEN == 249u, "the TBS template prefix changed size");
_Static_assert(TBS_LEN == 400u, "the TBSCertificate changed size");
/* The extensions field's constant part: the [3] wrapper, the SEQUENCE OF, the
 * whole DiceTcbInfo Extension, then the evidence Extension, its OID, the
 * critical flag and the three nested length headers down to the claims set. */
#define TBS_EXT_HEADER_LEN 46u
_Static_assert(TBS_OFF_SPKI + TFW_ALIAS_POINT_LEN
                   == TBS_PREFIX_LEN - TBS_EXT_HEADER_LEN,
               "the public key hole no longer ends where the extensions begin");
/* The spans the possession CSR reads out of this template. Each ends exactly
 * where it must: the key header at the key, the Extension at the claims set. */
_Static_assert(TFW_ALIAS_TBS_OFF_SPKI_HDR + TFW_ALIAS_TBS_SPKI_HDR_LEN
                   == TBS_OFF_SPKI,
               "the SubjectPublicKeyInfo header no longer ends at the point");
_Static_assert(TFW_ALIAS_TBS_OFF_UCCS_EXT + TFW_ALIAS_TBS_UCCS_EXT_LEN
                   == TBS_PREFIX_LEN,
               "the evidence Extension no longer ends at the claims set");

/*
 * The serial number, from the nonce.
 *
 * A CA that reissues one serial with different contents has broken the one
 * thing a serial is for, so this varies per challenge. The leading byte is read
 * TFW_TMPL_POS, which clears its high bit and sets its 0x40 bit and so puts it
 * in 0x40..0x7F: DER INTEGERs are signed and minimally encoded, so a leading
 * byte of 0x00 would need stripping and one at or above 0x80 would need a zero
 * prepended. Either would make the serial's width depend on the nonce, and a
 * template cannot have a field that changes length.
 */
#define SERIAL_LEN 8u

/* The signature AlgorithmIdentifier inside the TBS, which RFC 5280 section
 * 4.1.1.2 requires Certificate.signatureAlgorithm to repeat. It is read twice
 * from the one copy, so the two cannot differ. */
#define TBS_OFF_SIG_ALG 19u
#define SIG_ALG_LEN     12u
_Static_assert(TBS_OFF_SERIAL + SERIAL_LEN == TBS_OFF_SIG_ALG,
               "the AlgorithmIdentifier no longer follows the serial");

#define S_TBS(k)  ((uint8_t)((k) | TFW_ALIAS_SRC_TBS))

/*
 * The certificate's template: the TBS, then the signatureAlgorithm that follows
 * it. The first TBS_STEPS are the TBSCertificate -- the bytes the DeviceID key
 * signs -- and the last is read only by the windowing pass.
 */
static const tfw_tmpl_step_t STEPS[] TFW_PROGMEM = {
    { S_TBS(TFW_TMPL_PGM), 0u, TBS_OFF_SERIAL },
    { TFW_TMPL_POS | TFW_ALIAS_SRC_NONCE, 0u, 1u },
    { TFW_TMPL_RAM | TFW_ALIAS_SRC_NONCE, 1u, SERIAL_LEN - 1u },
    { S_TBS(TFW_TMPL_PGM), TBS_OFF_SIG_ALG, TBS_OFF_SPKI - TBS_OFF_SIG_ALG },
    { TFW_TMPL_RAM | TFW_ALIAS_SRC_PUBKEY, 0u, TFW_ALIAS_POINT_LEN },
    { S_TBS(TFW_TMPL_PGM), TBS_OFF_SPKI + TFW_ALIAS_POINT_LEN, TBS_EXT_HEADER_LEN },
    { TFW_TMPL_RAM | TFW_ALIAS_SRC_UCCS, 0u, TFW_UCCS_LEN },
    /* Certificate.signatureAlgorithm */
    { S_TBS(TFW_TMPL_PGM), TBS_OFF_SIG_ALG, SIG_ALG_LEN }
};

#define CERT_STEPS ((uint8_t)(sizeof STEPS / sizeof STEPS[0]))
#define TBS_STEPS  ((uint8_t)(CERT_STEPS - 1u))

/*
 * A software root's certificate: SOFT_PREFIX, read as the own source, beside
 * TBS_PREFIX.
 *
 * TCG DICE Attestation Architecture r23 has a software root set notSecure, bit
 * 1 of OperationalFlags, and as a DER named-bit BIT STRING that is 87 02 06 40:
 * one byte more than the element's 87 01 00. So the DiceTcbInfo, its OCTET
 * STRING, its Extension, the SEQUENCE OF, [3] and the TBS each grow by one, and
 * the certificate with them. Those are the only bytes that differ: the TBS
 * header first, then the extensions down to the end of the DiceTcbInfo
 * Extension. Everything else is TBS_PREFIX's, so the two cannot drift apart.
 */
static const uint8_t SOFT_PREFIX[] TFW_PROGMEM = {
    0x30, 0x82, 0x01, 0x8D,
    0xA3, 0x81, 0xC3, 0x30, 0x81, 0xC0,
    0x30, 0x10, 0x06, 0x06, 0x67, 0x81, 0x05, 0x05, 0x04, 0x01, 0x04, 0x06,
    0x30, 0x04, 0x87, 0x02, 0x06, 0x40
};

#define SOFT_TBS_HDR_LEN 4u   /* 30 82 hi lo */
#define SOFT_TCB_EXT_LEN 24u  /* [3], SEQUENCE OF, the DiceTcbInfo Extension */
#define SOFT_TBS_LEN     ((uint16_t)(TBS_LEN + 1u))
_Static_assert(sizeof SOFT_PREFIX == SOFT_TBS_HDR_LEN + SOFT_TCB_EXT_LEN,
               "the software root's template changed size");
_Static_assert(SOFT_TBS_LEN == 401u, "the software root's TBS changed size");
/* The element's counterpart of SOFT_TCB_EXT_LEN is the 23 bytes between the
 * key and the evidence Extension; the software root's replaces exactly those. */
_Static_assert(TBS_OFF_SPKI + TFW_ALIAS_POINT_LEN + SOFT_TCB_EXT_LEN - 1u
                   == TFW_ALIAS_TBS_OFF_UCCS_EXT,
               "the DiceTcbInfo Extension no longer ends at the evidence");

#define S_OWN(k)  ((uint8_t)((k) | TFW_ALIAS_SRC_OWN))

static const tfw_tmpl_step_t SOFT_STEPS[] TFW_PROGMEM = {
    { S_OWN(TFW_TMPL_PGM), 0u, SOFT_TBS_HDR_LEN },
    { S_TBS(TFW_TMPL_PGM), SOFT_TBS_HDR_LEN, TBS_OFF_SERIAL - SOFT_TBS_HDR_LEN },
    { TFW_TMPL_POS | TFW_ALIAS_SRC_NONCE, 0u, 1u },
    { TFW_TMPL_RAM | TFW_ALIAS_SRC_NONCE, 1u, SERIAL_LEN - 1u },
    { S_TBS(TFW_TMPL_PGM), TBS_OFF_SIG_ALG, TBS_OFF_SPKI - TBS_OFF_SIG_ALG },
    { TFW_TMPL_RAM | TFW_ALIAS_SRC_PUBKEY, 0u, TFW_ALIAS_POINT_LEN },
    { S_OWN(TFW_TMPL_PGM), SOFT_TBS_HDR_LEN, SOFT_TCB_EXT_LEN },
    { S_TBS(TFW_TMPL_PGM), TFW_ALIAS_TBS_OFF_UCCS_EXT, TFW_ALIAS_TBS_UCCS_EXT_LEN },
    { TFW_TMPL_RAM | TFW_ALIAS_SRC_UCCS, 0u, TFW_UCCS_LEN },
    /* Certificate.signatureAlgorithm */
    { S_TBS(TFW_TMPL_PGM), TBS_OFF_SIG_ALG, SIG_ALG_LEN }
};

#define SOFT_CERT_STEPS ((uint8_t)(sizeof SOFT_STEPS / sizeof SOFT_STEPS[0]))
#define SOFT_TBS_STEPS  ((uint8_t)(SOFT_CERT_STEPS - 1u))

tfw_status_t tfw_alias_sources(const tfw_alias_parts_t *p, const uint8_t *own,
                             uint8_t uccs[TFW_UCCS_LEN],
                             const uint8_t *src[TFW_ALIAS_SRC_COUNT])
{
    tfw_writer_t uw;
    tfw_status_t st;

    tfw_writer_init(&uw, uccs, (uint8_t)TFW_UCCS_LEN);
    st = tfw_uccs_encode(&uw, p->nonce, TFW_ALIAS_NONCE_LEN, p->ueid, p->fw_hash);
    if (st != TFW_OK) {
        return st;
    }
    /* The template's length fields were computed for exactly TFW_UCCS_LEN bytes.
     * A claims set of any other length would produce a certificate whose DER
     * lengths describe a different document than the one that follows them --
     * which is the failure mode that parses somewhere and means nothing. */
    if (tfw_writer_len(&uw) != TFW_UCCS_LEN) {
        return TFW_ERR_INTERNAL;
    }
    src[TFW_ALIAS_SRC_TBS]    = TBS_PREFIX;
    src[TFW_ALIAS_SRC_OWN]    = own;
    src[TFW_ALIAS_SRC_NONCE]  = p->nonce;
    src[TFW_ALIAS_SRC_PUBKEY] = p->pubkey;
    src[TFW_ALIAS_SRC_UCCS]   = uccs;
    return TFW_OK;
}

/*
 * One half of an ECDSA signature as a DER INTEGER.
 *
 * The element hands back r and s as fixed 32-byte big-endian values; DER wants
 * them minimally encoded and signed. So leading zero bytes come off, and a zero
 * goes back on when the first remaining byte has its high bit set. The loop
 * stops at 31 rather than 32 so an all-zero value encodes as 02 01 00 -- a
 * one-byte zero -- instead of an INTEGER with no content, which no parser
 * accepts.
 */
static uint8_t int_content_len(const uint8_t *v)
{
    uint8_t i = 0u;

    while (i < 31u && v[i] == 0u) {
        i++;
    }
    return (uint8_t)((v[i] & 0x80u) ? (33u - i) : (32u - i));
}

static void put_int(tfw_emit_t *e, const uint8_t *v)
{
    uint8_t i = 0u;
    uint8_t n = int_content_len(v);

    while (i < 31u && v[i] == 0u) {
        i++;
    }
    tfw_emit_put(e, 0x02u);
    tfw_emit_put(e, n);
    if (v[i] & 0x80u) {
        tfw_emit_put(e, 0x00u);
    }
    while (i < 32u) {
        tfw_emit_put(e, v[i++]);
    }
}

/* Whether the certificate is SOFT_STEPS's rather than STEPS's. Only the
 * software root sets notSecure; every other tier leaves the flags clear. */
static uint8_t soft_root(const tfw_alias_parts_t *p)
{
    return (uint8_t)(p->tier == TFW_CRYPTO_SOFTWARE);
}

static tfw_status_t emit_cert(tfw_emit_t *e, const tfw_alias_parts_t *p)
{
    /* ECDSA-Sig-Value SEQUENCE content, then the BIT STRING that wraps it with
     * a leading count of unused bits. Both stay under 128, so both take a
     * one-byte length; only the outermost SEQUENCE needs two, and it always
     * needs two because the certificate never falls below 256 bytes. */
    uint8_t  sv   = (uint8_t)(4u + int_content_len(p->sig)
                                 + int_content_len(&p->sig[32]));
    uint8_t  bits = (uint8_t)(3u + sv);
    uint8_t  soft = soft_root(p);
    uint16_t body = (uint16_t)((soft ? SOFT_TBS_LEN : TBS_LEN)
                               + SIG_ALG_LEN + 2u + bits);
    uint8_t        uccs[TFW_UCCS_LEN];
    const uint8_t *src[TFW_ALIAS_SRC_COUNT];
    tfw_status_t   st;

    st = tfw_alias_sources(p, soft ? SOFT_PREFIX : NULL, uccs, src);
    if (st != TFW_OK) {
        return st;
    }

    tfw_emit_put(e, 0x30u);
    tfw_emit_put(e, 0x82u);
    tfw_emit_put(e, (uint8_t)(body >> 8));
    tfw_emit_put(e, (uint8_t)body);

    tfw_tmpl_emit(e, soft ? SOFT_STEPS : STEPS,
                  soft ? SOFT_CERT_STEPS : CERT_STEPS, src);

    tfw_emit_put(e, 0x03u);
    tfw_emit_put(e, bits);
    tfw_emit_put(e, 0x00u);   /* unused bits */
    tfw_emit_put(e, 0x30u);
    tfw_emit_put(e, sv);
    put_int(e, p->sig);
    put_int(e, &p->sig[32]);
    return TFW_OK;
}

tfw_status_t tfw_alias_tbs_digest(const tfw_alias_parts_t *p,
                                uint8_t out[TFW_SHA256_LEN])
{
    uint8_t        soft = soft_root(p);
    uint8_t        uccs[TFW_UCCS_LEN];
    const uint8_t *src[TFW_ALIAS_SRC_COUNT];
    tfw_status_t   st;

    st = tfw_alias_sources(p, soft ? SOFT_PREFIX : NULL, uccs, src);
    if (st != TFW_OK) {
        return st;
    }
    tfw_tmpl_digest(soft ? SOFT_STEPS : STEPS,
                    soft ? SOFT_TBS_STEPS : TBS_STEPS, src, out);
    return TFW_OK;
}

tfw_status_t tfw_alias_cert(const tfw_alias_parts_t *p, uint16_t off,
                          uint8_t *out, uint8_t cap,
                          uint16_t *total, uint8_t *got)
{
    tfw_emit_t   e;
    tfw_status_t st;

    memset(&e, 0, sizeof e);
    e.out  = out;
    e.from = off;
    e.cap  = cap;
    st = emit_cert(&e, p);
    if (st != TFW_OK) {
        return st;
    }
    /* Past the end is a refusal, not an empty page. A host that asked for a
     * page it should not have asked for has lost count, and telling it so is
     * more use than handing it nothing and letting it conclude the certificate
     * ended there. */
    if (off >= e.pos) {
        return TFW_ERR_RANGE;
    }
    *total = e.pos;
    *got   = e.got;
    return TFW_OK;
}

/* ------------------------------------------------------------ the command */

/*
 * What is kept between the mint and the pages that follow it.
 *
 * The certificate is not: it is regenerated for every page from these, which is
 * the whole reason it fits. The public key IS kept, because fetching it from
 * the element costs a wake, a GenKey and a sleep -- 115 ms of bus time that
 * would otherwise be paid eight times over for a document that cannot change
 * between pages. The measurement is kept because an application's evidence is
 * its own to compute and may differ from one call to the next; read once per
 * page, it could put one value in the signed bytes and another in a later page.
 * ATTEST_CSR reads it for the same reason (tfw_alias_measurement_for).
 */
static uint8_t nonce[TFW_ALIAS_NONCE_LEN];
static uint8_t pubkey[TFW_ALIAS_POINT_LEN];
static uint8_t measurement[TFW_SHA256_LEN];
static uint8_t sig[TFW_SIG_LEN];
static uint8_t have_cert;

void tfw_alias_reset_state(void)
{
    memset(nonce, 0, sizeof nonce);
    memset(pubkey, 0, sizeof pubkey);
    memset(measurement, 0, sizeof measurement);
    memset(sig, 0, sizeof sig);
    have_cert = 0u;
}

tfw_status_t tfw_alias_measurement_for(const uint8_t *b,
                                       uint8_t out[TFW_SHA256_LEN])
{
    /* The nonce is the verifier's, public and already on the wire, so a
     * comparison that stops early tells nobody anything they lack. */
    if (!have_cert || memcmp(nonce, b, sizeof nonce) != 0) {
        return TFW_ERR_BAD_ARGC;
    }
    memcpy(out, measurement, sizeof measurement);
    return TFW_OK;
}

static void gather(tfw_alias_parts_t *p)
{
    p->nonce   = nonce;
    p->ueid    = tfw_attest_device_id();
    p->fw_hash = measurement;
    p->pubkey  = pubkey;
    p->sig     = sig;
    p->tier    = tfw_crypto_tier();
}

static tfw_status_t mint(const uint8_t *challenge)
{
    uint8_t          digest[TFW_SHA256_LEN];
    uint8_t          alg = TFW_ALG_NONE;
    tfw_alias_parts_t p;
    tfw_status_t      st;

    /* Cleared first. A failed mint that left the previous certificate
     * reachable would serve pages of a document bound to a nonce the host has
     * already moved on from, and the failure would look like a stale read
     * rather than the refusal it is. */
    have_cert = 0u;
    memcpy(nonce, challenge, sizeof nonce);

    /* The subject key. On a tier with no asymmetric key at all this is where
     * the command stops, with TFW_ERR_UNSUPPORTED from the provider -- the
     * software tier's identity is an HMAC and there is nothing to certify. An
     * element that is present but has no key answers TFW_ERR_NOT_PROVISIONED,
     * and both travel back to the host unaltered. */
    st = tfw_crypto_pubkey(pubkey);
    if (st != TFW_OK) {
        return st;
    }

    tfw_attest_measurement(measurement);
    gather(&p);
    st = tfw_alias_tbs_digest(&p, digest);
    if (st != TFW_OK) {
        return st;
    }
    st = tfw_crypto_sign(digest, sig, &alg);
    if (st != TFW_OK) {
        return st;
    }
    /* The template names ecdsa-with-SHA256 in two places and cannot say
     * anything else. A provider that signed with something else would produce a
     * certificate that lies about its own algorithm, so the answer is a refusal
     * rather than a document a verifier would reject for the wrong reason. */
    if (alg != TFW_ALG_ECDSA_P256_SHA256) {
        return TFW_ERR_UNSUPPORTED;
    }

    have_cert = 1u;
    return TFW_OK;
}

/*
 * PAGED, exactly as ATTEST is and for the same reason: the document is far
 * larger than a frame payload, and enlarging every frame buffer would charge
 * the cost to every command on every board to serve the one command that needs
 * it. The difference is the header -- a certificate is longer than 255 bytes,
 * so the total is two bytes where the evidence frame's is one.
 *
 *   response  [page:1][total:2 little-endian][bytes:0..TFW_ALIAS_PAGE]
 *
 * A host reads total from page 0 and asks for ceil(total / TFW_ALIAS_PAGE)
 * pages. The pages are regenerated, not cached, so they are consistent only for
 * as long as no new nonce arrives -- which is the same contract ATTEST has.
 */
tfw_status_t tfw_alias_cmd(tfw_reader_t *r, tfw_writer_t *w)
{
    uint8_t          n = tfw_reader_remaining(r);
    uint8_t          buf[TFW_ALIAS_PAGE];
    uint8_t          page;
    uint8_t          got   = 0u;
    uint16_t         total = 0u;
    uint16_t         off;
    tfw_alias_parts_t p;
    tfw_status_t      st;

    if (n == TFW_ALIAS_NONCE_LEN) {
        const uint8_t *challenge = tfw_read_blob(r, TFW_ALIAS_NONCE_LEN);

        if (!challenge) {
            return TFW_ERR_BAD_ARGC;
        }
        st = mint(challenge);
        if (st != TFW_OK) {
            return st;
        }
        page = 0u;
    } else if (n == 1u) {
        page = tfw_read_u8(r);
        /* A page request with no certificate behind it would return bytes of a
         * document that was never minted. Refused instead. */
        if (!have_cert) {
            return TFW_ERR_BAD_ARGC;
        }
    } else {
        return TFW_ERR_BAD_ARGC;
    }

    /* Computed wide. A byte page times a byte page size overflows a uint8_t
     * long before the page count runs out, and the wrapped offset would land
     * inside the certificate and be served as though it had been asked for.
     * 255 * TFW_ALIAS_PAGE stays inside a uint16_t, so the bound below is the
     * only one needed and tfw_alias_cert applies it. */
    off = (uint16_t)((uint16_t)page * (uint16_t)TFW_ALIAS_PAGE);

    gather(&p);
    st = tfw_alias_cert(&p, off, buf, (uint8_t)sizeof buf, &total, &got);
    if (st != TFW_OK) {
        return st;
    }

    tfw_write_u8(w, page);
    tfw_write_u16(w, total);
    tfw_write_blob(w, buf, got);
    return TFW_OK;
}
