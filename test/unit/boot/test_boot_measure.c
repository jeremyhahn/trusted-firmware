/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The measurement and the CDI handoff block.
 *
 * The measurement is FWHash: signed evidence a verifier must be able to compute
 * from source. So the property asserted here is not "it returns something
 * stable" -- it is that it returns the SAME digest as a plain SHA-256 over the
 * same bytes, computed a different way. A measurement that were merely
 * self-consistent would produce devices whose evidence no verifier can check.
 */

#include <string.h>
#include "unity.h"
#include "boot_measure.h"
#include "boot_hal_mock.h"
#include "sha256.h"

void setUp(void) { tfw_boot_mock_reset(); }
void tearDown(void) {}

/* The chunk size must not reach the digest. SHA-256 is defined over the byte
 * sequence, not over how it was delivered -- so 1000, which is not a multiple
 * of the 32-byte read chunk, must hash as a plain SHA-256 does. */
static void test_measures_the_same_digest_as_a_direct_hash(void)
{
    uint8_t *flash = tfw_boot_mock_flash();
    uint8_t  got[32], want[32];

    for (uint16_t i = 0u; i < 1000u; i++) {
        flash[i] = (uint8_t)(i * 7u + 3u);
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(1000u, got));
    tfw_sha256(flash, 1000u, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

/* The expected firmware hash is computable from source: scripts/fwhash.py
 * computes it from the built Intel HEX. This image and this literal are
 * scripts/test_fwhash.py's FIXED_HEX and FIXED_MEASUREMENT, so the host tool
 * and the bootloader are pinned to one number. The gap is never written:
 * erased flash, 0xFF, which is what the tool fills a gap with. */
static void test_the_fixed_image_measures_to_the_literal_fwhash_pins(void)
{
    static const uint8_t want[32] = {
        0x12,0x1f,0x2e,0xcd,0xba,0x08,0xaf,0x29,0x28,0x14,0xf0,0xc4,0xdc,0x75,0x38,0x86,
        0x23,0x0f,0x35,0x66,0x4a,0x3e,0x14,0x73,0x17,0xb7,0x80,0x5c,0x9a,0x0d,0xa3,0x30,
    };
    uint8_t *flash = tfw_boot_mock_flash();
    uint8_t  got[32];

    for (uint16_t i = 0u; i < 0x10u; i++) { flash[i] = (uint8_t)i; }
    for (uint16_t i = 0u; i < 0x20u; i++) { flash[0x100u + i] = (uint8_t)(i ^ 0x5Au); }

    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(0x120u, got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

static void test_one_changed_byte_changes_the_measurement(void)
{
    uint8_t *flash = tfw_boot_mock_flash();
    uint8_t  a[32], b[32];

    memset(flash, 0x5A, 512);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(512u, a));
    flash[300] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(512u, b));
    TEST_ASSERT_FALSE(memcmp(a, b, 32) == 0);
}

static void test_a_zero_length_measurement_is_refused(void)
{
    uint8_t out[32];
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_measure(0u, out));
}

/* Past the trailer's address an image would measure its own signature. */
static void test_a_length_past_the_trailer_is_refused(void)
{
    uint8_t out[32];
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_boot_measure(TFW_BOOT_MAX_IMAGE_LEN + 1u, out));
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_measure(TFW_BOOT_MAX_IMAGE_LEN, out));
}

/* ------------------------------------------------------------- handoff */

/* Parses into sentinel-filled outputs and checks a refusal wrote none of
 * them. Returns the parser's status. */
/* Where every parse in this suite puts the device_seed field. */
static uint8_t seed_out[TFW_BOOT_SEED_LEN];

static tfw_status_t parse_untouched(const uint8_t *block, uint8_t *tier,
                                    uint8_t cdi[32], uint8_t meas[32],
                                    uint32_t *ver, uint8_t *cause)
{
    uint8_t      sentinel[32];
    uint32_t     ver_sentinel;
    tfw_status_t st;

    memset(sentinel, 0xA5, sizeof sentinel);
    memset(&ver_sentinel, 0xA5, sizeof ver_sentinel);
    *tier  = 0xA5u;
    *cause = 0xA5u;
    *ver   = ver_sentinel;
    memcpy(cdi, sentinel, sizeof sentinel);
    memcpy(meas, sentinel, sizeof sentinel);
    memcpy(seed_out, sentinel, sizeof sentinel);
    st = tfw_boot_handoff_parse(block, tier, cdi, meas, ver, cause, seed_out);
    TEST_ASSERT_NOT_EQUAL_INT(TFW_OK, st);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, *tier);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, *cause);
    TEST_ASSERT_EQUAL_HEX32(ver_sentinel, *ver);
    TEST_ASSERT_EQUAL_MEMORY(sentinel, cdi, sizeof sentinel);
    TEST_ASSERT_EQUAL_MEMORY(sentinel, meas, sizeof sentinel);
    TEST_ASSERT_EQUAL_MEMORY(sentinel, seed_out, sizeof sentinel);
    return st;
}

static void test_the_handoff_block_round_trips(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], meas[32];
    uint8_t got_cdi[32], got_meas[32], tier = 0u, cause = 0xFFu;
    uint32_t ver = 0u;

    memset(cdi, 0xC1, sizeof cdi);
    memset(meas, 0x7E, sizeof meas);
    tfw_boot_handoff_build(block, TFW_CRYPTO_SECURE_ELEMENT, cdi, meas, 9u, TFW_RESET_UNKNOWN);

    TEST_ASSERT_EQUAL_INT(TFW_OK,
        tfw_boot_handoff_parse(block, &tier, got_cdi, got_meas, &ver, &cause, seed_out));
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_SECURE_ELEMENT, tier);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(cdi, got_cdi, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(meas, got_meas, 32);
    TEST_ASSERT_EQUAL_UINT32(9u, ver);
    TEST_ASSERT_EQUAL_UINT8(0x00u, cause);
}

/*
 * The reset cause round-trips too, and it is covered by the CRC -- issue #18.
 * What crosses is the abstract tfw_reset_cause_t, as its wire value -- the
 * literal 3, not the enum's name, so a renumbering fails here.
 */
static void test_the_reset_cause_round_trips_and_is_crc_covered(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], meas[32];
    uint8_t got_cdi[32], got_meas[32], tier, cause;
    uint32_t ver;

    memset(cdi, 0xAA, sizeof cdi);
    memset(meas, 0x55, sizeof meas);
    tfw_boot_handoff_build(block, TFW_CRYPTO_SOFTWARE, cdi, meas, 3u, TFW_RESET_BROWNOUT);

    TEST_ASSERT_EQUAL_UINT8(3u, block[TFW_BOOT_HO_RESET_CAUSE]);
    TEST_ASSERT_EQUAL_INT(TFW_OK,
        tfw_boot_handoff_parse(block, &tier, got_cdi, got_meas, &ver, &cause, seed_out));
    TEST_ASSERT_EQUAL_UINT8(3u, cause);

    /* Corrupting the reset cause byte must be caught: version 2 widened CRC
     * coverage to include it precisely so a stack that grew one byte further
     * into the block does not hand the application a cause nobody observed. */
    block[TFW_BOOT_HO_RESET_CAUSE] ^= 0xFFu;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRC,
        parse_untouched(block, &tier, got_cdi, got_meas, &ver, &cause));
}

/* The DeviceID seed (#173): build leaves it zero ("none"), set_seed writes it
 * at offset 80 and re-seals the CRC, and a seed corrupted after the seal is
 * caught rather than handed to the application as some other device's key. */
static void test_the_device_seed_round_trips_and_is_crc_covered(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], meas[32], seed[TFW_BOOT_SEED_LEN], zero[TFW_BOOT_SEED_LEN];
    uint8_t got_cdi[32], got_meas[32], tier, cause;
    uint32_t ver;

    memset(cdi, 0xAA, sizeof cdi);
    memset(meas, 0x55, sizeof meas);
    memset(seed, 0x5E, sizeof seed);
    memset(zero, 0x00, sizeof zero);
    tfw_boot_handoff_build(block, TFW_CRYPTO_SOFTWARE, cdi, meas, 3u, TFW_RESET_UNKNOWN);
    TEST_ASSERT_EQUAL_INT(TFW_OK,
        tfw_boot_handoff_parse(block, &tier, got_cdi, got_meas, &ver, &cause, seed_out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(zero, seed_out, sizeof zero);

    tfw_boot_handoff_set_seed(block, seed);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(seed, &block[80], sizeof seed);
    TEST_ASSERT_EQUAL_INT(TFW_OK,
        tfw_boot_handoff_parse(block, &tier, got_cdi, got_meas, &ver, &cause, seed_out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(seed, seed_out, sizeof seed);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(cdi, got_cdi, 32);

    block[80 + TFW_BOOT_SEED_LEN - 1u] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRC,
        parse_untouched(block, &tier, got_cdi, got_meas, &ver, &cause));
}

/* No bootloader ran, or one from a build that predates the handoff. The
 * application must refuse rather than read whatever SRAM held at reset. */
static void test_an_unwritten_block_is_not_a_handoff(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], meas[32], tier, cause;
    uint32_t ver;

    memset(block, 0, sizeof block);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_NOT_PROVISIONED,
        parse_untouched(block, &tier, cdi, meas, &ver, &cause));
}

/*
 * A stack that grew into the block. There is no MPU on a classic AVR, so this
 * is the failure the CRC is actually for -- and the application must not derive
 * an identity from a CDI that something else has partly overwritten.
 */
static void test_a_block_overwritten_by_the_stack_is_caught(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], meas[32], tier, cause;
    uint32_t ver;

    memset(cdi, 0xC1, sizeof cdi);
    memset(meas, 0x7E, sizeof meas);
    tfw_boot_handoff_build(block, TFW_CRYPTO_SOFTWARE, cdi, meas, 1u, 0u);
    block[TFW_BOOT_HO_CDI + 5u] ^= 0x40u;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRC,
        parse_untouched(block, &tier, cdi, meas, &ver, &cause));
}

static void test_an_unknown_handoff_version_is_refused(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], meas[32], tier, cause;
    uint32_t ver;

    memset(cdi, 1, sizeof cdi);
    memset(meas, 2, sizeof meas);
    tfw_boot_handoff_build(block, TFW_CRYPTO_SOFTWARE, cdi, meas, 1u, 0u);
    block[TFW_BOOT_HO_VERSION] = TFW_BOOT_HANDOFF_VERSION + 1u;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED,
        parse_untouched(block, &tier, cdi, meas, &ver, &cause));
}

/*
 * A block from a bootloader one version BEHIND this application -- version 1,
 * with reset_cause not yet defined. Refused the same way an unknown-FUTURE
 * version is: the application must not read offset 76 as reset_cause when the
 * bootloader that wrote it meant that byte as part of the version-1 CRC.
 */
static void test_an_older_handoff_version_is_refused_not_misread(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], meas[32], tier, cause;
    uint32_t ver;

    memset(cdi, 1, sizeof cdi);
    memset(meas, 2, sizeof meas);
    tfw_boot_handoff_build(block, TFW_CRYPTO_SOFTWARE, cdi, meas, 1u, 0u);
    block[TFW_BOOT_HO_VERSION] = 1u;

    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED,
        parse_untouched(block, &tier, cdi, meas, &ver, &cause));
}

/*
 * Version 2 put the RAW MCUSR at offset 76; version 3 puts the abstract enum
 * there. Same layout, different meaning, so the version byte is the only thing
 * standing between a version 2 watchdog (0x08) and an application reporting a
 * cause no host knows. It must be refused, never reinterpreted.
 */
static void test_a_version_2_raw_mcusr_block_is_refused_not_reinterpreted(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], meas[32], tier, cause;
    uint32_t ver;

    memset(cdi, 1, sizeof cdi);
    memset(meas, 2, sizeof meas);
    tfw_boot_handoff_build(block, TFW_CRYPTO_SOFTWARE, cdi, meas, 1u, TFW_RESET_UNKNOWN);
    block[TFW_BOOT_HO_VERSION] = 2u;
    block[TFW_BOOT_HO_RESET_CAUSE] = 0x08u;   /* WDRF, as v2 wrote it */

    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED,
        parse_untouched(block, &tier, cdi, meas, &ver, &cause));
}

/* The application reports the tier the BOOTLOADER observed, and a host decides
 * what evidence is worth before it accepts any. Carrying it verbatim is what
 * makes that ordering mean anything. */
static void test_the_tier_is_carried_verbatim(void)
{
    uint8_t block[TFW_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], meas[32], tier = 0xFFu, cause;
    uint32_t ver;

    memset(cdi, 0, sizeof cdi);
    memset(meas, 0, sizeof meas);
    tfw_boot_handoff_build(block, TFW_CRYPTO_NONE, cdi, meas, 1u, 0u);
    TEST_ASSERT_EQUAL_INT(TFW_OK,
        tfw_boot_handoff_parse(block, &tier, cdi, meas, &ver, &cause, seed_out));
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_NONE, tier);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_measures_the_same_digest_as_a_direct_hash);
    RUN_TEST(test_the_fixed_image_measures_to_the_literal_fwhash_pins);
    RUN_TEST(test_one_changed_byte_changes_the_measurement);
    RUN_TEST(test_a_zero_length_measurement_is_refused);
    RUN_TEST(test_a_length_past_the_trailer_is_refused);
    RUN_TEST(test_the_handoff_block_round_trips);
    RUN_TEST(test_the_reset_cause_round_trips_and_is_crc_covered);
    RUN_TEST(test_the_device_seed_round_trips_and_is_crc_covered);
    RUN_TEST(test_an_unwritten_block_is_not_a_handoff);
    RUN_TEST(test_a_block_overwritten_by_the_stack_is_caught);
    RUN_TEST(test_an_unknown_handoff_version_is_refused);
    RUN_TEST(test_an_older_handoff_version_is_refused_not_misread);
    RUN_TEST(test_a_version_2_raw_mcusr_block_is_refused_not_reinterpreted);
    RUN_TEST(test_the_tier_is_carried_verbatim);
    return UNITY_END();
}
