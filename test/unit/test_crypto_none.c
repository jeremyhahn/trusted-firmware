/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The unattested tier: every operation refuses, and none of them
 * approximates -- see the file header of src/crypto/none/provider.c for why
 * this tier exists at all.
 */

#include <string.h>
#include "unity.h"
#include "ag/crypto.h"
#include "ag/protocol.h"
#include "ag/boot.h"
#include "hal_mock.h"
#include "handoff.h"
#include "boot_measure.h"

void setUp(void) { ag_mock_reset(); }
void tearDown(void) {}

static void test_reports_the_unattested_tier(void)
{
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL(AG_CRYPTO_NONE, ag_crypto_tier());
}

static void test_the_algorithm_bitmap_is_empty(void)
{
    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL_UINT16(0u, ag_crypto_algorithms());
}

static void test_every_operation_refuses(void)
{
    uint8_t out[64];
    uint8_t alg;

    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL(AG_ERR_CRYPTO, ag_crypto_random(out, 16u));
    TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED, ag_crypto_sha256(out, 16u, out));
    TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED, ag_crypto_hmac(0u, out, 16u, out));
    TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED, ag_crypto_sign(out, out, &alg));
    TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED, ag_crypto_pubkey(out));
    {
        uint32_t n;
        TEST_ASSERT_EQUAL(AG_ERR_UNSUPPORTED, ag_crypto_counter(0u, &n));
    }
}

/*
 * A CDI ag-boot handed off must not sit resident in src/core/handoff.c for
 * the process lifetime, even on a tier that signs with nothing and derives
 * nothing itself -- the "consumed once" contract include/ag/boot.h describes
 * applies to every provider, not only the two that have a use for the value.
 * Provable from outside that module: ag_boot_handoff_take_cdi returns 0
 * (nothing left to take) after ag_crypto_init has run.
 */
static void test_init_drains_a_handed_off_cdi_even_though_this_tier_signs_with_nothing(void)
{
    uint8_t block[AG_BOOT_HANDOFF_LEN];
    uint8_t cdi[32], measurement[32], taken[32];
    uint8_t i;

    for (i = 0u; i < sizeof cdi; i++)         { cdi[i] = (uint8_t)(0x33u + i); }
    for (i = 0u; i < sizeof measurement; i++) { measurement[i] = (uint8_t)i; }
    ag_boot_handoff_build(block, AG_CRYPTO_SOFTWARE, cdi, measurement, 1u, 0u);
    memcpy(ag_mock_handoff(), block, AG_BOOT_HANDOFF_LEN);
    ag_boot_handoff_init();

    TEST_ASSERT_EQUAL(AG_OK, ag_crypto_init());
    TEST_ASSERT_EQUAL_UINT8(0u, ag_boot_handoff_take_cdi(taken));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reports_the_unattested_tier);
    RUN_TEST(test_the_algorithm_bitmap_is_empty);
    RUN_TEST(test_every_operation_refuses);
    RUN_TEST(test_init_drains_a_handed_off_cdi_even_though_this_tier_signs_with_nothing);
    return UNITY_END();
}
