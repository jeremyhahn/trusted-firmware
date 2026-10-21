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

void setUp(void)    { tfw_boot_mock_reset(); tfw_residue_reset(); }
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

/* Called straight after the hand-over, as tfw_residue_snapshot requires. */
static void test_the_hand_over_leaves_no_cdi_on_the_stack(void)
{
    tfw_boot_verdict_t v;

    verdict(&v);
    tfw_boot_hand_over(&v);
    tfw_residue_snapshot();

    TEST_ASSERT_FALSE_MESSAGE(tfw_residue_on_stack(kCdi, sizeof kCdi), "CDI bytes");
}

/* The last two clears before the jump: `cdi`, then `block`. Since #173 `cdi`
 * is spent on the DeviceID seed (boot/boot_main.c, include/tfw/boot.h version
 * 4), so its clear holds kSeed; the block's holds the CDI. The clear of `cdi` before the derive holds whatever the stack did and is
 * not the one looked for. */
static void test_the_hand_over_clears_its_cdi_then_its_block(void)
{
    tfw_boot_verdict_t v;

    verdict(&v);
    tfw_boot_hand_over(&v);

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
