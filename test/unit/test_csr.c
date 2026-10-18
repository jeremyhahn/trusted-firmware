/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * ATTEST_CSR: the possession signature over a CertificationRequestInfo the
 * device builds -- issue #30.
 *
 * Three halves. The first pins the CertificationRequestInfo and its digest
 * against literals an independent encoder produced and OpenSSL parsed, so a
 * pass means the device signs the document a host will rebuild, not one this
 * codebase agrees with itself about. The second drives the command against the
 * software element and verifies its signature the way a CA would. The third is
 * the CONFUSION CHECK: the DeviceID key now signs three structures, and none may
 * parse as another.
 */

#include <string.h>
#include "unity.h"
#include "csr.h"
#include "alias.h"
#include "attest.h"
#include "uccs.h"
#include "uccs_vectors.h"
#include "vectors.h"
#include "atecc608a_model.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/types.h"
#include "tfw/hal.h"
#include "tfw/boot.h"
#include "handoff.h"
#include "platform_id.h"

/* The same HAL stand-ins test_alias.c defines, for the same reasons: the
 * element model is this binary's I2C HAL, and attest.c's identity accessors
 * read the platform block and the handoff through these. */
tfw_ms_t tfw_hal_millis(void) { return 0u; }

static uint8_t test_eeprom[TFW_BOOT_EE_LEN];
static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];

tfw_status_t tfw_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if ((uint16_t)(addr + len) > sizeof test_eeprom) { return TFW_ERR_RANGE; }
    memcpy(buf, &test_eeprom[addr], len);
    return TFW_OK;
}

tfw_status_t tfw_hal_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    return tfw_hal_eeprom_read(addr, buf, len);
}

uint8_t *tfw_hal_handoff_block(void)
{
    return test_handoff;
}

static const uint8_t NONCE[]  = TFWV_UCCS_NONCE;
static const uint8_t UEID[]   = TFWV_UCCS_UEID;
static const uint8_t FWHASH[] = TFWV_UCCS_FWHASH;
static const uint8_t PUBKEY[] = TFWV_ALIAS_PUBKEY;
static const uint8_t SIG[]    = TFWV_ALIAS_SIG;
static const uint8_t CRI[]    = TFWV_CSR_CRI;
static const uint8_t CRIDIG[] = TFWV_CSR_CRI_DIGEST;
static const uint8_t CERT[]   = TFWV_ALIAS_CERT;

#define DOC_MAX 512u

void setUp(void)
{
    tfw_model_reset();
    tfw_attest_reset_state();
    tfw_alias_reset_state();
    memset(test_eeprom, 0, sizeof test_eeprom);
    memset(test_handoff, 0, sizeof test_handoff);
    tfw_platform_id_init();
    tfw_boot_handoff_init();
    tfw_crypto_init();
}

void tearDown(void) {}

static void provision(void)
{
    tfw_model_provision();
    tfw_crypto_init();
}

static tfw_alias_parts_t pinned(void)
{
    tfw_alias_parts_t p;
    p.nonce   = NONCE;
    p.ueid    = UEID;
    p.fw_hash = FWHASH;
    p.pubkey  = PUBKEY;
    p.sig     = NULL;
    return p;
}

/* The whole CertificationRequestInfo, out of 60-byte windows. */
static uint16_t collect(const tfw_alias_parts_t *p, uint8_t *dst)
{
    uint16_t total = 0u, off = 0u;
    uint8_t  got   = 0u;

    do {
        TEST_ASSERT_EQUAL(TFW_OK, tfw_csr_cri(p, off, &dst[off], 60u, &total, &got));
        TEST_ASSERT_TRUE(got > 0u);
        off = (uint16_t)(off + got);
    } while (off < total);
    TEST_ASSERT_EQUAL_UINT16(total, off);
    return total;
}

/* --------------------------------------------------------- the DER walker */

/* test_alias.c's walker: one DER TLV, its total length or 0, descending into
 * every constructed type and rejecting any length that could be shorter. */
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

/* The header length of the TLV at p, or 0. Only called on bytes der_exact has
 * already accepted. */
static uint16_t der_hdr(const uint8_t *p)
{
    return (uint16_t)(p[1] < 0x80u ? 2u : (p[1] == 0x81u ? 3u : 4u));
}

/* ---------------------------------------------------- the three readers */

/*
 * What each of the three signed structures must look like to a reader that
 * expects it. Each is the check a parser for that structure makes before it
 * will believe anything else, and the confusion tests below hold every
 * structure up to every reader but its own.
 */

/* The evidence frame: TFW_ATTEST_LEN bytes, version byte first. */
static uint8_t reads_as_evidence(const uint8_t *d, uint16_t n)
{
    return (uint8_t)(n == TFW_ATTEST_LEN && d[0] == TFW_ATTEST_VERSION);
}

/*
 * A TBSCertificate (RFC 5280 section 4.1): a SEQUENCE whose children are an
 * optional [0] version, an INTEGER serial, then the signature
 * AlgorithmIdentifier -- a SEQUENCE that opens with its OID, which is not
 * optional. Whichever reading of the version is taken, that AlgorithmIdentifier
 * must be there.
 */
static uint8_t reads_as_tbs(const uint8_t *d, uint16_t n)
{
    const uint8_t *c;

    if (!der_exact(d, n) || d[0] != 0x30u) {
        return 0u;
    }
    c = &d[der_hdr(d)];
    if (c[0] == 0xA0u) {
        c += der_tlv(c, (uint16_t)(n - (uint16_t)(c - d)), 1u);
    }
    if (c[0] != 0x02u) {
        return 0u;
    }
    c += der_tlv(c, (uint16_t)(n - (uint16_t)(c - d)), 1u);
    return (uint8_t)(c[0] == 0x30u && c[1] >= 2u && c[2] == 0x06u);
}

/*
 * A CertificationRequestInfo (RFC 2986 section 4.1): a SEQUENCE opening with
 * the INTEGER 0, then a Name, then a SubjectPublicKeyInfo -- a SEQUENCE that
 * opens with an AlgorithmIdentifier -- then [0] attributes.
 */
static uint8_t reads_as_cri(const uint8_t *d, uint16_t n)
{
    const uint8_t *c;

    if (!der_exact(d, n) || d[0] != 0x30u) {
        return 0u;
    }
    c = &d[der_hdr(d)];
    if (c[0] != 0x02u || c[1] != 0x01u || c[2] != 0x00u) {
        return 0u;
    }
    c += 3;
    if (c[0] != 0x30u) {
        return 0u;
    }
    c += der_tlv(c, (uint16_t)(n - (uint16_t)(c - d)), 1u);
    if (c[0] != 0x30u || c[der_hdr(c)] != 0x30u) {
        return 0u;
    }
    c += der_tlv(c, (uint16_t)(n - (uint16_t)(c - d)), 1u);
    return (uint8_t)(c[0] == 0xA0u);
}

/* ------------------------------------------------------------ the vectors */

/* THE CONTRACT. The bytes a host rebuilds and wraps, as an independent encoder
 * produced them and OpenSSL read them. */
static void test_the_request_info_is_the_bytes_the_vector_pins(void)
{
    tfw_alias_parts_t p = pinned();
    uint8_t          got[DOC_MAX];

    TEST_ASSERT_EQUAL_UINT16(TFWV_CSR_CRI_LEN, TFW_CSR_CRI_LEN);
    TEST_ASSERT_EQUAL_UINT16(TFWV_CSR_CRI_LEN, collect(&p, got));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CRI, got, TFWV_CSR_CRI_LEN);
}

/* The digest the DeviceID key signs. Never on the wire, so nothing else would
 * notice a request that moved by a byte -- until a CA rejected every one. */
static void test_the_digest_is_the_value_the_vector_pins(void)
{
    tfw_alias_parts_t p = pinned();
    uint8_t          d[TFW_SHA256_LEN];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_csr_cri_digest(&p, d));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CRIDIG, d, TFW_SHA256_LEN);
}

/* Sharing the walker and the template with the alias certificate must not have
 * moved the certificate by a byte. */
static void test_the_alias_certificate_is_unchanged_beside_it(void)
{
    tfw_alias_parts_t p = pinned();
    uint8_t          got[DOC_MAX];
    uint16_t         total = 0u, off = 0u;
    uint8_t          n = 0u;

    p.sig = SIG;
    do {
        TEST_ASSERT_EQUAL(TFW_OK, tfw_alias_cert(&p, off, &got[off], TFW_ALIAS_PAGE,
                                               &total, &n));
        off = (uint16_t)(off + n);
    } while (off < total);
    TEST_ASSERT_EQUAL_UINT16(TFWV_ALIAS_CERT_LEN, total);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(CERT, got, TFWV_ALIAS_CERT_LEN);
}

static void test_the_request_info_is_well_formed_der(void)
{
    tfw_alias_parts_t p = pinned();
    uint8_t          got[DOC_MAX];

    TEST_ASSERT_TRUE(der_exact(got, collect(&p, got)));
    TEST_ASSERT_TRUE(der_exact(CRI, TFWV_CSR_CRI_LEN));
}

/* The device is named by its whole UEID -- the 01 type byte, then the DeviceID
 * -- in lowercase hex, two digits a byte, whatever its value (go-xkms#1441): a
 * host rebuilding the request with Go's hex.EncodeToString gets these bytes. */
static void test_the_ueid_is_named_in_lowercase_hex(void)
{
    static const char want[] = "0100ff10af09a0"
                               "7f80c3d4e5f60718";
    static const uint8_t id[TFW_ATTEST_ID_LEN] = {
        0x00, 0xFF, 0x10, 0xAF, 0x09, 0xA0, 0x7F, 0x80,
        0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18, 0x29, 0x3A
    };
    tfw_alias_parts_t p = pinned();
    uint8_t          got[DOC_MAX];
    uint16_t         i;

    p.ueid = id;
    TEST_ASSERT_EQUAL_UINT16(TFWV_CSR_CRI_LEN, collect(&p, got));
    /* identifierValue sits at 157, after its 0C 22 header at 155 */
    TEST_ASSERT_EQUAL_HEX8(0x0Cu, got[155]);
    TEST_ASSERT_EQUAL_HEX8(0x22u, got[156]);
    TEST_ASSERT_EQUAL_MEMORY(want, &got[157], sizeof want - 1u);
    TEST_ASSERT_EQUAL_MEMORY("293a", &got[157 + 30], 4u);
    for (i = 157u; i < 191u; i++) {
        TEST_ASSERT_TRUE((got[i] >= '0' && got[i] <= '9')
                         || (got[i] >= 'a' && got[i] <= 'f'));
    }
    /* and the UCCS extension follows straight after the 34th digit */
    TEST_ASSERT_EQUAL_HEX8(0x30u, got[191]);
}

/* b is a fixed-width field: any value of it leaves every length where it was,
 * and moves the digest. */
static void test_b_changes_the_digest_and_nothing_else_about_the_shape(void)
{
    tfw_alias_parts_t p = pinned();
    uint8_t          a[DOC_MAX], c[DOC_MAX], other[TFW_ALIAS_NONCE_LEN];
    uint8_t          da[TFW_SHA256_LEN], dc[TFW_SHA256_LEN];
    uint16_t         i, diff = 0u;

    (void)collect(&p, a);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_csr_cri_digest(&p, da));
    memset(other, 0xFF, sizeof other);
    p.nonce = other;
    TEST_ASSERT_EQUAL_UINT16(TFWV_CSR_CRI_LEN, collect(&p, c));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_csr_cri_digest(&p, dc));
    TEST_ASSERT_FALSE(memcmp(da, dc, sizeof da) == 0);
    TEST_ASSERT_TRUE(reads_as_cri(c, TFWV_CSR_CRI_LEN));
    for (i = 0u; i < TFWV_CSR_CRI_LEN; i++) {
        diff = (uint16_t)(diff + (a[i] != c[i]));
    }
    TEST_ASSERT_EQUAL_UINT16(TFW_ALIAS_NONCE_LEN, diff);
}

static void test_a_window_past_the_end_is_refused(void)
{
    tfw_alias_parts_t p = pinned();
    uint8_t          buf[60];
    uint16_t         total = 0u;
    uint8_t          got   = 0u;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_csr_cri(&p, TFWV_CSR_CRI_LEN, buf,
                                               (uint8_t)sizeof buf, &total, &got));
}

/* ----------------------------------------------------------- confusion */

/* The CertificationRequestInfo reads as one, and as neither of the others. */
static void test_the_request_info_parses_as_nothing_else(void)
{
    TEST_ASSERT_TRUE(reads_as_cri(CRI, TFWV_CSR_CRI_LEN));
    TEST_ASSERT_FALSE(reads_as_tbs(CRI, TFWV_CSR_CRI_LEN));
    TEST_ASSERT_FALSE(reads_as_evidence(CRI, TFWV_CSR_CRI_LEN));
    /* and by the headers a reader keys on: neither the frame's version byte nor
     * the alias TBS's opening */
    TEST_ASSERT_NOT_EQUAL(TFW_ATTEST_VERSION, CRI[0]);
    TEST_ASSERT_FALSE(memcmp(CRI, "\x30\x82\x01\x7B\xA0\x03", 6u) == 0);
}

/* The alias TBS reads as one, and as neither of the others. It is the
 * certificate's first child, after the certificate's own four-byte header. */
static void test_the_alias_tbs_parses_as_nothing_else(void)
{
    const uint8_t *tbs = &CERT[4];
    uint16_t       n   = (uint16_t)(4u + (((uint32_t)tbs[2] << 8) | (uint32_t)tbs[3]));

    TEST_ASSERT_EQUAL_HEX8_ARRAY("\x30\x82\x01\x7B\xA0\x03", tbs, 6u);
    TEST_ASSERT_TRUE(reads_as_tbs(tbs, n));
    TEST_ASSERT_FALSE(reads_as_cri(tbs, n));
    TEST_ASSERT_FALSE(reads_as_evidence(tbs, n));
}

/* The evidence frame, as the device really produces it, reads as neither DER
 * structure: it does not even open with a SEQUENCE. */
static void test_the_evidence_frame_parses_as_nothing_else(void)
{
    static const uint8_t nonce16[TFW_ATTEST_NONCE_LEN] = { 0x5A };
    uint8_t     frame[TFW_ATTEST_LEN];
    uint8_t     out[TFW_MAX_PAYLOAD];
    uint8_t     in[1];
    uint16_t    got = 0u;
    uint8_t     page = 0u;
    tfw_reader_t r;
    tfw_writer_t w;

    provision();
    tfw_reader_init(&r, nonce16, TFW_ATTEST_NONCE_LEN);
    for (;;) {
        uint8_t len;

        tfw_writer_init(&w, out, (uint8_t)sizeof out);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_attest_cmd(&r, &w));
        len = (uint8_t)(tfw_writer_len(&w) - 2u);
        TEST_ASSERT_EQUAL_UINT8(TFW_ATTEST_LEN, out[1]);
        memcpy(&frame[got], &out[2], len);
        got = (uint16_t)(got + len);
        if (got >= TFW_ATTEST_LEN) {
            break;
        }
        in[0] = ++page;
        tfw_reader_init(&r, in, 1u);
    }
    TEST_ASSERT_EQUAL_UINT16(TFW_ATTEST_LEN, got);

    TEST_ASSERT_TRUE(reads_as_evidence(frame, TFW_ATTEST_LEN));
    TEST_ASSERT_FALSE(reads_as_tbs(frame, TFW_ATTEST_LEN));
    TEST_ASSERT_FALSE(reads_as_cri(frame, TFW_ATTEST_LEN));
    TEST_ASSERT_NOT_EQUAL(0x30u, frame[0]);
}

/* ------------------------------------------------------------ the command */

static tfw_status_t run(const uint8_t *p, uint8_t n, uint8_t *out, uint8_t *len)
{
    static uint8_t in[TFW_MAX_PAYLOAD];
    tfw_reader_t    r;
    tfw_writer_t    w;
    tfw_status_t    st;

    memcpy(in, p, n);
    tfw_reader_init(&r, in, n);
    tfw_writer_init(&w, out, TFW_MAX_PAYLOAD);
    st = tfw_csr_cmd(&r, &w);
    *len = tfw_writer_len(&w);
    return st;
}

/*
 * THE HAPPY PATH, checked the way a CA checks it: rebuild the request from b,
 * the DeviceID, the measurement and the element's real public key, hash it,
 * and verify the returned r||s under that key. RFC 6979 makes the signature a
 * function of key and digest, so it must also equal the one the test key makes
 * over the rebuilt digest -- a different digest reaching the element fails that.
 */
static void test_a_provisioned_element_signs_the_request_info(void)
{
    uint8_t          out[TFW_MAX_PAYLOAD];
    uint8_t          want[TFW_SIG_LEN];
    uint8_t          digest[TFW_SHA256_LEN];
    uint8_t          len = 0u;
    tfw_alias_parts_t p;

    provision();
    TEST_ASSERT_EQUAL(TFW_OK, run(NONCE, TFW_ALIAS_NONCE_LEN, out, &len));
    TEST_ASSERT_EQUAL_UINT8(TFW_SIG_LEN, len);

    p.nonce   = NONCE;
    p.ueid    = tfw_attest_device_id();
    p.fw_hash = tfw_attest_fw_hash();
    p.pubkey  = tfw_model_deviceid_public();
    p.sig     = NULL;
    TEST_ASSERT_NOT_NULL(p.pubkey);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_csr_cri_digest(&p, digest));

    TEST_ASSERT_TRUE(tfw_model_p256_verify(p.pubkey, digest, out));
    TEST_ASSERT_TRUE(tfw_model_p256_sign(tfw_model_deviceid_private(), digest, want));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, TFW_SIG_LEN);

    digest[0] ^= 0x01u;
    TEST_ASSERT_FALSE(tfw_model_p256_verify(p.pubkey, digest, out));
}

/* Exactly 32 bytes. b is a fixed-width field and a shorter or longer one must
 * not be padded or cut into it -- including 16, the evidence frame's nonce. */
static void test_a_wrong_sized_b_is_refused(void)
{
    uint8_t out[TFW_MAX_PAYLOAD];
    uint8_t len = 0u;
    uint8_t junk[TFW_ALIAS_NONCE_LEN + 1u] = { 0 };

    provision();
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, run(junk, 0u, out, &len));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, run(junk, 1u, out, &len));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, run(junk, TFW_ATTEST_NONCE_LEN, out, &len));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, run(junk, 31u, out, &len));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, run(junk, 33u, out, &len));
    TEST_ASSERT_EQUAL_UINT8(0u, len);
}

/* No key, no signature: the element's refusal travels back unaltered. */
static void test_an_unprovisioned_element_refuses(void)
{
    uint8_t out[TFW_MAX_PAYLOAD];
    uint8_t len = 0u;

    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED,
                      run(NONCE, TFW_ALIAS_NONCE_LEN, out, &len));
    TEST_ASSERT_EQUAL_UINT8(0u, len);
}

/* An element that fails a command fails this one, and nothing is written --
 * not a signature over a key the device never read. */
static void test_an_element_failure_is_returned_and_writes_nothing(void)
{
    uint8_t out[TFW_MAX_PAYLOAD];
    uint8_t len = 0u;

    provision();
    tfw_model_force_status(0x0Fu);                 /* execution error */
    TEST_ASSERT_NOT_EQUAL(TFW_OK, run(NONCE, TFW_ALIAS_NONCE_LEN, out, &len));
    TEST_ASSERT_EQUAL_UINT8(0u, len);
}

/* The public key is read and the signature then fails: the GenKey is taken,
 * every command after it is NACKed. Refused past the key, and nothing written. */
static void test_a_signing_failure_after_the_key_writes_nothing(void)
{
    uint8_t      out[TFW_MAX_PAYLOAD];
    uint8_t      len = 0xFFu;
    tfw_status_t st;

    provision();
    tfw_model_fail_commands_after(1u);
    st = run(NONCE, TFW_ALIAS_NONCE_LEN, out, &len);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, st);
    TEST_ASSERT_NOT_EQUAL(TFW_ERR_NOT_PROVISIONED, st);   /* the key was read */
    TEST_ASSERT_EQUAL_UINT8(0u, len);
}

/* No state: ATTEST_CSR leaves no certificate behind for ATTEST_CERT to page. */
static void test_the_command_leaves_no_certificate_behind(void)
{
    uint8_t     out[TFW_MAX_PAYLOAD];
    uint8_t     len = 0u, page = 0u;
    tfw_reader_t r;
    tfw_writer_t w;

    provision();
    TEST_ASSERT_EQUAL(TFW_OK, run(NONCE, TFW_ALIAS_NONCE_LEN, out, &len));
    tfw_reader_init(&r, &page, 1u);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, tfw_alias_cmd(&r, &w));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_request_info_is_the_bytes_the_vector_pins);
    RUN_TEST(test_the_digest_is_the_value_the_vector_pins);
    RUN_TEST(test_the_alias_certificate_is_unchanged_beside_it);
    RUN_TEST(test_the_request_info_is_well_formed_der);
    RUN_TEST(test_the_ueid_is_named_in_lowercase_hex);
    RUN_TEST(test_b_changes_the_digest_and_nothing_else_about_the_shape);
    RUN_TEST(test_a_window_past_the_end_is_refused);
    RUN_TEST(test_the_request_info_parses_as_nothing_else);
    RUN_TEST(test_the_alias_tbs_parses_as_nothing_else);
    RUN_TEST(test_the_evidence_frame_parses_as_nothing_else);
    RUN_TEST(test_a_provisioned_element_signs_the_request_info);
    RUN_TEST(test_a_wrong_sized_b_is_refused);
    RUN_TEST(test_an_unprovisioned_element_refuses);
    RUN_TEST(test_an_element_failure_is_returned_and_writes_nothing);
    RUN_TEST(test_a_signing_failure_after_the_key_writes_nothing);
    RUN_TEST(test_the_command_leaves_no_certificate_behind);
    return UNITY_END();
}
