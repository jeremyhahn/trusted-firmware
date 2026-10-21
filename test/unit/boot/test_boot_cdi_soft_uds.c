/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * boot/tier/cdi_soft.c with its UDS slot patched after compilation, as the
 * provisioning jig patches the built image: mk/boot.mk compiles the source
 * with -fdata-sections and replaces .rodata.uds_slot with the 32 bytes of
 * CDI_SOFT_TEST_UDS below. Nothing in the source under test changes.
 */
#include <string.h>
#include "unity.h"
#include "boot_cdi.h"
#include "boot_hal_mock.h"
#include "tfw/types.h"

void setUp(void)    { tfw_boot_mock_reset(); }
void tearDown(void) {}

/* The measurement 0x20..0x3F, and HMAC-SHA256 keyed by the 32 ASCII bytes
 * "cdi_soft.c TEST UDS, not a key!!" over it, computed with Python's hmac
 * module -- not with the code under test. */
static const uint8_t kCdi[TFW_BOOT_MEASURE_LEN] = {
    0x09, 0xe1, 0xf8, 0xc5, 0x62, 0x30, 0xb9, 0xc6, 0xdb, 0xbb, 0xa5, 0xdb, 0xde, 0x7e, 0x0f, 0x27,
    0xd3, 0x67, 0x9e, 0x52, 0x6f, 0x59, 0xdf, 0x65, 0x5a, 0x77, 0x0d, 0x07, 0x04, 0x20, 0xc7, 0x9c,
};

static void measurement(uint8_t m[TFW_BOOT_MEASURE_LEN])
{
    uint8_t i;

    for (i = 0u; i < TFW_BOOT_MEASURE_LEN; i++) {
        m[i] = (uint8_t)(0x20u + i);
    }
}

static void test_a_provisioned_uds_derives_the_hmac(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN], cdi[TFW_BOOT_MEASURE_LEN];

    measurement(m);
    memset(cdi, 0, sizeof cdi);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_derive(m, cdi));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kCdi, cdi, sizeof cdi);
}

/* A different measurement is a different CDI: the measurement is in the key's
 * input, not ignored. */
static void test_another_measurement_derives_another_cdi(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN], cdi[TFW_BOOT_MEASURE_LEN];

    measurement(m);
    m[0] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_derive(m, cdi));
    TEST_ASSERT_TRUE(memcmp(kCdi, cdi, sizeof cdi) != 0);
}

/* HMAC-SHA256 keyed by the same UDS over "tfw-deviceid-v1"
 * (TFW_DEVICEID_LABEL), computed with Python's hmac module (#173). */
static const uint8_t kSeed[TFW_BOOT_SEED_LEN] = {
    0xe3, 0x73, 0x84, 0xd3, 0x34, 0x46, 0x91, 0x7e, 0x78, 0x7b, 0xad, 0xdd, 0xa1, 0x51, 0x5b, 0xcd,
    0x75, 0x29, 0x46, 0xae, 0x66, 0x13, 0x43, 0xdb, 0xd5, 0xc9, 0xe1, 0x80, 0x91, 0x4d, 0x1e, 0xc4,
};

/* The DeviceID seed tfw-boot hands the application in place of the UDS. */
static void test_a_provisioned_uds_derives_the_device_seed(void)
{
    uint8_t seed[TFW_BOOT_SEED_LEN];

    memset(seed, 0, sizeof seed);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_device_seed(seed));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kSeed, seed, sizeof seed);
}

/* The seed does not depend on the measurement: it names the device, not the
 * image, so an update keeps the DeviceID. */
static void test_the_device_seed_is_not_the_cdi(void)
{
    uint8_t seed[TFW_BOOT_SEED_LEN];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_device_seed(seed));
    TEST_ASSERT_TRUE(memcmp(kCdi, seed, sizeof seed) != 0);
}

/* Provisioning changes the CDI, not the tier or the floor. */
static void test_the_tier_and_floor_do_not_depend_on_the_uds(void)
{
    uint32_t v = 0xA5A5A5A5u;

    TEST_ASSERT_EQUAL_UINT8(TFW_CRYPTO_SOFTWARE, tfw_boot_cdi_tier());
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_floor_read(&v));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, v);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_boot_floor_write(v));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_provisioned_uds_derives_the_hmac);
    RUN_TEST(test_another_measurement_derives_another_cdi);
    RUN_TEST(test_a_provisioned_uds_derives_the_device_seed);
    RUN_TEST(test_the_device_seed_is_not_the_cdi);
    RUN_TEST(test_the_tier_and_floor_do_not_depend_on_the_uds);
    return UNITY_END();
}
