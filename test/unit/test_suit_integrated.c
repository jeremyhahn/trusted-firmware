/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The integrated-payload SUIT reader (src/core/suit_integrated.c), one case per
 * row of docs/hsm-self-defence/adr-response-policy-format.md D7, against the
 * literals in testdata/suit_vectors.h. The caller's key goes through the
 * element tier's tfw_crypto_verify_operator on the software element, so every
 * signature is checked by real ECDSA. The board's own key sits in the
 * image-signing slot, as on a board, to show it buys nothing here. Built with
 * the ATmega2560's profile, so its policy and script class ids (D2) are the
 * ones held.
 */
#include <string.h>
#include "unity.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "tfw/crypto.h"
#include "tfw/boot.h"
#include "suit.h"
#include "suit_vectors.h"

static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

static uint8_t env[sizeof SUIT_POLICY_ENVELOPE];
static uint8_t kind;

#define BOTH  ((uint8_t)(TFW_SUIT_KIND_POLICY | TFW_SUIT_KIND_SCRIPT))
#define UNSET 0xEEu

/* Where things sit in SUIT_POLICY_ENVELOPE: the signature at the same offset
 * as SUIT_ENVELOPE's (the auth wrapper is the same shape), the manifest's
 * sequence number, then the payload's
 * tstr key "#policy" and its bstr, the last 22 bytes. */
#define SIG_OFFSET   57u
#define MANIFEST_SEQ 128u
#define NAME_OFFSET  239u
#define BODY_HEAD    247u
#define BODY_OFFSET  248u

void setUp(void)
{
    tfw_model_reset();
    memset(test_handoff, 0, sizeof test_handoff);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    tfw_model_set_stored_pubkey(TFW_ATECC_SLOT_IMAGE, SUIT_PUB);
    memcpy(env, SUIT_POLICY_ENVELOPE, sizeof env);
    kind = UNSET;
}

void tearDown(void) {}

static tfw_status_t read_env(uint16_t n, const uint8_t key[TFW_SUIT_KEY_LEN],
                             tfw_suit_manifest_t *m, const uint8_t **payload)
{
    return tfw_suit_read_integrated(env, n, key, BOTH, m, payload, &kind);
}

static void test_the_vectors_are_where_the_offsets_say(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x58u, SUIT_POLICY_ENVELOPE[SIG_OFFSET - 2u]);
    TEST_ASSERT_EQUAL_HEX8(0x40u, SUIT_POLICY_ENVELOPE[SIG_OFFSET - 1u]);
    TEST_ASSERT_EQUAL_HEX8(0x67u, SUIT_POLICY_ENVELOPE[NAME_OFFSET]);
    TEST_ASSERT_EQUAL_MEMORY("#policy", &SUIT_POLICY_ENVELOPE[NAME_OFFSET + 1u], 7u);
    TEST_ASSERT_EQUAL_HEX8(0x56u, SUIT_POLICY_ENVELOPE[BODY_HEAD]);
    TEST_ASSERT_EQUAL_MEMORY(SUIT_POLICY_BODY, &SUIT_POLICY_ENVELOPE[BODY_OFFSET],
                             sizeof SUIT_POLICY_BODY);
    TEST_ASSERT_EQUAL_UINT(sizeof SUIT_POLICY_ENVELOPE, BODY_OFFSET + sizeof SUIT_POLICY_BODY);
}

/* D7: a good envelope under its key. */
static void test_a_good_envelope_under_its_key_is_read(void)
{
    tfw_suit_manifest_t m;
    const uint8_t      *payload = 0;

    TEST_ASSERT_EQUAL(TFW_OK, read_env(sizeof env, SUIT_TENANT_PUB, &m, &payload));
    TEST_ASSERT_EQUAL_UINT32(3u, m.seq);
    TEST_ASSERT_EQUAL_UINT32(sizeof SUIT_POLICY_BODY, m.image_size);
    TEST_ASSERT_EQUAL_MEMORY(SUIT_POLICY_DIGEST, m.image_digest, sizeof SUIT_POLICY_DIGEST);
    TEST_ASSERT_EQUAL_PTR(&env[BODY_OFFSET], payload);
    TEST_ASSERT_EQUAL_MEMORY(SUIT_POLICY_BODY, payload, sizeof SUIT_POLICY_BODY);
}

/* D7: the board key is refused for a policy envelope. */
static void test_the_board_key_is_refused_for_a_policy_envelope(void)
{
    tfw_suit_manifest_t m;
    const uint8_t      *payload = 0;

    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, read_env(sizeof env, SUIT_PUB, &m, &payload));
    TEST_ASSERT_NULL(payload);
}

/* And the other way: the image reader, which checks the board key, refuses
 * the policy envelope, and the policy key does not verify an image. */
static void test_the_image_reader_refuses_a_policy_envelope(void)
{
    tfw_suit_manifest_t m;

    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_suit_read(SUIT_POLICY_ENVELOPE_DETACHED,
                                                sizeof SUIT_POLICY_ENVELOPE_DETACHED, &m));
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_suit_read(SUIT_POLICY_ENVELOPE,
                                                sizeof SUIT_POLICY_ENVELOPE, &m));
}

static void test_the_policy_key_does_not_verify_an_image(void)
{
    tfw_suit_manifest_t m;
    const uint8_t      *payload = 0;

    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_suit_read_integrated(SUIT_ENVELOPE, sizeof SUIT_ENVELOPE,
                                                               SUIT_TENANT_PUB, BOTH, &m, &payload,
                                                               &kind));
    TEST_ASSERT_NULL(payload);
}

static void test_a_flipped_signature_is_refused(void)
{
    tfw_suit_manifest_t m;
    const uint8_t      *payload = 0;

    env[SIG_OFFSET + 5u] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, read_env(sizeof env, SUIT_TENANT_PUB, &m, &payload));
    TEST_ASSERT_NULL(payload);
}

/* A byte of the manifest (its sequence number) no longer matches the digest
 * the signature covers. */
static void test_a_flipped_manifest_is_refused(void)
{
    tfw_suit_manifest_t m;
    const uint8_t      *payload = 0;

    TEST_ASSERT_EQUAL_HEX8(0x03u, env[MANIFEST_SEQ]);
    env[MANIFEST_SEQ] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, read_env(sizeof env, SUIT_TENANT_PUB, &m, &payload));
    TEST_ASSERT_NULL(payload);
}

/* D7: a payload digest mismatch. The payload is outside the signed bytes, so
 * the signature holds and the digest is what refuses it. */
static void test_a_payload_digest_mismatch_is_refused(void)
{
    tfw_suit_manifest_t m;
    const uint8_t      *payload = 0;

    env[BODY_OFFSET + 10u] ^= 0x01u;
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, read_env(sizeof env, SUIT_TENANT_PUB, &m, &payload));
    TEST_ASSERT_NULL(payload);
}

/* D7: a detached payload where integrated is required. */
static void test_a_detached_payload_is_refused(void)
{
    tfw_suit_manifest_t m;
    const uint8_t      *payload = 0;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
                      tfw_suit_read_integrated(SUIT_POLICY_ENVELOPE_DETACHED,
                                               sizeof SUIT_POLICY_ENVELOPE_DETACHED,
                                               SUIT_TENANT_PUB, BOTH, &m, &payload, &kind));
    TEST_ASSERT_NULL(payload);
}

static void test_a_payload_under_another_name_is_refused(void)
{
    tfw_suit_manifest_t m;
    const uint8_t      *payload = 0;

    env[NAME_OFFSET + 2u] = (uint8_t)'q';
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, read_env(sizeof env, SUIT_TENANT_PUB, &m, &payload));
    TEST_ASSERT_NULL(payload);
}

static void test_a_payload_of_the_wrong_size_is_refused(void)
{
    tfw_suit_manifest_t m;
    const uint8_t      *payload = 0;

    env[BODY_HEAD] = 0x55u;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, read_env(sizeof env - 1u, SUIT_TENANT_PUB, &m, &payload));
    TEST_ASSERT_NULL(payload);
}

/* D7: truncation, at every length short of the whole. */
static void test_every_truncation_is_refused(void)
{
    tfw_suit_manifest_t m;
    const uint8_t      *payload = 0;
    uint16_t            n;

    for (n = 0u; n < sizeof env; n++) {
        TEST_ASSERT_EQUAL(TFW_ERR_RANGE, read_env(n, SUIT_TENANT_PUB, &m, &payload));
    }
    TEST_ASSERT_NULL(payload);
}

static void test_trailing_bytes_are_refused(void)
{
    static uint8_t      longer[sizeof SUIT_POLICY_ENVELOPE + 1u];
    tfw_suit_manifest_t m;
    const uint8_t      *payload = 0;

    memcpy(longer, SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, tfw_suit_read_integrated(longer, sizeof longer, SUIT_TENANT_PUB,
                                                           BOTH, &m, &payload, &kind));
    TEST_ASSERT_NULL(payload);
}

static void test_an_envelope_past_the_bound_is_refused(void)
{
    tfw_suit_manifest_t m;
    const uint8_t      *payload = 0;

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE,
                      tfw_suit_read_integrated(env, (uint16_t)(TFW_SUIT_INTEGRATED_MAX + 1u),
                                               SUIT_TENANT_PUB, BOTH, &m, &payload, &kind));
    TEST_ASSERT_NULL(payload);
}

/* Slice 2: the class ids of ADR D2 and the kind they claim. Each refusal
 * leaves the manifest, the payload pointer and the kind as they were. */
static void refused(tfw_status_t want, const uint8_t *e, uint16_t n, uint8_t expect)
{
    tfw_suit_manifest_t m;
    tfw_suit_manifest_t before;
    const uint8_t      *payload = 0;
    uint8_t             k       = UNSET;

    memset(&m, 0xA5, sizeof m);
    memset(&before, 0xA5, sizeof before);
    TEST_ASSERT_EQUAL(want, tfw_suit_read_integrated(e, n, SUIT_TENANT_PUB, expect, &m, &payload, &k));
    TEST_ASSERT_NULL(payload);
    TEST_ASSERT_EQUAL_HEX8(UNSET, k);
    TEST_ASSERT_EQUAL_MEMORY(&before, &m, sizeof m);
}

static void test_a_policy_set_is_read_as_a_policy_set(void)
{
    tfw_suit_manifest_t m;
    const uint8_t      *payload = 0;

    TEST_ASSERT_EQUAL(TFW_OK, read_env(sizeof env, SUIT_TENANT_PUB, &m, &payload));
    TEST_ASSERT_EQUAL_HEX8(TFW_SUIT_KIND_POLICY, kind);
    kind = UNSET;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_suit_read_integrated(env, sizeof env, SUIT_TENANT_PUB,
                                                       TFW_SUIT_KIND_POLICY, &m, &payload, &kind));
    TEST_ASSERT_EQUAL_HEX8(TFW_SUIT_KIND_POLICY, kind);
}

static void test_a_script_is_read_as_a_script(void)
{
    tfw_suit_manifest_t m;
    const uint8_t      *payload = 0;
    const uint16_t      at      = sizeof SUIT_SCRIPT_ENVELOPE - sizeof SUIT_SCRIPT_BODY;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_suit_read_integrated(SUIT_SCRIPT_ENVELOPE,
                                                       sizeof SUIT_SCRIPT_ENVELOPE, SUIT_TENANT_PUB,
                                                       BOTH, &m, &payload, &kind));
    TEST_ASSERT_EQUAL_HEX8(TFW_SUIT_KIND_SCRIPT, kind);
    TEST_ASSERT_EQUAL_UINT32(1u, m.seq);
    TEST_ASSERT_EQUAL_UINT32(sizeof SUIT_SCRIPT_BODY, m.image_size);
    TEST_ASSERT_EQUAL_PTR(&SUIT_SCRIPT_ENVELOPE[at], payload);
    TEST_ASSERT_EQUAL_MEMORY(SUIT_SCRIPT_BODY, payload, sizeof SUIT_SCRIPT_BODY);
    kind = UNSET;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_suit_read_integrated(SUIT_SCRIPT_ENVELOPE,
                                                       sizeof SUIT_SCRIPT_ENVELOPE, SUIT_TENANT_PUB,
                                                       TFW_SUIT_KIND_SCRIPT, &m, &payload, &kind));
    TEST_ASSERT_EQUAL_HEX8(TFW_SUIT_KIND_SCRIPT, kind);
}

/* D2: the verifier is told which class it expects. */
static void test_a_kind_the_caller_does_not_expect_is_refused(void)
{
    refused(TFW_ERR_UNSUPPORTED, SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE,
            TFW_SUIT_KIND_SCRIPT);
    refused(TFW_ERR_UNSUPPORTED, SUIT_SCRIPT_ENVELOPE, sizeof SUIT_SCRIPT_ENVELOPE,
            TFW_SUIT_KIND_POLICY);
    refused(TFW_ERR_UNSUPPORTED, SUIT_POLICY_ENVELOPE, sizeof SUIT_POLICY_ENVELOPE, 0u);
    refused(TFW_ERR_UNSUPPORTED, SUIT_SCRIPT_ENVELOPE, sizeof SUIT_SCRIPT_ENVELOPE, 0u);
}

/* D7 step 4: a class not this board's policy or script class, each validly
 * signed under the caller's key. */
static void test_the_image_class_is_refused(void)
{
    refused(TFW_ERR_UNSUPPORTED, SUIT_POLICY_ENVELOPE_CLASS_IMAGE,
            sizeof SUIT_POLICY_ENVELOPE_CLASS_IMAGE, BOTH);
}

static void test_another_boards_policy_class_is_refused(void)
{
    refused(TFW_ERR_UNSUPPORTED, SUIT_POLICY_ENVELOPE_CLASS_328P,
            sizeof SUIT_POLICY_ENVELOPE_CLASS_328P, BOTH);
}

static void test_a_class_of_all_zero_is_refused(void)
{
    refused(TFW_ERR_UNSUPPORTED, SUIT_POLICY_ENVELOPE_CLASS_ZERO,
            sizeof SUIT_POLICY_ENVELOPE_CLASS_ZERO, BOTH);
}

static void test_another_vendor_is_refused(void)
{
    refused(TFW_ERR_UNSUPPORTED, SUIT_POLICY_ENVELOPE_VENDOR, sizeof SUIT_POLICY_ENVELOPE_VENDOR,
            BOTH);
}

/* Step 4 runs before the payload's: another class with a bad payload is the
 * class's refusal, not the digest's. */
static void test_the_class_is_held_before_the_payload(void)
{
    static uint8_t e[sizeof SUIT_POLICY_ENVELOPE_CLASS_IMAGE];

    memcpy(e, SUIT_POLICY_ENVELOPE_CLASS_IMAGE, sizeof e);
    e[sizeof e - 1u] ^= 0x01u;
    refused(TFW_ERR_UNSUPPORTED, e, sizeof e, BOTH);
}

/* An envelope that claims one kind and carries the other. */
static void test_a_policy_class_carrying_a_script_is_refused(void)
{
    refused(TFW_ERR_UNSUPPORTED, SUIT_POLICY_ENVELOPE_CARRIES_SCRIPT,
            sizeof SUIT_POLICY_ENVELOPE_CARRIES_SCRIPT, BOTH);
}

static void test_a_script_class_carrying_a_policy_set_is_refused(void)
{
    refused(TFW_ERR_UNSUPPORTED, SUIT_SCRIPT_ENVELOPE_CARRIES_POLICY,
            sizeof SUIT_SCRIPT_ENVELOPE_CARRIES_POLICY, BOTH);
}

/* A payload of size 0 carries neither kind. The byte past the envelope is a
 * map head, so a reader that looked past the payload would take it for one. */
static void test_an_empty_payload_is_refused(void)
{
    static uint8_t e[sizeof SUIT_POLICY_ENVELOPE_EMPTY + 1u];

    memcpy(e, SUIT_POLICY_ENVELOPE_EMPTY, sizeof SUIT_POLICY_ENVELOPE_EMPTY);
    e[sizeof SUIT_POLICY_ENVELOPE_EMPTY] = 0xA0u;
    refused(TFW_ERR_UNSUPPORTED, e, sizeof SUIT_POLICY_ENVELOPE_EMPTY, BOTH);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_vectors_are_where_the_offsets_say);
    RUN_TEST(test_a_good_envelope_under_its_key_is_read);
    RUN_TEST(test_the_board_key_is_refused_for_a_policy_envelope);
    RUN_TEST(test_the_image_reader_refuses_a_policy_envelope);
    RUN_TEST(test_the_policy_key_does_not_verify_an_image);
    RUN_TEST(test_a_flipped_signature_is_refused);
    RUN_TEST(test_a_flipped_manifest_is_refused);
    RUN_TEST(test_a_payload_digest_mismatch_is_refused);
    RUN_TEST(test_a_detached_payload_is_refused);
    RUN_TEST(test_a_payload_under_another_name_is_refused);
    RUN_TEST(test_a_payload_of_the_wrong_size_is_refused);
    RUN_TEST(test_every_truncation_is_refused);
    RUN_TEST(test_trailing_bytes_are_refused);
    RUN_TEST(test_an_envelope_past_the_bound_is_refused);
    RUN_TEST(test_a_policy_set_is_read_as_a_policy_set);
    RUN_TEST(test_a_script_is_read_as_a_script);
    RUN_TEST(test_a_kind_the_caller_does_not_expect_is_refused);
    RUN_TEST(test_the_image_class_is_refused);
    RUN_TEST(test_another_boards_policy_class_is_refused);
    RUN_TEST(test_a_class_of_all_zero_is_refused);
    RUN_TEST(test_another_vendor_is_refused);
    RUN_TEST(test_the_class_is_held_before_the_payload);
    RUN_TEST(test_a_policy_class_carrying_a_script_is_refused);
    RUN_TEST(test_a_script_class_carrying_a_policy_set_is_refused);
    RUN_TEST(test_an_empty_payload_is_refused);
    return UNITY_END();
}
