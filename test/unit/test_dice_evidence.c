/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The soft root's DICE chain, end to end, against literals.
 *
 * A UDS and an application image go in; a whole ATTEST frame comes out, and
 * every byte of it is pinned. The literals below were computed outside this
 * code, from the TCG text and docs/dice-attestation/api.md, by an independent
 * implementation (Python's hmac and hashlib), never by running this firmware:
 *
 *   FWHash   = SHA-256(code)
 *   CDI      = HMAC-SHA256(UDS, FWHash)        TCG DICE Hardware Requirements
 *                                              rev 78, section 5, form (2)
 *   evidence = Version 1 | Nonce | DeviceID | FWHash | BootCount 0 (LE)
 *              | UptimeMs (LE)                 bytes [0, 73)
 *   MAC      = HMAC-SHA256(CDI, SHA-256(evidence[0, 73)))
 *   frame    = evidence | AlgID 0x10 | MAC | 32 zero | 26 zero reserved
 *
 * AlgID sits at 73, outside the signed span. A failure here is a break in the
 * derivation or the wire, not a literal to update.
 */

#include <string.h>
#include "unity.h"
#include "attest.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "sha256.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/boot.h"
#include "uds_mock.h"
#include "handoff.h"
#include "platform_id.h"
#include "boot_platform.h"
#include "boot_measure.h"

/* "tfw dice vector UDS, not a key!!" */
static const uint8_t kUds[32] = {
    0x74, 0x66, 0x77, 0x20, 0x64, 0x69, 0x63, 0x65, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x20,
    0x55, 0x44, 0x53, 0x2c, 0x20, 0x6e, 0x6f, 0x74, 0x20, 0x61, 0x20, 0x6b, 0x65, 0x79, 0x21, 0x21,
};

/* "tfw dice vector application image" */
static const uint8_t kCode[] = {
    0x74, 0x66, 0x77, 0x20, 0x64, 0x69, 0x63, 0x65, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x20,
    0x61, 0x70, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x69, 0x6d, 0x61, 0x67,
    0x65,
};

static const uint8_t kFwHash[32] = {
    0x08, 0x69, 0x32, 0x86, 0x88, 0xbd, 0x00, 0xda, 0xad, 0xd3, 0x1c, 0xfb, 0xfc, 0x2b, 0x09, 0x4f,
    0xa2, 0x56, 0xac, 0x0c, 0x6d, 0x35, 0xb6, 0xde, 0x3f, 0xf9, 0x66, 0xca, 0x59, 0x55, 0x69, 0x53,
};

static const uint8_t kCdi[32] = {
    0x41, 0x5d, 0x76, 0xf2, 0xa3, 0x54, 0x1d, 0x16, 0x59, 0xa7, 0x98, 0x7a, 0xc1, 0x3a, 0x31, 0xd6,
    0x36, 0xb6, 0xb4, 0xda, 0xa1, 0x4c, 0xb1, 0xe6, 0xed, 0x48, 0xf4, 0xae, 0xf4, 0x24, 0x0c, 0x78,
};

/* "TFW-DICE-VEC-001" */
static const uint8_t kSerial[TFW_ATTEST_ID_LEN] = {
    0x54, 0x46, 0x57, 0x2d, 0x44, 0x49, 0x43, 0x45, 0x2d, 0x56, 0x45, 0x43, 0x2d, 0x30, 0x30, 0x31,
};

static const uint8_t kNonce[TFW_ATTEST_NONCE_LEN] = {
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
};

#define UPTIME_MS 0x01020304u

static const uint8_t kEvidence[TFW_ATTEST_LEN] = {
    0x01, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce,
    0xcf, 0x54, 0x46, 0x57, 0x2d, 0x44, 0x49, 0x43, 0x45, 0x2d, 0x56, 0x45, 0x43, 0x2d, 0x30, 0x30,
    0x31, 0x08, 0x69, 0x32, 0x86, 0x88, 0xbd, 0x00, 0xda, 0xad, 0xd3, 0x1c, 0xfb, 0xfc, 0x2b, 0x09,
    0x4f, 0xa2, 0x56, 0xac, 0x0c, 0x6d, 0x35, 0xb6, 0xde, 0x3f, 0xf9, 0x66, 0xca, 0x59, 0x55, 0x69,
    0x53, 0x00, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01, 0x10, 0x0e, 0x5e, 0x3d, 0x46, 0x0a, 0x7f,
    0x09, 0x81, 0xee, 0x86, 0x1d, 0xe5, 0xd2, 0x27, 0x99, 0x4f, 0x22, 0xae, 0x34, 0xf1, 0xda, 0xa9,
    0x04, 0xaa, 0xea, 0x99, 0xe9, 0x54, 0xc8, 0xdd, 0x56, 0xbb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

static uint8_t      inbuf[TFW_MAX_PAYLOAD];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)
{
    tfw_mock_reset();
    tfw_attest_reset_state();
    tfw_mock_set_uds(0);            /* the CDI comes from the handoff alone */
}

void tearDown(void) {}

static tfw_status_t run(const uint8_t *p, uint8_t n)
{
    memcpy(inbuf, p, n);
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(TFW_CMD_ATTEST, &r, &w);
}

/* What tfw-boot leaves behind on a soft root: the platform serial in EEPROM
 * and a SOFTWARE-tier handoff carrying the CDI and the measurement. */
static void boot_soft_root(void)
{
    tfw_boot_platform_t plat;
    uint8_t             raw[TFW_BOOT_EE_LEN];
    uint8_t             block[TFW_BOOT_HANDOFF_LEN];

    memset(&plat, 0, sizeof plat);
    memcpy(plat.serial, kSerial, sizeof plat.serial);
    memset(raw, 0, sizeof raw);
    tfw_boot_platform_serialise(&plat, raw);
    memcpy(tfw_mock_eeprom(), raw, TFW_BOOT_EE_LEN);
    tfw_platform_id_init();

    tfw_boot_handoff_build(block, TFW_CRYPTO_SOFTWARE, kCdi, kFwHash, 1u, 0u);
    memcpy(tfw_mock_handoff(), block, TFW_BOOT_HANDOFF_LEN);
    tfw_boot_handoff_init();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());

    tfw_mock_set_millis(UPTIME_MS);
}

static void whole_frame(uint8_t *whole)
{
    uint8_t got;
    uint8_t page;

    TEST_ASSERT_EQUAL(TFW_OK, run(kNonce, TFW_ATTEST_NONCE_LEN));
    memcpy(whole, &outbuf[2], (size_t)(tfw_writer_len(&w) - 2u));
    got = (uint8_t)(tfw_writer_len(&w) - 2u);
    for (page = 1u; got < TFW_ATTEST_LEN; page++) {
        TEST_ASSERT_EQUAL(TFW_OK, run(&page, 1u));
        memcpy(&whole[got], &outbuf[2], (size_t)(tfw_writer_len(&w) - 2u));
        got = (uint8_t)(got + (tfw_writer_len(&w) - 2u));
    }
    TEST_ASSERT_EQUAL_UINT8(TFW_ATTEST_LEN, got);
}

/* The inputs first: if SHA-256 of the image is not the literal, nothing below
 * means anything. */
static void test_the_measurement_is_sha256_of_the_image(void)
{
    uint8_t digest[32];
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256(kCode, (uint16_t)sizeof kCode, digest));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kFwHash, digest, sizeof digest);
}

/* DICE Hardware Requirements rev 78, section 5, form (2): the Layer 0 CDI is
 * HMAC(UDS, H(code)). The primitive both tfw-boot tiers derive with. */
static void test_the_cdi_is_hmac_of_the_uds_over_the_measurement(void)
{
    uint8_t cdi[32];
    tfw_hmac_sha256(kUds, (uint8_t)sizeof kUds, kFwHash, (uint16_t)sizeof kFwHash, cdi);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kCdi, cdi, sizeof cdi);
}

/* THE HEADLINE. Every byte of the soft root's evidence, across the pages,
 * equals the independently computed frame. */
static void test_the_soft_root_evidence_frame_matches_the_literal(void)
{
    uint8_t whole[TFW_ATTEST_LEN];

    boot_soft_root();
    whole_frame(whole);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kEvidence, whole, TFW_ATTEST_LEN);
}

/* The algorithm names the root: an HMAC-SHA256 soft identity, never a code a
 * verifier would read as an asymmetric signature. */
static void test_the_soft_root_names_hmac_sha256(void)
{
    uint8_t whole[TFW_ATTEST_LEN];

    boot_soft_root();
    whole_frame(whole);
    TEST_ASSERT_EQUAL_HEX8(TFW_ALG_HMAC_SHA256, whole[73]);
}

/* The handoff is authoritative: a soft root with a CDI from tfw-boot signs
 * with that CDI and no other. One flipped CDI bit changes the MAC and nothing
 * outside it. */
static void test_another_cdi_changes_only_the_mac(void)
{
    uint8_t whole[TFW_ATTEST_LEN];
    uint8_t cdi[32];
    uint8_t block[TFW_BOOT_HANDOFF_LEN];

    boot_soft_root();
    memcpy(cdi, kCdi, sizeof cdi);
    cdi[0] ^= 0x01u;
    tfw_boot_handoff_build(block, TFW_CRYPTO_SOFTWARE, cdi, kFwHash, 1u, 0u);
    memcpy(tfw_mock_handoff(), block, TFW_BOOT_HANDOFF_LEN);
    tfw_boot_handoff_init();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    tfw_attest_reset_state();

    whole_frame(whole);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kEvidence, whole, 74u);
    TEST_ASSERT_FALSE(memcmp(&kEvidence[74], &whole[74], 32u) == 0);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&kEvidence[106], &whole[106], TFW_ATTEST_LEN - 106u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_measurement_is_sha256_of_the_image);
    RUN_TEST(test_the_cdi_is_hmac_of_the_uds_over_the_measurement);
    RUN_TEST(test_the_soft_root_evidence_frame_matches_the_literal);
    RUN_TEST(test_the_soft_root_names_hmac_sha256);
    RUN_TEST(test_another_cdi_changes_only_the_mac);
    return UNITY_END();
}
