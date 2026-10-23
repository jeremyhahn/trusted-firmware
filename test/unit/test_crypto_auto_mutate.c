/*
 * Mutation tests for src/crypto/auto/provider.c (#163): the probe's three
 * checks, the sleep on both of its exits, the tier check after the element's
 * own init, the soft root's init only when no element answers, and each entry
 * forwarding its arguments to the provider the boot chose.
 *
 * Same link as test_crypto_auto: the software element is the I2C HAL, both
 * providers are renamed by CRYPTO_RENAME. The element is modelled, never
 * required (m435); the soft root is the default when no element answers.
 */
#include <string.h>
#include "unity.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/boot.h"
#include "../mock/uds_mock.h"

static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

/* src/core/auth_session_auto.c is on the link; its element half is not. */
void         tfw_auth_element_session_init(void) { }
tfw_status_t tfw_auth_element_gate(void) { return TFW_OK; }

tfw_crypto_tier_t tfw_crypto_soft_tier(void);
uint16_t     tfw_crypto_soft_algorithms(void);
tfw_status_t tfw_crypto_soft_random(uint8_t *out, uint8_t len);
tfw_status_t tfw_crypto_soft_hmac(uint8_t slot, const uint8_t *data, uint16_t len, uint8_t out[32]);
tfw_status_t tfw_crypto_soft_counter(uint8_t idx, uint32_t *out);
tfw_status_t tfw_crypto_element_pubkey(uint8_t out[64]);
uint16_t     tfw_crypto_element_algorithms(void);

static const uint8_t soft_uds[32] = {
    0x5a, 0x17, 0xc3, 0x09, 0x41, 0xee, 0x02, 0x7d, 0x90, 0x33, 0x6b, 0xa8, 0x1f, 0x54, 0xd2, 0x0c,
    0x77, 0x8e, 0x25, 0xb1, 0x4a, 0xf6, 0x03, 0x99, 0x6e, 0xc0, 0x38, 0x12, 0xdd, 0x81, 0x4f, 0xa6
};
static const uint8_t msg[3] = { 'a', 'b', 'c' };

void setUp(void)
{
    tfw_model_reset();
    memset(test_handoff, 0, sizeof test_handoff);
    tfw_mock_set_uds(soft_uds);
}

void tearDown(void) {}

/* A bus that will not start: the probe stops there and the part is never
 * spoken to. */
void test_a_bus_that_will_not_start_is_never_spoken_to(void)
{
    tfw_model_provision();
    tfw_model_fail_i2c_init(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SOFTWARE, tfw_crypto_tier());
}

/* A part that will not wake gets no command. */
void test_a_part_that_will_not_wake_gets_no_command(void)
{
    tfw_model_provision();
    tfw_model_fail_wakes_after(0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SOFTWARE, tfw_crypto_tier());
}

/* The probe is one INFO naming the revision. */
void test_the_probe_is_one_info_revision(void)
{
    const tfw_model_cmd_t *c;

    tfw_model_provision();
    tfw_model_force_status(0x0Fu);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_model_log_count());
    c = tfw_model_log(0u);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_OP_INFO, c->opcode);
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_INFO_REVISION, c->p1);
    TEST_ASSERT_EQUAL_HEX16(0u, c->p2);
}

/* An INFO that fails puts the part back to sleep and goes no further. */
void test_a_failed_probe_sleeps_the_part(void)
{
    tfw_model_provision();
    tfw_model_force_status(0x0Fu);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SOFTWARE, tfw_crypto_tier());
}

/* A probe that answers and an element init that then fails: the part is
 * asleep and the soft root runs, because the element's own tier says so. */
void test_an_element_whose_init_fails_runs_the_soft_root(void)
{
    tfw_model_provision();
    tfw_model_fail_commands_after(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_is_awake());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SOFTWARE, tfw_crypto_tier());
    TEST_ASSERT_EQUAL_HEX16(tfw_crypto_soft_algorithms(), tfw_crypto_algorithms());
}

/* An element boot never initialises the soft root: a soft root left
 * unprovisioned by a boot with no UDS stays so. */
void test_an_element_boot_leaves_the_soft_root_alone(void)
{
    uint8_t out[32];

    tfw_mock_set_uds(0);
    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_hmac(0u, msg, sizeof msg, out));

    tfw_model_reset();
    tfw_mock_set_uds(soft_uds);
    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_soft_hmac(0u, msg, sizeof msg, out));
}

/* The element boot: its algorithms and its public key. (Its SHA-256 is the
 * soft root's code by design, so which one answers cannot be told apart.) */
void test_an_element_boot_answers_algorithms_pubkey_and_sha(void)
{
    uint8_t got[64], want[64];

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_HEX16(tfw_crypto_element_algorithms(), tfw_crypto_algorithms());
    TEST_ASSERT_NOT_EQUAL(tfw_crypto_soft_algorithms(), tfw_crypto_algorithms());

    memset(got, 0, sizeof got);
    memset(want, 0xFF, sizeof want);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_element_pubkey(want));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 64u);
}

/* The element boot forwards the slot, the counter index and the length. */
void test_an_element_boot_forwards_slot_index_and_length(void)
{
    uint8_t  out[32];
    uint32_t v;
    const tfw_model_cmd_t *c;

    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());

    (void)tfw_crypto_hmac(3u, msg, sizeof msg, out);
    c = tfw_model_log(0u);
    for (uint8_t i = 0u; i < tfw_model_log_count(); i++) {
        if (tfw_model_log(i)->opcode == TFW_ATECC_OP_SHA &&
            tfw_model_log(i)->p1 == TFW_ATECC_SHA_HMAC_START) {
            c = tfw_model_log(i);
        }
    }
    TEST_ASSERT_EQUAL_HEX8(TFW_ATECC_SHA_HMAC_START, c->p1);
    TEST_ASSERT_EQUAL_HEX16(3u, c->p2);

    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_crypto_counter(2u, &v));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_crypto_random(out, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_crypto_random(out, 33u));
}

/* The soft boot: random and SHA-256 from the soft root, with no element on
 * the bus to answer either; the slot and length reach it. */
void test_a_soft_boot_answers_random_and_sha_without_an_element(void)
{
    static const uint8_t abc[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    uint8_t out[32];

    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SOFTWARE, tfw_crypto_tier());
    TEST_ASSERT_EQUAL(tfw_crypto_soft_random(out, 32u), tfw_crypto_random(out, 32u));
    TEST_ASSERT_EQUAL(tfw_crypto_soft_random(out, 0u), tfw_crypto_random(out, 0u));
    TEST_ASSERT_EQUAL(tfw_crypto_soft_random(out, 33u), tfw_crypto_random(out, 33u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256(msg, sizeof msg, out));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(abc, out, 32u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_crypto_hmac(1u, msg, sizeof msg, out));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(0u, msg, sizeof msg, out));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_bus_that_will_not_start_is_never_spoken_to);
    RUN_TEST(test_a_part_that_will_not_wake_gets_no_command);
    RUN_TEST(test_the_probe_is_one_info_revision);
    RUN_TEST(test_a_failed_probe_sleeps_the_part);
    RUN_TEST(test_an_element_whose_init_fails_runs_the_soft_root);
    RUN_TEST(test_an_element_boot_leaves_the_soft_root_alone);
    RUN_TEST(test_an_element_boot_answers_algorithms_pubkey_and_sha);
    RUN_TEST(test_an_element_boot_forwards_slot_index_and_length);
    RUN_TEST(test_a_soft_boot_answers_random_and_sha_without_an_element);
    return UNITY_END();
}
