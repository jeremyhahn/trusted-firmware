/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The decision engine (src/core/hsm_engine.c) against the level 1 engine table
 * of docs/hsm-self-defence/engineering.md: one test per row of
 * architecture.md section 5's table, one per rule R1 to R7, and the default
 * and replay rows of the verifier table (ADR D4's floor).
 *
 * The payloads are literal CBOR. Each is wrapped in an envelope by
 * tools/suit-emit/ and signed at run time with the throwaway tenant key
 * (private scalar 0x40..0x5F, public SUIT_TENANT_PUB), never a real key. The
 * port is a mock that records each action in order, so a test asserts the
 * exact sequence.
 */
#include <string.h>
#include "unity.h"
#include "atecc608a_model.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/boot.h"
#include "suit.h"
#include "sha256.h"
#include "uECC.h"
#include "emit.h"
#include "suit_vectors.h"
#include "hsm.h"
#include "hsm_port.h"

static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

static const uint8_t vendor[TFW_SUIT_ID_LEN]       = TFW_SUIT_VENDOR_ID;
static const uint8_t policy_class[TFW_SUIT_ID_LEN] = TFW_SUIT_POLICY_CLASS_ID;
static const uint8_t script_class[TFW_SUIT_ID_LEN] = TFW_SUIT_SCRIPT_CLASS_ID;

/* Script A: [record, shutdown]. Script B: [phy-off, keys-wipe, power-off].
 * Each set names a script by the SHA-256 of these bytes. */
static const uint8_t SCRIPT_A[] = { 0x82u, 0x06u, 0x05u };
static const uint8_t SCRIPT_B[] = { 0x83u, 0x01u, 0x04u, 0x02u };
#define DIG_A 0x70u, 0x0Au, 0x8Du, 0x12u, 0x88u, 0xAEu, 0x11u, 0x91u, 0x59u, 0xBBu, 0xEEu, \
              0x83u, 0x95u, 0x15u, 0x07u, 0x9Fu, 0x4Cu, 0x82u, 0x20u, 0x6Bu, 0xDBu, 0x6Bu, \
              0x71u, 0x09u, 0x37u, 0x4Bu, 0x9Bu, 0x65u, 0x25u, 0x4Du, 0xF3u, 0x41u
#define DIG_B 0x21u, 0xEBu, 0x54u, 0xCDu, 0x86u, 0x46u, 0x74u, 0x49u, 0xEEu, 0x52u, 0x40u, \
              0x5Eu, 0x6Du, 0xB3u, 0x2Eu, 0x1Fu, 0x95u, 0x56u, 0xECu, 0x65u, 0x80u, 0x78u, \
              0x1Bu, 0xCEu, 0x8Cu, 0xFFu, 0xCAu, 0x37u, 0xBAu, 0x01u, 0xD6u, 0xB9u

/* Set A, heartbeat 1000 ms, W 500 ms. Policy 0:
 *   S1: [keys-lock, shutdown], wipe
 *   S2: [record, keys-wipe, phy-off]
 *   S3: script A
 *   S4: script B, wipe
 *   S6: [record], wipe
 * Policy 1:
 *   S5: [record] */
static const uint8_t SET_A[] = {
    0xA5u, 0x01u, 0x01u, 0x02u, 0x82u,
    0x85u,
    0x83u, 0x01u, 0x82u, 0x03u, 0x05u, 0xF5u,
    0x83u, 0x02u, 0x83u, 0x06u, 0x04u, 0x01u, 0xF4u,
    0x83u, 0x03u, 0x58u, 0x20u, DIG_A, 0xF4u,
    0x83u, 0x04u, 0x58u, 0x20u, DIG_B, 0xF5u,
    0x83u, 0x06u, 0x81u, 0x06u, 0xF5u,
    0x81u, 0x83u, 0x05u, 0x81u, 0x06u, 0xF4u,
    0x03u, 0xA0u, 0x04u, 0x19u, 0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u,
};
/* Set B, laxer: one policy, S5: [phy-off]; heartbeat 2000 ms, W 100 ms. */
static const uint8_t SET_B[] = {
    0xA5u, 0x01u, 0x01u, 0x02u, 0x81u, 0x81u, 0x83u, 0x05u, 0x81u, 0x01u, 0xF4u,
    0x03u, 0xA0u, 0x04u, 0x19u, 0x07u, 0xD0u, 0x05u, 0x18u, 0x64u,
};
/* Set C: S3: script A, and nothing else. */
static const uint8_t SET_C[] = {
    0xA5u, 0x01u, 0x01u, 0x02u, 0x81u, 0x81u, 0x83u, 0x03u, 0x58u, 0x20u, DIG_A, 0xF4u,
    0x03u, 0xA0u, 0x04u, 0x19u, 0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u,
};
/* SET_A with signal 7 where S2 is: refused by the reader. */
static const uint8_t SET_BAD[] = {
    0xA5u, 0x01u, 0x01u, 0x02u, 0x81u, 0x81u, 0x83u, 0x07u, 0x81u, 0x01u, 0xF4u,
    0x03u, 0xA0u, 0x04u, 0x19u, 0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u,
};

#define LOCK TFW_HSM_ACT_PHY_OFF, TFW_HSM_ACT_POWER_OFF, TFW_HSM_ACT_KEYS_LOCK, TFW_HSM_ACT_RECORD
#define S1 TFW_HSM_SIG_TAMPER_PIN
#define S2 TFW_HSM_SIG_AHAB_FAILURE
#define S3 TFW_HSM_SIG_MEASUREMENT
#define S4 TFW_HSM_SIG_IMA_VIOLATION
#define S5 TFW_HSM_SIG_HOTPLUG
#define S6 TFW_HSM_SIG_HEARTBEAT_LOSS

static tfw_hsm_t h;
static uint8_t   buf[TFW_SUIT_INTEGRATED_MAX];
static uint8_t   priv[32];
static uint32_t  rng;

/* The mock port. */
static uint8_t      acted[64];
static uint8_t      n_acted;
static tfw_status_t wipe_st;
static tfw_status_t act_st;

tfw_status_t tfw_hsm_port_act(uint8_t action)
{
    TEST_ASSERT_LESS_THAN_UINT8(sizeof acted, n_acted);
    acted[n_acted++] = action;
    return action == TFW_HSM_ACT_KEYS_WIPE ? wipe_st : act_st;
}

/* The mock's record is exactly these actions, in order; then it is cleared. */
#define ACTS(...)                                                         \
    do {                                                                  \
        static const uint8_t want_[] = { __VA_ARGS__ };                   \
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(sizeof want_, n_acted, "count");  \
        TEST_ASSERT_EQUAL_HEX8_ARRAY(want_, acted, sizeof want_);         \
        n_acted = 0u;                                                     \
    } while (0)
#define NO_ACTS() TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, n_acted, "count")

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int ecdsa_rng(uint8_t *d, unsigned n)
{
    for (unsigned i = 0u; i < n; i++) {
        d[i] = (uint8_t)rnd();
    }
    return 1;
}

void setUp(void)
{
    uint8_t pub[64];

    rng = 0x5017C0DEu;
    tfw_model_reset();
    memset(test_handoff, 0, sizeof test_handoff);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    for (uint8_t i = 0u; i < sizeof priv; i++) {
        priv[i] = (uint8_t)(0x40u + i);
    }
    TEST_ASSERT_EQUAL_INT(1, uECC_compute_public_key(priv, pub, uECC_secp256r1()));
    TEST_ASSERT_EQUAL_MEMORY(SUIT_TENANT_PUB, pub, sizeof pub);
    n_acted = 0u;
    wipe_st = TFW_OK;
    act_st  = TFW_OK;
    tfw_hsm_init(&h, 0u);
}

void tearDown(void) {}

/* `body` under a manifest of sequence `seq` and class `cls`, signed with the
 * tenant key, in buf. */
static uint16_t envelope(const uint8_t *cls, const char *uri, const uint8_t *body, size_t bn,
                         uint32_t seq)
{
    uint8_t man[160];
    uint8_t dig[32];
    uint8_t tbs[192];
    uint8_t hash[32];
    uint8_t sig[64];
    size_t  mn;
    size_t  tn;
    size_t  n;

    tfw_sha256(body, (uint16_t)bn, dig);
    mn = tfw_suit_emit_manifest_integrated(man, sizeof man, dig, (uint32_t)bn, seq, vendor, cls,
                                           uri);
    TEST_ASSERT_NOT_EQUAL(0u, mn);
    tn = tfw_suit_emit_tbs(tbs, sizeof tbs, man, mn);
    TEST_ASSERT_NOT_EQUAL(0u, tn);
    tfw_sha256(tbs, (uint16_t)tn, hash);
    uECC_set_rng(ecdsa_rng);
    TEST_ASSERT_EQUAL_INT(1, uECC_sign(priv, hash, sizeof hash, sig, uECC_secp256r1()));
    n = tfw_suit_emit_envelope_integrated(buf, sizeof buf, man, mn, sig, uri, body, bn);
    TEST_ASSERT_NOT_EQUAL(0u, n);
    return (uint16_t)n;
}

static tfw_status_t set_at(const uint8_t *body, size_t bn, uint32_t seq, uint32_t now)
{
    uint16_t n = envelope(policy_class, "#policy", body, bn, seq);

    return tfw_hsm_load(&h, buf, n, SUIT_TENANT_PUB, now);
}

static tfw_status_t script(const uint8_t *body, size_t bn)
{
    uint16_t n = envelope(script_class, "#script", body, bn, 1u);

    return tfw_hsm_load(&h, buf, n, SUIT_TENANT_PUB, 0u);
}

#define SET(body, seq, now) set_at((body), sizeof(body), (seq), (now))

/* The engine is as `before` was, but for the refusal count and the reader's
 * scratch. */
static void unchanged_since(const tfw_hsm_t *before)
{
    static tfw_hsm_t now;

    memcpy(&now, &h, sizeof now);
    memcpy(&now.next, &before->next, sizeof now.next);
    now.refused = before->refused;
    TEST_ASSERT_EQUAL_MEMORY(before, &now, sizeof now);
}

static void test_the_script_digests_are_their_sha256(void)
{
    static const uint8_t a[] = { DIG_A };
    static const uint8_t b[] = { DIG_B };
    uint8_t              d[32];

    tfw_sha256(SCRIPT_A, sizeof SCRIPT_A, d);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, d, sizeof d);
    tfw_sha256(SCRIPT_B, sizeof SCRIPT_B, d);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(b, d, sizeof d);
}

/* --- architecture.md section 5's table, one test per row --------------- */

/* any | none | any | - | lockdown (R1) */
static void test_row_any_signal_with_no_policy_locks_down(void)
{
    for (uint8_t s = S1; s <= S6; s++) {
        tfw_hsm_init(&h, 0u);
        tfw_hsm_verdict(&h, TFW_HSM_VERDICT_COMPROMISED, 0u);
        tfw_hsm_signal(&h, s, 0u);
        ACTS(LOCK);
        TEST_ASSERT_EQUAL_UINT8(1u, h.locked);
        TEST_ASSERT_EQUAL_UINT8(0u, h.wiped);
    }
}

/* S1-S6 | yes | none, or not compromised | any | P's actions, wipe excluded;
 * lockdown if P names nothing (R2) */
static void test_row_signal_with_policy_and_no_compromised_verdict_runs_p(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    tfw_hsm_signal(&h, S1, 10u);
    ACTS(TFW_HSM_ACT_KEYS_LOCK, TFW_HSM_ACT_SHUTDOWN);
    tfw_hsm_signal(&h, S2, 10u);
    ACTS(TFW_HSM_ACT_RECORD, TFW_HSM_ACT_PHY_OFF);
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_CLEAR, 20u);
    tfw_hsm_signal(&h, S1, 30u);
    ACTS(TFW_HSM_ACT_KEYS_LOCK, TFW_HSM_ACT_SHUTDOWN);
    /* A verdict value the engine does not know is not "compromised". */
    tfw_hsm_verdict(&h, 2u, 40u);
    tfw_hsm_signal(&h, S1, 50u);
    ACTS(TFW_HSM_ACT_KEYS_LOCK, TFW_HSM_ACT_SHUTDOWN);
    TEST_ASSERT_EQUAL_UINT8(0u, h.locked);
    TEST_ASSERT_EQUAL_UINT8(0u, h.wiped);
    tfw_hsm_signal(&h, S5, 60u);
    ACTS(LOCK);
    TEST_ASSERT_EQUAL_UINT8(1u, h.locked);
}

/* S1-S6 | yes | compromised | wipe | wipe, committed by a strict operation,
 * then lockdown (R3, A4) */
static void test_row_compromised_verdict_and_a_wipe_rule_wipes_then_locks_down(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_COMPROMISED, 100u);
    tfw_hsm_signal(&h, S1, 200u);
    ACTS(TFW_HSM_ACT_KEYS_WIPE, LOCK);
    TEST_ASSERT_EQUAL_UINT8(1u, h.wiped);
    TEST_ASSERT_EQUAL_UINT8(1u, h.locked);
}

/* S1-S6 | yes | compromised | no wipe | P's actions (R2) */
static void test_row_compromised_verdict_and_no_wipe_rule_runs_p(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_COMPROMISED, 100u);
    tfw_hsm_signal(&h, S2, 200u);
    ACTS(TFW_HSM_ACT_RECORD, TFW_HSM_ACT_PHY_OFF);
    TEST_ASSERT_EQUAL_UINT8(0u, h.wiped);
    TEST_ASSERT_EQUAL_UINT8(0u, h.locked);
}

/* none | any | any | - | none; selection only (R4) */
static void test_row_no_signal_acts_on_nothing(void)
{
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_COMPROMISED, 0u);
    NO_ACTS();
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_COMPROMISED, 10u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_select(&h, 1u));
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_CLEAR, 20u);
    tfw_hsm_tick(&h, 1000u);
    NO_ACTS();
    TEST_ASSERT_EQUAL_UINT8(1u, h.selected);
    TEST_ASSERT_EQUAL_UINT8(0u, h.locked);
    TEST_ASSERT_EQUAL_UINT8(0u, h.wiped);
}

/* S6 | any | - | - | lockdown, never wipe (R5) */
static void test_row_heartbeat_loss_locks_down_and_never_wipes(void)
{
    /* No set; a set that names S6 with a wipe, under a compromised V in W. */
    tfw_hsm_signal(&h, S6, 0u);
    ACTS(LOCK);
    tfw_hsm_init(&h, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_COMPROMISED, 0u);
    tfw_hsm_signal(&h, S6, 0u);
    ACTS(LOCK);
    TEST_ASSERT_EQUAL_UINT8(0u, h.wiped);
    TEST_ASSERT_EQUAL_UINT8(1u, h.locked);
}

/* bad envelope | any | any | - | refused, counted (R7) */
static void test_row_a_bad_envelope_is_refused_and_counted(void)
{
    static tfw_hsm_t before;
    uint16_t         n;

    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 5u, 0u));
    memcpy(&before, &h, sizeof before);
    n = envelope(policy_class, "#policy", SET_B, sizeof SET_B, 6u);
    buf[n - 1u] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_hsm_load(&h, buf, n, SUIT_TENANT_PUB, 0u));
    TEST_ASSERT_EQUAL_UINT32(1u, h.refused);
    unchanged_since(&before);
    tfw_hsm_signal(&h, S2, 0u);
    ACTS(TFW_HSM_ACT_RECORD, TFW_HSM_ACT_PHY_OFF);
}

/* --- R1 to R7, each on its own ----------------------------------------- */

static void test_r1_no_policy_never_wipes(void)
{
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_COMPROMISED, 0u);
    tfw_hsm_signal(&h, S1, 0u);
    ACTS(LOCK);
    TEST_ASSERT_EQUAL_UINT8(0u, h.wiped);
}

static void test_r2_a_signal_p_does_not_name_locks_down(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_select(&h, 1u));
    tfw_hsm_signal(&h, S5, 0u);
    ACTS(TFW_HSM_ACT_RECORD);
    tfw_hsm_signal(&h, S1, 0u);
    ACTS(LOCK);
    TEST_ASSERT_EQUAL_UINT8(1u, h.locked);
    /* A signal outside S1-S6 is one no P names. */
    tfw_hsm_init(&h, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    tfw_hsm_signal(&h, 7u, 0u);
    ACTS(LOCK);
}

/* R3: W bounds the verdict's age inclusively; outside it, or not
 * compromised, the rule's actions run without the wipe. */
static void test_r3_a_wipe_needs_a_compromised_verdict_inside_w(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_COMPROMISED, 1000u);
    tfw_hsm_signal(&h, S1, 1501u);
    ACTS(TFW_HSM_ACT_KEYS_LOCK, TFW_HSM_ACT_SHUTDOWN);
    TEST_ASSERT_EQUAL_UINT8(0u, h.wiped);
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_CLEAR, 1600u);
    tfw_hsm_signal(&h, S1, 1600u);
    ACTS(TFW_HSM_ACT_KEYS_LOCK, TFW_HSM_ACT_SHUTDOWN);
    /* A later clear verdict replaces a compromised one. */
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_COMPROMISED, 1700u);
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_CLEAR, 1701u);
    tfw_hsm_signal(&h, S1, 1702u);
    ACTS(TFW_HSM_ACT_KEYS_LOCK, TFW_HSM_ACT_SHUTDOWN);
    TEST_ASSERT_EQUAL_UINT8(0u, h.locked);
    /* At W exactly, across the timer's wrap. */
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_COMPROMISED, 0xFFFFFF00u);
    tfw_hsm_signal(&h, S1, 0xF4u);
    ACTS(TFW_HSM_ACT_KEYS_WIPE, LOCK);
    TEST_ASSERT_EQUAL_UINT8(1u, h.wiped);
}

/* R3 through a script-ref: the wipe needs no script held, and a script's own
 * keys-wipe never runs outside R3. */
static void test_r3_a_scripted_rule_wipes_only_by_its_flag(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, script(SCRIPT_B, sizeof SCRIPT_B));
    tfw_hsm_signal(&h, S4, 0u);
    ACTS(TFW_HSM_ACT_PHY_OFF, TFW_HSM_ACT_POWER_OFF);
    TEST_ASSERT_EQUAL_UINT8(0u, h.wiped);
    tfw_hsm_init(&h, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_COMPROMISED, 0u);
    tfw_hsm_signal(&h, S4, 0u);
    ACTS(TFW_HSM_ACT_KEYS_WIPE, LOCK);
    TEST_ASSERT_EQUAL_UINT8(1u, h.wiped);
}

/* A4 is done only once the strict operation is acknowledged; lockdown follows
 * either way, and an action the SoC cannot perform stops none after it. */
static void test_r3_an_unacknowledged_wipe_is_not_counted_done(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    wipe_st = TFW_ERR_UNSUPPORTED;
    act_st  = TFW_ERR_UNSUPPORTED;
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_COMPROMISED, 0u);
    tfw_hsm_signal(&h, S1, 0u);
    ACTS(TFW_HSM_ACT_KEYS_WIPE, LOCK);
    TEST_ASSERT_EQUAL_UINT8(0u, h.wiped);
    TEST_ASSERT_EQUAL_UINT8(1u, h.locked);
}

static void test_r4_a_verdict_alone_acts_on_nothing(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_COMPROMISED, 7u);
    NO_ACTS();
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_VERDICT_COMPROMISED, h.verdict);
    TEST_ASSERT_EQUAL_UINT32(7u, h.verdict_at);
    TEST_ASSERT_EQUAL_UINT8(0u, h.locked);
}

/* R5 by time: silence past the set's heartbeat period is S6. */
static void test_r5_silence_past_the_heartbeat_period_locks_down(void)
{
    tfw_hsm_tick(&h, 0xFFFFFFFFu);
    NO_ACTS();
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 100u));
    tfw_hsm_tick(&h, 1100u);
    NO_ACTS();
    tfw_hsm_heartbeat(&h, 900u);
    tfw_hsm_tick(&h, 1900u);
    NO_ACTS();
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_COMPROMISED, 1900u);
    tfw_hsm_tick(&h, 1901u);
    ACTS(LOCK);
    TEST_ASSERT_EQUAL_UINT8(0u, h.wiped);
    /* Latched: the timer raises S6 no more. */
    tfw_hsm_tick(&h, 9000u);
    NO_ACTS();
}

/* R6: lockdown, then every input: a laxer set, a selection, a verdict, a
 * heartbeat, a tick, a signal. */
static void test_r6_lockdown_then_any_input_is_still_locked_down(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    tfw_hsm_signal(&h, S5, 0u);
    ACTS(LOCK);
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_B, 2u, 0u));
    TEST_ASSERT_EQUAL_UINT8(1u, h.locked);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_select(&h, 0u));
    tfw_hsm_verdict(&h, TFW_HSM_VERDICT_CLEAR, 0u);
    tfw_hsm_heartbeat(&h, 0u);
    tfw_hsm_tick(&h, 5000u);
    TEST_ASSERT_EQUAL_UINT8(1u, h.locked);
    tfw_hsm_signal(&h, S5, 5000u);
    ACTS(TFW_HSM_ACT_PHY_OFF);
    TEST_ASSERT_EQUAL_UINT8(1u, h.locked);
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, SET(SET_A, 1u, 0u));
    TEST_ASSERT_EQUAL_UINT8(1u, h.locked);
}

/* R7: each way an envelope fails, counted, the previous set staying. */
static void test_r7_a_refused_envelope_leaves_the_previous_policy(void)
{
    static tfw_hsm_t before;
    uint16_t         n;

    tfw_hsm_init(&h, 4u);
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 5u, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_select(&h, 1u));
    memcpy(&before, &h, sizeof before);
    /* The image key. */
    n = envelope(policy_class, "#policy", SET_B, sizeof SET_B, 6u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_hsm_load(&h, buf, n, SUIT_PUB, 0u));
    /* Below the floor. */
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, SET(SET_B, 4u, 0u));
    /* A payload the reader refuses, above the floor. */
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, SET(SET_BAD, 9u, 0u));
    /* A script the set does not name. */
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, script(SUIT_SCRIPT_BODY, sizeof SUIT_SCRIPT_BODY));
    TEST_ASSERT_EQUAL_UINT32(4u, h.refused);
    unchanged_since(&before);
    tfw_hsm_signal(&h, S5, 0u);
    ACTS(TFW_HSM_ACT_RECORD);
    /* The refused payload did not raise the floor: a set at 6 still loads. */
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_B, 6u, 0u));
    TEST_ASSERT_EQUAL_UINT32(4u, h.refused);
}

/* --- the rest of the engine table -------------------------------------- */

static void test_a_selection_out_of_range_is_refused_and_unchanged(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_select(&h, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_select(&h, 1u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_select(&h, 2u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_select(&h, 0xFFu));
    TEST_ASSERT_EQUAL_UINT8(1u, h.selected);
    tfw_hsm_signal(&h, S5, 0u);
    ACTS(TFW_HSM_ACT_RECORD);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_select(&h, 0u));
    tfw_hsm_signal(&h, S2, 0u);
    ACTS(TFW_HSM_ACT_RECORD, TFW_HSM_ACT_PHY_OFF);
}

static void test_a_script_the_engine_does_not_hold_locks_down(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    tfw_hsm_signal(&h, S3, 0u);
    ACTS(LOCK);
    tfw_hsm_init(&h, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, script(SCRIPT_A, sizeof SCRIPT_A));
    tfw_hsm_signal(&h, S3, 0u);
    ACTS(TFW_HSM_ACT_RECORD, TFW_HSM_ACT_SHUTDOWN);
    TEST_ASSERT_EQUAL_UINT8(0u, h.locked);
}

/* Scripts load only while named, once each, up to TFW_HSM_SCRIPTS_MAX; a new
 * set drops the ones it does not name. */
#define NINE_LEN (4u + 1u + 1u + 9u * 37u + 10u)

/* Script i of nine: [2, a, b], distinct for every i. */
static void nth_body(uint8_t i, uint8_t body[3])
{
    body[0] = 0x82u;
    body[1] = (uint8_t)(1u + i % 6u);
    body[2] = (uint8_t)(1u + i / 6u);
}

/* One policy whose nine S1 rules each name one of nine scripts. */
static void nine_named(uint8_t set[NINE_LEN])
{
    static const uint8_t tail[] = { 0x03u, 0xA0u, 0x04u, 0x19u, 0x03u, 0xE8u,
                                    0x05u, 0x19u, 0x01u, 0xF4u };
    uint8_t  body[3];
    uint16_t at;

    memcpy(set, (const uint8_t[]){ 0xA5u, 0x01u, 0x01u, 0x02u, 0x81u, 0x89u }, 6u);
    at = 6u;
    for (uint8_t i = 0u; i < 9u; i++) {
        nth_body(i, body);
        set[at++] = 0x83u;
        set[at++] = 0x01u;
        set[at++] = 0x58u;
        set[at++] = 0x20u;
        tfw_sha256(body, sizeof body, &set[at]);
        at = (uint16_t)(at + 32u);
        set[at++] = 0xF4u;
    }
    memcpy(&set[at], tail, sizeof tail);
    at = (uint16_t)(at + sizeof tail);
    TEST_ASSERT_EQUAL_UINT16(NINE_LEN, at);
}

static void test_scripts_are_held_while_named_and_bounded(void)
{
    static uint8_t set[NINE_LEN];
    uint8_t        body[3];

    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, script(SCRIPT_A, sizeof SCRIPT_A));
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 1u, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, script(SCRIPT_B, sizeof SCRIPT_B));
    TEST_ASSERT_EQUAL(TFW_OK, script(SCRIPT_A, sizeof SCRIPT_A));
    TEST_ASSERT_EQUAL(TFW_OK, script(SCRIPT_A, sizeof SCRIPT_A));
    TEST_ASSERT_EQUAL_UINT8(2u, h.n_scripts);
    /* SET_C names A alone, held second: the one kept moves down. */
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_C, 2u, 0u));
    TEST_ASSERT_EQUAL_UINT8(1u, h.n_scripts);
    tfw_hsm_signal(&h, S3, 0u);
    ACTS(TFW_HSM_ACT_RECORD, TFW_HSM_ACT_SHUTDOWN);
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_B, 3u, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, h.n_scripts);

    /* Nine scripts [2, a, b], each named by an S1 rule of one policy: eight
     * are held, the ninth finds no slot. */
    nine_named(set);
    TEST_ASSERT_EQUAL(TFW_OK, set_at(set, sizeof set, 4u, 0u));
    for (uint8_t i = 0u; i < 9u; i++) {
        nth_body(i, body);
        TEST_ASSERT_EQUAL(i < TFW_HSM_SCRIPTS_MAX ? TFW_OK : TFW_ERR_NO_SLOT,
                          script(body, sizeof body));
    }
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_SCRIPTS_MAX, h.n_scripts);
    TEST_ASSERT_EQUAL_UINT32(2u, h.refused);
    /* S1's first rule names the first script: [phy-off, phy-off]. */
    tfw_hsm_signal(&h, S1, 0u);
    ACTS(TFW_HSM_ACT_PHY_OFF, TFW_HSM_ACT_PHY_OFF);
}

/* load_script's TFW_ERR_NO_SLOT: a named, readable script past the last slot
 * is refused and counted, and the scripts held, the set and the floor stay. */
static void test_a_script_past_the_last_slot_is_refused_and_the_held_stay(void)
{
    static uint8_t   set[NINE_LEN];
    static tfw_hsm_t before;
    uint8_t          body[3];

    nine_named(set);
    TEST_ASSERT_EQUAL(TFW_OK, set_at(set, sizeof set, 4u, 0u));
    for (uint8_t i = 0u; i < TFW_HSM_SCRIPTS_MAX; i++) {
        nth_body(i, body);
        TEST_ASSERT_EQUAL(TFW_OK, script(body, sizeof body));
    }
    memcpy(&before, &h, sizeof before);
    nth_body(TFW_HSM_SCRIPTS_MAX, body);
    TEST_ASSERT_EQUAL(TFW_ERR_NO_SLOT, script(body, sizeof body));
    TEST_ASSERT_EQUAL_UINT32(before.refused + 1u, h.refused);
    unchanged_since(&before);
}

/* A named script whose payload is not D6's CDDL is refused and not held. */
static void test_a_named_script_that_does_not_read_is_refused(void)
{
    static const uint8_t bad[] = { 0x81u, 0x07u };
    uint8_t set[4u + 2u + 4u + 32u + 1u + 10u] = {
        0xA5u, 0x01u, 0x01u, 0x02u, 0x81u, 0x81u, 0x83u, 0x01u, 0x58u, 0x20u,
    };
    static const uint8_t tail[] = { 0xF4u, 0x03u, 0xA0u, 0x04u, 0x19u, 0x03u,
                                    0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u };

    tfw_sha256(bad, sizeof bad, &set[10]);
    memcpy(&set[42], tail, sizeof tail);
    TEST_ASSERT_EQUAL(TFW_OK, set_at(set, sizeof set, 1u, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, script(bad, sizeof bad));
    TEST_ASSERT_EQUAL_UINT8(0u, h.n_scripts);
    TEST_ASSERT_EQUAL_UINT32(1u, h.refused);
    tfw_hsm_signal(&h, S1, 0u);
    ACTS(LOCK);
}

/* --- ADR D4: the floor, the default and the replay rows ---------------- */

/* A policy set signed with the tenant key: set, floor and selection
 * replaced. */
static void test_a_set_signed_with_the_tenant_key_replaces_set_floor_and_selection(void)
{
    tfw_hsm_init(&h, 2u);
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 5u, 0u));
    TEST_ASSERT_EQUAL_UINT8(2u, h.set.n);
    TEST_ASSERT_EQUAL_UINT32(5u, h.floor);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_select(&h, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_B, 6u, 0u));
    TEST_ASSERT_EQUAL_UINT8(1u, h.set.n);
    TEST_ASSERT_EQUAL_UINT32(2000u, h.set.heartbeat_ms);
    TEST_ASSERT_EQUAL_UINT32(100u, h.set.window_ms);
    TEST_ASSERT_EQUAL_UINT32(6u, h.floor);
    TEST_ASSERT_EQUAL_UINT8(0u, h.selected);
    TEST_ASSERT_EQUAL_UINT32(0u, h.refused);
    tfw_hsm_signal(&h, S5, 0u);
    ACTS(TFW_HSM_ACT_PHY_OFF);
    /* Whole: S2, a rule of set A's first policy, is named no more. */
    tfw_hsm_signal(&h, S2, 0u);
    ACTS(LOCK);
}

/* The vector envelope of slice 1 loads, at its sequence number 3. */
static void test_the_policy_vector_loads(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_load(&h, SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE,
                                           SUIT_TENANT_PUB, 0u));
    TEST_ASSERT_EQUAL_UINT32(3u, h.floor);
    tfw_hsm_signal(&h, S1, 0u);
    ACTS(TFW_HSM_ACT_KEYS_LOCK, TFW_HSM_ACT_SHUTDOWN);
}

static void test_the_same_set_with_the_image_key_is_crypto(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_hsm_load(&h, SUIT_POLICY_ENVELOPE,
                                                   sizeof SUIT_POLICY_ENVELOPE, SUIT_PUB, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, h.set.n);
    TEST_ASSERT_EQUAL_UINT32(1u, h.refused);
}

/* One flipped bit in the signature, the manifest, the payload (offsets as
 * test_suit_integrated_mutate.c's), and every truncation. */
static void test_a_flipped_bit_or_a_truncation_is_refused(void)
{
    static const uint16_t at[] = { 13u, 57u, 120u, sizeof SUIT_POLICY_ENVELOPE - 1u };

    for (uint8_t i = 0u; i < sizeof at / sizeof at[0]; i++) {
        memcpy(buf, SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE);
        buf[at[i]] ^= 0x01u;
        TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO,
                          tfw_hsm_load(&h, buf, sizeof SUIT_POLICY_ENVELOPE, SUIT_TENANT_PUB, 0u));
    }
    for (uint16_t n = 0u; n < sizeof SUIT_POLICY_ENVELOPE; n++) {
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_hsm_load(&h, SUIT_POLICY_ENVELOPE, n,
                                                      SUIT_TENANT_PUB, 0u));
    }
    TEST_ASSERT_EQUAL_UINT8(0u, h.set.n);
    TEST_ASSERT_EQUAL_UINT32(sizeof at / sizeof at[0] + sizeof SUIT_POLICY_ENVELOPE, h.refused);
}

static void test_a_sequence_below_the_floor_is_protected_region(void)
{
    tfw_hsm_init(&h, 5u);
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, SET(SET_A, 4u, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, h.set.n);
    /* The built-in minimum itself is a floor no set has raised yet. */
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 5u, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_B, 9u, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, SET(SET_A, 8u, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, SET(SET_A, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT32(9u, h.floor);
    TEST_ASSERT_EQUAL_UINT32(3u, h.refused);
}

/* Equal to the floor: a re-send of the same manifest is accepted, signed
 * again or not; any other manifest at that number is not. */
static void test_a_sequence_equal_to_the_floor_needs_the_same_digest(void)
{
    static uint8_t first[TFW_SUIT_INTEGRATED_MAX];
    uint16_t       n;

    n = envelope(policy_class, "#policy", SET_A, sizeof SET_A, 7u);
    memcpy(first, buf, n);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_load(&h, first, n, SUIT_TENANT_PUB, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_select(&h, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hsm_load(&h, first, n, SUIT_TENANT_PUB, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_A, 7u, 0u));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(first, buf, n));
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, SET(SET_B, 7u, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, SET(SET_C, 7u, 0u));
    TEST_ASSERT_EQUAL_UINT32(2u, h.refused);
    TEST_ASSERT_EQUAL_UINT8(2u, h.set.n);
    TEST_ASSERT_EQUAL(TFW_OK, SET(SET_B, 8u, 0u));
    TEST_ASSERT_EQUAL_UINT8(1u, h.set.n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_script_digests_are_their_sha256);
    RUN_TEST(test_row_any_signal_with_no_policy_locks_down);
    RUN_TEST(test_row_signal_with_policy_and_no_compromised_verdict_runs_p);
    RUN_TEST(test_row_compromised_verdict_and_a_wipe_rule_wipes_then_locks_down);
    RUN_TEST(test_row_compromised_verdict_and_no_wipe_rule_runs_p);
    RUN_TEST(test_row_no_signal_acts_on_nothing);
    RUN_TEST(test_row_heartbeat_loss_locks_down_and_never_wipes);
    RUN_TEST(test_row_a_bad_envelope_is_refused_and_counted);
    RUN_TEST(test_r1_no_policy_never_wipes);
    RUN_TEST(test_r2_a_signal_p_does_not_name_locks_down);
    RUN_TEST(test_r3_a_wipe_needs_a_compromised_verdict_inside_w);
    RUN_TEST(test_r3_a_scripted_rule_wipes_only_by_its_flag);
    RUN_TEST(test_r3_an_unacknowledged_wipe_is_not_counted_done);
    RUN_TEST(test_r4_a_verdict_alone_acts_on_nothing);
    RUN_TEST(test_r5_silence_past_the_heartbeat_period_locks_down);
    RUN_TEST(test_r6_lockdown_then_any_input_is_still_locked_down);
    RUN_TEST(test_r7_a_refused_envelope_leaves_the_previous_policy);
    RUN_TEST(test_a_selection_out_of_range_is_refused_and_unchanged);
    RUN_TEST(test_a_script_the_engine_does_not_hold_locks_down);
    RUN_TEST(test_scripts_are_held_while_named_and_bounded);
    RUN_TEST(test_a_script_past_the_last_slot_is_refused_and_the_held_stay);
    RUN_TEST(test_a_named_script_that_does_not_read_is_refused);
    RUN_TEST(test_a_set_signed_with_the_tenant_key_replaces_set_floor_and_selection);
    RUN_TEST(test_the_policy_vector_loads);
    RUN_TEST(test_the_same_set_with_the_image_key_is_crypto);
    RUN_TEST(test_a_flipped_bit_or_a_truncation_is_refused);
    RUN_TEST(test_a_sequence_below_the_floor_is_protected_region);
    RUN_TEST(test_a_sequence_equal_to_the_floor_needs_the_same_digest);
    return UNITY_END();
}
