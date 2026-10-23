/*
 * The no-feature stand-ins of src/core (#163): handoff_none.c,
 * platform_id_none.c, app_runner_none.c, auth_session_none.c,
 * ota_stage_none.c, ota_prev_none.c, ota_trial_none.c and
 * update_request_none.c, linked together and alone -- no host library, no
 * HAL mock -- so a stand-in that reached the HAL, the EEPROM or a real unit
 * would not link.
 *
 * A seeded loop calls them in a random order, out-parameters filled with
 * random bytes, and after every call checks every answer the headers give
 * for a build without the feature: a stand-in whose answer moves with the
 * calls before it, or that writes an out-parameter it documents as left
 * alone, is seen.
 *
 * Deterministic: xorshift32 from a literal seed, a literal iteration count.
 *
 * Mutants this file kills (each tried by hand against the eight units):
 *   - handoff: measurement or tier non-NULL; fw_hash NULL or one byte
 *     non-zero; reset_cause / take_cdi return 1 or write *out
 *   - platform_id: serial NULL or one byte non-zero; operator_pubkey one
 *     byte short, one byte wide, or not zero
 *   - app_runner: state not IDLE (fixed, or after start / stop(1)),
 *     overruns non-zero, evidence TFW_OK or writing out, after_update
 *     refusing
 *   - auth_gate refusing (fixed, or after init)
 *   - stage_install_pending non-zero (fixed, or after init)
 *   - ota_prev_backup refusing
 *   - update_request_supported non-zero (fixed, or after arm)
 *   - any of them counting calls and changing answer after the first
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "tfw/app.h"
#include "tfw/boot.h"
#include "app_runner.h"
#include "auth_session.h"
#include "handoff.h"
#include "ota_stage.h"
#include "ota_trial.h"
#include "platform_id.h"
#include "update_request.h"

#define ITERATIONS 3000u
#define GUARD      8u

static uint32_t rng;

static uint32_t next_rand(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void fill(uint8_t *b, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        b[i] = (uint8_t)next_rand();
    }
}

static void assert_all_zero(const uint8_t *b, size_t n)
{
    size_t i;

    TEST_ASSERT_NOT_NULL(b);
    for (i = 0; i < n; i++) {
        TEST_ASSERT_EQUAL_HEX8(0u, b[i]);
    }
}

/* What a build without the feature answers, whatever came before. */
static void assert_every_answer(void)
{
    uint8_t out[TFW_BOOT_MEASURE_LEN];
    uint8_t keep[TFW_BOOT_MEASURE_LEN];
    uint8_t ev[TFW_APP_EVIDENCE_LEN];
    uint8_t ev_keep[TFW_APP_EVIDENCE_LEN];
    uint8_t key[TFW_BOOT_OPERATOR_PUBKEY_LEN + 2u * GUARD];
    uint8_t key_keep[sizeof key];

    TEST_ASSERT_NULL(tfw_boot_handoff_measurement());
    TEST_ASSERT_NULL(tfw_boot_handoff_tier());
    assert_all_zero(tfw_boot_handoff_fw_hash(), TFW_BOOT_MEASURE_LEN);

    fill(out, sizeof out);
    memcpy(keep, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_handoff_reset_cause(out));
    TEST_ASSERT_EQUAL_MEMORY(keep, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_boot_handoff_take_cdi(out));
    TEST_ASSERT_EQUAL_MEMORY(keep, out, sizeof out);

    assert_all_zero(tfw_platform_id_serial(), TFW_BOOT_SERIAL_LEN);
    fill(key, sizeof key);
    memcpy(key_keep, key, sizeof key);
    tfw_platform_id_operator_pubkey(key + GUARD);
    TEST_ASSERT_EQUAL_MEMORY(key_keep, key, GUARD);
    assert_all_zero(key + GUARD, TFW_BOOT_OPERATOR_PUBKEY_LEN);
    TEST_ASSERT_EQUAL_MEMORY(key_keep + GUARD + TFW_BOOT_OPERATOR_PUBKEY_LEN,
                             key + GUARD + TFW_BOOT_OPERATOR_PUBKEY_LEN, GUARD);

    TEST_ASSERT_EQUAL_UINT8(TFW_APP_STATE_IDLE, tfw_app_runner_state());
    TEST_ASSERT_EQUAL_UINT16(0u, tfw_app_runner_overruns());
    fill(ev, sizeof ev);
    memcpy(ev_keep, ev, sizeof ev);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_app_runner_evidence(ev));
    TEST_ASSERT_EQUAL_MEMORY(ev_keep, ev, sizeof ev);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_app_runner_after_update());

    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_stage_install_pending());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_ota_prev_backup());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_update_request_supported());
}

/* Every call that could move a stand-in's answer, if it kept state. */
static void call_one(uint32_t which)
{
    switch (which % 12u) {
    case 0:  tfw_boot_handoff_init(); break;
    case 1:  tfw_platform_id_init(); break;
    case 2:  tfw_app_runner_start(); break;
    case 3:  tfw_app_runner_poll(); break;
    case 4:  tfw_app_runner_stop(0u); break;
    case 5:  tfw_app_runner_stop(1u); break;
    case 6:  tfw_auth_session_init(); break;
    case 7:  tfw_stage_init(); break;
    case 8:  tfw_ota_trial_start(); break;
    case 9:  tfw_ota_trial_after_init(); break;
    case 10: tfw_ota_trial_poll(); break;
    default: tfw_update_request_arm(); break;
    }
}

void setUp(void)
{
    rng = 0x163A0E5Eu;
}

void tearDown(void) {}

static void test_every_answer_before_any_init(void)
{
    assert_every_answer();
}

/* Each call alone, then the answers: one entry per call, so a stand-in that
 * moved on exactly one of them is named by the test that fails. */
static void test_every_answer_after_each_call(void)
{
    uint32_t c;

    for (c = 0; c < 12u; c++) {
        call_one(c);
        assert_every_answer();
        call_one(c);
        assert_every_answer();
    }
}

static void test_every_answer_after_a_random_call_order(void)
{
    uint32_t i;

    for (i = 0; i < ITERATIONS; i++) {
        call_one(next_rand());
        assert_every_answer();
    }
}

/* The two zero blocks are read-only answers: asking again returns the same
 * zeros, however often. */
static void test_the_zero_blocks_hold_across_calls(void)
{
    const uint8_t *h = tfw_boot_handoff_fw_hash();
    const uint8_t *s = tfw_platform_id_serial();
    uint32_t i;

    for (i = 0; i < 64u; i++) {
        TEST_ASSERT_EQUAL_PTR(h, tfw_boot_handoff_fw_hash());
        TEST_ASSERT_EQUAL_PTR(s, tfw_platform_id_serial());
    }
    assert_all_zero(h, TFW_BOOT_MEASURE_LEN);
    assert_all_zero(s, TFW_BOOT_SERIAL_LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_answer_before_any_init);
    RUN_TEST(test_every_answer_after_each_call);
    RUN_TEST(test_every_answer_after_a_random_call_order);
    RUN_TEST(test_the_zero_blocks_hold_across_calls);
    return UNITY_END();
}
