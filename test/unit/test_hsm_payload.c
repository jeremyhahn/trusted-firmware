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
#include <stdlib.h>
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

static void test_a_float64_is_a_type_the_cddl_lacks(void)
{
    /* wipe = 1.0 and 0.0 as float64: one status whatever the value. */
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x81u, 0x01u, 0xFBu, 0x3Fu,
         0xF0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, TAIL);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x81u, 0x01u, 0xFBu, 0x00u,
         0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, TAIL);
    /* A script that is a float64 1.0. */
    SCRIPT(TFW_ERR_UNSUPPORTED, 0xFBu, 0x3Fu, 0xF0u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u);
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

/* A cut just before a head: the reader looks at no byte past the cut. A
 * float64 lead byte past it is not read (head()'s n != 0); a set that ends
 * where a rule's response or its wipe begins reads nothing more (response()'s
 * n != 0, which ASan sees: READ's array ends at the cut). An eight-byte head
 * at the end with its four high bytes present and zero is too long a form;
 * with three of them it is a truncation (head()'s n > 4, both sides). */
static void test_a_cut_at_a_head_reads_nothing_past_it(void)
{
    static const uint8_t fl[] = { HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x81u, 0x01u, 0xFBu, 0x3Fu };

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, rd(fl, sizeof fl - 2u));
    READ(TFW_ERR_RANGE, HEAD, 0x81u, 0x81u, 0x83u, 0x01u);
    READ(TFW_ERR_RANGE, HEAD, 0x81u, 0x81u, 0x83u, 0x01u, 0x81u, 0x01u);
    READ(TFW_ERR_UNSUPPORTED, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA0u, 0x04u, 0x1Bu, 0x00u,
         0x00u, 0x00u, 0x00u);
    READ(TFW_ERR_RANGE, HEAD, 0x81u, 0x81u, RULE(1u), 0x03u, 0xA0u, 0x04u, 0x1Bu, 0x00u,
         0x00u, 0x00u);
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

/* Seeded mutational fuzz, as test_suit_mutate.c: xorshift32 from a literal
 * seed, so a failure replays; each input in a malloc of exactly its length,
 * so ASan sees a read past it. */
static uint32_t rng;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

#define FUZZ_CAP 512u

/* The valid sets of the literal tests above: the vector set, the truncation
 * test's and the bit-flip test's. */
static const uint8_t seed_trunc[] = {
    HEAD, 0x81u, 0x82u, 0x83u, 0x06u, 0x58u, 0x20u, DIGEST, 0xF5u, RULE(2u),
    0x03u, 0xA1u, 0x01u, 0x42u, 0xAAu, 0xBBu, 0x04u, 0x19u, 0x03u, 0xE8u,
    0x05u, 0x1Au, 0x00u, 0x01u, 0x00u, 0x00u,
};
static const uint8_t seed_flip[] = {
    HEAD, 0x82u,
    0x82u, 0x83u, 0x06u, 0x58u, 0x20u, DIGEST, 0xF5u,
           0x83u, 0x02u, 0x83u, 0x06u, 0x04u, 0x02u, 0xF4u,
    0x81u, RULE(0x05u),
    0x03u, 0xA2u, 0x01u, 0x41u, 0xAAu, 0x18u, 0x18u, 0x42u, 0xBBu, 0xCCu,
    0x04u, 0x00u, 0x05u, 0x1Au, 0x00u, 0x01u, 0x00u, 0x00u,
};
static const uint8_t seed_script3[] = { 0x83u, 0x05u, 0x04u, 0x06u };
static const uint8_t seed_script16[] = { 0x90u, X16(0x03u) };

typedef struct {
    const uint8_t *p;
    size_t         n;
} seed_t;

static const seed_t set_seeds[] = {
    { SUIT_POLICY_BODY, sizeof SUIT_POLICY_BODY },
    { seed_trunc, sizeof seed_trunc },
    { seed_flip, sizeof seed_flip },
};
static const seed_t script_seeds[] = {
    { seed_script3, sizeof seed_script3 },
    { seed_script16, sizeof seed_script16 },
};

/* CBOR lead bytes at the edges of each major type: the argument widths, the
 * reserved and indefinite forms, the empty and long containers, the simple
 * values and float64. */
static const uint8_t leads[] = {
    0x18u, 0x19u, 0x1Au, 0x1Bu, 0x1Cu, 0x1Fu, 0x38u, 0x58u, 0x59u, 0x5Fu, 0x7Fu,
    0x80u, 0x8Fu, 0x90u, 0x9Fu, 0xA0u, 0xBFu, 0xF4u, 0xF5u, 0xF6u, 0xFBu, 0xFFu,
};

/* One input into `b` (FUZZ_CAP bytes): a seed with 1 to 4 mutations, or one
 * time in 16 a random buffer of 0 to 300 bytes. Returns its length. */
static size_t make_input(uint8_t *b, const seed_t *seeds, size_t n_seeds)
{
    size_t n;

    if ((rnd() & 15u) == 0u) {
        n = rnd() % 301u;
        for (size_t i = 0u; i < n; i++) {
            b[i] = (uint8_t)rnd();
        }
        return n;
    }
    const seed_t *s = &seeds[rnd() % n_seeds];
    memcpy(b, s->p, s->n);
    n = s->n;
    for (uint32_t k = 1u + rnd() % 4u; k > 0u; k--) {
        size_t at = (n == 0u) ? 0u : rnd() % n;
        switch (rnd() % 7u) {
        case 0u:
            if (n > 0u) {
                b[at] = (uint8_t)rnd();
            }
            break;
        case 1u:
            if (n > 0u) {
                b[at] = leads[rnd() % sizeof leads];
            }
            break;
        case 2u:
            if (n < FUZZ_CAP) {
                memmove(&b[at + 1u], &b[at], n - at);
                b[at] = (uint8_t)rnd();
                n++;
            }
            break;
        case 3u:
            if (n > 0u) {
                memmove(&b[at], &b[at + 1u], n - at - 1u);
                n--;
            }
            break;
        case 4u:
            n = (n == 0u) ? 0u : rnd() % n;
            break;
        case 5u: {
            size_t len = (n - at == 0u) ? 0u : 1u + rnd() % (n - at);
            if (len > FUZZ_CAP - n) {
                len = FUZZ_CAP - n;
            }
            /* The tail moves up by len; the span stays, so it is twice. */
            memmove(&b[at + len], &b[at], n - at);
            n += len;
            break;
        }
        default: {
            const seed_t *o   = &seeds[rnd() % n_seeds];
            size_t        from = rnd() % o->n;
            size_t        len  = 1u + rnd() % (o->n - from);
            if (at + len > FUZZ_CAP) {
                len = FUZZ_CAP - at;
            }
            memcpy(&b[at], &o->p[from], len);
            if (at + len > n) {
                n = at + len;
            }
            break;
        }
        }
    }
    return n;
}

/* `b` copied into a malloc of exactly `n` bytes (one byte for n == 0, which
 * is passed as 0, so no byte of it may be read). */
static uint8_t *exact(const uint8_t *b, size_t n)
{
    uint8_t *e = malloc(n == 0u ? 1u : n);
    TEST_ASSERT_NOT_NULL(e);
    memcpy(e, b, n);
    return e;
}

static void check_set(const tfw_hsm_set_t *s)
{
    TEST_ASSERT_TRUE(s->n >= 1u && s->n <= TFW_HSM_POLICIES_MAX);
    for (uint8_t i = 0u; i < s->n; i++) {
        TEST_ASSERT_TRUE(s->policy[i].n >= 1u && s->policy[i].n <= TFW_HSM_RULES_MAX);
    }
    TEST_ASSERT_TRUE(s->n_expected <= TFW_HSM_EXPECTED_MAX);
    for (uint8_t i = 0u; i < s->n_expected; i++) {
        TEST_ASSERT_TRUE(s->expected[i].n <= TFW_HSM_EXPECTED_LEN);
        if (i > 0u) {
            TEST_ASSERT_TRUE(s->expected[i - 1u].key < s->expected[i].key);
        }
    }
}

/* Every mutated set is refused with one of the contract's two statuses or
 * read inside its bounds; a read set is consumed exactly (one byte more or
 * one less is refused), and a second read of the same bytes agrees. */
static void test_mutated_sets_read_inside_their_contract(void)
{
    static uint8_t       b[FUZZ_CAP + 1u];
    static tfw_hsm_set_t again;
    uint32_t             ok = 0u, range = 0u, unsupported = 0u;

    for (size_t i = 0u; i < sizeof set_seeds / sizeof set_seeds[0]; i++) {
        TEST_ASSERT_EQUAL(TFW_OK, rd(set_seeds[i].p, set_seeds[i].n));
    }
    rng = 20261127u;
    for (uint32_t it = 0u; it < 200000u; it++) {
        size_t       n = make_input(b, set_seeds, sizeof set_seeds / sizeof set_seeds[0]);
        uint8_t     *e = exact(b, n);
        tfw_status_t st;

        memset(&set, 0, sizeof set);
        memset(&again, 0, sizeof again);
        st = tfw_hsm_read_set(e, (uint16_t)n, &set);
        TEST_ASSERT_EQUAL(st, tfw_hsm_read_set(e, (uint16_t)n, &again));
        if (st == TFW_OK) {
            ok++;
            check_set(&set);
            TEST_ASSERT_EQUAL_MEMORY(&set, &again, sizeof set);
            free(e);
            b[n] = (uint8_t)rnd();
            e    = exact(b, n + 1u);
            TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_hsm_read_set(e, (uint16_t)(n + 1u), &again));
            TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_hsm_read_set(e, (uint16_t)(n - 1u), &again));
        } else if (st == TFW_ERR_RANGE) {
            range++;
        } else {
            TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, st);
            unsupported++;
        }
        free(e);
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0u, ok);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, range);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, unsupported);
}

/* The same harness for the script: 1 to 16 actions, consumed exactly. */
static void test_mutated_scripts_read_inside_their_contract(void)
{
    static uint8_t          b[FUZZ_CAP + 1u];
    static tfw_hsm_script_t again;
    uint32_t                ok = 0u, refused = 0u;

    for (size_t i = 0u; i < sizeof script_seeds / sizeof script_seeds[0]; i++) {
        TEST_ASSERT_EQUAL(TFW_OK, rs(script_seeds[i].p, script_seeds[i].n));
    }
    rng = 20261128u;
    for (uint32_t it = 0u; it < 100000u; it++) {
        size_t       n = make_input(b, script_seeds, sizeof script_seeds / sizeof script_seeds[0]);
        uint8_t     *e = exact(b, n);
        tfw_status_t st;

        memset(&script, 0, sizeof script);
        memset(&again, 0, sizeof again);
        st = tfw_hsm_read_script(e, (uint16_t)n, &script);
        TEST_ASSERT_EQUAL(st, tfw_hsm_read_script(e, (uint16_t)n, &again));
        if (st == TFW_OK) {
            ok++;
            TEST_ASSERT_TRUE(script.n >= 1u && script.n <= TFW_HSM_ACTIONS_MAX);
            TEST_ASSERT_EQUAL_MEMORY(&script, &again, sizeof script);
            free(e);
            b[n] = (uint8_t)rnd();
            e    = exact(b, n + 1u);
            TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_hsm_read_script(e, (uint16_t)(n + 1u), &again));
            TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_hsm_read_script(e, (uint16_t)(n - 1u), &again));
        } else {
            TEST_ASSERT_TRUE(st == TFW_ERR_RANGE || st == TFW_ERR_UNSUPPORTED);
            refused++;
        }
        free(e);
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0u, ok);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, refused);
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
    RUN_TEST(test_a_float64_is_a_type_the_cddl_lacks);
    RUN_TEST(test_each_bound_at_its_limit_and_one_past);
    RUN_TEST(test_every_truncation_is_refused);
    RUN_TEST(test_a_cut_at_a_head_reads_nothing_past_it);
    RUN_TEST(test_trailing_bytes_are_refused);
    RUN_TEST(test_every_bit_flip_is_refused_or_reads_another_set);
    RUN_TEST(test_mutated_sets_read_inside_their_contract);
    RUN_TEST(test_mutated_scripts_read_inside_their_contract);
    return UNITY_END();
}
