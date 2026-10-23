/*
 * boot/boot_trial.c under mutation (#163): the one trial (Q9), its confirm
 * and its rollback.
 *
 * test_boot_trial walks the trial through the states a device reaches. This
 * suite drives every transition from EVERY pair of boot_state and ota_state,
 * the known values and one past them, and holds each against the trial as
 * docs/ota/adr-ota-image-format-and-trust.md decision 9 states it: a refusal
 * leaves the block as it was, and an acceptance changes only what it names.
 *
 * Mutants this suite kills (each tried against the unit and seen to fail it):
 *   T1  at_reset: a running TRIAL no longer fails first
 *   T2  at_reset: a PENDING image is booted, not tried
 *   T3  at_reset: PENDING checked before CONFIRMED
 *   T4  arm: the PENDING condition dropped
 *   T5  arm: arming over an unsettled CONFIRMED allowed
 *   T6  arm: ota_state left PENDING
 *   T7  fail: `&&` -> `||` in its refusal
 *   T8  fail: boot_state not kept TRIAL
 *   T9  fall_back: GOOD not written
 *   T10 fall_back: GOOD written over a refusal
 *   T11 settle: *next unset on a refusal
 *   T12 settle: GOOD not written
 *   T13 confirm: a FAILED trial confirmed
 *   T14 activate: the failed image compared one byte short
 *   T15 activate: activating during a TRIAL allowed
 *   T16 begin_permit: a transfer over an unsettled CONFIRMED allowed
 */
#include <string.h>
#include "unity.h"
#include "boot_trial.h"
#include "boot_image.h"

#define NSTATE 7u   /* 0..5 named or reserved, and one past them */

typedef struct {
    uint8_t b;
    uint8_t o;
} pair_t;

static tfw_boot_platform_t mk(uint8_t b, uint8_t o)
{
    tfw_boot_platform_t p;

    memset(&p, 0xA5, sizeof p);
    p.boot_state = b;
    p.ota_state  = o;
    return p;
}

/* Everything but the two state bytes is as mk() left it. */
static void assert_rest_untouched(const tfw_boot_platform_t *p)
{
    tfw_boot_platform_t q = *p;

    q.boot_state = 0xA5u;
    q.ota_state  = 0xA5u;
    {
        tfw_boot_platform_t r = mk(0xA5u, 0xA5u);
        TEST_ASSERT_EQUAL_MEMORY(&r, &q, sizeof q);
    }
}

static void assert_pair(uint8_t b, uint8_t o, const tfw_boot_platform_t *p)
{
    TEST_ASSERT_EQUAL_HEX8(b, p->boot_state);
    TEST_ASSERT_EQUAL_HEX8(o, p->ota_state);
    assert_rest_untouched(p);
}

/* ------------------------------------------------------- the reference */

static tfw_boot_reset_action_t ref_at_reset(uint8_t b, uint8_t o)
{
    if (b == TFW_BOOT_STATE_TRIAL)     { return TFW_BOOT_RESET_FAIL; }
    if (b == TFW_BOOT_STATE_CONFIRMED) { return TFW_BOOT_RESET_SETTLE; }
    if (o == TFW_BOOT_OTA_PENDING)     { return TFW_BOOT_RESET_TRY; }
    return TFW_BOOT_RESET_BOOT;
}

static int arm_ok(uint8_t b, uint8_t o)
{
    return o == TFW_BOOT_OTA_PENDING && b != TFW_BOOT_STATE_TRIAL
        && b != TFW_BOOT_STATE_CONFIRMED;
}

static int fail_ok(uint8_t b, uint8_t o)
{
    return b == TFW_BOOT_STATE_TRIAL || o == TFW_BOOT_OTA_PENDING;
}

static int confirm_ok(uint8_t b, uint8_t o)
{
    return b == TFW_BOOT_STATE_TRIAL && o != TFW_BOOT_OTA_FAILED;
}

static int activate_ok(uint8_t b, uint8_t o)
{
    return o == TFW_BOOT_OTA_VERIFIED && b != TFW_BOOT_STATE_TRIAL;
}

static int begin_ok(uint8_t b, uint8_t o)
{
    return o != TFW_BOOT_OTA_PENDING && b != TFW_BOOT_STATE_CONFIRMED;
}

void setUp(void) {}
void tearDown(void) {}

/* T1-T3 */
static void test_at_reset_over_every_pair(void)
{
    for (uint8_t b = 0u; b < NSTATE; b++) {
        for (uint8_t o = 0u; o < NSTATE; o++) {
            tfw_boot_platform_t p = mk(b, o);

            TEST_ASSERT_EQUAL_INT(ref_at_reset(b, o), tfw_boot_trial_at_reset(&p));
            assert_pair(b, o, &p);
        }
    }
}

/* T4-T6 */
static void test_arm_over_every_pair(void)
{
    for (uint8_t b = 0u; b < NSTATE; b++) {
        for (uint8_t o = 0u; o < NSTATE; o++) {
            tfw_boot_platform_t p = mk(b, o);

            if (arm_ok(b, o)) {
                TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trial_arm(&p));
                assert_pair(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_IDLE, &p);
                TEST_ASSERT_EQUAL_INT(TFW_BOOT_RESET_FAIL, tfw_boot_trial_at_reset(&p));
            } else {
                TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_boot_trial_arm(&p));
                assert_pair(b, o, &p);
            }
        }
    }
}

/* T7-T10: an ended trial stays a TRIAL that fails at every reset; the fall
 * back leaves GOOD; a refusal of either writes nothing. */
static void test_fail_and_fall_back_over_every_pair(void)
{
    for (uint8_t b = 0u; b < NSTATE; b++) {
        for (uint8_t o = 0u; o < NSTATE; o++) {
            tfw_boot_platform_t p = mk(b, o);
            tfw_boot_platform_t q = mk(b, o);

            if (fail_ok(b, o)) {
                TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trial_fail(&p));
                assert_pair(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_FAILED, &p);
                TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trial_fall_back(&q));
                assert_pair(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_FAILED, &q);
                TEST_ASSERT_EQUAL_INT(TFW_BOOT_RESET_BOOT, tfw_boot_trial_at_reset(&q));
            } else {
                TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_boot_trial_fail(&p));
                assert_pair(b, o, &p);
                TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_boot_trial_fall_back(&q));
                assert_pair(b, o, &q);
            }
        }
    }
}

/* T13 */
static void test_confirm_over_every_pair(void)
{
    for (uint8_t b = 0u; b < NSTATE; b++) {
        for (uint8_t o = 0u; o < NSTATE; o++) {
            tfw_boot_platform_t p = mk(b, o);

            if (confirm_ok(b, o)) {
                TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trial_confirm(&p));
                assert_pair(TFW_BOOT_STATE_CONFIRMED, o, &p);
            } else {
                TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_boot_trial_confirm(&p));
                assert_pair(b, o, &p);
            }
        }
    }
}

/* T11, T12: the settle's answer is the floor's advance, and only from
 * CONFIRMED; a refusal still answers the floor it was given. */
static void test_settle_over_every_pair_and_floor(void)
{
    static const uint32_t v[] = { 0u, 1u, 5u, 6u, 0x7FFFFFFFuL, 0xFFFFFFFFuL };
    const size_t nv = sizeof v / sizeof v[0];

    for (uint8_t b = 0u; b < NSTATE; b++) {
        for (uint8_t o = 0u; o < NSTATE; o++) {
            for (size_t i = 0u; i < nv; i++) {
                for (size_t j = 0u; j < nv; j++) {
                    tfw_boot_platform_t p = mk(b, o);
                    uint32_t next = 0xDEADBEEFuL;
                    uint32_t want = v[j];   /* settle answers the floor first */
                    tfw_status_t st = tfw_boot_trial_settle(&p, v[i], v[j], &next);

                    if (b == TFW_BOOT_STATE_CONFIRMED) {
                        tfw_status_t ref = tfw_boot_floor_advance(v[i], v[j], &want);

                        TEST_ASSERT_EQUAL_INT(ref, st);
                        TEST_ASSERT_EQUAL_UINT32(want, next);
                        TEST_ASSERT_TRUE(next >= v[j]);
                        assert_pair(TFW_BOOT_STATE_GOOD, o, &p);
                    } else {
                        TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, st);
                        TEST_ASSERT_EQUAL_UINT32(v[j], next);
                        assert_pair(b, o, &p);
                    }
                }
            }
        }
    }
}

/* T14, T15: activate from every pair, with no failed image, the same image,
 * and an image that differs from it only in its first or its last byte. */
static void test_activate_over_every_pair(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN];
    uint8_t f[TFW_BOOT_MEASURE_LEN];

    memset(m, 0x3Cu, sizeof m);
    for (uint8_t b = 0u; b < NSTATE; b++) {
        for (uint8_t o = 0u; o < NSTATE; o++) {
            for (unsigned k = 0u; k < 4u; k++) {
                tfw_boot_platform_t p = mk(b, o);
                const uint8_t *failed = f;
                int same = 0;

                memcpy(f, m, sizeof f);
                switch (k) {
                case 0: failed = 0; break;
                case 1: same = 1; break;
                case 2: f[0] ^= 0x01u; break;
                default: f[TFW_BOOT_MEASURE_LEN - 1u] ^= 0x80u; break;
                }
                if (activate_ok(b, o) && !same) {
                    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trial_activate(&p, m, failed));
                    assert_pair(b, TFW_BOOT_OTA_PENDING, &p);
                } else {
                    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_boot_trial_activate(&p, m, failed));
                    assert_pair(b, o, &p);
                }
            }
        }
    }
}

/* T16 */
static void test_begin_permit_over_every_pair(void)
{
    for (uint8_t b = 0u; b < NSTATE; b++) {
        for (uint8_t o = 0u; o < NSTATE; o++) {
            tfw_boot_platform_t p = mk(b, o);

            TEST_ASSERT_EQUAL_INT(begin_ok(b, o) ? TFW_OK : TFW_ERR_BAD_STATE,
                                  tfw_boot_trial_begin_permit(&p));
            assert_pair(b, o, &p);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_at_reset_over_every_pair);
    RUN_TEST(test_arm_over_every_pair);
    RUN_TEST(test_fail_and_fall_back_over_every_pair);
    RUN_TEST(test_confirm_over_every_pair);
    RUN_TEST(test_settle_over_every_pair_and_floor);
    RUN_TEST(test_activate_over_every_pair);
    RUN_TEST(test_begin_permit_over_every_pair);
    return UNITY_END();
}
