/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The policy payload reader (src/core/hsm_payload.c): ADR D5's policy set and
 * D6's script in deterministic CBOR, against literal payloads. One case per
 * refusal the engine table of docs/hsm-self-defence/engineering.md names for
 * the reader: an unknown map key, signal or action, a non-deterministic
 * encoding, and each bound at its limit and one past it. That a refusal
 * leaves the loaded set is the engine's, in test_hsm_engine.c.
 */
#include <string.h>
#include "unity.h"
#include "tfw/boot.h"
#include "hsm.h"
#include "suit_vectors.h"

static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

/* {1: 1, 2: ... */
#define HEAD 0xA5u, 0x01u, 0x01u, 0x02u
/* ... 3: {}, 4: 1000, 5: 500} */
#define TAIL 0x03u, 0xA0u, 0x04u, 0x19u, 0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u
/* [signal, [phy-off], false] */
#define RULE(s) 0x83u, (s), 0x81u, 0x01u, 0xF4u
#define X2(...)  __VA_ARGS__, __VA_ARGS__
#define X4(...)  X2(X2(__VA_ARGS__))
#define X8(...)  X2(X4(__VA_ARGS__))
#define X16(...) X2(X8(__VA_ARGS__))
#define DIGEST   X16(0xD1u, 0xD2u)

static tfw_hsm_set_t    set;
static tfw_hsm_script_t script;

void setUp(void)
{
    memset(&set, 0xA5, sizeof set);
    memset(&script, 0xA5, sizeof script);
}

void tearDown(void) {}

static tfw_status_t rd(const uint8_t *p, size_t n)
{
    return tfw_hsm_read_set(p, (uint16_t)n, &set);
}

static tfw_status_t rs(const uint8_t *p, size_t n)
{
    return tfw_hsm_read_script(p, (uint16_t)n, &script);
}

#define READ(want, ...)                                                   \
    do {                                                                  \
        static const uint8_t in_[] = { __VA_ARGS__ };                     \
        TEST_ASSERT_EQUAL_MESSAGE((want), rd(in_, sizeof in_), #want);    \
    } while (0)

#define SCRIPT(want, ...)                                                 \
    do {                                                                  \
        static const uint8_t in_[] = { __VA_ARGS__ };                     \
        TEST_ASSERT_EQUAL_MESSAGE((want), rs(in_, sizeof in_), #want);    \
    } while (0)

static void test_the_vector_policy_set_is_read(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, rd(SUIT_POLICY_BODY, sizeof SUIT_POLICY_BODY));
    TEST_ASSERT_EQUAL_UINT8(1u, set.n);
    TEST_ASSERT_EQUAL_UINT8(1u, set.policy[0].n);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_SIG_TAMPER_PIN, set.policy[0].rule[0].signal);
    TEST_ASSERT_EQUAL_UINT8(2u, set.policy[0].rule[0].n);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_ACT_KEYS_LOCK, set.policy[0].rule[0].body[0]);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_ACT_SHUTDOWN, set.policy[0].rule[0].body[1]);
    TEST_ASSERT_EQUAL_UINT8(0u, set.policy[0].rule[0].wipe);
    TEST_ASSERT_EQUAL_UINT8(0u, set.n_expected);
    TEST_ASSERT_EQUAL_UINT32(1000u, set.heartbeat_ms);
    TEST_ASSERT_EQUAL_UINT32(500u, set.window_ms);
}

static void test_a_script_ref_a_wipe_and_expected_values_are_read(void)
{
    static const uint8_t in[] = {
        HEAD, 0x82u,
        0x82u, 0x83u, 0x06u, 0x58u, 0x20u, DIGEST, 0xF5u,
               0x83u, 0x02u, 0x83u, 0x06u, 0x04u, 0x02u, 0xF4u,
        0x81u, RULE(0x05u),
        0x03u, 0xA2u, 0x01u, 0x41u, 0xAAu, 0x18u, 0x18u, 0x42u, 0xBBu, 0xCCu,
        0x04u, 0x00u, 0x05u, 0x1Au, 0x00u, 0x01u, 0x00u, 0x00u,
    };
    static const uint8_t dg[] = { DIGEST };

    TEST_ASSERT_EQUAL(TFW_OK, rd(in, sizeof in));
    TEST_ASSERT_EQUAL_UINT8(2u, set.n);
    TEST_ASSERT_EQUAL_UINT8(2u, set.policy[0].n);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_SIG_HEARTBEAT_LOSS, set.policy[0].rule[0].signal);
    TEST_ASSERT_EQUAL_UINT8(0u, set.policy[0].rule[0].n);
    TEST_ASSERT_EQUAL_MEMORY(dg, set.policy[0].rule[0].body, sizeof dg);
    TEST_ASSERT_EQUAL_UINT8(1u, set.policy[0].rule[0].wipe);
    TEST_ASSERT_EQUAL_UINT8(3u, set.policy[0].rule[1].n);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_ACT_RECORD, set.policy[0].rule[1].body[0]);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_ACT_KEYS_WIPE, set.policy[0].rule[1].body[1]);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_ACT_POWER_OFF, set.policy[0].rule[1].body[2]);
    TEST_ASSERT_EQUAL_UINT8(0u, set.policy[0].rule[1].wipe);
    TEST_ASSERT_EQUAL_UINT8(1u, set.policy[1].n);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_SIG_HOTPLUG, set.policy[1].rule[0].signal);
    TEST_ASSERT_EQUAL_UINT8(2u, set.n_expected);
    TEST_ASSERT_EQUAL_UINT32(1u, set.expected[0].key);
    TEST_ASSERT_EQUAL_UINT8(1u, set.expected[0].n);
    TEST_ASSERT_EQUAL_HEX8(0xAAu, set.expected[0].value[0]);
    TEST_ASSERT_EQUAL_UINT32(24u, set.expected[1].key);
    TEST_ASSERT_EQUAL_UINT8(2u, set.expected[1].n);
    TEST_ASSERT_EQUAL_HEX8(0xCCu, set.expected[1].value[1]);
    TEST_ASSERT_EQUAL_UINT32(0u, set.heartbeat_ms);
    TEST_ASSERT_EQUAL_UINT32(65536u, set.window_ms);
}

static void test_a_script_is_read(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, rs(SUIT_SCRIPT_BODY, sizeof SUIT_SCRIPT_BODY));
    SCRIPT(TFW_OK, 0x83u, 0x05u, 0x04u, 0x06u);
    TEST_ASSERT_EQUAL_UINT8(3u, script.n);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_ACT_SHUTDOWN, script.act[0]);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_ACT_KEYS_WIPE, script.act[1]);
    TEST_ASSERT_EQUAL_UINT8(TFW_HSM_ACT_RECORD, script.act[2]);
}

/* A map key the M7 does not know refuses the whole set (ADR D5). */
static void test_an_unknown_map_key_is_refused(void)
{
    READ(TFW_OK, HEAD, 0x81u, 0x81u, RULE(1u), TAIL);
    READ(TFW_ERR_UNSUPPORTED, 0xA6u, 0x01u, 0x01u, 0x02u, 0x81u, 0x81u, RULE(1u), TAIL,
         0x06u, 0x00u);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA0u, 0x04u, 0x19u,
         0x03u, 0xE8u, 0x06u, 0x19u, 0x01u, 0xF4u);
    READ(TFW_ERR_UNSUPPORTED, 0xA5u, 0x00u, 0x01u, 0x02u, 0x81u, 0x81u, RULE(1u), TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(1u), 0x07u, 0xA0u, 0x04u, 0x19u,
         0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA0u, 0x09u, 0x19u,
         0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u);
    /* A member missing is not the set either. */
    READ(TFW_ERR_UNSUPPORTED, 0xA4u, 0x01u, 0x01u, 0x02u, 0x81u, 0x81u, RULE(1u), 0x03u,
         0xA0u, 0x04u, 0x19u, 0x03u, 0xE8u);
    /* A text key, a negative key. */
    READ(TFW_ERR_UNSUPPORTED, 0xA5u, 0x61u, 0x31u, 0x01u, 0x02u, 0x81u, 0x81u, RULE(1u), TAIL);
    READ(TFW_ERR_UNSUPPORTED, 0xA5u, 0x20u, 0x01u, 0x02u, 0x81u, 0x81u, RULE(1u), TAIL);
}

static void test_a_version_other_than_1_is_refused(void)
{
    READ(TFW_ERR_UNSUPPORTED, 0xA5u, 0x01u, 0x02u, 0x02u, 0x81u, 0x81u, RULE(1u), TAIL);
    READ(TFW_ERR_UNSUPPORTED, 0xA5u, 0x01u, 0x00u, 0x02u, 0x81u, 0x81u, RULE(1u), TAIL);
}

static void test_an_unknown_signal_is_refused(void)
{
    READ(TFW_OK, HEAD, 0x81u, 0x81u, RULE(6u), TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(0u), TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(7u), TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(0x20u), TAIL);
}

static void test_an_unknown_action_is_refused(void)
{
    READ(TFW_OK, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x81u, 0x06u, 0xF4u, TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x81u, 0x00u, 0xF4u, TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x82u, 0x01u, 0x07u, 0xF4u, TAIL);
    SCRIPT(TFW_OK, 0x81u, 0x06u);
    SCRIPT(TFW_ERR_UNSUPPORTED, 0x81u, 0x00u);
    SCRIPT(TFW_ERR_UNSUPPORTED, 0x82u, 0x01u, 0x07u);
    SCRIPT(TFW_ERR_UNSUPPORTED, 0x81u, 0x21u);
}

/* RULE's shape: three members, a known response type, a bool. */
static void test_a_rule_not_of_the_cddl_is_refused(void)
{
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x82u, 0x01u, 0x81u, 0x01u, TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x84u, 0x01u, 0x81u, 0x01u, 0xF4u, 0xF4u, TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x01u, 0xF4u, TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x81u, 0x01u, 0x00u, TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x81u, 0x01u, 0x14u, TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x81u, 0x01u, 0x15u, TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x81u, 0x01u, 0xF6u, TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x81u, 0x01u, 0xF7u, TAIL);
    /* script-ref = bstr .size 32: 31 and 33 bytes are not one. */
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x58u, 0x1Fu, X16(0xD1u),
         X8(0xD1u), X4(0xD1u), X2(0xD1u), 0xD1u, 0xF4u, TAIL);
    READ(TFW_ERR_RANGE, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x58u, 0x21u, DIGEST, 0xD1u, 0xF4u,
         TAIL);
    /* A policy that is not an array, a set whose policies are not. */
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0xA0u, TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0xA1u, 0x00u, 0x81u, RULE(1u), TAIL);
    /* Expected values that are not a map of bstr. */
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0x80u, 0x04u, 0x19u,
         0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA1u, 0x01u, 0x61u,
         0x41u, 0x04u, 0x19u, 0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA1u, 0x20u, 0x41u,
         0x41u, 0x04u, 0x19u, 0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u);
    /* A period that is not a uint; a set or script of another type. */
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA0u, 0x04u, 0x39u,
         0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u);
    READ(TFW_ERR_UNSUPPORTED, 0x81u, 0x01u);
    SCRIPT(TFW_ERR_UNSUPPORTED, 0xA1u, 0x01u, 0x01u);
}

/* RFC 8949 section 4.2.1: shortest arguments, definite lengths, keys in
 * order. Each case is the valid set with one encoding made longer, made
 * indefinite or reordered. */
static void test_a_non_deterministic_encoding_is_refused(void)
{
    /* The version in two bytes. */
    READ(TFW_ERR_UNSUPPORTED, 0xA5u, 0x01u, 0x18u, 0x01u, 0x02u, 0x81u, 0x81u, RULE(1u), TAIL);
    /* A key in two bytes. */
    READ(TFW_ERR_UNSUPPORTED, 0xA5u, 0x18u, 0x01u, 0x01u, 0x02u, 0x81u, 0x81u, RULE(1u), TAIL);
    /* The map's count in two bytes; an array's. */
    READ(TFW_ERR_UNSUPPORTED, 0xB8u, 0x05u, 0x01u, 0x01u, 0x02u, 0x81u, 0x81u, RULE(1u), TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x98u, 0x01u, 0x81u, RULE(1u), TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x98u, 0x01u, 0x01u, 0xF4u, TAIL);
    /* A signal, an action in two bytes. */
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x18u, 0x01u, 0x81u, 0x01u, 0xF4u, TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x81u, 0x18u, 0x01u, 0xF4u, TAIL);
    /* 1000 in five bytes, 500 in nine, 255 in three. */
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA0u, 0x04u, 0x1Au,
         0x00u, 0x00u, 0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA0u, 0x04u, 0x19u,
         0x03u, 0xE8u, 0x05u, 0x1Bu, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u, 0xF4u);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA0u, 0x04u, 0x19u,
         0x00u, 0xFFu, 0x05u, 0x19u, 0x01u, 0xF4u);
    /* The shortest forms at each width's edge are read. */
    READ(TFW_OK, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA0u, 0x04u, 0x17u, 0x05u, 0x18u, 0x18u);
    READ(TFW_OK, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA0u, 0x04u, 0x18u, 0xFFu, 0x05u,
         0x19u, 0x01u, 0x00u);
    READ(TFW_OK, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA0u, 0x04u, 0x19u, 0xFFu, 0xFFu,
         0x05u, 0x1Au, 0xFFu, 0xFFu, 0xFFu, 0xFFu);
    /* A bool as a two-byte simple value; a bstr length in two bytes. */
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x81u, 0x01u, 0xF8u, 0x14u, TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x59u, 0x00u, 0x20u, DIGEST,
         0xF4u, TAIL);
    /* Indefinite lengths: an array, a map. */
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x9Fu, 0x81u, RULE(1u), 0xFFu, TAIL);
    READ(TFW_ERR_UNSUPPORTED, 0xBFu, 0x01u, 0x01u, 0x02u, 0x81u, 0x81u, RULE(1u), TAIL, 0xFFu);
    /* The keys out of order. */
    READ(TFW_ERR_UNSUPPORTED, 0xA5u, 0x02u, 0x81u, 0x81u, RULE(1u), 0x01u, 0x01u, 0x03u, 0xA0u,
         0x04u, 0x19u, 0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u);
    /* Expected values: keys descending, a key twice. */
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA2u, 0x02u, 0x40u, 0x01u,
         0x40u, 0x04u, 0x19u, 0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA2u, 0x01u, 0x40u, 0x01u,
         0x40u, 0x04u, 0x19u, 0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u);
    READ(TFW_OK, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA2u, 0x01u, 0x40u, 0x02u,
         0x40u, 0x04u, 0x19u, 0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u);
    /* A script's count in two bytes. */
    SCRIPT(TFW_ERR_UNSUPPORTED, 0x98u, 0x01u, 0x01u);
}

static void test_each_bound_at_its_limit_and_one_past(void)
{
    /* Policies. */
    READ(TFW_OK, HEAD, 0x88u, X8(0x81u, RULE(1u)), TAIL);
    TEST_ASSERT_EQUAL_UINT8(8u, set.n);
    READ(TFW_ERR_RANGE, HEAD, 0x89u, X8(0x81u, RULE(1u)), 0x81u, RULE(1u), TAIL);
    READ(TFW_ERR_RANGE, HEAD, 0x80u, TAIL);
    /* Rules. */
    READ(TFW_OK, HEAD, 0x81u, 0x90u, X16(RULE(2u)), TAIL);
    TEST_ASSERT_EQUAL_UINT8(16u, set.policy[0].n);
    READ(TFW_ERR_RANGE, HEAD, 0x81u, 0x91u, X16(RULE(2u)), RULE(2u), TAIL);
    READ(TFW_ERR_RANGE, HEAD, 0x81u, 0x80u, TAIL);
    /* Actions, in a response and in a script. */
    READ(TFW_OK, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x90u, X16(0x06u), 0xF4u, TAIL);
    TEST_ASSERT_EQUAL_UINT8(16u, set.policy[0].rule[0].n);
    READ(TFW_ERR_RANGE, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x91u, X16(0x06u), 0x06u, 0xF4u, TAIL);
    READ(TFW_ERR_RANGE, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x80u, 0xF4u, TAIL);
    SCRIPT(TFW_OK, 0x90u, X16(0x03u));
    TEST_ASSERT_EQUAL_UINT8(16u, script.n);
    SCRIPT(TFW_ERR_RANGE, 0x91u, X16(0x03u), 0x03u);
    SCRIPT(TFW_ERR_RANGE, 0x80u);
    /* Expected values: how many, and how long. */
    READ(TFW_OK, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA8u, 0x01u, 0x40u, 0x02u, 0x40u, 0x03u,
         0x40u, 0x04u, 0x40u, 0x05u, 0x40u, 0x06u, 0x40u, 0x07u, 0x40u, 0x08u, 0x40u, 0x04u,
         0x19u, 0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u);
    TEST_ASSERT_EQUAL_UINT8(8u, set.n_expected);
    READ(TFW_ERR_RANGE, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA9u, 0x01u, 0x40u, 0x02u, 0x40u,
         0x03u, 0x40u, 0x04u, 0x40u, 0x05u, 0x40u, 0x06u, 0x40u, 0x07u, 0x40u, 0x08u, 0x40u,
         0x09u, 0x40u, 0x04u, 0x19u, 0x03u, 0xE8u, 0x05u, 0x19u, 0x01u, 0xF4u);
    READ(TFW_OK, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA1u, 0x01u, 0x58u, 0x40u, X16(0xE1u),
         X16(0xE2u), X16(0xE3u), X16(0xE4u), 0x04u, 0x19u, 0x03u, 0xE8u, 0x05u, 0x19u, 0x01u,
         0xF4u);
    TEST_ASSERT_EQUAL_UINT8(64u, set.expected[0].n);
    TEST_ASSERT_EQUAL_HEX8(0xE4u, set.expected[0].value[63]);
    READ(TFW_ERR_RANGE, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA1u, 0x01u, 0x58u, 0x41u,
         X16(0xE1u), X16(0xE2u), X16(0xE3u), X16(0xE4u), 0xE5u, 0x04u, 0x19u, 0x03u, 0xE8u,
         0x05u, 0x19u, 0x01u, 0xF4u);
    /* A period past 32 bits. */
    READ(TFW_ERR_RANGE, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA0u, 0x04u, 0x1Bu, 0x00u,
         0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x05u, 0x19u, 0x01u, 0xF4u);
}

static void test_every_truncation_is_refused(void)
{
    static const uint8_t in[] = {
        HEAD, 0x81u, 0x82u, 0x83u, 0x06u, 0x58u, 0x20u, DIGEST, 0xF5u, RULE(2u),
        0x03u, 0xA1u, 0x01u, 0x42u, 0xAAu, 0xBBu, 0x04u, 0x19u, 0x03u, 0xE8u,
        0x05u, 0x1Au, 0x00u, 0x01u, 0x00u, 0x00u,
    };
    static const uint8_t sc[] = { 0x83u, 0x05u, 0x04u, 0x06u };

    TEST_ASSERT_EQUAL(TFW_OK, rd(in, sizeof in));
    for (size_t n = 0u; n < sizeof in; n++) {
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, rd(in, n));
    }
    TEST_ASSERT_EQUAL(TFW_OK, rs(sc, sizeof sc));
    for (size_t n = 0u; n < sizeof sc; n++) {
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, rs(sc, n));
    }
}

static void test_trailing_bytes_are_refused(void)
{
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(1u), TAIL, 0x00u);
    SCRIPT(TFW_ERR_UNSUPPORTED, 0x81u, 0x01u, 0x01u);
}

/* Deterministic CBOR has one encoding per value: every single-bit flip of a
 * valid set is refused, or reads as a different set. */
static void test_every_bit_flip_is_refused_or_reads_another_set(void)
{
    static const uint8_t in[] = {
        HEAD, 0x82u,
        0x82u, 0x83u, 0x06u, 0x58u, 0x20u, DIGEST, 0xF5u,
               0x83u, 0x02u, 0x83u, 0x06u, 0x04u, 0x02u, 0xF4u,
        0x81u, RULE(0x05u),
        0x03u, 0xA2u, 0x01u, 0x41u, 0xAAu, 0x18u, 0x18u, 0x42u, 0xBBu, 0xCCu,
        0x04u, 0x00u, 0x05u, 0x1Au, 0x00u, 0x01u, 0x00u, 0x00u,
    };
    static tfw_hsm_set_t want;
    uint8_t              b[sizeof in];
    uint32_t             refused = 0u;
    uint32_t             other   = 0u;

    memset(&set, 0, sizeof set);
    TEST_ASSERT_EQUAL(TFW_OK, rd(in, sizeof in));
    memcpy(&want, &set, sizeof want);
    for (uint16_t i = 0u; i < sizeof in; i++) {
        for (uint8_t bit = 0u; bit < 8u; bit++) {
            memcpy(b, in, sizeof b);
            b[i] ^= (uint8_t)(1u << bit);
            memset(&set, 0, sizeof set);
            if (rd(b, sizeof b) == TFW_OK) {
                TEST_ASSERT_NOT_EQUAL(0, memcmp(&want, &set, sizeof set));
                other++;
            } else {
                refused++;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT32(8u * sizeof in, refused + other);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, refused);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, other);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_vector_policy_set_is_read);
    RUN_TEST(test_a_script_ref_a_wipe_and_expected_values_are_read);
    RUN_TEST(test_a_script_is_read);
    RUN_TEST(test_an_unknown_map_key_is_refused);
    RUN_TEST(test_a_version_other_than_1_is_refused);
    RUN_TEST(test_an_unknown_signal_is_refused);
    RUN_TEST(test_an_unknown_action_is_refused);
    RUN_TEST(test_a_rule_not_of_the_cddl_is_refused);
    RUN_TEST(test_a_non_deterministic_encoding_is_refused);
    RUN_TEST(test_each_bound_at_its_limit_and_one_past);
    RUN_TEST(test_every_truncation_is_refused);
    RUN_TEST(test_trailing_bytes_are_refused);
    RUN_TEST(test_every_bit_flip_is_refused_or_reads_another_set);
    return UNITY_END();
}
