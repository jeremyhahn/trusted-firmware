/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE ONE TRIAL: every transition of boot/boot_trial.c and every refusal.
 *
 * docs/ota/adr-ota-image-format-and-trust.md, decisions 5 and 9. The functions
 * are pure over the parsed platform block, so the "every pair" tests drive
 * each transition from EVERY pair of boot_state and ota_state, the known
 * values and one past them, and hold each against the trial as decision 9
 * states it: a refusal answers TFW_ERR_BAD_STATE and leaves the block as it
 * was, and an acceptance changes only what it names. The rest walk the trial
 * through the rounds a device reaches, and pin the settle's floor.
 *
 * Mutants the every-pair tests kill (#163; each tried against the unit and
 * seen to fail it):
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

static tfw_boot_platform_t p;

void setUp(void) {}
void tearDown(void) {}

static void at(uint8_t boot_state, uint8_t ota_state)
{
    memset(&p, 0, sizeof p);
    p.boot_state = boot_state;
    p.ota_state  = ota_state;
}

static void assert_state(uint8_t boot_state, uint8_t ota_state)
{
    TEST_ASSERT_EQUAL_UINT8(boot_state, p.boot_state);
    TEST_ASSERT_EQUAL_UINT8(ota_state, p.ota_state);
}

/* ------------------------------------------------- every pair (#163) */

#define NSTATE 7u   /* 0..5 named or reserved, and one past them */

static tfw_boot_platform_t mk(uint8_t b, uint8_t o)
{
    tfw_boot_platform_t q;

    memset(&q, 0xA5, sizeof q);
    q.boot_state = b;
    q.ota_state  = o;
    return q;
}

/* Everything but the two state bytes is as mk() left it. */
static void assert_rest_untouched(const tfw_boot_platform_t *q)
{
    tfw_boot_platform_t c = *q;

    c.boot_state = 0xA5u;
    c.ota_state  = 0xA5u;
    {
        tfw_boot_platform_t r = mk(0xA5u, 0xA5u);
        TEST_ASSERT_EQUAL_MEMORY(&r, &c, sizeof c);
    }
}

static void assert_pair(uint8_t b, uint8_t o, const tfw_boot_platform_t *q)
{
    TEST_ASSERT_EQUAL_HEX8(b, q->boot_state);
    TEST_ASSERT_EQUAL_HEX8(o, q->ota_state);
    assert_rest_untouched(q);
}

/* The reference: the trial as decision 9 states it. */

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

/* T1-T3 */
void test_at_reset_over_every_pair(void)
{
    for (uint8_t b = 0u; b < NSTATE; b++) {
        for (uint8_t o = 0u; o < NSTATE; o++) {
            tfw_boot_platform_t q = mk(b, o);

            TEST_ASSERT_EQUAL_INT(ref_at_reset(b, o), tfw_boot_trial_at_reset(&q));
            assert_pair(b, o, &q);
        }
    }
}

/* T4-T6 */
void test_arm_over_every_pair(void)
{
    for (uint8_t b = 0u; b < NSTATE; b++) {
        for (uint8_t o = 0u; o < NSTATE; o++) {
            tfw_boot_platform_t q = mk(b, o);

            if (arm_ok(b, o)) {
                TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trial_arm(&q));
                assert_pair(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_IDLE, &q);
                TEST_ASSERT_EQUAL_INT(TFW_BOOT_RESET_FAIL, tfw_boot_trial_at_reset(&q));
            } else {
                TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_boot_trial_arm(&q));
                assert_pair(b, o, &q);
            }
        }
    }
}

/* T7-T10: an ended trial stays a TRIAL that fails at every reset; the fall
 * back leaves GOOD; a refusal of either writes nothing. */
void test_fail_and_fall_back_over_every_pair(void)
{
    for (uint8_t b = 0u; b < NSTATE; b++) {
        for (uint8_t o = 0u; o < NSTATE; o++) {
            tfw_boot_platform_t q = mk(b, o);
            tfw_boot_platform_t r = mk(b, o);

            if (fail_ok(b, o)) {
                TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trial_fail(&q));
                assert_pair(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_FAILED, &q);
                TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trial_fall_back(&r));
                assert_pair(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_FAILED, &r);
                TEST_ASSERT_EQUAL_INT(TFW_BOOT_RESET_BOOT, tfw_boot_trial_at_reset(&r));
            } else {
                TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_boot_trial_fail(&q));
                assert_pair(b, o, &q);
                TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_boot_trial_fall_back(&r));
                assert_pair(b, o, &r);
            }
        }
    }
}

/* T13 */
void test_confirm_over_every_pair(void)
{
    for (uint8_t b = 0u; b < NSTATE; b++) {
        for (uint8_t o = 0u; o < NSTATE; o++) {
            tfw_boot_platform_t q = mk(b, o);

            if (confirm_ok(b, o)) {
                TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trial_confirm(&q));
                assert_pair(TFW_BOOT_STATE_CONFIRMED, o, &q);
            } else {
                TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_boot_trial_confirm(&q));
                assert_pair(b, o, &q);
            }
        }
    }
}

/* T11, T12: the settle's answer is the floor's advance, and only from
 * CONFIRMED; a refusal still answers the floor it was given. */
void test_settle_over_every_pair_and_floor(void)
{
    static const uint32_t v[] = { 0u, 1u, 5u, 6u, 0x7FFFFFFFuL, 0xFFFFFFFFuL };
    const size_t nv = sizeof v / sizeof v[0];

    for (uint8_t b = 0u; b < NSTATE; b++) {
        for (uint8_t o = 0u; o < NSTATE; o++) {
            for (size_t i = 0u; i < nv; i++) {
                for (size_t j = 0u; j < nv; j++) {
                    tfw_boot_platform_t q = mk(b, o);
                    uint32_t next = 0xDEADBEEFuL;
                    uint32_t want = v[j];   /* settle answers the floor first */
                    tfw_status_t st = tfw_boot_trial_settle(&q, v[i], v[j], &next);

                    if (b == TFW_BOOT_STATE_CONFIRMED) {
                        tfw_status_t ref = tfw_boot_floor_advance(v[i], v[j], &want);

                        TEST_ASSERT_EQUAL_INT(ref, st);
                        TEST_ASSERT_EQUAL_UINT32(want, next);
                        TEST_ASSERT_TRUE(next >= v[j]);
                        assert_pair(TFW_BOOT_STATE_GOOD, o, &q);
                    } else {
                        TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, st);
                        TEST_ASSERT_EQUAL_UINT32(v[j], next);
                        assert_pair(b, o, &q);
                    }
                }
            }
        }
    }
}

/* T14, T15: activate from every pair, with no failed image, the same image,
 * and an image that differs from it only in its first or its last byte. */
void test_activate_over_every_pair(void)
{
    uint8_t m[TFW_BOOT_MEASURE_LEN];
    uint8_t f[TFW_BOOT_MEASURE_LEN];

    memset(m, 0x3Cu, sizeof m);
    for (uint8_t b = 0u; b < NSTATE; b++) {
        for (uint8_t o = 0u; o < NSTATE; o++) {
            for (unsigned k = 0u; k < 4u; k++) {
                tfw_boot_platform_t q = mk(b, o);
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
                    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_boot_trial_activate(&q, m, failed));
                    assert_pair(b, TFW_BOOT_OTA_PENDING, &q);
                } else {
                    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_STATE, tfw_boot_trial_activate(&q, m, failed));
                    assert_pair(b, o, &q);
                }
            }
        }
    }
}

/* T16 */
void test_begin_permit_over_every_pair(void)
{
    for (uint8_t b = 0u; b < NSTATE; b++) {
        for (uint8_t o = 0u; o < NSTATE; o++) {
            tfw_boot_platform_t q = mk(b, o);

            TEST_ASSERT_EQUAL_INT(begin_ok(b, o) ? TFW_OK : TFW_ERR_BAD_STATE,
                                  tfw_boot_trial_begin_permit(&q));
            assert_pair(b, o, &q);
        }
    }
}

/* ------------------------------------------------------------- fail */

void test_a_trial_ended_unconfirmed_fails_and_stays_a_trial(void)
{
    at(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_IDLE);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_fail(&p));
    assert_state(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_FAILED);
    /* every later reset lands here again, and changes nothing */
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_FAIL, tfw_boot_trial_at_reset(&p));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_fail(&p));
    assert_state(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_FAILED);
}

void test_a_pending_image_that_does_not_verify_fails_untried(void)
{
    at(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_fail(&p));
    assert_state(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_FAILED);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_arm(&p));
}

/* The reset after an evaluation refused a pending image ends it again, as a
 * reset after an unconfirmed trial does: nothing is evaluated, nothing runs. */
void test_a_pending_image_refused_at_evaluation_fails_at_every_reset(void)
{
    at(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_TRY, tfw_boot_trial_at_reset(&p));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_fail(&p));

    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_FAIL, tfw_boot_trial_at_reset(&p));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_fail(&p));
    assert_state(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_FAILED);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_FAIL, tfw_boot_trial_at_reset(&p));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_confirm(&p));
    /* another OTA round is permitted */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_begin_permit(&p));
}

/* A transient refusal -- an element that did not answer -- then a reset at
 * which the same image would verify. The reset must not reach the evaluation
 * that would pass: from GOOD it would boot the image as accepted, with no trial
 * and no confirm. It fails instead, and no confirm can make it good. */
void test_a_transient_refusal_is_not_undone_by_a_good_reset(void)
{
    at(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_fail(&p));

    TEST_ASSERT_NOT_EQUAL(TFW_BOOT_RESET_BOOT, tfw_boot_trial_at_reset(&p));
    TEST_ASSERT_NOT_EQUAL(TFW_BOOT_RESET_TRY, tfw_boot_trial_at_reset(&p));
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_FAIL, tfw_boot_trial_at_reset(&p));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_arm(&p));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_confirm(&p));
    TEST_ASSERT_NOT_EQUAL(TFW_BOOT_STATE_GOOD, p.boot_state);
}

/* ----------------------------------------------------------- settle */

void test_settle_advances_the_floor_to_the_confirmed_version(void)
{
    uint32_t next = 0u;

    at(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_settle(&p, 7u, 3u, &next));
    TEST_ASSERT_EQUAL_UINT32(7u, next);
    assert_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
}

void test_settle_never_lowers_the_floor(void)
{
    uint32_t next = 0u;

    at(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_settle(&p, 3u, 7u, &next));
    TEST_ASSERT_EQUAL_UINT32(7u, next);
    assert_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
}

void test_settle_repeated_after_a_reset_is_no_step(void)
{
    uint32_t next = 0u;

    /* the counter was written and the reset came before GOOD was */
    at(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_settle(&p, 7u, 7u, &next));
    TEST_ASSERT_EQUAL_UINT32(7u, next);
    assert_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
}

void test_settle_with_a_step_too_large_keeps_the_floor_and_is_good(void)
{
    uint32_t next = 0u;

    at(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
                      tfw_boot_trial_settle(&p, 3u + TFW_BOOT_FLOOR_MAX_STEP + 1u,
                                            3u, &next));
    TEST_ASSERT_EQUAL_UINT32(3u, next);
    assert_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
}

/* ------------------------------------------------------ whole rounds */

static const uint8_t M_NEW[TFW_BOOT_MEASURE_LEN]  = { 0x11 };

/* ------------------------------------------------------ whole rounds */

void test_a_confirmed_round_moves_the_floor_once_at_the_end(void)
{
    uint32_t floor = 3u;
    uint32_t next  = 0u;

    at(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_VERIFIED);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_activate(&p, M_NEW, NULL));
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_TRY, tfw_boot_trial_at_reset(&p));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&p));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_confirm(&p));
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_SETTLE, tfw_boot_trial_at_reset(&p));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_settle(&p, 4u, floor, &next));
    TEST_ASSERT_EQUAL_UINT32(4u, next);
    assert_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, tfw_boot_trial_at_reset(&p));
}

void test_an_unconfirmed_round_never_runs_again(void)
{
    at(TFW_BOOT_STATE_STAGED, TFW_BOOT_OTA_PENDING);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&p));
    /* the image ran, and the next reset came without a confirm */
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_FAIL, tfw_boot_trial_at_reset(&p));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_fail(&p));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_confirm(&p));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_arm(&p));
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_FAIL, tfw_boot_trial_at_reset(&p));
    /* another OTA round is permitted */
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_begin_permit(&p));
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
    RUN_TEST(test_a_trial_ended_unconfirmed_fails_and_stays_a_trial);
    RUN_TEST(test_a_pending_image_that_does_not_verify_fails_untried);
    RUN_TEST(test_a_pending_image_refused_at_evaluation_fails_at_every_reset);
    RUN_TEST(test_a_transient_refusal_is_not_undone_by_a_good_reset);
    RUN_TEST(test_settle_advances_the_floor_to_the_confirmed_version);
    RUN_TEST(test_settle_never_lowers_the_floor);
    RUN_TEST(test_settle_repeated_after_a_reset_is_no_step);
    RUN_TEST(test_settle_with_a_step_too_large_keeps_the_floor_and_is_good);
    RUN_TEST(test_a_confirmed_round_moves_the_floor_once_at_the_end);
    RUN_TEST(test_an_unconfirmed_round_never_runs_again);
    return UNITY_END();
}
