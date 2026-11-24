/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The TF-M side of the DICE partition (#608 slice 3):
 * src/tz/tfm/tfw_dice_partition.c over stand-ins for TF-M's crypto service
 * (the HUK derivation), tf-m-extras' Measured Boot read and the SPM's
 * psa_read/psa_write (headers in test/unit/tfw_dice_glue/), linked to the real
 * partition core and service. Init with a locked SHA-256 measurement in
 * slot 3 starts the service; every refusal leaves it unprovisioned and still
 * lets TF-M boot; the UDS buffer is zero after init on every path; the SFN
 * hands the call's type and sizes to the dispatch as they are. The slot-3
 * measurement is the alias certificate's claim 273 hash and moves the alias
 * key, not the DeviceID.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "psa/service.h"
#include "psa/crypto.h"
#include "tfm_builtin_key_ids.h"
#include "measured_boot_api.h"
#include "tfw/status.h"
#include "tfw/crypto.h"
#include "sha256.h"
#include "handoff.h"
#include "../../src/caps/attest.h"
#include "../../src/tz/dice_partition.h"

/* src/caps/alias.c and the soft DeviceID provider link the device path's
 * providers; the partition's service never reaches them (its inputs come
 * from tfw_dice_partition_start), so each fails loudly if it is called. */
static int device_path_calls;
uint8_t tfw_boot_handoff_device_seed(uint8_t out[32])
{
    device_path_calls++;
    memset(out, 0, 32u);
    return 0u;
}
tfw_status_t tfw_crypto_pubkey(uint8_t out[64])
{
    device_path_calls++;
    memset(out, 0, 64u);
    return TFW_ERR_NOT_PROVISIONED;
}
tfw_status_t tfw_crypto_sign(const uint8_t digest[32], uint8_t sig[64], uint8_t *alg)
{
    (void)digest;
    (void)alg;
    device_path_calls++;
    memset(sig, 0, 64u);
    return TFW_ERR_NOT_PROVISIONED;
}
tfw_crypto_tier_t tfw_crypto_tier(void)
{
    device_path_calls++;
    return TFW_CRYPTO_SOFTWARE;
}
void tfw_attest_measurement(uint8_t out[TFW_SHA256_LEN])
{
    device_path_calls++;
    memset(out, 0, TFW_SHA256_LEN);
}
const uint8_t *tfw_attest_device_id(void)
{
    static const uint8_t none[32];

    device_path_calls++;
    return none;
}

psa_status_t tfw_dice_partition_init(void);
psa_status_t tfw_dice_service_sfn(const psa_msg_t *msg);

#define HANDLE 0x1234

/* The derivation stand-in: which step fails, what it saw. */
enum { STEP_NONE, STEP_SETUP, STEP_KEY, STEP_INFO, STEP_OUTPUT };
static int fail_step;
static int abort_calls;
static int setup_calls;
static psa_algorithm_t seen_alg;
static psa_key_id_t seen_key;
static psa_key_derivation_step_t seen_key_step, seen_info_step;
static char seen_info[32];
static size_t seen_info_len;
static size_t seen_out_len;
static uint8_t *uds_buf; /* the glue's UDS buffer, from output_bytes */

/* The Measured Boot stand-in. */
static psa_status_t mb_status;
static bool mb_locked;
static size_t mb_len;
static uint32_t mb_algo;
static uint8_t seen_index;
static int mb_calls;
static uint8_t mb_value[32]; /* the slot-3 measurement it hands out */

/* The SPM stand-in. */
static uint8_t in_vec[PSA_MAX_IOVEC][64];
static uint8_t out_vec[PSA_MAX_IOVEC][256];
static size_t out_len[PSA_MAX_IOVEC];
static int bad_handle;

psa_status_t psa_key_derivation_setup(psa_key_derivation_operation_t *operation,
                                      psa_algorithm_t alg)
{
    (void)operation;
    setup_calls++;
    seen_alg = alg;
    return fail_step == STEP_SETUP ? PSA_ERROR_GENERIC_ERROR : PSA_SUCCESS;
}

psa_status_t psa_key_derivation_input_key(psa_key_derivation_operation_t *operation,
                                          psa_key_derivation_step_t step,
                                          psa_key_id_t key)
{
    (void)operation;
    seen_key_step = step;
    seen_key = key;
    return fail_step == STEP_KEY ? PSA_ERROR_NOT_PERMITTED : PSA_SUCCESS;
}

psa_status_t psa_key_derivation_input_bytes(psa_key_derivation_operation_t *operation,
                                            psa_key_derivation_step_t step,
                                            const uint8_t *data,
                                            size_t data_length)
{
    (void)operation;
    seen_info_step = step;
    seen_info_len = data_length;
    if (data_length < sizeof(seen_info)) {
        memcpy(seen_info, data, data_length);
        seen_info[data_length] = '\0';
    }
    return fail_step == STEP_INFO ? PSA_ERROR_GENERIC_ERROR : PSA_SUCCESS;
}

psa_status_t psa_key_derivation_output_bytes(psa_key_derivation_operation_t *operation,
                                             uint8_t *output,
                                             size_t output_length)
{
    (void)operation;
    uds_buf = output;
    seen_out_len = output_length;
    /* A failing step may still have written part of the key. */
    memset(output, 0xA5, output_length);
    return fail_step == STEP_OUTPUT ? PSA_ERROR_GENERIC_ERROR : PSA_SUCCESS;
}

psa_status_t psa_key_derivation_abort(psa_key_derivation_operation_t *operation)
{
    (void)operation;
    abort_calls++;
    return PSA_SUCCESS;
}

psa_status_t tfm_measured_boot_read_measurement(uint8_t index,
                                                uint8_t *signer_id,
                                                size_t signer_id_size,
                                                size_t *signer_id_len,
                                                uint8_t *version,
                                                size_t version_size,
                                                size_t *version_len,
                                                uint32_t *measurement_algo,
                                                uint8_t *sw_type,
                                                size_t sw_type_size,
                                                size_t *sw_type_len,
                                                uint8_t *measurement_value,
                                                size_t measurement_value_size,
                                                size_t *measurement_value_len,
                                                bool *is_locked)
{
    mb_calls++;
    seen_index = index;
    if (mb_status != PSA_SUCCESS) {
        return mb_status;
    }
    TEST_ASSERT_TRUE(signer_id_size >= 32u && version_size >= 1u &&
                     sw_type_size >= 1u && measurement_value_size >= mb_len);
    memset(signer_id, 0x51, 32u);
    *signer_id_len = 32u;
    memcpy(version, "1.0.0", 5u);
    *version_len = 5u;
    memcpy(sw_type, "NS", 2u);
    *sw_type_len = 2u;
    *measurement_algo = mb_algo;
    memset(measurement_value, 0x3C, mb_len);
    memcpy(measurement_value, mb_value,
           mb_len < sizeof(mb_value) ? mb_len : sizeof(mb_value));
    *measurement_value_len = mb_len;
    *is_locked = mb_locked;
    return PSA_SUCCESS;
}

size_t psa_read(psa_handle_t msg_handle, uint32_t invec_idx, void *buffer,
                size_t num_bytes)
{
    if (msg_handle != HANDLE || invec_idx >= PSA_MAX_IOVEC) {
        bad_handle++;
        return 0;
    }
    if (num_bytes > sizeof(in_vec[0])) {
        num_bytes = sizeof(in_vec[0]);
    }
    memcpy(buffer, in_vec[invec_idx], num_bytes);
    return num_bytes;
}

void psa_write(psa_handle_t msg_handle, uint32_t outvec_idx,
               const void *buffer, size_t num_bytes)
{
    if (msg_handle != HANDLE || outvec_idx >= PSA_MAX_IOVEC ||
        num_bytes > sizeof(out_vec[0])) {
        bad_handle++;
        return;
    }
    memcpy(out_vec[outvec_idx], buffer, num_bytes);
    out_len[outvec_idx] = num_bytes;
}

static psa_msg_t call(int32_t type)
{
    psa_msg_t m;

    memset(&m, 0, sizeof(m));
    m.type = type;
    m.handle = HANDLE;
    return m;
}

static psa_status_t deviceid_pub(void)
{
    psa_msg_t m = call(TFW_DICE_OP_DEVICEID_PUB);

    m.out_size[0] = 64u;
    return tfw_dice_service_sfn(&m);
}

static void deviceid(uint8_t out[64])
{
    TEST_ASSERT_EQUAL(TFW_OK, deviceid_pub());
    TEST_ASSERT_EQUAL(64u, out_len[0]);
    memcpy(out, out_vec[0], 64u);
}

static void alias_pub(uint8_t out[64])
{
    psa_msg_t m = call(TFW_DICE_OP_ALIAS_PUB);

    m.out_size[0] = 64u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_service_sfn(&m));
    TEST_ASSERT_EQUAL(64u, out_len[0]);
    memcpy(out, out_vec[0], 64u);
}

/* The whole alias certificate, paged as the client does it. */
static uint32_t alias_cert(uint8_t *buf, uint32_t cap)
{
    uint32_t off = 0u, total = 0u;
    psa_msg_t m;

    do {
        m = call(TFW_DICE_OP_ALIAS_CERT);
        memset(in_vec[0], 0x4E, 32u); /* a fixed nonce */
        memcpy(in_vec[1], &off, sizeof(off));
        m.in_size[0] = 32u;
        m.in_size[1] = sizeof(off);
        m.out_size[0] = 255u;
        m.out_size[1] = sizeof(total);
        memset(out_len, 0, sizeof(out_len));
        TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_service_sfn(&m));
        TEST_ASSERT_EQUAL(sizeof(total), out_len[1]);
        memcpy(&total, out_vec[1], sizeof(total));
        TEST_ASSERT_TRUE(out_len[0] > 0u && total <= cap &&
                         off + out_len[0] <= total);
        memcpy(buf + off, out_vec[0], out_len[0]);
        off += (uint32_t)out_len[0];
    } while (off < total);
    return total;
}

static int contains(const uint8_t *hay, size_t n, const uint8_t *needle,
                    size_t len)
{
    for (size_t i = 0u; i + len <= n; i++) {
        if (memcmp(hay + i, needle, len) == 0) {
            return 1;
        }
    }
    return 0;
}

/* The claim 273 file-map hash entry src/caps/uccs.c writes for a
 * measurement: {7: [1, bstr(32) hash], 24: "app"}. */
#define HASH_ENTRY_LEN 43u
static void hash_entry(uint8_t out[HASH_ENTRY_LEN], const uint8_t meas[32])
{
    static const uint8_t head[5] = {0x07, 0x82, 0x01, 0x58, 0x20};
    static const uint8_t tail[6] = {0x18, 0x18, 0x63, 0x61, 0x70, 0x70};

    memcpy(out, head, sizeof(head));
    memcpy(out + 5, meas, 32u);
    memcpy(out + 37, tail, sizeof(tail));
}

static void assert_uds_zero(void)
{
    static const uint8_t zero[32];

    TEST_ASSERT_NOT_NULL(uds_buf);
    TEST_ASSERT_EQUAL_MEMORY(zero, uds_buf, sizeof(zero));
}

void setUp(void)
{
    fail_step = STEP_NONE;
    abort_calls = 0;
    setup_calls = 0;
    seen_alg = 0;
    seen_key = 0;
    seen_key_step = 0;
    seen_info_step = 0;
    seen_info_len = 0;
    seen_out_len = 0;
    memset(seen_info, 0, sizeof(seen_info));
    mb_status = PSA_SUCCESS;
    mb_locked = true;
    mb_len = 32u;
    mb_algo = PSA_ALG_SHA_256;
    seen_index = 0xFF;
    mb_calls = 0;
    memset(mb_value, 0x3C, sizeof(mb_value));
    memset(in_vec, 0, sizeof(in_vec));
    memset(out_vec, 0, sizeof(out_vec));
    memset(out_len, 0, sizeof(out_len));
    bad_handle = 0;
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL(0, device_path_calls);
}

static void start_good(void)
{
    setUp();
    TEST_ASSERT_EQUAL(PSA_SUCCESS, tfw_dice_partition_init());
    TEST_ASSERT_EQUAL(TFW_OK, deviceid_pub());
}

static void test_locked_slot3_starts_the_service(void)
{
    TEST_ASSERT_EQUAL(PSA_SUCCESS, tfw_dice_partition_init());
    TEST_ASSERT_EQUAL(PSA_ALG_HKDF(PSA_ALG_SHA_256), seen_alg);
    TEST_ASSERT_EQUAL(PSA_KEY_DERIVATION_INPUT_SECRET, seen_key_step);
    TEST_ASSERT_EQUAL_HEX32(TFM_BUILTIN_KEY_ID_HUK, seen_key);
    TEST_ASSERT_EQUAL(PSA_KEY_DERIVATION_INPUT_INFO, seen_info_step);
    TEST_ASSERT_EQUAL(15u, seen_info_len);
    TEST_ASSERT_EQUAL_STRING("tfw-dice-uds-v1", seen_info);
    TEST_ASSERT_EQUAL(32u, seen_out_len);
    TEST_ASSERT_EQUAL(1, abort_calls);
    TEST_ASSERT_EQUAL(1, mb_calls);
    TEST_ASSERT_EQUAL(3u, seen_index);
    assert_uds_zero();

    TEST_ASSERT_EQUAL(TFW_OK, deviceid_pub());
    TEST_ASSERT_EQUAL(64u, out_len[0]);
    TEST_ASSERT_EQUAL(0, bad_handle);
}

/* Codex P1 (#900): the slot-3 value is what the alias evidence signs. Two
 * boots with measurements one bit apart: the certificate carries each as its
 * claim 273 hash, the alias key moves and the DeviceID does not. */
static void test_the_slot3_measurement_reaches_the_alias_evidence(void)
{
    static uint8_t cert_a[2048], cert_b[2048];
    uint8_t a[32], b[32], need_a[HASH_ENTRY_LEN], need_b[HASH_ENTRY_LEN];
    uint8_t dev_a[64], dev_b[64], pub_a[64], pub_b[64];
    uint32_t len_a, len_b, i;

    for (i = 0u; i < 32u; i++) {
        a[i] = (uint8_t)i;
    }
    memcpy(b, a, 32u);
    b[31] ^= 0x01u;
    hash_entry(need_a, a);
    hash_entry(need_b, b);

    memcpy(mb_value, a, 32u);
    TEST_ASSERT_EQUAL(PSA_SUCCESS, tfw_dice_partition_init());
    deviceid(dev_a);
    alias_pub(pub_a);
    len_a = alias_cert(cert_a, sizeof(cert_a));
    TEST_ASSERT_TRUE(contains(cert_a, len_a, need_a, sizeof(need_a)));
    TEST_ASSERT_TRUE(contains(cert_a, len_a, pub_a, 32u));
    TEST_ASSERT_TRUE(contains(cert_a, len_a, pub_a + 32, 32u));

    setUp();
    memcpy(mb_value, b, 32u);
    TEST_ASSERT_EQUAL(PSA_SUCCESS, tfw_dice_partition_init());
    deviceid(dev_b);
    alias_pub(pub_b);
    len_b = alias_cert(cert_b, sizeof(cert_b));
    TEST_ASSERT_EQUAL_MEMORY(dev_a, dev_b, 64u);
    TEST_ASSERT_TRUE(memcmp(pub_a, pub_b, 64u) != 0);
    TEST_ASSERT_TRUE(contains(cert_b, len_b, need_b, sizeof(need_b)));
    TEST_ASSERT_TRUE(contains(cert_b, len_b, pub_b, 32u));
    TEST_ASSERT_FALSE(contains(cert_b, len_b, a, 32u));
    TEST_ASSERT_FALSE(contains(cert_b, len_b, need_a, sizeof(need_a)));
    TEST_ASSERT_EQUAL(0, bad_handle);
}

static void refused(void)
{
    TEST_ASSERT_EQUAL(PSA_SUCCESS, tfw_dice_partition_init());
    assert_uds_zero();
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, deviceid_pub());
    TEST_ASSERT_EQUAL(0u, out_len[0]);
}

static void test_unlocked_slot_is_refused(void)
{
    start_good();
    setUp();
    mb_locked = false;
    refused();
}

static void test_wrong_length_measurement_is_refused(void)
{
    static const size_t lens[] = {0u, 20u, 31u, 33u, 48u, 64u};
    size_t i;

    for (i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
        start_good();
        setUp();
        mb_len = lens[i];
        refused();
    }
}

static void test_other_hash_algorithm_is_refused(void)
{
    start_good();
    setUp();
    mb_algo = 0x02000008u; /* PSA_ALG_SHA_224 */
    refused();
}

static void test_failed_read_is_refused(void)
{
    start_good();
    setUp();
    mb_status = PSA_ERROR_INVALID_HANDLE;
    refused();
    TEST_ASSERT_EQUAL(3u, seen_index);
}

static void test_failed_hkdf_step_is_refused_and_aborted(void)
{
    static const int steps[] = {STEP_SETUP, STEP_KEY, STEP_INFO, STEP_OUTPUT};
    size_t i;

    for (i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        start_good();
        setUp();
        fail_step = steps[i];
        refused();
        TEST_ASSERT_EQUAL(1, abort_calls);
        TEST_ASSERT_EQUAL(0, mb_calls); /* nothing read without a UDS */
    }
}

static void test_sfn_passes_sizes_through(void)
{
    psa_msg_t m;

    start_good();

    /* A short out-vector: the dispatch's range answer, nothing written. */
    memset(out_len, 0, sizeof(out_len));
    m = call(TFW_DICE_OP_DEVICEID_PUB);
    m.out_size[0] = 63u;
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_dice_service_sfn(&m));
    TEST_ASSERT_EQUAL(0u, out_len[0]);

    /* An input the operation does not take. */
    m = call(TFW_DICE_OP_DEVICEID_PUB);
    m.in_size[0] = 1u;
    m.out_size[0] = 64u;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, tfw_dice_service_sfn(&m));
    TEST_ASSERT_EQUAL(0u, out_len[0]);

    /* A fourth in-vector. */
    m = call(TFW_DICE_OP_DEVICEID_PUB);
    m.in_size[3] = 1u;
    m.out_size[0] = 64u;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, tfw_dice_service_sfn(&m));

    /* A sign reads its digest through psa_read on the call's handle. */
    m = call(TFW_DICE_OP_ALIAS_SIGN);
    memset(in_vec[0], 0x77, 32u);
    m.in_size[0] = 32u;
    m.out_size[0] = 64u;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dice_service_sfn(&m));
    TEST_ASSERT_EQUAL(64u, out_len[0]);
    TEST_ASSERT_EQUAL(0, bad_handle);
}

static void test_sfn_unknown_type_is_unknown_cmd(void)
{
    static const int32_t types[] = {0, 5, 99, -1, INT32_MAX};
    size_t i;
    psa_msg_t m;

    start_good();
    for (i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        memset(out_len, 0, sizeof(out_len));
        m = call(types[i]);
        m.out_size[0] = 64u;
        TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, tfw_dice_service_sfn(&m));
        TEST_ASSERT_EQUAL(0u, out_len[0]);
    }
}

static void test_sfn_null_msg_is_refused(void)
{
    TEST_ASSERT_NOT_EQUAL(PSA_SUCCESS, tfw_dice_service_sfn(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_locked_slot3_starts_the_service);
    RUN_TEST(test_the_slot3_measurement_reaches_the_alias_evidence);
    RUN_TEST(test_unlocked_slot_is_refused);
    RUN_TEST(test_wrong_length_measurement_is_refused);
    RUN_TEST(test_other_hash_algorithm_is_refused);
    RUN_TEST(test_failed_read_is_refused);
    RUN_TEST(test_failed_hkdf_step_is_refused_and_aborted);
    RUN_TEST(test_sfn_passes_sizes_through);
    RUN_TEST(test_sfn_unknown_type_is_unknown_cmd);
    RUN_TEST(test_sfn_null_msg_is_refused);
    return UNITY_END();
}
