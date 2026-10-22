/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The channel's commands (src/core/hsm_cmd.c, docs/hsm-self-defence/ slice 4)
 * driven as the dispatcher drives them: a reader over the request payload, a
 * writer for the reply. The engine behind them is the real one; the port is a
 * mock that records each action and answers the tenant key, and the clock is
 * this file's.
 *
 * The envelope is the literal SUIT_POLICY_ENVELOPE (testdata/suit_vectors.h):
 * SUIT_POLICY_BODY, one policy whose one rule is S1: [keys-lock, shutdown],
 * heartbeat 1000 ms, under sequence 3. Among the rows here is the verifier
 * table's "larger than the buffer" (engineering.md): refused, TFW_ERR_RANGE.
 */
#include <string.h>
#include "unity.h"
#include "atecc608a_model.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/boot.h"
#include "tfw/hal.h"
#include "tfw/protocol.h"
#include "suit.h"
#include "suit_vectors.h"
#include "reader.h"
#include "writer.h"
#include "hsm.h"
#include "hsm_cmd.h"
#include "hsm_port.h"

static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

static tfw_ms_t clock_ms;
tfw_ms_t tfw_hal_millis(void) { return clock_ms; }

#define LOCK TFW_HSM_ACT_PHY_OFF, TFW_HSM_ACT_POWER_OFF, TFW_HSM_ACT_KEYS_LOCK, TFW_HSM_ACT_RECORD
#define SET    TFW_HSM_STATE_SET
#define LOCKED TFW_HSM_STATE_LOCKED

/* The mock port. */
static uint8_t acted[64];
static uint8_t n_acted;

tfw_status_t tfw_hsm_port_act(uint8_t action)
{
    TEST_ASSERT_LESS_THAN_UINT8(sizeof acted, n_acted);
    acted[n_acted++] = action;
    return TFW_OK;
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

typedef tfw_status_t (*handler_t)(tfw_reader_t *, tfw_writer_t *);

static uint8_t out[TFW_MAX_PAYLOAD];
static uint8_t out_n;
static uint8_t env[TFW_SUIT_INTEGRATED_MAX];

void setUp(void)
{
    tfw_model_reset();
    memset(test_handoff, 0, sizeof test_handoff);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    clock_ms = 0u;
    n_acted  = 0u;
    tfw_hsm_cmd_reset();
}

void tearDown(void) {}

static tfw_status_t call(handler_t fn, const uint8_t *p, uint8_t n)
{
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_status_t st;

    tfw_reader_init(&r, p, n);
    tfw_writer_init(&w, out, sizeof out);
    st = fn(&r, &w);
    TEST_ASSERT_TRUE(tfw_writer_ok(&w));
    out_n = tfw_writer_len(&w);
    if (st != TFW_OK) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, out_n, "a refusal carries no payload");
    }
    return st;
}

static tfw_status_t call1(handler_t fn, uint8_t arg)
{
    return call(fn, &arg, 1u);
}

/* The reply is the engine's state: [flags][selected][refused:4 LE]. */
static void state_is(uint8_t flags, uint8_t selected, uint32_t refused)
{
    const uint8_t want[TFW_HSM_STATE_LEN] = {
        flags, selected, (uint8_t)refused, (uint8_t)(refused >> 8),
        (uint8_t)(refused >> 16), (uint8_t)(refused >> 24),
    };

    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_STATE_LEN, out_n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, sizeof want);
}

static tfw_status_t chunk(uint16_t total, uint16_t offset, const uint8_t *b, uint8_t n)
{
    uint8_t p[TFW_MAX_PAYLOAD];

    TEST_ASSERT_LESS_OR_EQUAL_UINT8(TFW_MAX_PAYLOAD - TFW_HSM_LOAD_HDR, n);
    p[0] = (uint8_t)total;
    p[1] = (uint8_t)(total >> 8);
    p[2] = (uint8_t)offset;
    p[3] = (uint8_t)(offset >> 8);
    if (n != 0u) {
        memcpy(&p[TFW_HSM_LOAD_HDR], b, n);
    }
    return call(tfw_hsm_cmd_load, p, (uint8_t)(TFW_HSM_LOAD_HDR + n));
}

/* The whole envelope in full chunks; the status of the last. */
static tfw_status_t load(const uint8_t *e, uint16_t len)
{
    const uint8_t step = TFW_MAX_PAYLOAD - TFW_HSM_LOAD_HDR;
    tfw_status_t  st   = TFW_OK;

    for (uint16_t off = 0u; off < len && st == TFW_OK; off = (uint16_t)(off + step)) {
        uint8_t n = (uint8_t)((len - off < step) ? (len - off) : step);

        st = chunk(len, off, &e[off], n);
    }
    return st;
}

#define LOAD_POLICY() load(SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE)

static void test_a_reset_holds_no_set(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_hsm_cmd_heartbeat, NULL, 0u));
    state_is(0u, 0u, 0u);
    NO_ACTS();
}

static void test_a_chunked_envelope_is_held_once_whole(void)
{
    const uint8_t step = TFW_MAX_PAYLOAD - TFW_HSM_LOAD_HDR;

    TEST_ASSERT_EQUAL(TFW_OK, chunk(sizeof SUIT_POLICY_ENVELOPE, 0u, SUIT_POLICY_ENVELOPE, step));
    state_is(0u, 0u, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, LOAD_POLICY());
    state_is(SET, 0u, 0u);
    NO_ACTS();
}

static void test_chunks_of_any_size_reassemble(void)
{
    static const uint8_t sizes[] = { 1u, 7u, 33u };

    for (uint8_t i = 0u; i < sizeof sizes; i++) {
        tfw_status_t st  = TFW_OK;
        uint16_t     off = 0u;

        tfw_hsm_cmd_reset();
        /* An empty first chunk opens the envelope and moves nothing. */
        TEST_ASSERT_EQUAL(TFW_OK, chunk(sizeof SUIT_POLICY_ENVELOPE, 0u, NULL, 0u));
        while (off < sizeof SUIT_POLICY_ENVELOPE && st == TFW_OK) {
            uint8_t n = (uint8_t)((sizeof SUIT_POLICY_ENVELOPE - off < sizes[i])
                                      ? (sizeof SUIT_POLICY_ENVELOPE - off) : sizes[i]);

            st  = chunk(sizeof SUIT_POLICY_ENVELOPE, off, &SUIT_POLICY_ENVELOPE[off], n);
            off = (uint16_t)(off + n);
        }
        TEST_ASSERT_EQUAL(TFW_OK, st);
        state_is(SET, 0u, 0u);
    }
}

/* The verifier table's row: larger than the buffer is refused before a byte
 * is taken, and counted (R7). */
static void test_an_envelope_larger_than_the_buffer_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
                      chunk((uint16_t)(TFW_SUIT_INTEGRATED_MAX + 1u), 0u, SUIT_POLICY_ENVELOPE, 60u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, chunk(0xFFFFu, 0u, SUIT_POLICY_ENVELOPE, 60u));
    /* Nothing is pending: its next chunk has nothing to follow. */
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
                      chunk((uint16_t)(TFW_SUIT_INTEGRATED_MAX + 1u), 60u, SUIT_POLICY_ENVELOPE, 60u));
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_hsm_cmd_heartbeat, NULL, 0u));
    state_is(0u, 0u, 2u);
}

static void test_an_envelope_that_fills_the_buffer_is_read(void)
{
    /* SUIT_POLICY_ENVELOPE with zero padding to the buffer's size: buffered
     * whole and handed on, and refused by the reader, not the channel. */
    memset(env, 0, sizeof env);
    memcpy(env, SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, load(env, (uint16_t)sizeof env));
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_hsm_cmd_heartbeat, NULL, 0u));
    state_is(0u, 0u, 1u);
}

static void test_an_empty_envelope_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, chunk(0u, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_hsm_cmd_heartbeat, NULL, 0u));
    state_is(0u, 0u, 1u);
}

/* Out of sequence: a gap, a step back, a changed total, a chunk past the
 * total, a continuation with nothing begun. Each drops what was pending. */
static void test_a_chunk_out_of_sequence_drops_the_envelope(void)
{
    const uint16_t total = sizeof SUIT_POLICY_ENVELOPE;
    const uint8_t *e     = SUIT_POLICY_ENVELOPE;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, chunk(total, 60u, &e[60], 60u));

    TEST_ASSERT_EQUAL(TFW_OK, chunk(total, 0u, e, 60u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, chunk(total, 61u, &e[61], 59u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, chunk(total, 60u, &e[60], 60u));

    TEST_ASSERT_EQUAL(TFW_OK, chunk(total, 0u, e, 60u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, chunk(total, 0u + 30u, &e[30], 30u));

    TEST_ASSERT_EQUAL(TFW_OK, chunk(total, 0u, e, 60u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, chunk((uint16_t)(total + 1u), 60u, &e[60], 60u));

    TEST_ASSERT_EQUAL(TFW_OK, chunk(10u, 0u, e, 8u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, chunk(10u, 8u, &e[8], 3u));

    /* None of these was an envelope: nothing counted, nothing held. */
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_hsm_cmd_heartbeat, NULL, 0u));
    state_is(0u, 0u, 0u);
    NO_ACTS();
}

/* A new offset 0 starts over, whatever was pending. */
static void test_offset_zero_begins_again(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, chunk(sizeof SUIT_POLICY_ENVELOPE, 0u, SUIT_SCRIPT_ENVELOPE, 60u));
    TEST_ASSERT_EQUAL(TFW_OK, LOAD_POLICY());
    state_is(SET, 0u, 0u);
}

static void test_a_bad_signature_is_refused_and_counted(void)
{
    memcpy(env, SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE);
    env[57] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, load(env, sizeof SUIT_POLICY_ENVELOPE));
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_hsm_cmd_heartbeat, NULL, 0u));
    state_is(0u, 0u, 1u);
    /* A refusal leaves the set it found. */
    TEST_ASSERT_EQUAL(TFW_OK, LOAD_POLICY());
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, load(env, sizeof SUIT_POLICY_ENVELOPE));
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_hsm_cmd_heartbeat, NULL, 0u));
    state_is(SET, 0u, 2u);
}

/* The floor's own set again (ADR D4): taken, and nothing counted. */
static void test_the_same_set_again_is_taken(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, LOAD_POLICY());
    TEST_ASSERT_EQUAL(TFW_OK, LOAD_POLICY());
    state_is(SET, 0u, 0u);
}

static void test_select_names_a_policy_of_the_set(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, call1(tfw_hsm_cmd_select, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, LOAD_POLICY());
    TEST_ASSERT_EQUAL(TFW_OK, call1(tfw_hsm_cmd_select, 0u));
    state_is(SET, 0u, 0u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, call1(tfw_hsm_cmd_select, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, call1(tfw_hsm_cmd_select, 0xFFu));
    NO_ACTS();
}

/* S4 and S5 only: the A55 cannot claim the M7's own signals. */
static void test_report_admits_only_the_a55s_signals(void)
{
    static const uint8_t refused[] = {
        0u, TFW_HSM_SIG_TAMPER_PIN, TFW_HSM_SIG_AHAB_FAILURE, TFW_HSM_SIG_MEASUREMENT,
        TFW_HSM_SIG_HEARTBEAT_LOSS, 7u, 0xFFu,
    };

    TEST_ASSERT_EQUAL(TFW_OK, LOAD_POLICY());
    for (uint8_t i = 0u; i < sizeof refused; i++) {
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, call1(tfw_hsm_cmd_report, refused[i]));
    }
    NO_ACTS();
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_hsm_cmd_heartbeat, NULL, 0u));
    state_is(SET, 0u, 0u);
}

/* R1: with no P, a reported signal is the built-in default's lockdown. */
static void test_report_with_no_set_locks_down(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, call1(tfw_hsm_cmd_report, TFW_HSM_SIG_HOTPLUG));
    ACTS(LOCK);
    state_is(LOCKED, 0u, 0u);
}

/* R2: P names S1 alone, so S4 is a signal P does not name. */
static void test_report_of_a_signal_p_does_not_name_locks_down(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, LOAD_POLICY());
    TEST_ASSERT_EQUAL(TFW_OK, call1(tfw_hsm_cmd_report, TFW_HSM_SIG_IMA_VIOLATION));
    ACTS(LOCK);
    state_is(SET | LOCKED, 0u, 0u);
}

static void test_verdict_is_clear_or_compromised(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, LOAD_POLICY());
    TEST_ASSERT_EQUAL(TFW_OK, call1(tfw_hsm_cmd_verdict, TFW_HSM_VERDICT_CLEAR));
    state_is(SET, 0u, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, call1(tfw_hsm_cmd_verdict, TFW_HSM_VERDICT_COMPROMISED));
    state_is(SET, 0u, 0u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, call1(tfw_hsm_cmd_verdict, 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, call1(tfw_hsm_cmd_verdict, 0xFFu));
    NO_ACTS();
}

/* The heartbeat keeps S6 away; a late one finds S6 already raised (R5). */
static void test_a_heartbeat_in_time_keeps_s6_away(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, LOAD_POLICY());
    clock_ms = 1000u;
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_hsm_cmd_heartbeat, NULL, 0u));
    clock_ms = 2000u;
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_hsm_cmd_heartbeat, NULL, 0u));
    NO_ACTS();
    state_is(SET, 0u, 0u);
}

static void test_a_late_heartbeat_is_s6_at_the_next_message(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, LOAD_POLICY());
    clock_ms = 1001u;
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_hsm_cmd_heartbeat, NULL, 0u));
    ACTS(LOCK);
    state_is(SET | LOCKED, 0u, 0u);
}

static void test_any_message_applies_the_time_first(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, LOAD_POLICY());
    clock_ms = 5000u;
    TEST_ASSERT_EQUAL(TFW_OK, call1(tfw_hsm_cmd_verdict, TFW_HSM_VERDICT_CLEAR));
    ACTS(LOCK);
    state_is(SET | LOCKED, 0u, 0u);
}

/* A reset is the reset of the M7: no P, nothing pending, nothing counted. */
static void test_reset_clears_the_engine_and_the_pending_envelope(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, chunk(0u, 0u, NULL, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, LOAD_POLICY());
    TEST_ASSERT_EQUAL(TFW_OK, chunk(sizeof SUIT_POLICY_ENVELOPE, 0u, SUIT_POLICY_ENVELOPE, 60u));
    tfw_hsm_cmd_reset();
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
                      chunk(sizeof SUIT_POLICY_ENVELOPE, 60u, &SUIT_POLICY_ENVELOPE[60], 60u));
    TEST_ASSERT_EQUAL(TFW_OK, call(tfw_hsm_cmd_heartbeat, NULL, 0u));
    state_is(0u, 0u, 0u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_reset_holds_no_set);
    RUN_TEST(test_a_chunked_envelope_is_held_once_whole);
    RUN_TEST(test_chunks_of_any_size_reassemble);
    RUN_TEST(test_an_envelope_larger_than_the_buffer_is_refused);
    RUN_TEST(test_an_envelope_that_fills_the_buffer_is_read);
    RUN_TEST(test_an_empty_envelope_is_refused);
    RUN_TEST(test_a_chunk_out_of_sequence_drops_the_envelope);
    RUN_TEST(test_offset_zero_begins_again);
    RUN_TEST(test_a_bad_signature_is_refused_and_counted);
    RUN_TEST(test_the_same_set_again_is_taken);
    RUN_TEST(test_select_names_a_policy_of_the_set);
    RUN_TEST(test_report_admits_only_the_a55s_signals);
    RUN_TEST(test_report_with_no_set_locks_down);
    RUN_TEST(test_report_of_a_signal_p_does_not_name_locks_down);
    RUN_TEST(test_verdict_is_clear_or_compromised);
    RUN_TEST(test_a_heartbeat_in_time_keeps_s6_away);
    RUN_TEST(test_a_late_heartbeat_is_s6_at_the_next_message);
    RUN_TEST(test_any_message_applies_the_time_first);
    RUN_TEST(test_reset_clears_the_engine_and_the_pending_envelope);
    return UNITY_END();
}
