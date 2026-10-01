/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The alias certificate: the document that carries tcg-dice-UCCS-evidence.
 *
 * Two halves. The first pins the DER against vectors that OpenSSL and Python
 * `cryptography` both accepted before they were frozen, so a pass means the
 * firmware emits a certificate a verifier can parse rather than one this
 * codebase agrees with itself about. The second drives the command against the
 * software element, because the certificate is only real once an element has
 * signed it -- and the element is the one part of the path a pure vector cannot
 * exercise.
 *
 * THE STRUCTURAL CHECK IS NOT DECORATION. der_exact below descends into every
 * constructed type, and the test runs it over the extension's own value as
 * well as over the certificate. That is deliberate: a general X.509 parser
 * treats an extension it does not know as opaque bytes, so a certificate whose
 * evidence is internally inconsistent parses cleanly everywhere and means
 * nothing -- and AGV_ALIAS_CERT_BAD_INNER is exactly that certificate. It is
 * the failure this project has to fear, so it has a vector and a test.
 */

#include <string.h>
#include "unity.h"
#include "alias.h"
#include "attest.h"
#include "uccs.h"
#include "uccs_vectors.h"
#include "atecc608a_model.h"
#include "ag/config.h"
#include "ag/crypto.h"
#include "ag/protocol.h"
#include "ag/types.h"
#include "ag/hal.h"
#include "ag/boot.h"
#include "boot_platform.h"
#include "boot_measure.h"
#include "handoff.h"
#include "platform_id.h"

/* attest.c reports uptime in the evidence frame. Nothing here reads it, but
 * the translation unit is linked for its identity accessors and the HAL under
 * this binary is the element model, which has no tick. */
ag_ms_t ag_hal_millis(void) { return 0u; }

/*
 * Both identity sources src/core/platform_id.c and src/core/handoff.c read,
 * for attest.c's device-id/FWHash accessors -- the element model above is
 * this binary's only OTHER HAL need. A test-settable backing store, zeroed by
 * default: an unprovisioned platform block and no handoff are both the
 * zero-rather-than-fabricated case, so a test that never calls the setters
 * below gets that answer, not a shortcut.
 */
static uint8_t test_eeprom[AG_BOOT_EE_LEN];
static uint8_t test_handoff[AG_BOOT_HANDOFF_LEN];

ag_status_t ag_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if ((uint16_t)(addr + len) > sizeof test_eeprom) { return AG_ERR_RANGE; }
    memcpy(buf, &test_eeprom[addr], len);
    return AG_OK;
}

uint8_t *ag_hal_handoff_block(void)
{
    return test_handoff;
}

static const uint8_t NONCE[]    = AGV_UCCS_NONCE;
static const uint8_t UEID[]     = AGV_UCCS_UEID;
static const uint8_t FWHASH[]   = AGV_UCCS_FWHASH;
static const uint8_t PUBKEY[]   = AGV_ALIAS_PUBKEY;
static const uint8_t SIG[]      = AGV_ALIAS_SIG;
static const uint8_t SIG_PAD[]  = AGV_ALIAS_SIG_PAD;
static const uint8_t CERT[]     = AGV_ALIAS_CERT;
static const uint8_t CERT_PAD[] = AGV_ALIAS_CERT_PAD;
static const uint8_t BAD_OUT[]  = AGV_ALIAS_CERT_BAD_OUTER;
static const uint8_t BAD_IN[]   = AGV_ALIAS_CERT_BAD_INNER;
static const uint8_t TBSDIG[]   = AGV_ALIAS_TBS_DIGEST;

#define CERT_MAX 512u

void setUp(void)
{
    ag_model_reset();
    ag_attest_reset_state();
    ag_alias_reset_state();
    /* Zeroed and re-loaded on every test: a test that populates test_eeprom/
     * test_handoff must not leak a non-zero DeviceID/FWHash into whichever
     * test Unity runs next, which otherwise expects the vector-pinned
     * identity every other test in this file assumes. */
    memset(test_eeprom, 0, sizeof test_eeprom);
    memset(test_handoff, 0, sizeof test_handoff);
    ag_platform_id_init();
    ag_boot_handoff_init();
    ag_crypto_init();
}

void tearDown(void) {}

static void provision(void)
{
    ag_model_provision();
    ag_crypto_init();
}

static ag_alias_parts_t pinned(void)
{
    ag_alias_parts_t p;
    p.nonce   = NONCE;
    p.ueid    = UEID;
    p.fw_hash = FWHASH;
    p.pubkey  = PUBKEY;
    p.sig     = SIG;
    return p;
}

/* Assembles the whole certificate out of AG_ALIAS_PAGE-sized windows, the way
 * a host does. Returns its length. */
static uint16_t collect(const ag_alias_parts_t *p, uint8_t *dst)
{
    uint16_t total = 0u, off = 0u;
    uint8_t  got   = 0u;

    do {
        TEST_ASSERT_EQUAL(AG_OK, ag_alias_cert(p, off, &dst[off],
                                               AG_ALIAS_PAGE, &total, &got));
        TEST_ASSERT_TRUE(got > 0u);
        off = (uint16_t)(off + got);
    } while (off < total);
    TEST_ASSERT_EQUAL_UINT16(total, off);
    return total;
}

/* ------------------------------------------------------- the DER walker */

/*
 * One DER TLV, returning its total encoded length or 0 if these bytes are not
 * one. Rejects the high-tag-number form, the indefinite length, and any long
 * form that could have been written shorter -- a non-minimal length is still
 * BER, and BER accepted where DER was promised is how two implementations come
 * to hash different bytes.
 */
static uint16_t der_tlv(const uint8_t *p, uint16_t n, uint8_t depth)
{
    uint16_t hdr = 2u;
    uint16_t len;

    if (depth > 8u || n < 2u) {
        return 0u;
    }
    if ((p[0] & 0x1Fu) == 0x1Fu) {
        return 0u;
    }
    if (p[1] < 0x80u) {
        len = p[1];
    } else if (p[1] == 0x81u) {
        if (n < 3u || p[2] < 0x80u) {
            return 0u;
        }
        len = p[2];
        hdr = 3u;
    } else if (p[1] == 0x82u) {
        if (n < 4u || p[2] == 0u) {
            return 0u;
        }
        len = (uint16_t)(((uint16_t)p[2] << 8) | p[3]);
        hdr = 4u;
    } else {
        return 0u;
    }
    if ((uint32_t)hdr + (uint32_t)len > (uint32_t)n) {
        return 0u;
    }
    if (p[0] & 0x20u) {
        uint16_t off = 0u;
        while (off < len) {
            uint16_t k = der_tlv(&p[hdr + off], (uint16_t)(len - off),
                                 (uint8_t)(depth + 1u));
            if (k == 0u) {
                return 0u;
            }
            off = (uint16_t)(off + k);
        }
    }
    return (uint16_t)(hdr + len);
}

static uint8_t der_exact(const uint8_t *p, uint16_t n)
{
    return (uint8_t)(der_tlv(p, n, 0u) == n);
}

/*
 * The extension value's content: the UccsEvidence SEQUENCE, found by looking
 * for the encoded OID rather than by a hardcoded offset, so the helper survives
 * a template whose earlier fields move.
 */
static uint8_t uccs_evidence(const uint8_t *cert, uint16_t n,
                             const uint8_t **out, uint16_t *outlen)
{
    static const uint8_t OID[] = { 0x06u, 0x06u, 0x67u, 0x81u,
                                   0x05u, 0x05u, 0x04u, 0x06u };
    uint16_t i;

    for (i = 0u; (uint16_t)(i + sizeof OID) <= n; i++) {
        const uint8_t *q;
        uint16_t       rest;

        if (memcmp(&cert[i], OID, sizeof OID) != 0) {
            continue;
        }
        q    = &cert[i + sizeof OID];
        rest = (uint16_t)(n - i - sizeof OID);
        if (rest > 3u && q[0] == 0x01u && q[1] == 0x01u) {
            q    += 3;                 /* critical BOOLEAN */
            rest  = (uint16_t)(rest - 3u);
        }
        if (rest < 3u || q[0] != 0x04u || q[1] != 0x81u) {
            return 0u;
        }
        *out    = &q[3];
        *outlen = q[2];
        return (uint8_t)((uint16_t)(3u + q[2]) <= rest);
    }
    return 0u;
}

/* ------------------------------------------------------------ the vectors */

/* THE CONTRACT. Every byte of a certificate OpenSSL parsed and whose signature
 * verified over the TBS that parser extracted. */
static void test_the_certificate_is_the_bytes_the_vector_pins(void)
{
    ag_alias_parts_t p = pinned();
    uint8_t          got[CERT_MAX];

    TEST_ASSERT_EQUAL_UINT16(AGV_ALIAS_CERT_LEN, collect(&p, got));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CERT, got, AGV_ALIAS_CERT_LEN);
}

/* The digest the DeviceID key signs. It is never on the wire, so nothing else
 * would notice a TBS that moved by one byte -- the certificate would still
 * parse and the signature would still be over something. */
static void test_the_tbs_digest_is_the_value_the_vector_pins(void)
{
    ag_alias_parts_t p = pinned();
    uint8_t          d[AG_SHA256_LEN];

    TEST_ASSERT_EQUAL(AG_OK, ag_alias_tbs_digest(&p, d));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TBSDIG, d, AG_SHA256_LEN);
}

/*
 * The DER INTEGER rules, which are where a homegrown certificate writer goes
 * wrong: r needs a zero prepended because its top bit is set, s loses two
 * leading zeros, and the certificate comes out a different length as a result.
 * The element will hand us both cases eventually and neither is rare.
 */
static void test_a_signature_needing_der_padding_changes_the_length(void)
{
    ag_alias_parts_t p = pinned();
    uint8_t          got[CERT_MAX];

    p.sig = SIG_PAD;
    TEST_ASSERT_EQUAL_UINT16(AGV_ALIAS_CERT_PAD_LEN, collect(&p, got));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CERT_PAD, got, AGV_ALIAS_CERT_PAD_LEN);
    /* and the two certificates really are different lengths, which is the
     * reason the total is reported rather than assumed */
    TEST_ASSERT_NOT_EQUAL(AGV_ALIAS_CERT_LEN, AGV_ALIAS_CERT_PAD_LEN);
}

/* ------------------------------------------------------ structural checks */

static void test_the_emitted_certificate_is_well_formed_der(void)
{
    ag_alias_parts_t p = pinned();
    uint8_t          got[CERT_MAX];
    const uint8_t   *ev;
    uint16_t         evlen;
    uint16_t         n = collect(&p, got);

    TEST_ASSERT_TRUE(der_exact(got, n));
    TEST_ASSERT_TRUE(uccs_evidence(got, n, &ev, &evlen));
    TEST_ASSERT_TRUE(der_exact(ev, evlen));
}

/* The easy break: the TBSCertificate's length moved by one. Every X.509 parser
 * rejects this, and it is here so the checker above is known to have teeth --
 * a checker that accepts everything proves nothing about the one below. */
static void test_the_structural_check_rejects_a_moved_outer_length(void)
{
    TEST_ASSERT_FALSE(der_exact(BAD_OUT, AGV_ALIAS_CERT_BAD_OUTER_LEN));
    /* and the good one from the same generator passes, so the difference is
     * the corruption and not the checker */
    TEST_ASSERT_TRUE(der_exact(CERT, AGV_ALIAS_CERT_LEN));
}

/*
 * THE BREAK THAT MATTERS. One length byte inside the extension value. OpenSSL
 * accepts this certificate. So does Python `cryptography`. Neither descends
 * into an extension it does not recognise, so both see 141 opaque bytes and
 * report a well-formed document -- and a gateway that "verified" it would relay
 * evidence that cannot be decoded. Only walking the extension's own DER finds
 * it.
 */
static void test_the_structural_check_rejects_evidence_broken_inside_the_extension(void)
{
    const uint8_t *ev;
    uint16_t       evlen;

    /* The certificate itself is still well formed -- that is the whole point */
    TEST_ASSERT_TRUE(der_exact(BAD_IN, AGV_ALIAS_CERT_BAD_INNER_LEN));
    TEST_ASSERT_TRUE(uccs_evidence(BAD_IN, AGV_ALIAS_CERT_BAD_INNER_LEN,
                                   &ev, &evlen));
    TEST_ASSERT_FALSE(der_exact(ev, evlen));

    /* and the same walk over the good certificate passes */
    TEST_ASSERT_TRUE(uccs_evidence(CERT, AGV_ALIAS_CERT_LEN, &ev, &evlen));
    TEST_ASSERT_TRUE(der_exact(ev, evlen));
}

/* The evidence inside the certificate is the claims set the encoder produces,
 * byte for byte. Two layers that agree about the bytes and disagree about the
 * lengths around them is the defect the template exists to prevent. */
static void test_the_extension_carries_the_claims_set_verbatim(void)
{
    ag_alias_parts_t p = pinned();
    uint8_t          got[CERT_MAX];
    const uint8_t   *ev;
    uint16_t         evlen, n;
    uint8_t          uccs[AG_UCCS_LEN];
    ag_writer_t      w;

    n = collect(&p, got);
    TEST_ASSERT_TRUE(uccs_evidence(got, n, &ev, &evlen));
    /* ev is SEQUENCE { OCTET STRING }; the claims set is the inner content */
    TEST_ASSERT_EQUAL_HEX8(0x30u, ev[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04u, ev[3]);
    TEST_ASSERT_EQUAL_UINT8(AG_UCCS_LEN, ev[5]);

    ag_writer_init(&w, uccs, (uint8_t)sizeof uccs);
    TEST_ASSERT_EQUAL(AG_OK, ag_uccs_encode(&w, NONCE, AG_ALIAS_NONCE_LEN,
                                            UEID, FWHASH));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(uccs, &ev[6], AG_UCCS_LEN);
}

/* ---------------------------------------------------------- window bounds */

/* An offset at or past the end is a refusal, not an empty page: a host that
 * asked for a page it should not have asked for has lost count, and an empty
 * success reads as "the document ended here". */
static void test_a_window_past_the_end_is_refused(void)
{
    ag_alias_parts_t p = pinned();
    uint8_t          buf[AG_ALIAS_PAGE];
    uint16_t         total = 0u;
    uint8_t          got   = 0u;

    TEST_ASSERT_EQUAL(AG_ERR_RANGE,
                      ag_alias_cert(&p, AGV_ALIAS_CERT_LEN, buf,
                                    AG_ALIAS_PAGE, &total, &got));
    TEST_ASSERT_EQUAL(AG_ERR_RANGE,
                      ag_alias_cert(&p, 60000u, buf, AG_ALIAS_PAGE,
                                    &total, &got));
}

/* The last window is short, and says so rather than padding. A page padded to
 * its full width would append bytes to the certificate. */
static void test_the_last_window_is_short_and_says_so(void)
{
    ag_alias_parts_t p = pinned();
    uint8_t          buf[AG_ALIAS_PAGE];
    uint16_t         total = 0u;
    uint16_t         last  = (uint16_t)(AGV_ALIAS_CERT_LEN
                                        - (AGV_ALIAS_CERT_LEN % AG_ALIAS_PAGE));
    uint8_t          got   = 0u;

    TEST_ASSERT_EQUAL(AG_OK, ag_alias_cert(&p, last, buf, AG_ALIAS_PAGE,
                                           &total, &got));
    TEST_ASSERT_EQUAL_UINT16(AGV_ALIAS_CERT_LEN, total);
    TEST_ASSERT_EQUAL_UINT8(AGV_ALIAS_CERT_LEN % AG_ALIAS_PAGE, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&CERT[last], buf, got);
}

/* Every window reports the WHOLE length, not the remainder, so a host that
 * lost page 0 can still learn how many pages there are. */
static void test_every_window_reports_the_whole_length(void)
{
    ag_alias_parts_t p = pinned();
    uint8_t          buf[AG_ALIAS_PAGE];
    uint16_t         total = 0u;
    uint8_t          got   = 0u;
    uint16_t         off;

    for (off = 0u; off < AGV_ALIAS_CERT_LEN; off = (uint16_t)(off + AG_ALIAS_PAGE)) {
        total = 0u;
        TEST_ASSERT_EQUAL(AG_OK, ag_alias_cert(&p, off, buf, AG_ALIAS_PAGE,
                                               &total, &got));
        TEST_ASSERT_EQUAL_UINT16(AGV_ALIAS_CERT_LEN, total);
    }
}

/* ------------------------------------------------------ binding to inputs */

/* A different challenge must produce a different document, or the certificate
 * is not evidence of anything a verifier asked for. The serial number moves
 * too, because a CA that reissues one serial with different contents has
 * broken the one thing a serial is for. */
static void test_a_different_nonce_produces_a_different_certificate(void)
{
    ag_alias_parts_t p = pinned();
    uint8_t          a[CERT_MAX], b[CERT_MAX], other[AG_ALIAS_NONCE_LEN];
    uint16_t         n;

    n = collect(&p, a);
    memcpy(other, NONCE, sizeof other);
    other[0] ^= 0xFFu;
    p.nonce = other;
    TEST_ASSERT_EQUAL_UINT16(n, collect(&p, b));
    TEST_ASSERT_FALSE(memcmp(a, b, n) == 0);
    /* the serial number, at offset 4 + 11 into the certificate */
    TEST_ASSERT_FALSE(a[15] == b[15]);
}

/*
 * The serial number is always a positive, minimally encoded INTEGER whatever
 * the nonce is. A leading byte of zero or one at or above 0x80 would make the
 * field's width depend on the challenge, and the template has no room for a
 * field that changes length.
 */
static void test_the_serial_number_is_always_a_minimal_positive_integer(void)
{
    ag_alias_parts_t p = pinned();
    uint8_t          nz[AG_ALIAS_NONCE_LEN];
    uint8_t          got[CERT_MAX];
    uint8_t          v;

    for (v = 0u; v < 255u; v = (uint8_t)(v + 17u)) {
        memset(nz, v, sizeof nz);
        p.nonce = nz;
        (void)collect(&p, got);
        TEST_ASSERT_EQUAL_HEX8(0x02u, got[13]);   /* INTEGER */
        TEST_ASSERT_EQUAL_HEX8(0x08u, got[14]);   /* eight bytes, always */
        TEST_ASSERT_TRUE(got[15] != 0x00u);
        TEST_ASSERT_TRUE(got[15] < 0x80u);
    }
}

/* ------------------------------------------------------------ the command */

/*
 * THE HEADLINE REFUSAL. An element with no key has no identity to certify, and
 * must say so rather than issue a certificate over a public point it invented.
 */
static void test_an_unprovisioned_element_refuses_to_mint(void)
{
    uint8_t     out[AG_MAX_PAYLOAD];
    ag_reader_t r;
    ag_writer_t w;

    ag_reader_init(&r, NONCE, AG_ALIAS_NONCE_LEN);
    ag_writer_init(&w, out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL(AG_ERR_NOT_PROVISIONED, ag_alias_cmd(&r, &w));
}

/* And a page request afterwards is still refused: a failed mint must not leave
 * a document reachable. */
static void test_a_page_request_with_no_certificate_is_refused(void)
{
    uint8_t     out[AG_MAX_PAYLOAD];
    uint8_t     page = 0u;
    ag_reader_t r;
    ag_writer_t w;

    ag_reader_init(&r, &page, 1u);
    ag_writer_init(&w, out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL(AG_ERR_BAD_ARGC, ag_alias_cmd(&r, &w));
}

static ag_status_t run(const uint8_t *p, uint8_t n, uint8_t *out, uint8_t *len)
{
    static uint8_t in[AG_MAX_PAYLOAD];
    ag_reader_t    r;
    ag_writer_t    w;
    ag_status_t    st;

    memcpy(in, p, n);
    ag_reader_init(&r, in, n);
    ag_writer_init(&w, out, AG_MAX_PAYLOAD);
    st = ag_alias_cmd(&r, &w);
    *len = ag_writer_len(&w);
    return st;
}

/*
 * The whole path: mint against the element, then page the document back and
 * check it is one certificate rather than eight fragments that happen to
 * concatenate.
 */
static void test_a_provisioned_element_mints_a_certificate_and_pages_it(void)
{
    uint8_t  out[AG_MAX_PAYLOAD];
    uint8_t  doc[CERT_MAX];
    uint8_t  len = 0u, page;
    uint16_t total, got = 0u;
    const uint8_t *ev;
    uint16_t       evlen;

    provision();
    TEST_ASSERT_EQUAL(AG_OK, run(NONCE, AG_ALIAS_NONCE_LEN, out, &len));
    TEST_ASSERT_EQUAL_UINT8(0u, out[0]);
    total = (uint16_t)((uint16_t)out[1] | ((uint16_t)out[2] << 8));
    TEST_ASSERT_TRUE(total > 400u && total < CERT_MAX);
    memcpy(doc, &out[3], (size_t)(len - 3u));
    got = (uint16_t)(len - 3u);

    for (page = 1u; got < total; page++) {
        TEST_ASSERT_EQUAL(AG_OK, run(&page, 1u, out, &len));
        TEST_ASSERT_EQUAL_UINT8(page, out[0]);
        TEST_ASSERT_EQUAL_UINT16(total,
                                 (uint16_t)out[1] | ((uint16_t)out[2] << 8));
        memcpy(&doc[got], &out[3], (size_t)(len - 3u));
        got = (uint16_t)(got + (len - 3u));
    }
    TEST_ASSERT_EQUAL_UINT16(total, got);
    TEST_ASSERT_TRUE(der_exact(doc, total));
    TEST_ASSERT_TRUE(uccs_evidence(doc, total, &ev, &evlen));
    TEST_ASSERT_TRUE(der_exact(ev, evlen));
    /* the verifier's challenge really is in the evidence */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NONCE, &ev[6 + 4], AG_ALIAS_NONCE_LEN);
}

/*
 * THE REGRESSION THIS EXISTS TO CATCH. ATTEST_CERT is minted here WITHOUT
 * ATTEST ever having run first. Before this was fixed,
 * ag_attest_device_id()/ag_attest_fw_hash() returned pointers into attest.c's
 * OWN evidence buffer, which nothing populates except ATTEST's build() -- so
 * a certificate minted this way embedded an all-zero UEID and measurement on
 * a FULLY PROVISIONED device, and nothing about the exchange said so. The
 * accessors now read src/core/platform_id.c and src/core/handoff.c directly,
 * which are populated independently of whether ATTEST ever ran.
 */
static void test_a_certificate_minted_before_attest_still_carries_the_real_identity(void)
{
    static const uint8_t serial[AG_BOOT_SERIAL_LEN] = {
        0xAB,0xAB,0xAB,0xAB,0xAB,0xAB,0xAB,0xAB,
        0xAB,0xAB,0xAB,0xAB,0xAB,0xAB,0xAB,0xAB
    };
    ag_boot_platform_t plat;
    uint8_t  raw[AG_BOOT_EE_LEN];
    uint8_t  block[AG_BOOT_HANDOFF_LEN];
    uint8_t  cdi[32], measurement[32];
    uint8_t  out[AG_MAX_PAYLOAD];
    uint8_t  doc[CERT_MAX];
    uint8_t  len = 0u, page;
    uint16_t total, got = 0u;
    unsigned i;
    int      found_serial, found_measurement;

    memset(&plat, 0, sizeof plat);
    memcpy(plat.serial, serial, sizeof serial);
    memset(raw, 0, sizeof raw);
    ag_boot_platform_serialise(&plat, raw);
    memcpy(test_eeprom, raw, sizeof test_eeprom);
    ag_platform_id_init();

    memset(cdi, 0, sizeof cdi);
    for (i = 0u; i < sizeof measurement; i++) { measurement[i] = (uint8_t)(0x70u + i); }
    ag_boot_handoff_build(block, AG_CRYPTO_SECURE_ELEMENT, cdi, measurement, 1u, 0u);
    memcpy(test_handoff, block, sizeof test_handoff);
    ag_boot_handoff_init();

    provision();
    /* ATTEST is never called -- ag_attest_cmd's build() never runs. */
    TEST_ASSERT_EQUAL(AG_OK, run(NONCE, AG_ALIAS_NONCE_LEN, out, &len));
    total = (uint16_t)((uint16_t)out[1] | ((uint16_t)out[2] << 8));
    memcpy(doc, &out[3], (size_t)(len - 3u));
    got = (uint16_t)(len - 3u);
    for (page = 1u; got < total; page++) {
        TEST_ASSERT_EQUAL(AG_OK, run(&page, 1u, out, &len));
        memcpy(&doc[got], &out[3], (size_t)(len - 3u));
        got = (uint16_t)(got + (len - 3u));
    }
    TEST_ASSERT_EQUAL_UINT16(total, got);

    found_serial = 0;
    for (i = 0u; i + AG_BOOT_SERIAL_LEN <= total; i++) {
        if (memcmp(&doc[i], serial, AG_BOOT_SERIAL_LEN) == 0) { found_serial = 1; }
    }
    found_measurement = 0;
    for (i = 0u; i + sizeof measurement <= total; i++) {
        if (memcmp(&doc[i], measurement, sizeof measurement) == 0) { found_measurement = 1; }
    }
    TEST_ASSERT_TRUE(found_serial);
    TEST_ASSERT_TRUE(found_measurement);
}

/*
 * THE PROPERTY THE WHOLE THING EXISTS FOR: the signature is over the TBS this
 * firmware built, under the key the certificate names. The model signs for real
 * (ECDSA P-256, RFC 6979), so the check is the one a verifier makes: take the
 * certificate the device paged out, hash its TBS, and verify the signature in
 * it against the subject public key in it -- which must be the element's key.
 *
 * Then the same document is rebuilt from the outside, from the digest alone.
 * RFC 6979 makes the signature a function of key and digest, so the two must
 * be byte-identical, and a different digest reaching the element would change
 * the signature and fail the comparison.
 */
static void test_the_signature_is_over_the_tbs_the_device_built(void)
{
    uint8_t  out[AG_MAX_PAYLOAD];
    uint8_t  doc[CERT_MAX];
    uint8_t  want[CERT_MAX];
    uint8_t  expect_sig[AG_SIG_LEN];
    uint8_t  digest[AG_SHA256_LEN];
    uint8_t  len = 0u, page;
    uint16_t total, got;
    ag_alias_parts_t p;

    provision();
    TEST_ASSERT_EQUAL(AG_OK, run(NONCE, AG_ALIAS_NONCE_LEN, out, &len));
    total = (uint16_t)((uint16_t)out[1] | ((uint16_t)out[2] << 8));
    memcpy(doc, &out[3], (size_t)(len - 3u));
    got = (uint16_t)(len - 3u);
    for (page = 1u; got < total; page++) {
        TEST_ASSERT_EQUAL(AG_OK, run(&page, 1u, out, &len));
        memcpy(&doc[got], &out[3], (size_t)(len - 3u));
        got = (uint16_t)(got + (len - 3u));
    }

    /* The digest the device signed, rebuilt from the inputs it used. The public
     * key is the element's real one; the identity and measurement come from the
     * same accessors the command used. */
    p.nonce   = NONCE;
    p.ueid    = ag_attest_device_id();
    p.fw_hash = ag_attest_fw_hash();
    p.pubkey  = ag_model_deviceid_public();
    p.sig     = expect_sig;
    TEST_ASSERT_NOT_NULL(p.pubkey);
    TEST_ASSERT_EQUAL(AG_OK, ag_alias_tbs_digest(&p, digest));

    TEST_ASSERT_TRUE(ag_model_p256_sign(ag_model_deviceid_private(), digest, expect_sig));
    TEST_ASSERT_TRUE(ag_model_p256_verify(p.pubkey, digest, expect_sig));
    TEST_ASSERT_EQUAL_UINT16(total, collect(&p, want));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, doc, total);

    /* And a verifier that has only the DeviceID public key rejects the same
     * signature over any other digest. */
    digest[0] ^= 0x01u;
    TEST_ASSERT_FALSE(ag_model_p256_verify(p.pubkey, digest, expect_sig));
}

/* A fresh challenge replaces the document. Serving the previous one would be
 * serving evidence bound to a nonce the host has moved on from. */
static void test_a_new_nonce_replaces_the_certificate(void)
{
    uint8_t out[AG_MAX_PAYLOAD], first[AG_MAX_PAYLOAD];
    uint8_t len = 0u, other[AG_ALIAS_NONCE_LEN];

    provision();
    TEST_ASSERT_EQUAL(AG_OK, run(NONCE, AG_ALIAS_NONCE_LEN, out, &len));
    memcpy(first, out, len);

    memcpy(other, NONCE, sizeof other);
    other[0] ^= 0xFFu;
    TEST_ASSERT_EQUAL(AG_OK, run(other, AG_ALIAS_NONCE_LEN, out, &len));
    TEST_ASSERT_FALSE(memcmp(first, out, len) == 0);
}

/* Arity: a 16-byte nonce mints and a 1-byte page fetches. Nothing else is
 * either, and a nonce of the wrong length must not be padded into one. */
static void test_a_wrong_sized_argument_is_refused(void)
{
    uint8_t out[AG_MAX_PAYLOAD];
    uint8_t len = 0u;
    uint8_t junk[AG_ALIAS_NONCE_LEN] = { 0 };

    provision();
    TEST_ASSERT_EQUAL(AG_ERR_BAD_ARGC, run(junk, 8u, out, &len));
    TEST_ASSERT_EQUAL(AG_ERR_BAD_ARGC, run(junk, 31u, out, &len));
    TEST_ASSERT_EQUAL(AG_ERR_BAD_ARGC, run(junk, 0u, out, &len));
    /* Sixteen too. It is the evidence frame's nonce width, and a host that
     * sent the wrong one of this firmware's two nonces must be told so rather
     * than handed a certificate over a padded challenge. */
    TEST_ASSERT_EQUAL(AG_ERR_BAD_ARGC,
                      run(junk, AG_ATTEST_NONCE_LEN, out, &len));
}

/* A page past the end of the document is refused rather than wrapped. The
 * offset is computed at sixteen bits precisely so page 99 cannot land back
 * inside the certificate. */
static void test_a_page_past_the_end_is_refused(void)
{
    uint8_t out[AG_MAX_PAYLOAD];
    uint8_t len = 0u, page;

    provision();
    TEST_ASSERT_EQUAL(AG_OK, run(NONCE, AG_ALIAS_NONCE_LEN, out, &len));
    page = 99u;
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, run(&page, 1u, out, &len));
    page = 255u;
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, run(&page, 1u, out, &len));
}

/* Every page fits one payload, header included. If it did not, the dispatcher
 * would answer AG_ERR_TOO_LONG for a request that was entirely valid. */
static void test_a_page_and_its_header_fit_one_payload(void)
{
    uint8_t out[AG_MAX_PAYLOAD];
    uint8_t len = 0u;

    TEST_ASSERT_LESS_OR_EQUAL_UINT8(AG_MAX_PAYLOAD, AG_ALIAS_PAGE + 3u);
    provision();
    TEST_ASSERT_EQUAL(AG_OK, run(NONCE, AG_ALIAS_NONCE_LEN, out, &len));
    TEST_ASSERT_EQUAL_UINT8(AG_ALIAS_PAGE + 3u, len);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_certificate_is_the_bytes_the_vector_pins);
    RUN_TEST(test_the_tbs_digest_is_the_value_the_vector_pins);
    RUN_TEST(test_a_signature_needing_der_padding_changes_the_length);
    RUN_TEST(test_the_emitted_certificate_is_well_formed_der);
    RUN_TEST(test_the_structural_check_rejects_a_moved_outer_length);
    RUN_TEST(test_the_structural_check_rejects_evidence_broken_inside_the_extension);
    RUN_TEST(test_the_extension_carries_the_claims_set_verbatim);
    RUN_TEST(test_a_window_past_the_end_is_refused);
    RUN_TEST(test_the_last_window_is_short_and_says_so);
    RUN_TEST(test_every_window_reports_the_whole_length);
    RUN_TEST(test_a_different_nonce_produces_a_different_certificate);
    RUN_TEST(test_the_serial_number_is_always_a_minimal_positive_integer);
    RUN_TEST(test_an_unprovisioned_element_refuses_to_mint);
    RUN_TEST(test_a_page_request_with_no_certificate_is_refused);
    RUN_TEST(test_a_provisioned_element_mints_a_certificate_and_pages_it);
    RUN_TEST(test_a_certificate_minted_before_attest_still_carries_the_real_identity);
    RUN_TEST(test_the_signature_is_over_the_tbs_the_device_built);
    RUN_TEST(test_a_new_nonce_replaces_the_certificate);
    RUN_TEST(test_a_wrong_sized_argument_is_refused);
    RUN_TEST(test_a_page_past_the_end_is_refused);
    RUN_TEST(test_a_page_and_its_header_fit_one_payload);
    return UNITY_END();
}
