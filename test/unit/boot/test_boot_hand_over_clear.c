/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * TCG DICE HW Req 6.4 at the hand-over: tfw_boot_hand_over (boot/boot_main.c)
 * puts the CDI in the handoff block once, at TFW_BOOT_HO_CDI, and leaves no
 * other copy -- its own `cdi` and `block` locals are cleared before the jump.
 *
 * Linked with boot_main.c as built, its main() renamed by objcopy so Unity's
 * is the program's; with the soft tier and its UDS slot patched as in
 * test_boot_cdi_soft_uds; with src/crypto/sha256_small.c, the boot's hash; and
 * with test/mock/cdi_residue.c in place of src/core/secure_zero.c, so every
 * clear is recorded. tfw_boot_evaluate is the test's: main() and the OTA
 * recovery call it, tfw_boot_hand_over does not. kCdi was computed with
 * Python's hmac and hashlib, not with the code under test.
 */
#include <string.h>
#include "unity.h"
#include "boot_hal_mock.h"
#include "boot_policy.h"
#include "boot_recovery.h"
#include "cdi_residue.h"
#include "tfw/boot.h"

static unsigned derives;
static unsigned derive_clears;
static int      uds_cleared_last;
static int      at_return[7];

void setUp(void)
{
    tfw_boot_mock_reset();
    tfw_residue_reset();
    derives          = 0u;
    derive_clears    = 0u;
    uds_cleared_last = 0;
    memset(at_return, 0, sizeof at_return);
}
void tearDown(void) {}

tfw_status_t tfw_boot_evaluate(uint32_t floor, tfw_boot_verdict_t *out)
{
    (void)floor;
    (void)out;
    TEST_FAIL_MESSAGE("tfw_boot_hand_over called tfw_boot_evaluate");
    return TFW_ERR_BAD_STATE;
}

/* HMAC-SHA256("cdi_soft.c TEST UDS, not a key!!", 0x20..0x3F): the CDI the
 * soft tier derives from the patched UDS and verdict()'s measurement. */
static const uint8_t kCdi[TFW_BOOT_MEASURE_LEN] = {
    0x09, 0xe1, 0xf8, 0xc5, 0x62, 0x30, 0xb9, 0xc6, 0xdb, 0xbb, 0xa5, 0xdb, 0xde, 0x7e, 0x0f, 0x27,
    0xd3, 0x67, 0x9e, 0x52, 0x6f, 0x59, 0xdf, 0x65, 0x5a, 0x77, 0x0d, 0x07, 0x04, 0x20, 0xc7, 0x9c,
};

/* HMAC-SHA256(that UDS, "tfw-deviceid-v1"): the DeviceID seed (#173). */
static const uint8_t kSeed[TFW_BOOT_SEED_LEN] = {
    0xe3, 0x73, 0x84, 0xd3, 0x34, 0x46, 0x91, 0x7e, 0x78, 0x7b, 0xad, 0xdd, 0xa1, 0x51, 0x5b, 0xcd,
    0x75, 0x29, 0x46, 0xae, 0x66, 0x13, 0x43, 0xdb, 0xd5, 0xc9, 0xe1, 0x80, 0x91, 0x4d, 0x1e, 0xc4,
};

/* The rest of what the derive held, as test_boot_cdi_soft_clear has them and
 * computed the same way. The UDS the test patches in (mk/boot.mk
 * CDI_SOFT_TEST_UDS): 32 bytes, and the terminator the literal brings. */
static const char kUds[33] = "cdi_soft.c TEST UDS, not a key!!";

/* kCdi as the eight state words a SHA-256 context holds it in. */
static const uint32_t kCdiWords[8] = {
    0x09e1f8c5u, 0x6230b9c6u, 0xdbbba5dbu, 0xde7e0f27u,
    0xd3679e52u, 0x6f59df65u, 0x5a770d07u, 0x0420c79cu,
};

/* SHA-256((kUds || 0^32) ^ ipad || 0x20..0x3F), the inner digest. */
static const uint8_t kInner[32] = {
    0x7c, 0xdc, 0xc5, 0xd6, 0x33, 0x18, 0x44, 0x9f, 0x08, 0x58, 0xcd, 0x54, 0x07, 0xee, 0x21, 0x1c,
    0xa8, 0xc2, 0x88, 0x93, 0x57, 0x20, 0xea, 0x4c, 0x66, 0x67, 0x05, 0xde, 0x01, 0xd3, 0xba, 0x13,
};

/* The first half of (kUds || 0^32) ^ opad: what the HMAC's pad holds last. */
static const uint8_t kOpadUds[32] = {
    0x3f, 0x38, 0x35, 0x03, 0x2f, 0x33, 0x3a, 0x28, 0x72, 0x3f, 0x7c, 0x08, 0x19, 0x0f, 0x08, 0x7c,
    0x09, 0x18, 0x0f, 0x70, 0x7c, 0x32, 0x33, 0x28, 0x7c, 0x3d, 0x7c, 0x37, 0x39, 0x25, 0x7d, 0x7d,
};

/* The outer hash's last compression: W[48..63] as the ring holds them, and
 * a..h after round 63. The rounds run backwards from these to the CDI. */
static const uint32_t kW[16] = {
    0x8df96d05u, 0xed595266u, 0xb700593eu, 0x325233e9u, 0xccc8d5e8u, 0x01e8b467u, 0xc1b79666u, 0x0be05bcfu,
    0x5fe51deeu, 0x422d9e31u, 0x8139f116u, 0x2ab48b2cu, 0xe9fccd8du, 0x9df82ce9u, 0x2375d798u, 0x42d1cf16u,
};
static const uint32_t kV[8] = {
    0x51c7f5cbu, 0x358bfb5fu, 0xd9a2c3ffu, 0x2f984c9eu, 0xbdff379au, 0x4de6d799u, 0x7200ab70u, 0xef08f09cu,
};

/* What the stack held the moment the derive returned, read before the
 * hand-over's next call reuses the derive's frame: a scan after the jump finds
 * the UDS gone whether or not the derive cleared it. -Wl,--wrap (mk/boot.mk)
 * sends boot_main.c's call here; the snapshot's frame takes the derive's
 * place, so its window holds what the derive left. Set by setUp. */
tfw_status_t __real_tfw_boot_cdi_derive(const uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                                        uint8_t       cdi[TFW_BOOT_MEASURE_LEN]);
tfw_status_t __wrap_tfw_boot_cdi_derive(const uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                                        uint8_t       cdi[TFW_BOOT_MEASURE_LEN]);

tfw_status_t __wrap_tfw_boot_cdi_derive(const uint8_t measurement[TFW_BOOT_MEASURE_LEN],
                                        uint8_t       cdi[TFW_BOOT_MEASURE_LEN])
{
    unsigned     before = tfw_residue_zero_calls();
    tfw_status_t st     = __real_tfw_boot_cdi_derive(measurement, cdi);

    tfw_residue_snapshot();
    derives++;
    derive_clears    = tfw_residue_zero_calls() - before;
    uds_cleared_last = tfw_residue_last_cleared(0u, 32u, kUds, 32u);
    at_return[0]     = tfw_residue_on_stack(kCdi, sizeof kCdi);
    at_return[1]     = tfw_residue_on_stack(kCdiWords, sizeof kCdiWords);
    at_return[2]     = tfw_residue_on_stack(kUds, 32u);
    at_return[3]     = tfw_residue_on_stack(kOpadUds, sizeof kOpadUds);
    at_return[4]     = tfw_residue_on_stack(kInner, sizeof kInner);
    at_return[5]     = tfw_residue_on_stack(kW, sizeof kW);
    at_return[6]     = tfw_residue_on_stack(kV, sizeof kV);
    return st;
}

static void verdict(tfw_boot_verdict_t *v)
{
    uint8_t i;

    memset(v, 0, sizeof *v);
    for (i = 0u; i < TFW_BOOT_MEASURE_LEN; i++) {
        v->measurement[i] = (uint8_t)(0x20u + i);
    }
    v->trailer.image_version = 0x01020304uL;
}

static void test_the_handoff_carries_the_cdi_once_at_its_offset(void)
{
    tfw_boot_verdict_t v;
    const uint8_t     *h     = tfw_boot_mock_handoff();
    const size_t       after = TFW_BOOT_HO_CDI + TFW_BOOT_MEASURE_LEN;

    verdict(&v);
    tfw_boot_hand_over(&v);

    TEST_ASSERT_EQUAL_UINT(1u, tfw_boot_mock_jumped());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(kCdi, &h[TFW_BOOT_HO_CDI], sizeof kCdi);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(v.measurement, &h[TFW_BOOT_HO_MEASUREMENT], TFW_BOOT_MEASURE_LEN);

    /* Everything after it, read where it is: a copy of the block on this
     * frame would be a CDI for the next test's stack check to find. The eight
     * bytes before it are too few to hold a part of one. */
    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_in(&h[after], TFW_BOOT_HANDOFF_LEN - after,
                                             kCdi, sizeof kCdi),
                              "a second copy of the CDI in the handoff");
}

/* The derive's secrets are looked for as the derive returns, where they lie
 * until the hand-over's next call overwrites them, and the CDI again straight
 * after the hand-over, as tfw_residue_snapshot requires. */
static void test_the_hand_over_leaves_no_cdi_on_the_stack(void)
{
    tfw_boot_verdict_t v;

    verdict(&v);
    tfw_boot_hand_over(&v);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, derives, "derives");
    TEST_ASSERT_FALSE_MESSAGE(at_return[0], "CDI bytes as the derive returns");
    TEST_ASSERT_FALSE_MESSAGE(at_return[1], "CDI state words as the derive returns");
    TEST_ASSERT_FALSE_MESSAGE(at_return[2], "UDS as the derive returns");
    TEST_ASSERT_FALSE_MESSAGE(at_return[3], "outer pad as the derive returns");
    TEST_ASSERT_FALSE_MESSAGE(at_return[4], "inner digest as the derive returns");
    TEST_ASSERT_FALSE_MESSAGE(at_return[5], "last schedule as the derive returns");
    TEST_ASSERT_FALSE_MESSAGE(at_return[6], "last working variables as the derive returns");
    tfw_residue_snapshot();

    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kCdi, sizeof kCdi), "CDI bytes");
}

/* The last two clears before the jump: `cdi`, then `block`. Since #173 `cdi`
 * is spent on the DeviceID seed (boot/boot_main.c, include/tfw/boot.h version
 * 4), so its clear holds kSeed; the block's holds the CDI. The clear of `cdi` before the derive holds whatever the stack did and is
 * not the one looked for. Inside the derive: four compressions (two blocks
 * each for the inner and the outer hash), the HMAC's state, then the UDS copy
 * last -- the HMAC's clear holds the UDS too, so only the order tells the
 * copy's own clear from it. */
static void test_the_hand_over_clears_its_cdi_then_its_block(void)
{
    tfw_boot_verdict_t v;

    verdict(&v);
    tfw_boot_hand_over(&v);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, derives, "derives");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(6u, derive_clears, "clears in the derive");
    TEST_ASSERT_TRUE_MESSAGE(uds_cleared_last, "the UDS copy, last in the derive");
    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_cleared(kOpadUds, sizeof kOpadUds), "outer pad");
    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_cleared(kW, sizeof kW), "last schedule");

    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_last_cleared(0u, TFW_BOOT_HANDOFF_LEN, kCdi, sizeof kCdi),
                             "block");
    TEST_ASSERT_TRUE_MESSAGE(tfw_residue_last_cleared(1u, TFW_BOOT_MEASURE_LEN, kSeed, sizeof kSeed),
                             "cdi (the seed)");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_handoff_carries_the_cdi_once_at_its_offset);
    RUN_TEST(test_the_hand_over_leaves_no_cdi_on_the_stack);
    RUN_TEST(test_the_hand_over_clears_its_cdi_then_its_block);
    return UNITY_END();
}
