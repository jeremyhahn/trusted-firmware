/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The tamper-response loop (src/core/hsm_loop.c, docs/hsm-self-defence/
 * m7-design.md slice 3): one pass of the superloop at a time, over the real
 * engine and the channel's commands that share it (hsm_cmd.c). The port is a
 * mock whose lines this file asserts and whose actions it records; the clock
 * is this file's.
 *
 * The set is the literal SUIT_POLICY_ENVELOPE (testdata/suit_vectors.h): one
 * policy whose one rule is S1: [keys-lock, shutdown], heartbeat 1000 ms.
 */
#include <string.h>
#include "unity.h"
#include "atecc608a_model.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/boot.h"
#include "tfw/hal.h"
#include "suit.h"
#include "suit_vectors.h"
#include "hsm.h"
#include "hsm_cmd.h"
#include "hsm_loop.h"
#include "hsm_port.h"

static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

static tfw_ms_t clock_ms;
tfw_ms_t tfw_hal_millis(void) { return clock_ms; }

#define LOCK TFW_HSM_ACT_PHY_OFF, TFW_HSM_ACT_POWER_OFF, TFW_HSM_ACT_KEYS_LOCK, TFW_HSM_ACT_RECORD
#define S1_ACTS TFW_HSM_ACT_KEYS_LOCK, TFW_HSM_ACT_SHUTDOWN

/* The mock port: the lines asserted, and every action asked of it. */
#define LINE(sig) ((uint8_t)(1u << ((sig) - 1u)))

static uint8_t lines;
static uint8_t acted[64];
static uint8_t n_acted;

uint8_t tfw_hsm_port_sense(void) { return lines; }

tfw_status_t tfw_hsm_port_act(uint8_t action)
{
    TEST_ASSERT_LESS_THAN_UINT8(sizeof acted, n_acted);
    acted[n_acted++] = action;
    return TFW_OK;
}

/* S3 is the engine's test's (test_hsm_engine.c); here nothing measures. */
tfw_status_t tfw_hsm_port_measure(const tfw_hsm_set_t *set)
{
    (void)set;
    return TFW_ERR_UNSUPPORTED;
}

const uint8_t *tfw_hsm_port_key(void) { return SUIT_TENANT_PUB; }

#define ACTS(...)                                                         \
    do {                                                                  \
        static const uint8_t want_[] = { __VA_ARGS__ };                   \
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(sizeof want_, n_acted, "count");  \
        TEST_ASSERT_EQUAL_HEX8_ARRAY(want_, acted, sizeof want_);         \
        n_acted = 0u;                                                     \
    } while (0)
#define NO_ACTS() TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, n_acted, "count")

void setUp(void)
{
    tfw_model_reset();
    memset(test_handoff, 0, sizeof test_handoff);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    clock_ms = 0u;
    lines    = 0u;
    n_acted  = 0u;
    tfw_hsm_cmd_reset();
    tfw_hsm_loop_reset();
}

void tearDown(void)
{
    tfw_hsm_cmd_reset();
    tfw_hsm_loop_reset();
}

static tfw_hsm_t *engine(void)
{
    return tfw_hsm_cmd_engine(clock_ms);
}

static void load_policy(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_load(engine(), SUIT_POLICY_ENVELOPE,
                                           sizeof SUIT_POLICY_ENVELOPE,
                                           SUIT_TENANT_PUB, clock_ms));
    NO_ACTS();
}

static void test_a_pass_with_no_line_does_nothing(void)
{
    tfw_hsm_loop_poll();
    tfw_hsm_loop_poll();
    NO_ACTS();
    TEST_ASSERT_EQUAL_UINT8(0u, engine()->locked);
}

/* R1: no set, so a sensed signal is the built-in lockdown. */
static void test_a_line_with_no_set_locks_down(void)
{
    lines = LINE(TFW_HSM_SIG_HOTPLUG);
    tfw_hsm_loop_poll();
    ACTS(LOCK);
    TEST_ASSERT_EQUAL_UINT8(1u, engine()->locked);
}

/* R2: the set's actions for the signal sensed. */
static void test_a_held_line_is_one_event(void)
{
    load_policy();
    lines = LINE(TFW_HSM_SIG_TAMPER_PIN);
    tfw_hsm_loop_poll();
    ACTS(S1_ACTS);
    TEST_ASSERT_EQUAL_UINT8(0u, engine()->locked);
    for (uint8_t i = 0u; i < 5u; i++) {
        clock_ms = (tfw_ms_t)(clock_ms + 10u);
        tfw_hsm_loop_poll();
    }
    NO_ACTS();
}

static void test_a_line_released_and_asserted_again_is_a_second_event(void)
{
    load_policy();
    lines = LINE(TFW_HSM_SIG_TAMPER_PIN);
    tfw_hsm_loop_poll();
    ACTS(S1_ACTS);
    lines = 0u;
    tfw_hsm_loop_poll();
    NO_ACTS();
    lines = LINE(TFW_HSM_SIG_TAMPER_PIN);
    tfw_hsm_loop_poll();
    ACTS(S1_ACTS);
}

/* Two lines rising in one pass are raised in signal order: S1's rule, then S5,
 * which the set does not name (R2), so lockdown. */
static void test_lines_rising_together_are_raised_in_signal_order(void)
{
    load_policy();
    lines = (uint8_t)(LINE(TFW_HSM_SIG_TAMPER_PIN) | LINE(TFW_HSM_SIG_HOTPLUG));
    tfw_hsm_loop_poll();
    ACTS(S1_ACTS, LOCK);
    TEST_ASSERT_EQUAL_UINT8(1u, engine()->locked);
}

/* A port with a heartbeat line of its own raises S6 off it (R5). */
static void test_the_last_signal_line_is_sensed(void)
{
    load_policy();
    lines = LINE(TFW_HSM_SIG_HEARTBEAT_LOSS);
    tfw_hsm_loop_poll();
    ACTS(LOCK);
}

/* Bits above S6 name no signal and raise nothing. */
static void test_bits_above_the_last_signal_are_ignored(void)
{
    load_policy();
    lines = 0xC0u;
    tfw_hsm_loop_poll();
    NO_ACTS();
    TEST_ASSERT_EQUAL_UINT8(0u, engine()->locked);
}

/* Every pass ticks the engine, so silence past the heartbeat is S6 with no
 * message on the channel (R5). */
static void test_each_pass_keeps_the_heartbeat_deadline(void)
{
    load_policy();
    clock_ms = 1000u;
    tfw_hsm_loop_poll();
    NO_ACTS();
    clock_ms = 1001u;
    tfw_hsm_loop_poll();
    ACTS(LOCK);
    TEST_ASSERT_EQUAL_UINT8(1u, engine()->locked);
    clock_ms = 5000u;
    tfw_hsm_loop_poll();
    NO_ACTS();
}

/* A heartbeat over the channel moves the deadline the loop keeps. */
static void test_a_heartbeat_moves_the_deadline_the_loop_keeps(void)
{
    load_policy();
    clock_ms = 900u;
    tfw_hsm_heartbeat(engine(), clock_ms);
    clock_ms = 1500u;
    tfw_hsm_loop_poll();
    NO_ACTS();
    clock_ms = 1901u;
    tfw_hsm_loop_poll();
    ACTS(LOCK);
}

/* After a reset no line has been seen, so one still asserted is new. */
static void test_reset_forgets_the_lines_seen(void)
{
    load_policy();
    lines = LINE(TFW_HSM_SIG_TAMPER_PIN);
    tfw_hsm_loop_poll();
    ACTS(S1_ACTS);
    tfw_hsm_loop_reset();
    tfw_hsm_loop_poll();
    ACTS(S1_ACTS);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_pass_with_no_line_does_nothing);
    RUN_TEST(test_a_line_with_no_set_locks_down);
    RUN_TEST(test_a_held_line_is_one_event);
    RUN_TEST(test_a_line_released_and_asserted_again_is_a_second_event);
    RUN_TEST(test_lines_rising_together_are_raised_in_signal_order);
    RUN_TEST(test_the_last_signal_line_is_sensed);
    RUN_TEST(test_bits_above_the_last_signal_are_ignored);
    RUN_TEST(test_each_pass_keeps_the_heartbeat_deadline);
    RUN_TEST(test_a_heartbeat_moves_the_deadline_the_loop_keeps);
    RUN_TEST(test_reset_forgets_the_lines_seen);
    return UNITY_END();
}
