/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * THE ONE TRIAL: every transition of boot/boot_trial.c and every refusal.
 *
 * docs/ota/adr-ota-image-format-and-trust.md, decisions 5 and 9. The functions
 * are pure over the parsed platform block, so each test sets the two state
 * bytes, makes one call, and reads them back. A refusal must answer
 * TFW_ERR_BAD_STATE and leave both bytes as they were.
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

/* ---------------------------------------------------------- at reset */

void test_a_running_trial_found_at_reset_fails_first(void)
{
    at(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_IDLE);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_FAIL, tfw_boot_trial_at_reset(&p));
    /* even with something pending: the trial is ended before anything else */
    at(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_PENDING);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_FAIL, tfw_boot_trial_at_reset(&p));
    /* and again after a fail: there is no second trial */
    at(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_FAILED);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_FAIL, tfw_boot_trial_at_reset(&p));
}

void test_a_confirm_settles_before_anything_pending(void)
{
    at(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_SETTLE, tfw_boot_trial_at_reset(&p));
    at(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_PENDING);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_SETTLE, tfw_boot_trial_at_reset(&p));
}

void test_a_pending_image_is_tried(void)
{
    at(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING);
    TEST_ASSERT_EQUAL(TFW_BOOT_RESET_TRY, tfw_boot_trial_at_reset(&p));
}

void test_nothing_in_flight_boots(void)
{
    static const uint8_t ota[] = {
        TFW_BOOT_OTA_IDLE, TFW_BOOT_OTA_RECEIVING, TFW_BOOT_OTA_VERIFIED,
        TFW_BOOT_OTA_FAILED
    };
    uint8_t i;

    for (i = 0u; i < sizeof ota; i++) {
        at(TFW_BOOT_STATE_GOOD, ota[i]);
        TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, tfw_boot_trial_at_reset(&p));
        at(TFW_BOOT_STATE_EMPTY, ota[i]);
        TEST_ASSERT_EQUAL(TFW_BOOT_RESET_BOOT, tfw_boot_trial_at_reset(&p));
    }
}

/* -------------------------------------------------------------- arm */

void test_arm_turns_pending_into_the_one_trial(void)
{
    at(TFW_BOOT_STATE_STAGED, TFW_BOOT_OTA_PENDING);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&p));
    assert_state(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_IDLE);

    at(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_arm(&p));
    assert_state(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_IDLE);
}

void test_arm_is_refused_with_nothing_pending(void)
{
    static const uint8_t ota[] = {
        TFW_BOOT_OTA_IDLE, TFW_BOOT_OTA_RECEIVING, TFW_BOOT_OTA_VERIFIED,
        TFW_BOOT_OTA_FAILED
    };
    uint8_t i;

    for (i = 0u; i < sizeof ota; i++) {
        at(TFW_BOOT_STATE_GOOD, ota[i]);
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_arm(&p));
        assert_state(TFW_BOOT_STATE_GOOD, ota[i]);
    }
}

void test_arm_is_refused_over_a_trial_or_an_unsettled_confirm(void)
{
    at(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_PENDING);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_arm(&p));
    assert_state(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_PENDING);

    at(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_PENDING);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_arm(&p));
    assert_state(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_PENDING);
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

void test_fail_is_refused_with_no_trial_and_nothing_pending(void)
{
    at(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_fail(&p));
    assert_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE);

    at(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_fail(&p));
    assert_state(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE);
}

/* The fall back is the fail plus GOOD: refused where the fail is, and then
 * GOOD is not written either -- a block already fallen back stays FAILED. */
void test_fall_back_is_refused_with_no_trial_and_nothing_pending(void)
{
    at(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_FAILED);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_fall_back(&p));
    assert_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_FAILED);

    at(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_fall_back(&p));
    assert_state(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE);
}

/* ---------------------------------------------------------- confirm */

void test_confirm_accepts_a_running_trial(void)
{
    at(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_IDLE);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_confirm(&p));
    assert_state(TFW_BOOT_STATE_CONFIRMED, TFW_BOOT_OTA_IDLE);
}

void test_confirm_is_refused_outside_a_trial(void)
{
    static const uint8_t boot[] = {
        TFW_BOOT_STATE_EMPTY, TFW_BOOT_STATE_STAGED, TFW_BOOT_STATE_GOOD,
        TFW_BOOT_STATE_CONFIRMED
    };
    uint8_t i;

    for (i = 0u; i < sizeof boot; i++) {
        at(boot[i], TFW_BOOT_OTA_IDLE);
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_confirm(&p));
        assert_state(boot[i], TFW_BOOT_OTA_IDLE);
    }
}

void test_confirm_is_refused_after_the_trial_failed(void)
{
    at(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_FAILED);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_confirm(&p));
    assert_state(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_FAILED);
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

void test_settle_is_refused_without_a_confirm(void)
{
    static const uint8_t boot[] = {
        TFW_BOOT_STATE_EMPTY, TFW_BOOT_STATE_STAGED, TFW_BOOT_STATE_GOOD,
        TFW_BOOT_STATE_TRIAL
    };
    uint32_t next;
    uint8_t  i;

    for (i = 0u; i < sizeof boot; i++) {
        next = 0u;
        at(boot[i], TFW_BOOT_OTA_IDLE);
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE,
                          tfw_boot_trial_settle(&p, 7u, 3u, &next));
        TEST_ASSERT_EQUAL_UINT32(3u, next);   /* the floor does not move on trial */
        assert_state(boot[i], TFW_BOOT_OTA_IDLE);
    }
}

/* --------------------------------------------------------- activate */

static const uint8_t M_NEW[TFW_BOOT_MEASURE_LEN]  = { 0x11 };
static const uint8_t M_FAIL[TFW_BOOT_MEASURE_LEN] = { 0x22 };

void test_activate_makes_a_verified_image_pending(void)
{
    at(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_VERIFIED);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_activate(&p, M_NEW, NULL));
    assert_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING);

    at(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_VERIFIED);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_activate(&p, M_NEW, M_FAIL));
    assert_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING);
}

void test_activate_is_refused_with_no_verified_image(void)
{
    static const uint8_t ota[] = {
        TFW_BOOT_OTA_IDLE, TFW_BOOT_OTA_RECEIVING, TFW_BOOT_OTA_FAILED,
        TFW_BOOT_OTA_PENDING
    };
    uint8_t i;

    for (i = 0u; i < sizeof ota; i++) {
        at(TFW_BOOT_STATE_GOOD, ota[i]);
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE,
                          tfw_boot_trial_activate(&p, M_NEW, NULL));
        assert_state(TFW_BOOT_STATE_GOOD, ota[i]);
    }
}

void test_activate_is_refused_during_a_trial(void)
{
    at(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_VERIFIED);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_activate(&p, M_NEW, NULL));
    assert_state(TFW_BOOT_STATE_TRIAL, TFW_BOOT_OTA_VERIFIED);
}

void test_the_image_that_failed_gets_no_second_trial(void)
{
    at(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_VERIFIED);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE,
                      tfw_boot_trial_activate(&p, M_FAIL, M_FAIL));
    assert_state(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_VERIFIED);
}

/* ------------------------------------------------------------ begin */

void test_begin_is_refused_only_while_pending_or_unsettled(void)
{
    static const uint8_t ota[] = {
        TFW_BOOT_OTA_IDLE, TFW_BOOT_OTA_RECEIVING, TFW_BOOT_OTA_VERIFIED,
        TFW_BOOT_OTA_FAILED
    };
    uint8_t i;

    for (i = 0u; i < sizeof ota; i++) {
        at(TFW_BOOT_STATE_GOOD, ota[i]);
        TEST_ASSERT_EQUAL(TFW_OK, tfw_boot_trial_begin_permit(&p));
        at(TFW_BOOT_STATE_CONFIRMED, ota[i]);
        TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_begin_permit(&p));
    }
    at(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_PENDING);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, tfw_boot_trial_begin_permit(&p));
}

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
    RUN_TEST(test_a_running_trial_found_at_reset_fails_first);
    RUN_TEST(test_a_confirm_settles_before_anything_pending);
    RUN_TEST(test_a_pending_image_is_tried);
    RUN_TEST(test_nothing_in_flight_boots);
    RUN_TEST(test_arm_turns_pending_into_the_one_trial);
    RUN_TEST(test_arm_is_refused_with_nothing_pending);
    RUN_TEST(test_arm_is_refused_over_a_trial_or_an_unsettled_confirm);
    RUN_TEST(test_a_trial_ended_unconfirmed_fails_and_stays_a_trial);
    RUN_TEST(test_a_pending_image_that_does_not_verify_fails_untried);
    RUN_TEST(test_a_pending_image_refused_at_evaluation_fails_at_every_reset);
    RUN_TEST(test_a_transient_refusal_is_not_undone_by_a_good_reset);
    RUN_TEST(test_fail_is_refused_with_no_trial_and_nothing_pending);
    RUN_TEST(test_fall_back_is_refused_with_no_trial_and_nothing_pending);
    RUN_TEST(test_confirm_accepts_a_running_trial);
    RUN_TEST(test_confirm_is_refused_outside_a_trial);
    RUN_TEST(test_confirm_is_refused_after_the_trial_failed);
    RUN_TEST(test_settle_advances_the_floor_to_the_confirmed_version);
    RUN_TEST(test_settle_never_lowers_the_floor);
    RUN_TEST(test_settle_repeated_after_a_reset_is_no_step);
    RUN_TEST(test_settle_with_a_step_too_large_keeps_the_floor_and_is_good);
    RUN_TEST(test_settle_is_refused_without_a_confirm);
    RUN_TEST(test_activate_makes_a_verified_image_pending);
    RUN_TEST(test_activate_is_refused_with_no_verified_image);
    RUN_TEST(test_activate_is_refused_during_a_trial);
    RUN_TEST(test_the_image_that_failed_gets_no_second_trial);
    RUN_TEST(test_begin_is_refused_only_while_pending_or_unsettled);
    RUN_TEST(test_a_confirmed_round_moves_the_floor_once_at_the_end);
    RUN_TEST(test_an_unconfirmed_round_never_runs_again);
    return UNITY_END();
}
