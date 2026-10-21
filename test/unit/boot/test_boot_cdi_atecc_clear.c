/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * TCG DICE HW Req 6.4 on the element boot tier: once boot/tier/cdi_atecc.c
 * has read the CDI from the element, the only copy on this side of the bus is
 * the caller's. Not in the driver's response buffer (src/crypto/atecc608a/
 * atecc.c, file-static), not in the frames below the caller.
 *
 * The software element runs on the same stack as the code under test, so its
 * own HMAC is computed in frames the stack check can see. This suite links
 * src/crypto/sha256_small.c, the boot's hash, which leaves nothing there, and
 * the model clears what it answered from; what is left is what the MCU code
 * left. test/mock/cdi_residue.c stands in for src/core/secure_zero.c and
 * records every clear. The literals were computed with Python's hashlib and
 * hmac and a SHA-256 written out in Python -- not with the code under test.
 */
#include <string.h>
#include "unity.h"
#include "boot_cdi.h"
#include "boot_hal_mock.h"
#include "atecc608a_model.h"
#include "cdi_residue.h"
#include "tfw/types.h"

void setUp(void)    { tfw_model_reset(); tfw_boot_mock_reset(); tfw_residue_reset(); }
void tearDown(void) {}

/* HMAC-SHA256(SHA-256("tfwsim-atecc608a TEST UDS v1"), 0x20..0x3F): the
 * model's UDS slot over the test measurement, as bytes and as state words. */
static const uint8_t kCdi[TFW_BOOT_MEASURE_LEN] = {
    0xae, 0x44, 0x0f, 0x23, 0xc0, 0xd4, 0x19, 0xdd, 0x86, 0x4c, 0x77, 0xd4, 0xa4, 0x91, 0xad, 0xff,
    0x93, 0x05, 0x2f, 0xca, 0x7a, 0x65, 0xae, 0x92, 0x88, 0xaf, 0x4d, 0x88, 0x71, 0x76, 0x24, 0x39,
};
static const uint32_t kCdiWords[8] = {
    0xae440f23u, 0xc0d419ddu, 0x864c77d4u, 0xa491adffu,
    0x93052fcau, 0x7a65ae92u, 0x88af4d88u, 0x71762439u,
};

/* The inner digest, and the outer hash's last schedule ring and working
 * variables, from which the CDI can be computed back. */
static const uint8_t kInner[32] = {
    0xa1, 0xe3, 0x96, 0x9d, 0xf2, 0x73, 0xe2, 0xcc, 0x81, 0xc0, 0xde, 0x61, 0xf1, 0x23, 0xcb, 0xb4,
    0x44, 0x28, 0x4c, 0xd6, 0x48, 0x79, 0x23, 0xed, 0x4f, 0x96, 0x09, 0x64, 0x6a, 0x4c, 0xc9, 0xe0,
};
static const uint32_t kW[16] = {
    0x976695c2u, 0x4b5898a6u, 0xe64aac83u, 0x82cda819u, 0xc891df85u, 0x8d84937bu, 0xabbf350au, 0x644f7793u,
    0xe411deb5u, 0x5b897abau, 0x8c837b12u, 0x1d62c113u, 0xc2188d1cu, 0xefadbc09u, 0xb98c22dcu, 0x76acaa31u,
};
static const uint32_t kV[8] = {
    0xde53a0e6u, 0xdf7a01c8u, 0xbfd68d60u, 0xea1dbd31u, 0x1c6ce431u, 0xa92327efu, 0x7be7ac3fu, 0xbd432708u,
};

static void measurement(uint8_t m[TFW_BOOT_MEASURE_LEN])
{
    uint8_t i;

    for (i = 0u; i < TFW_BOOT_MEASURE_LEN; i++) {
        m[i] = (uint8_t)(0x20u + i);
    }
}

static void element_present(void)
{
    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_init());
}

static void test_the_driver_buffer_holds_no_cdi_after_the_derive(void)
{
    uint8_t        m[TFW_BOOT_MEASURE_LEN], cdi[TFW_BOOT_MEASURE_LEN];
    const uint8_t *rd;
    uint8_t        len = 0u;
    uint8_t        i;

    element_present();
    measurement(m);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_derive(m, cdi));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kCdi, cdi, sizeof cdi);

    /* The last data response read was HMAC END's: count, CDI, CRC. */
    rd = tfw_model_last_data_read(&len);
    TEST_ASSERT_NOT_NULL(rd);
    TEST_ASSERT_EQUAL_UINT8(35u, len);
    for (i = 0u; i < len; i++) {
        TEST_ASSERT_EQUAL_HEX8(0u, rd[i]);
    }
    /* And it held the CDI until a tfw_secure_zero took it. */
    TEST_ASSERT_TRUE(tfw_residue_cleared(kCdi, sizeof kCdi));
}

static void test_the_derive_leaves_no_cdi_on_the_stack(void)
{
    uint8_t      m[TFW_BOOT_MEASURE_LEN], cdi[TFW_BOOT_MEASURE_LEN];
    tfw_status_t st;

    element_present();
    measurement(m);
    st = tfw_boot_cdi_derive(m, cdi);
    tfw_residue_snapshot();

    TEST_ASSERT_EQUAL(TFW_OK, st);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kCdi, cdi, sizeof cdi);
    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kCdi, sizeof kCdi), "CDI bytes");
    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kCdiWords, sizeof kCdiWords), "CDI state words");
    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kInner, sizeof kInner), "inner digest");
    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kW, sizeof kW), "last schedule");
    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kV, sizeof kV), "last working variables");
}

/* A HMAC END the element refuses leaves nothing either: the driver clears the
 * buffer on that path too, and there is no CDI to find. */
static void test_a_refused_derive_leaves_a_clear_buffer(void)
{
    uint8_t        m[TFW_BOOT_MEASURE_LEN], cdi[TFW_BOOT_MEASURE_LEN];
    const uint8_t *rd;
    uint8_t        len = 0u;

    element_present();
    measurement(m);
    tfw_model_corrupt_next_read(5u, 0x01u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_boot_cdi_derive(m, cdi));
    rd = tfw_model_last_data_read(&len);
    TEST_ASSERT_NOT_NULL(rd);
    TEST_ASSERT_FALSE(tfw_residue_in(rd, len, kCdi, sizeof kCdi));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_driver_buffer_holds_no_cdi_after_the_derive);
    RUN_TEST(test_the_derive_leaves_no_cdi_on_the_stack);
    RUN_TEST(test_a_refused_derive_leaves_a_clear_buffer);
    return UNITY_END();
}
