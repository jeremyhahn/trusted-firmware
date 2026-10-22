/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * TCG DICE HW Req 6.4 on the soft boot tier: once boot/tier/cdi_soft.c has
 * derived the CDI, no copy of it, of the UDS, or of anything the CDI can be
 * computed back from is left in the frames below its caller.
 *
 * Linked with the UDS slot patched as in test_boot_cdi_soft_uds, with
 * src/crypto/sha256_small.c (the boot's HMAC), and with test/mock/
 * cdi_residue.c in place of src/core/secure_zero.c, so every clear is
 * recorded. Every literal below was computed with Python's hashlib and hmac
 * and a SHA-256 written out in Python -- not with the code under test.
 */
#include <string.h>
#include "unity.h"
#include "boot_cdi.h"
#include "boot_hal_mock.h"
#include "cdi_residue.h"
#include "tfw/types.h"

void setUp(void)    { tfw_boot_mock_reset(); tfw_residue_reset(); }
void tearDown(void) {}

/* The UDS the test patches in (mk/boot.mk CDI_SOFT_TEST_UDS): 32 bytes, and
 * the terminator the string literal brings, which is not part of it. */
static const char kUds[33] = "cdi_soft.c TEST UDS, not a key!!";

/* HMAC-SHA256(kUds, 0x20..0x3F): the CDI, as bytes and as the eight state
 * words a SHA-256 context holds it in. */
static const uint8_t kCdi[TFW_BOOT_MEASURE_LEN] = {
    0x09, 0xe1, 0xf8, 0xc5, 0x62, 0x30, 0xb9, 0xc6, 0xdb, 0xbb, 0xa5, 0xdb, 0xde, 0x7e, 0x0f, 0x27,
    0xd3, 0x67, 0x9e, 0x52, 0x6f, 0x59, 0xdf, 0x65, 0x5a, 0x77, 0x0d, 0x07, 0x04, 0x20, 0xc7, 0x9c,
};
static const uint32_t kCdiWords[8] = {
    0x09e1f8c5u, 0x6230b9c6u, 0xdbbba5dbu, 0xde7e0f27u,
    0xd3679e52u, 0x6f59df65u, 0x5a770d07u, 0x0420c79cu,
};

/* SHA-256((kUds || 0^32) ^ ipad || 0x20..0x3F), the inner digest. */
static const uint8_t kInner[32] = {
    0x7c, 0xdc, 0xc5, 0xd6, 0x33, 0x18, 0x44, 0x9f, 0x08, 0x58, 0xcd, 0x54, 0x07, 0xee, 0x21, 0x1c,
    0xa8, 0xc2, 0x88, 0x93, 0x57, 0x20, 0xea, 0x4c, 0x66, 0x67, 0x05, 0xde, 0x01, 0xd3, 0xba, 0x13,
};

/* The first half of (kUds || 0^32) ^ opad: what the HMAC's pad holds last. The
 * UDS behind a one-byte XOR, so as much the UDS as kUds itself. */
static const uint8_t kOpadUds[32] = {
    0x3f, 0x38, 0x35, 0x03, 0x2f, 0x33, 0x3a, 0x28, 0x72, 0x3f, 0x7c, 0x08, 0x19, 0x0f, 0x08, 0x7c,
    0x09, 0x18, 0x0f, 0x70, 0x7c, 0x32, 0x33, 0x28, 0x7c, 0x3d, 0x7c, 0x37, 0x39, 0x25, 0x7d, 0x7d,
};

/* The outer hash's last compression: the schedule words W[48..63] as the
 * sixteen-word ring holds them, and the working variables a..h after round
 * 63. Run the rounds backwards from these two and the block's starting state
 * falls out; add v and that is the CDI. */
static const uint32_t kW[16] = {
    0x8df96d05u, 0xed595266u, 0xb700593eu, 0x325233e9u, 0xccc8d5e8u, 0x01e8b467u, 0xc1b79666u, 0x0be05bcfu,
    0x5fe51deeu, 0x422d9e31u, 0x8139f116u, 0x2ab48b2cu, 0xe9fccd8du, 0x9df82ce9u, 0x2375d798u, 0x42d1cf16u,
};
static const uint32_t kV[8] = {
    0x51c7f5cbu, 0x358bfb5fu, 0xd9a2c3ffu, 0x2f984c9eu, 0xbdff379au, 0x4de6d799u, 0x7200ab70u, 0xef08f09cu,
};

static void measurement(uint8_t m[TFW_BOOT_MEASURE_LEN])
{
    uint8_t i;

    for (i = 0u; i < TFW_BOOT_MEASURE_LEN; i++) {
        m[i] = (uint8_t)(0x20u + i);
    }
}

/* What the stack under the caller still holds once the derive has returned. */
static void test_the_derive_leaves_no_secret_on_the_stack(void)
{
    uint8_t      m[TFW_BOOT_MEASURE_LEN], cdi[TFW_BOOT_MEASURE_LEN];
    tfw_status_t st;

    measurement(m);
    st = tfw_boot_cdi_derive(m, cdi);
    tfw_residue_snapshot();

    TEST_ASSERT_EQUAL(TFW_OK, st);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kCdi, cdi, sizeof cdi);
    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kCdi, sizeof kCdi), "CDI bytes");
    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kCdiWords, sizeof kCdiWords), "CDI state words");
    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kUds, 32u), "UDS");
    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kOpadUds, sizeof kOpadUds), "outer pad");
    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kInner, sizeof kInner), "inner digest");
    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kW, sizeof kW), "last schedule");
    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kV, sizeof kV), "last working variables");
}

/* Each of those was there, and a tfw_secure_zero took it: the stack test above
 * is not passing because the secret was never written. */
static void test_every_secret_the_derive_held_was_cleared(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN], cdi[TFW_BOOT_MEASURE_LEN];

    measurement(m);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_cdi_derive(m, cdi));
    /* Four compressions (two blocks each for the inner and the outer hash),
     * the HMAC's state, then the UDS copy last. The HMAC's clear holds the UDS
     * too, so only the order tells the copy's own clear from it. In the
     * sanitizer build the derive's frames lie outside the window the stack
     * scan reads, and without the copy's clear only this half fails. */
    TEST_ASSERT_EQUAL_UINT_MESSAGE(6u, tfw_residue_zero_calls(), "clears");
    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_last_cleared(0u, 32u, kUds, 32u), "the UDS copy, last");
    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_cleared(kUds, 32u), "UDS");
    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_cleared(kOpadUds, sizeof kOpadUds), "outer pad");
    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_cleared(kCdiWords, sizeof kCdiWords), "CDI state words");
    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_cleared(kInner, sizeof kInner), "inner digest");
    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_cleared(kW, sizeof kW), "last schedule");
    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_cleared(kV, sizeof kV), "last working variables");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_derive_leaves_no_secret_on_the_stack);
    RUN_TEST(test_every_secret_the_derive_held_was_cleared);
    return UNITY_END();
}
