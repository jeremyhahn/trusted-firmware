/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/tier/cdi_soft.c as it is built: the UDS slot is the all-0xFF
 * placeholder the provisioning jig has not yet patched. test_boot_cdi_soft_uds
 * links the same source with the slot patched, the way the jig does it.
 */
#include <string.h>
#include "unity.h"
#include "boot_cdi.h"
#include "boot_hal_mock.h"
#include "tfw/types.h"

void setUp(void)    { tfw_boot_mock_reset(); }
void tearDown(void) {}

static void test_init_has_nothing_to_prepare(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
}

static void test_the_tier_is_software(void)
{
    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_SOFTWARE, tfw_boot_cdi_tier());
}

/* Erased flash is the one value the tier refuses as a UDS, and the CDI buffer
 * is left as the caller had it. */
static void test_an_unprovisioned_uds_derives_nothing(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN], cdi[TFW_BOOT_MEASURE_LEN], want[TFW_BOOT_MEASURE_LEN];

    memset(m, 0x5A, sizeof m);
    memset(cdi, 0xC3, sizeof cdi);
    memset(want, 0xC3, sizeof want);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_boot_cdi_derive(m, cdi));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, cdi, sizeof cdi);
}

/* No UDS, no seed: the handoff carries zeros, which the application reads as
 * "none" (#173). */
static void test_an_unprovisioned_uds_hands_off_a_zero_seed(void)
{
    uint8_t seed[TFW_BOOT_SEED_LEN], zero[TFW_BOOT_SEED_LEN];

    memset(seed, 0xC3, sizeof seed);
    memset(zero, 0, sizeof zero);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_boot_cdi_device_seed(seed));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, seed, sizeof seed);
}

/* No counter on this tier: both directions say so, and the read leaves the
 * caller's value alone rather than reporting a zero floor. */
static void test_the_floor_is_unsupported(void)
{
    uint32_t v = 0xDEADBEEFu;

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_floor_read(&v));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, v);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_floor_write(7u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_has_nothing_to_prepare);
    RUN_TEST(test_the_tier_is_software);
    RUN_TEST(test_an_unprovisioned_uds_derives_nothing);
    RUN_TEST(test_an_unprovisioned_uds_hands_off_a_zero_seed);
    RUN_TEST(test_the_floor_is_unsupported);
    return UNITY_END();
}
