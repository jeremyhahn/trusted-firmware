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

void setUp(void) { ag_boot_mock_reset(); }
void tearDown(void) {}

static void test_measures_the_same_digest_as_a_direct_hash(void)
{
    uint8_t *flash = ag_boot_mock_flash();
    uint8_t  got[32], want[32];

    for (uint16_t i = 0u; i < 1000u; i++) {
        flash[i] = (uint8_t)(i * 7u + 3u);
    }
    TEST_ASSERT_EQUAL_INT(AG_OK, ag_boot_measure(1000u, got));
    ag_sha256(flash, 1000u, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

/* The chunk size must not reach the digest. SHA-256 is defined over the byte
 * sequence, not over how it was delivered -- so a length that is not a multiple
 * of the 32-byte read chunk must hash identically to one that is. */
static void test_a_length_that_is_not_a_whole_chunk_hashes_correctly(void)
{
    uint8_t *flash = ag_boot_mock_flash();
    uint8_t  got[32], want[32];

    for (uint16_t i = 0u; i < 200u; i++) { flash[i] = (uint8_t)i; }

    TEST_ASSERT_EQUAL_INT(AG_OK, ag_boot_measure(201u, got));
    ag_sha256(flash, 201u, want);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
}

static void test_one_changed_byte_changes_the_measurement(void)
{
    uint8_t *flash = ag_boot_mock_flash();
    uint8_t  a[32], b[32];

    memset(flash, 0x5A, 512);
    TEST_ASSERT_EQUAL_INT(AG_OK, ag_boot_measure(512u, a));
    flash[300] ^= 0x01u;
    TEST_ASSERT_EQUAL_INT(AG_OK, ag_boot_measure(512u, b));
    TEST_ASSERT_FALSE(memcmp(a, b, 32) == 0);
}

static void test_a_zero_length_measurement_is_refused(void)
{
    uint8_t out[32];
    TEST_ASSERT_EQUAL_INT(AG_ERR_RANGE, ag_boot_measure(0u, out));
}

/* Past the trailer's address an image would measure its own signature. */
static void test_a_length_past_the_trailer_is_refused(void)
{
    uint8_t out[32];
    TEST_ASSERT_EQUAL_INT(AG_ERR_RANGE, ag_boot_measure(AG_BOOT_MAX_IMAGE_LEN + 1u, out));
    TEST_ASSERT_EQUAL_INT(AG_OK, ag_boot_measure(AG_BOOT_MAX_IMAGE_LEN, out));
}

/* ------------------------------------------------------------- handoff */

static void test_the_handoff_block_round_trips(void)
{
    uint8_t block[AG_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], meas[32];
    uint8_t got_cdi[32], got_meas[32], tier = 0u;
    uint32_t ver = 0u;

    memset(cdi, 0xC1, sizeof cdi);
    memset(meas, 0x7E, sizeof meas);
    ag_boot_handoff_build(block, AG_CRYPTO_SECURE_ELEMENT, cdi, meas, 9u);

    TEST_ASSERT_EQUAL_INT(AG_OK,
        ag_boot_handoff_parse(block, &tier, got_cdi, got_meas, &ver));
    TEST_ASSERT_EQUAL_UINT8(AG_CRYPTO_SECURE_ELEMENT, tier);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(cdi, got_cdi, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(meas, got_meas, 32);
    TEST_ASSERT_EQUAL_UINT32(9u, ver);
}

/* No bootloader ran, or one from a build that predates the handoff. The
 * application must refuse rather than read whatever SRAM held at reset. */
static void test_an_unwritten_block_is_not_a_handoff(void)
{
    uint8_t block[AG_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], meas[32], tier;
    uint32_t ver;

    memset(block, 0, sizeof block);
    TEST_ASSERT_EQUAL_INT(AG_ERR_NOT_PROVISIONED,
        ag_boot_handoff_parse(block, &tier, cdi, meas, &ver));
}

/*
 * A stack that grew into the block. There is no MPU on a classic AVR, so this
 * is the failure the CRC is actually for -- and the application must not derive
 * an identity from a CDI that something else has partly overwritten.
 */
static void test_a_block_overwritten_by_the_stack_is_caught(void)
{
    uint8_t block[AG_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], meas[32], tier;
    uint32_t ver;

    memset(cdi, 0xC1, sizeof cdi);
    memset(meas, 0x7E, sizeof meas);
    ag_boot_handoff_build(block, AG_CRYPTO_SOFTWARE, cdi, meas, 1u);
    block[AG_BOOT_HO_CDI + 5u] ^= 0x40u;

    TEST_ASSERT_EQUAL_INT(AG_ERR_CRC,
        ag_boot_handoff_parse(block, &tier, cdi, meas, &ver));
}

static void test_an_unknown_handoff_version_is_refused(void)
{
    uint8_t block[AG_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], meas[32], tier;
    uint32_t ver;

    memset(cdi, 1, sizeof cdi);
    memset(meas, 2, sizeof meas);
    ag_boot_handoff_build(block, AG_CRYPTO_SOFTWARE, cdi, meas, 1u);
    block[AG_BOOT_HO_VERSION] = AG_BOOT_HANDOFF_VERSION + 1u;

    TEST_ASSERT_EQUAL_INT(AG_ERR_UNSUPPORTED,
        ag_boot_handoff_parse(block, &tier, cdi, meas, &ver));
}

/* The application reports the tier the BOOTLOADER observed, and a host decides
 * what evidence is worth before it accepts any. Carrying it verbatim is what
 * makes that ordering mean anything. */
static void test_the_tier_is_carried_verbatim(void)
{
    uint8_t block[AG_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], meas[32], tier = 0xFFu;
    uint32_t ver;

    memset(cdi, 0, sizeof cdi);
    memset(meas, 0, sizeof meas);
    ag_boot_handoff_build(block, AG_CRYPTO_NONE, cdi, meas, 1u);
    TEST_ASSERT_EQUAL_INT(AG_OK,
        ag_boot_handoff_parse(block, &tier, cdi, meas, &ver));
    TEST_ASSERT_EQUAL_UINT8(AG_CRYPTO_NONE, tier);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_measures_the_same_digest_as_a_direct_hash);
    RUN_TEST(test_a_length_that_is_not_a_whole_chunk_hashes_correctly);
    RUN_TEST(test_one_changed_byte_changes_the_measurement);
    RUN_TEST(test_a_zero_length_measurement_is_refused);
    RUN_TEST(test_a_length_past_the_trailer_is_refused);
    RUN_TEST(test_the_handoff_block_round_trips);
    RUN_TEST(test_an_unwritten_block_is_not_a_handoff);
    RUN_TEST(test_a_block_overwritten_by_the_stack_is_caught);
    RUN_TEST(test_an_unknown_handoff_version_is_refused);
    RUN_TEST(test_the_tier_is_carried_verbatim);
    return UNITY_END();
}
