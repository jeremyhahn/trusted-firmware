/*
 * Mutation tests for src/crypto/atecc608a/provider.c (#163). test_atecc
 * proves each operation works; these pin what it lets slip: that every
 * operation puts the part back to sleep, that a failed step stops the steps
 * after it, which slot and mode each command names, and that both CDI copies
 * init holds are zeroed.
 *
 * Same software element as test_atecc. This link has no src/core/secure_zero.c:
 * the suite's own tfw_secure_zero keeps a copy of each 32-byte buffer it zeroes.
 */
#include <string.h>
#include "unity.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "tfw/crypto.h"
#include "tfw/hal.h"
#include "tfw/protocol.h"
#include "tfw/boot.h"
#include "boot_measure.h"
#include "handoff.h"
#include "sha256.h"

static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];
uint8_t *tfw_hal_handoff_block(void) { return test_handoff; }

#define ZEROED_MAX 8u
static uint8_t  zeroed[ZEROED_MAX][32];
static unsigned zeroed_n;

void tfw_secure_zero(void *p, size_t n)
{
    if (n == 32u && zeroed_n < ZEROED_MAX) {
        memcpy(zeroed[zeroed_n++], p, 32u);
    }
    memset(p, 0, n);
}

static unsigned zeroed_matching(const uint8_t v[32])
{
    unsigned i, hits = 0u;

    for (i = 0u; i < zeroed_n; i++) {
        hits += memcmp(zeroed[i], v, 32u) == 0 ? 1u : 0u;
    }
    return hits;
}

/* The first logged command from index `from` with this opcode, or null. */
static const tfw_model_cmd_t *find_op(uint8_t from, uint8_t opcode)
{
    uint8_t i;

    for (i = from; i < tfw_model_log_count(); i++) {
        if (tfw_model_log(i)->opcode == opcode) {
            return tfw_model_log(i);
        }
    }
    return 0;
}

static void init_provisioned(void)
{
    tfw_model_provision();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
}

static const uint8_t DIGEST[32] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f
};

void setUp(void)
{
    tfw_model_reset();
    memset(test_handoff, 0, sizeof test_handoff);
    tfw_boot_handoff_init();
    zeroed_n = 0u;
}

void tearDown(void) {}

/* A bus that will not start has no element on it: nothing is sent. */
void test_init_on_a_bus_that_will_not_start_sends_nothing(void)
{
    tfw_model_fail_i2c_init(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL(TFW_CRYPTO_NONE, tfw_crypto_tier());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
}

/* The CDI drained from the handoff is zeroed where it was drained to. */
void test_init_zeroes_the_drained_cdi(void)
{
    uint8_t cdi[32], measurement[32];
    uint8_t i;

    for (i = 0u; i < 32u; i++) { cdi[i] = (uint8_t)(0xC0u ^ i); measurement[i] = i; }
    tfw_boot_handoff_build(test_handoff, TFW_CRYPTO_SECURE_ELEMENT, cdi, measurement, 1u, 0u);
    tfw_boot_handoff_init();
    zeroed_n = 0u;

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT(2u, zeroed_matching(cdi));   /* the handoff's, and ours */
}

/* The UDS probe is HMAC(slot UDS, H(label)); its result is zeroed and the
 * part is left asleep. */
void test_init_probes_the_uds_slot_with_the_label_hash(void)
{
    static const char label[] = "tfw-unmeasured-application-v1";
    const tfw_model_cmd_t *start, *end;
    uint8_t meas[32], cdi[32];

    init_provisioned();
    TEST_ASSERT_FALSE(tfw_model_is_awake());

    start = find_op(0u, TFW_ATECC_OP_SHA);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_EQUAL_UINT8(TFW_ATECC_SHA_HMAC_START, start->p1);
    TEST_ASSERT_EQUAL_UINT16(TFW_ATECC_SLOT_UDS, start->p2);

    tfw_sha256((const uint8_t *)label, (uint16_t)(sizeof label - 1u), meas);
    end = tfw_model_log((uint8_t)(tfw_model_log_count() - 1u));
    TEST_ASSERT_EQUAL_UINT8(TFW_ATECC_SHA_HMAC_END, end->p1);
    TEST_ASSERT_EQUAL_UINT8(32u, end->dlen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(meas, end->data, 32);

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(TFW_ATECC_SLOT_UDS, meas, 32u, cdi));
    TEST_ASSERT_EQUAL_UINT(1u, zeroed_matching(cdi));
}

/* Every operation ends with the part asleep, TempKey and all cleared. */
void test_every_operation_leaves_the_part_asleep(void)
{
    uint8_t  out[64], sig[64], alg;
    uint32_t ctr;

    init_provisioned();
    (void)tfw_crypto_random(out, 8u);
    TEST_ASSERT_FALSE(tfw_model_is_awake());
    (void)tfw_crypto_hmac(TFW_ATECC_SLOT_UDS, DIGEST, 32u, out);
    TEST_ASSERT_FALSE(tfw_model_is_awake());
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sign(DIGEST, sig, &alg));
    TEST_ASSERT_FALSE(tfw_model_is_awake());
    (void)tfw_crypto_pubkey(out);
    TEST_ASSERT_FALSE(tfw_model_is_awake());
    (void)tfw_crypto_counter(0u, &ctr);
    TEST_ASSERT_FALSE(tfw_model_is_awake());
    (void)tfw_crypto_verify_operator(DIGEST, sig, out);
    TEST_ASSERT_FALSE(tfw_model_is_awake());
    (void)tfw_crypto_verify_image(DIGEST, sig);
    TEST_ASSERT_FALSE(tfw_model_is_awake());
}

/* A refused Random reports the bus's answer, and a one-byte request is
 * filled from the element. */
void test_random_reports_the_bus_and_fills_one_byte(void)
{
    uint8_t a = 0x00u, b = 0xFFu;

    init_provisioned();
    tfw_model_nack_next_command();
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_crypto_random(&a, 1u));

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_random(&a, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_random(&b, 1u));
    TEST_ASSERT_FALSE(a == 0x00u && b == 0xFFu);
}

/* Exactly one block goes in End, with no Update before it. */
void test_hmac_of_one_block_is_start_then_end(void)
{
    uint8_t data[64], out[32], from;

    init_provisioned();
    memset(data, 0x6B, sizeof data);
    from = tfw_model_log_count();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_hmac(TFW_ATECC_SLOT_UDS, data, 64u, out));
    TEST_ASSERT_EQUAL_UINT8(from + 2u, tfw_model_log_count());
    TEST_ASSERT_EQUAL_UINT8(TFW_ATECC_SHA_HMAC_END, tfw_model_log(from + 1u)->p1);
    TEST_ASSERT_EQUAL_UINT8(64u, tfw_model_log(from + 1u)->dlen);
}

/* A Start the element refuses ends the HMAC with its reason: no Update, no
 * End sent after it. */
void test_hmac_stops_at_a_refused_start(void)
{
    uint8_t data[100], out[32];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());      /* present, no UDS */
    memset(data, 0x2D, sizeof data);
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_hmac(TFW_ATECC_SLOT_UDS, data, 100u, out));
    TEST_ASSERT_EQUAL(TFW_ERR_UNSUPPORTED, tfw_crypto_hmac(TFW_ATECC_SLOT_UDS, data, 10u, out));
}

/* A refused Nonce means no Sign, and the algorithm out-parameter is left
 * alone. */
void test_sign_stops_at_a_refused_nonce(void)
{
    uint8_t sig[64], alg = 0xEEu, from;

    init_provisioned();
    from = tfw_model_log_count();
    tfw_model_nack_next_command();
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_crypto_sign(DIGEST, sig, &alg));
    TEST_ASSERT_EQUAL_UINT8(0xEEu, alg);
    TEST_ASSERT_NULL(find_op(from, TFW_ATECC_OP_SIGN));
}

/* GenKey names the DeviceID slot; any failure reads as not provisioned. */
void test_pubkey_names_the_deviceid_slot(void)
{
    uint8_t out[64], from;

    init_provisioned();
    from = tfw_model_log_count();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_pubkey(out));
    TEST_ASSERT_NOT_NULL(find_op(from, TFW_ATECC_OP_GENKEY));
    TEST_ASSERT_EQUAL_UINT16(TFW_ATECC_SLOT_DEVICEID, find_op(from, TFW_ATECC_OP_GENKEY)->p2);

    tfw_model_nack_next_command();
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, tfw_crypto_pubkey(out));
}

/* Counter 1 is the part's second counter, named in Param2; a refused read
 * reports the bus. */
void test_counter_one_is_read_by_index(void)
{
    uint32_t v;
    uint8_t  from;

    init_provisioned();
    from = tfw_model_log_count();
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_counter(1u, &v));
    TEST_ASSERT_NOT_NULL(find_op(from, TFW_ATECC_OP_COUNTER));
    TEST_ASSERT_EQUAL_UINT16(1u, find_op(from, TFW_ATECC_OP_COUNTER)->p2);

    tfw_model_nack_next_command();
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_crypto_counter(1u, &v));
}

/* A refused Nonce means no Verify, for an operator signature and an image. */
void test_verifies_stop_at_a_refused_nonce(void)
{
    uint8_t sig[64], pub[64], from;

    memset(sig, 0x11, sizeof sig);
    memset(pub, 0x22, sizeof pub);
    init_provisioned();
    from = tfw_model_log_count();
    tfw_model_nack_next_command();
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_crypto_verify_operator(DIGEST, sig, pub));
    tfw_model_nack_next_command();
    TEST_ASSERT_EQUAL(TFW_ERR_NACK, tfw_crypto_verify_image(DIGEST, sig));
    TEST_ASSERT_NULL(find_op(from, TFW_ATECC_OP_VERIFY));
}

/* The image check: digest passed through whole, Verify in stored mode
 * against the image slot, and a miscompare is a refusal. */
void test_verify_image_frames_and_refuses_a_miscompare(void)
{
    const tfw_model_cmd_t *nonce, *verify;
    uint8_t sig[64], from;

    memset(sig, 0x33, sizeof sig);
    init_provisioned();
    from = tfw_model_log_count();
    tfw_model_set_verify_result(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_verify_image(DIGEST, sig));
    nonce  = find_op(from, TFW_ATECC_OP_NONCE);
    verify = find_op(from, TFW_ATECC_OP_VERIFY);
    TEST_ASSERT_NOT_NULL(nonce);
    TEST_ASSERT_NOT_NULL(verify);
    TEST_ASSERT_EQUAL_UINT8(TFW_ATECC_NONCE_PASSTHRU, nonce->p1);
    TEST_ASSERT_EQUAL_UINT8(32u, nonce->dlen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DIGEST, nonce->data, 32);
    TEST_ASSERT_EQUAL_UINT8(TFW_ATECC_VERIFY_STORED, verify->p1);
    TEST_ASSERT_EQUAL_UINT16(TFW_ATECC_SLOT_IMAGE, verify->p2);

    tfw_model_set_verify_result(0u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_image(DIGEST, sig));
}

/* No element found at init: the verifies never touch the bus, even when a
 * part would answer now. */
void test_verifies_without_an_element_send_nothing(void)
{
    uint8_t sig[64], pub[64];

    memset(sig, 0x44, sizeof sig);
    memset(pub, 0x55, sizeof pub);
    tfw_model_provision();
    tfw_model_fail_i2c_init(1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_init());
    tfw_model_fail_i2c_init(0u);
    tfw_model_set_verify_result(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(DIGEST, sig, pub));
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_image(DIGEST, sig));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_model_log_count());
}

/* A part that will not wake for the image check is a crypto failure. */
void test_verify_image_on_a_part_that_will_not_wake(void)
{
    uint8_t sig[64];

    memset(sig, 0x66, sizeof sig);
    init_provisioned();
    tfw_model_fail_wakes_after(0u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, tfw_crypto_verify_image(DIGEST, sig));
}

/* SHA-256 is computed here, over public data, and never crosses the bus. */
void test_sha256_is_software_even_with_the_element(void)
{
    uint8_t want[32], got[32], from;

    init_provisioned();
    from = tfw_model_log_count();
    tfw_sha256(DIGEST, 32u, want);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_crypto_sha256(DIGEST, 32u, got));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, got, 32);
    TEST_ASSERT_EQUAL_UINT8(from, tfw_model_log_count());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sha256_is_software_even_with_the_element);
    RUN_TEST(test_init_on_a_bus_that_will_not_start_sends_nothing);
    RUN_TEST(test_init_zeroes_the_drained_cdi);
    RUN_TEST(test_init_probes_the_uds_slot_with_the_label_hash);
    RUN_TEST(test_every_operation_leaves_the_part_asleep);
    RUN_TEST(test_random_reports_the_bus_and_fills_one_byte);
    RUN_TEST(test_hmac_of_one_block_is_start_then_end);
    RUN_TEST(test_hmac_stops_at_a_refused_start);
    RUN_TEST(test_sign_stops_at_a_refused_nonce);
    RUN_TEST(test_pubkey_names_the_deviceid_slot);
    RUN_TEST(test_counter_one_is_read_by_index);
    RUN_TEST(test_verifies_stop_at_a_refused_nonce);
    RUN_TEST(test_verify_image_frames_and_refuses_a_miscompare);
    RUN_TEST(test_verifies_without_an_element_send_nothing);
    RUN_TEST(test_verify_image_on_a_part_that_will_not_wake);
    return UNITY_END();
}
