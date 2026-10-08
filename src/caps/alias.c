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
 * template is 232 bytes of flash and three range tests; a general DER writer is
 * a length stack, a two-pass size computation and a class of bug where the
 * lengths and the contents disagree. The builder is already the largest thing
 * this firmware gained; a general writer is not a close call.
 *
 * THE THREE HOLES are the serial number, the subject public key and the claims
 * set. The first two sit inside the prefix; the claims set is the last thing in
 * the TBS, so it is appended rather than patched, which is why the template
 * stops at 232 bytes and carries no room for it.
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
    /* TBSCertificate SEQUENCE, 0x017B = 379 bytes of content */
    0x30, 0x82, 0x01, 0x7B,
    /* version [0] EXPLICIT INTEGER 2 -- v3, which extensions require */
    0xA0, 0x03, 0x02, 0x01, 0x02,
    /* serialNumber INTEGER, 8 bytes. THE HOLE: see serial_byte for why the
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
    /* extensions [3] EXPLICIT SEQUENCE OF Extension, holding exactly one:
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
    0xA3, 0x81, 0xB1, 0x30, 0x81, 0xAE, 0x30, 0x81, 0xAB, 0x06, 0x06, 0x67,
    0x81, 0x05, 0x05, 0x04, 0x06, 0x01, 0x01, 0xFF, 0x04, 0x81, 0x9D, 0x30,
    0x81, 0x9A, 0x04, 0x81, 0x97
};

/* The same AlgorithmIdentifier again, in Certificate.signatureAlgorithm. RFC
 * 5280 section 4.1.1.2 requires the two to match; they are the same bytes and
 * the template's copy is not reachable from here, so this is the second. */
static const uint8_t SIG_ALG[] TFW_PROGMEM = {
    0x30, 0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02
};

#define TBS_PREFIX_LEN ((uint16_t)sizeof TBS_PREFIX)
#define TBS_LEN        ((uint16_t)(TBS_PREFIX_LEN + TFW_UCCS_LEN))

/* The claims set is the tail of the TBS, not a patch into it. If that ever
 * stopped being true the template would need room for it and this file would
 * need a third hole, so it is asserted rather than remembered. */
_Static_assert(TBS_PREFIX_LEN == 232u, "the TBS template prefix changed size");
_Static_assert(TBS_LEN == 383u, "the TBSCertificate changed size");
/* The extensions field's constant part: the [3] wrapper, the SEQUENCE OF, the
 * Extension, its OID, the critical flag and the three nested length headers
 * down to the claims set. */
#define TBS_EXT_HEADER_LEN 29u
_Static_assert(TBS_OFF_SPKI + TFW_ALIAS_POINT_LEN
                   == TBS_PREFIX_LEN - TBS_EXT_HEADER_LEN,
               "the public key hole no longer ends where the extensions begin");

/*
 * The emit sink: one pass hashes, the other windows, and the certificate is
 * described once for both.
 *
 * A certificate is 471 to 473 bytes and an element-backed 328P image has 239
 * bytes of its RAM budget left, so it is never held whole. The bytes are
 * generated afresh for every window -- once to be signed, once per page to be
 * sent -- which costs time on a device that is idle between frames anyway, and
 * costs no RAM at all.
 */
typedef struct {
    tfw_sha256_t *hash;   /* the signing pass */
    uint8_t     *out;    /* the windowing pass */
    uint16_t     from;   /* first offset the window wants */
    uint8_t      cap;    /* how many bytes it can take */
    uint16_t     pos;    /* offsets emitted so far; the total, at the end */
    uint8_t      got;    /* bytes placed in the window */
} emit_t;

static void put(emit_t *e, uint8_t b)
{
    if (e->hash) {
        tfw_sha256_update(e->hash, &b, 1u);
    }
    if (e->out && e->pos >= e->from && e->got < e->cap) {
        e->out[e->got++] = b;
    }
    e->pos++;
}

static void put_pgm(emit_t *e, const uint8_t *p, uint16_t n)
{
    uint16_t i;
    for (i = 0u; i < n; i++) {
        put(e, tfw_pgm_u8(&p[i]));
    }
}

/*
 * The serial number, from the nonce.
 *
 * A CA that reissues one serial with different contents has broken the one
 * thing a serial is for, so this varies per challenge. The high bit is cleared
 * and the 0x40 bit set, which puts the leading byte in 0x40..0x7F: DER INTEGERs
 * are signed and minimally encoded, so a leading byte of 0x00 would need
 * stripping and one at or above 0x80 would need a zero prepended. Either would
 * make the serial's width depend on the nonce, and a template cannot have a
 * field that changes length.
 */
static uint8_t serial_byte(const uint8_t *nonce, uint8_t i)
{
    return i == 0u ? (uint8_t)((nonce[0] | 0x40u) & 0x7Fu) : nonce[i];
}

#define SERIAL_LEN 8u

static tfw_status_t emit_tbs(emit_t *e, const tfw_alias_parts_t *p)
{
    uint8_t     uccs[TFW_UCCS_LEN];
    tfw_writer_t uw;
    tfw_status_t st;
    uint16_t    i;

    tfw_writer_init(&uw, uccs, (uint8_t)sizeof uccs);
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

    for (i = 0u; i < TBS_PREFIX_LEN; i++) {
        uint8_t b = tfw_pgm_u8(&TBS_PREFIX[i]);
        if (i >= TBS_OFF_SERIAL && i < TBS_OFF_SERIAL + SERIAL_LEN) {
            b = serial_byte(p->nonce, (uint8_t)(i - TBS_OFF_SERIAL));
        } else if (i >= TBS_OFF_SPKI && i < TBS_OFF_SPKI + TFW_ALIAS_POINT_LEN) {
            b = p->pubkey[i - TBS_OFF_SPKI];
        }
        put(e, b);
    }
    for (i = 0u; i < TFW_UCCS_LEN; i++) {
        put(e, uccs[i]);
    }
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

static void put_int(emit_t *e, const uint8_t *v)
{
    uint8_t i = 0u;
    uint8_t n = int_content_len(v);

    while (i < 31u && v[i] == 0u) {
        i++;
    }
    put(e, 0x02u);
    put(e, n);
    if (v[i] & 0x80u) {
        put(e, 0x00u);
    }
    while (i < 32u) {
        put(e, v[i++]);
    }
}

static tfw_status_t emit_cert(emit_t *e, const tfw_alias_parts_t *p)
{
    /* ECDSA-Sig-Value SEQUENCE content, then the BIT STRING that wraps it with
     * a leading count of unused bits. Both stay under 128, so both take a
     * one-byte length; only the outermost SEQUENCE needs two, and it always
     * needs two because the certificate never falls below 256 bytes. */
    uint8_t  sv   = (uint8_t)(4u + int_content_len(p->sig)
                                 + int_content_len(&p->sig[32]));
    uint8_t  bits = (uint8_t)(3u + sv);
    uint16_t body = (uint16_t)(TBS_LEN + (uint16_t)sizeof SIG_ALG + 2u + bits);
    tfw_status_t st;

    put(e, 0x30u);
    put(e, 0x82u);
    put(e, (uint8_t)(body >> 8));
    put(e, (uint8_t)body);

    st = emit_tbs(e, p);
    if (st != TFW_OK) {
        return st;
    }

    put_pgm(e, SIG_ALG, (uint16_t)sizeof SIG_ALG);

    put(e, 0x03u);
    put(e, bits);
    put(e, 0x00u);            /* unused bits */
    put(e, 0x30u);
    put(e, sv);
    put_int(e, p->sig);
    put_int(e, &p->sig[32]);
    return TFW_OK;
}

tfw_status_t tfw_alias_tbs_digest(const tfw_alias_parts_t *p,
                                uint8_t out[TFW_SHA256_LEN])
{
    tfw_sha256_t c;
    emit_t      e;
    tfw_status_t st;

    memset(&e, 0, sizeof e);
    tfw_sha256_init(&c);
    e.hash = &c;
    st = emit_tbs(&e, p);
    if (st != TFW_OK) {
        return st;
    }
    tfw_sha256_final(&c, out);
    return TFW_OK;
}

tfw_status_t tfw_alias_cert(const tfw_alias_parts_t *p, uint16_t off,
                          uint8_t *out, uint8_t cap,
                          uint16_t *total, uint8_t *got)
{
    emit_t      e;
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
 * between pages.
 */
static uint8_t nonce[TFW_ALIAS_NONCE_LEN];
static uint8_t pubkey[TFW_ALIAS_POINT_LEN];
static uint8_t sig[TFW_SIG_LEN];
static uint8_t have_cert;

void tfw_alias_reset_state(void)
{
    memset(nonce, 0, sizeof nonce);
    memset(pubkey, 0, sizeof pubkey);
    memset(sig, 0, sizeof sig);
    have_cert = 0u;
}

static void gather(tfw_alias_parts_t *p)
{
    p->nonce   = nonce;
    p->ueid    = tfw_attest_device_id();
    p->fw_hash = tfw_attest_fw_hash();
    p->pubkey  = pubkey;
    p->sig     = sig;
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
