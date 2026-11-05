/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The OP-TEE trusted application (#253) on the host: optee/ta/tfw_ta.c's
 * entry points and command dispatch against the literal vectors, and
 * tfw-ta-check (optee/check/check.c) against that same TA through the host's
 * TEE (test/mock/gp_bridge.c), the broken answers included. The guest run of
 * both is `make integration-test-optee`.
 */

#include <stdio.h>
#include <string.h>
#include <tee_internal_api.h>
#include <tee_client_api.h>

#include "unity.h"
#include "check.h"
#include "gp_bridge.h"
#include "tfw_ta.h"
#include "tfw_blob.h"
#include "tee_crypto_mock.h"
#include "tee_session_mock.h"
/* By its path: the firmware's own testdata/vectors.h is on the -I list. */
#include "../../optee/testdata/vectors.h"

void setUp(void)
{
    tfw_gp_mock_reset();
    tfw_tee_crypto_mock_reset();
    tfw_tee_session_mock_reset();
}
void tearDown(void) {}

static TEE_Result invoke(uint32_t cmd, uint32_t types, TEE_Param params[4])
{
    return TA_InvokeCommandEntryPoint(NULL, cmd, types, params);
}

/* --- the header against the vectors ---------------------------------- */

static void test_the_uuid_is_the_vector_in_rfc9562_order(void)
{
    static const TEEC_UUID u = TFW_TA_UUID;
    TEST_ASSERT_EQUAL_HEX32(0x23751b49u, u.timeLow);
    TEST_ASSERT_EQUAL_HEX16(0x3a76u, u.timeMid);
    TEST_ASSERT_EQUAL_HEX16(0x4e49u, u.timeHiAndVersion);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TFW_TA_VEC_UUID + 8, u.clockSeqAndNode, 8);
    TEST_ASSERT_EQUAL_HEX8(0x23, TFW_TA_VEC_UUID[0]);
    TEST_ASSERT_EQUAL_HEX8(0x49, TFW_TA_VEC_UUID[3]);
    TEST_ASSERT_EQUAL_HEX8(0x40, TFW_TA_VEC_UUID[6] & 0xF0); /* version 4 */
    TEST_ASSERT_EQUAL_HEX8(0x80, TFW_TA_VEC_UUID[8] & 0xC0); /* RFC 9562 variant */
}

static void test_the_command_ids_and_bounds_are_the_vectors(void)
{
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_CMD_GET_INFO, TFW_TA_CMD_GET_INFO);
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_CMD_GET_RANDOM, TFW_TA_CMD_GET_RANDOM);
    TEST_ASSERT_EQUAL_UINT32(TFW_TA_VEC_INFO_VERSION, TFW_TA_INTERFACE_VERSION);
    TEST_ASSERT_EQUAL_UINT32(TFW_TA_VEC_INFO_FLAGS, TFW_TA_INFO_FLAGS);
    TEST_ASSERT_EQUAL_UINT32(TFW_TA_VEC_RANDOM_MIN, TFW_TA_RANDOM_MIN);
    TEST_ASSERT_EQUAL_UINT32(TFW_TA_VEC_RANDOM_MAX, TFW_TA_RANDOM_MAX);
}

static void test_the_gp_numbers_are_the_vectors(void)
{
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_PT_GET_INFO,
                            TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT, TEE_PARAM_TYPE_NONE,
                                            TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE));
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_PT_GET_RANDOM,
                            TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT, TEE_PARAM_TYPE_NONE,
                                            TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE));
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_SUCCESS, TEE_SUCCESS);
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_BAD_PARAMETERS, TEE_ERROR_BAD_PARAMETERS);
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_NOT_SUPPORTED, TEE_ERROR_NOT_SUPPORTED);
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_SHORT_BUFFER, TEE_ERROR_SHORT_BUFFER);
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_MAC_INVALID, TEE_ERROR_MAC_INVALID);
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_PT_SEAL,
                            TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT, TEE_PARAM_TYPE_MEMREF_INPUT,
                                            TEE_PARAM_TYPE_MEMREF_OUTPUT, TEE_PARAM_TYPE_NONE));
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_PT_UNSEAL, TFW_TA_VEC_PT_SEAL);
}

/* --- the TA's entry points ------------------------------------------- */

static void test_a_session_opens_with_no_parameter_and_no_context(void)
{
    TEE_Param p[4];
    void *ctx = &p;
    memset(p, 0, sizeof(p));
    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS, TA_CreateEntryPoint());
    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS, TA_OpenSessionEntryPoint(TFW_TA_VEC_PT_NONE, p, &ctx));
    TEST_ASSERT_NULL(ctx);
    TA_CloseSessionEntryPoint(ctx);
    TA_DestroyEntryPoint();
}

static void test_an_open_with_a_parameter_is_refused(void)
{
    TEE_Param p[4];
    void *ctx = &p;
    memset(p, 0, sizeof(p));
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_BAD_PARAMETERS,
                            TA_OpenSessionEntryPoint(TFW_TA_VEC_PT_VALUE_IN, p, &ctx));
    TEST_ASSERT_NULL(ctx);
}

static void test_get_info_answers_version_1_and_no_flags(void)
{
    TEE_Param p[4];
    memset(p, 0xA5, sizeof(p));
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_SUCCESS,
                            invoke(TFW_TA_VEC_CMD_GET_INFO, TFW_TA_VEC_PT_GET_INFO, p));
    TEST_ASSERT_EQUAL_UINT32(TFW_TA_VEC_INFO_VERSION, p[0].value.a);
    TEST_ASSERT_EQUAL_UINT32(TFW_TA_VEC_INFO_FLAGS, p[0].value.b);
}

static void test_get_info_with_the_wrong_types_is_refused_untouched(void)
{
    TEE_Param p[4];
    memset(p, 0xA5, sizeof(p));
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_BAD_PARAMETERS,
                            invoke(TFW_TA_VEC_CMD_GET_INFO, TFW_TA_VEC_PT_NONE, p));
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_BAD_PARAMETERS,
                            invoke(TFW_TA_VEC_CMD_GET_INFO, TFW_TA_VEC_PT_GET_RANDOM, p));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, p[0].value.a);
}

static TEE_Result draw(void *buf, size_t size)
{
    TEE_Param p[4];
    memset(p, 0, sizeof(p));
    p[0].memref.buffer = buf;
    p[0].memref.size = size;
    return invoke(TFW_TA_VEC_CMD_GET_RANDOM, TFW_TA_VEC_PT_GET_RANDOM, p);
}

static void test_get_random_fills_every_size_from_1_to_4096(void)
{
    static uint8_t buf[TFW_TA_VEC_RANDOM_MAX];
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_SUCCESS, draw(buf, TFW_TA_VEC_RANDOM_MIN));
    TEST_ASSERT_EQUAL_size_t(1u, tfw_gp_mock.random_bytes);
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_SUCCESS, draw(buf, TFW_TA_VEC_RANDOM_MAX));
    TEST_ASSERT_EQUAL_size_t(1u + TFW_TA_VEC_RANDOM_MAX, tfw_gp_mock.random_bytes);
}

static void test_get_random_refuses_a_size_outside_the_bounds_and_draws_nothing(void)
{
    static uint8_t buf[TFW_TA_VEC_RANDOM_MAX + 1u];
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_BAD_PARAMETERS, draw(buf, 0));
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_BAD_PARAMETERS, draw(buf, TFW_TA_VEC_RANDOM_MAX + 1u));
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_BAD_PARAMETERS, draw(NULL, 16));
    TEST_ASSERT_EQUAL_size_t(0u, tfw_gp_mock.random_bytes);
}

static void test_get_random_with_the_wrong_types_is_refused(void)
{
    uint8_t buf[16];
    TEE_Param p[4];
    memset(p, 0, sizeof(p));
    p[0].memref.buffer = buf;
    p[0].memref.size = sizeof(buf);
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_BAD_PARAMETERS,
                            invoke(TFW_TA_VEC_CMD_GET_RANDOM, TFW_TA_VEC_PT_GET_INFO, p));
    TEST_ASSERT_EQUAL_size_t(0u, tfw_gp_mock.random_bytes);
}

/* Ids extend, never renumber: reserved, held and unassigned ids are unknown. */
static void test_every_other_id_is_not_supported(void)
{
    static const uint32_t ids[] = {
        TFW_TA_VEC_CMD_RESERVED, 0x00000005u, 0x80000001u, TFW_TA_VEC_CMD_LAST,
    };
    TEE_Param p[4];
    size_t i;
    memset(p, 0, sizeof(p));
    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_NOT_SUPPORTED, invoke(ids[i], TFW_TA_VEC_PT_NONE, p));
        TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_NOT_SUPPORTED,
                                invoke(ids[i], TFW_TA_VEC_PT_GET_INFO, p));
    }
}

/* --- tfw-ta-check ---------------------------------------------------- */

static char out_text[4096];

/* Runs the check into a temporary file the C library removes, and keeps its text. */
static int run_check(void)
{
    FILE *f = tmpfile();
    size_t n;
    int rc;
    TEST_ASSERT_NOT_NULL(f);
    rc = tfw_ta_check(f);
    rewind(f);
    n = fread(out_text, 1, sizeof(out_text) - 1u, f);
    out_text[n] = '\0';
    fclose(f);
    TEST_ASSERT_EQUAL_INT(0, tfw_gp_mock.sessions);
    TEST_ASSERT_EQUAL_INT(0, tfw_gp_mock.contexts);
    return rc;
}

static void assert_line(const char *line)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(out_text, line), line);
}

static void test_the_check_passes_every_case_against_the_ta(void)
{
    TEST_ASSERT_EQUAL_INT(0, run_check());
    assert_line("tfw-ta-check: ok uuid\n");
    assert_line("tfw-ta-check: ok get-info\n");
    assert_line("tfw-ta-check: ok get-info-bad-types\n");
    assert_line("tfw-ta-check: ok get-random\n");
    assert_line("tfw-ta-check: ok get-random-min\n");
    assert_line("tfw-ta-check: ok get-random-max\n");
    assert_line("tfw-ta-check: ok get-random-empty\n");
    assert_line("tfw-ta-check: ok get-random-over\n");
    assert_line("tfw-ta-check: ok get-random-bad-types\n");
    assert_line("tfw-ta-check: ok seal-unseal\n");
    assert_line("tfw-ta-check: ok seal-fresh-nonce\n");
    assert_line("tfw-ta-check: ok unseal-mac-invalid\n");
    assert_line("tfw-ta-check: ok seal-short-buffer\n");
    assert_line("tfw-ta-check: ok seal-bad-types\n");
    assert_line("tfw-ta-check: ok unseal-bad-types\n");
    assert_line("tfw-ta-check: ok unknown-0x00000000\n");
    assert_line("tfw-ta-check: ok unknown-0xffffffff\n");
    assert_line("tfw-ta-check: ok open-bad-parameters\n");
    assert_line("tfw-ta-check: PASS, 18 of 18 passed\n");
    TEST_ASSERT_NULL(strstr(out_text, "FAIL"));
}

static void test_no_context_is_reported_and_nothing_else_runs(void)
{
    tfw_gp_mock.init_result = TEEC_ERROR_GENERIC;
    TEST_ASSERT_EQUAL_INT(-1, run_check());
    assert_line("tfw-ta-check: FAIL context 0xffff0000\n");
    TEST_ASSERT_NULL(strstr(out_text, "get-info"));
}

static void test_no_session_is_reported_and_the_context_finalised(void)
{
    tfw_gp_mock.open_result = TEEC_ERROR_ITEM_NOT_FOUND;
    TEST_ASSERT_EQUAL_INT(-1, run_check());
    assert_line("tfw-ta-check: FAIL session 0xffff0008 origin 3\n");
}

static void test_a_wrong_version_fails_get_info_only(void)
{
    tfw_gp_mock.value_a_xor = 1u;
    TEST_ASSERT_EQUAL_INT(1, run_check());
    assert_line("tfw-ta-check: FAIL get-info\n");
    assert_line("tfw-ta-check: FAIL, 17 of 18 passed\n");
}

static void test_a_stuck_generator_fails_get_random(void)
{
    tfw_gp_mock.stuck_random = 1;
    TEST_ASSERT_EQUAL_INT(2, run_check());
    assert_line("tfw-ta-check: FAIL get-random\n");
    assert_line("tfw-ta-check: FAIL seal-fresh-nonce\n");
}

/* The right code from the wrong origin is not the TA's answer. */
static void test_an_error_not_from_the_ta_fails(void)
{
    tfw_gp_mock.force = 1;
    tfw_gp_mock.force_cmd = TFW_TA_VEC_CMD_LAST;
    tfw_gp_mock.force_rc = TEEC_ERROR_NOT_SUPPORTED;
    tfw_gp_mock.force_origin = TEEC_ORIGIN_TEE;
    TEST_ASSERT_EQUAL_INT(1, run_check());
    assert_line("tfw-ta-check: FAIL unknown-0xffffffff\n");
}

static void test_a_failed_draw_fails_get_random(void)
{
    tfw_gp_mock.force = 1;
    tfw_gp_mock.force_cmd = TFW_TA_VEC_CMD_GET_RANDOM;
    tfw_gp_mock.force_rc = TEEC_ERROR_GENERIC;
    tfw_gp_mock.force_origin = TEEC_ORIGIN_TRUSTED_APP;
    TEST_ASSERT_EQUAL_INT(6, run_check());
    assert_line("tfw-ta-check: FAIL get-random\n");
    assert_line("tfw-ta-check: FAIL get-random-bad-types\n");
}

static void test_an_open_the_ta_should_refuse_fails_and_is_closed(void)
{
    tfw_gp_mock.open_always = 1;
    TEST_ASSERT_EQUAL_INT(1, run_check());
    assert_line("tfw-ta-check: FAIL open-bad-parameters\n");
}

/*
 * The sealed-blob codec, go-xkms's docs/optee-sealed-blob/README.md §2 to §5,
 * against go-xkms's vectors from their literal key and nonce onwards.
 */
static void assert_seals_to(const uint8_t *key, const uint8_t *nonce,
                            const uint8_t *plaintext, size_t plaintext_len,
                            const uint8_t *want, size_t want_len)
{
    uint8_t blob[128];
    size_t blob_len = sizeof(blob);

    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS,
                            tfw_blob_seal(key, TFW_BLOB_KEY_LEN, TFW_TA_VEC_SEAL_UUID,
                                          sizeof(TFW_TA_VEC_SEAL_UUID), nonce, plaintext,
                                          plaintext_len, blob, &blob_len));
    TEST_ASSERT_EQUAL_size_t(want_len, blob_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, blob, want_len);
    TEST_ASSERT_EQUAL_size_t(sizeof(TFW_TA_VEC_SEAL_AAD), tfw_tee_mock_aad_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TFW_TA_VEC_SEAL_AAD, tfw_tee_mock_aad,
                                 sizeof(TFW_TA_VEC_SEAL_AAD));
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_mock_live);
}

static void assert_unseals_to(const uint8_t *key, const uint8_t *blob, size_t blob_len,
                              const uint8_t *want, size_t want_len)
{
    uint8_t plaintext[64];
    size_t plaintext_len = sizeof(plaintext);
    enum tfw_blob_refusal refusal = TFW_BLOB_HEADER;

    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS,
                            tfw_blob_unseal(key, TFW_BLOB_KEY_LEN, TFW_TA_VEC_SEAL_UUID,
                                            sizeof(TFW_TA_VEC_SEAL_UUID), blob, blob_len,
                                            plaintext, &plaintext_len, &refusal));
    TEST_ASSERT_EQUAL_INT(TFW_BLOB_ACCEPTED, refusal);
    TEST_ASSERT_EQUAL_size_t(want_len, plaintext_len);
    if (want_len != 0u)
        TEST_ASSERT_EQUAL_HEX8_ARRAY(want, plaintext, want_len);
    TEST_ASSERT_EQUAL_size_t(sizeof(TFW_TA_VEC_SEAL_AAD), tfw_tee_mock_aad_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TFW_TA_VEC_SEAL_AAD, tfw_tee_mock_aad,
                                 sizeof(TFW_TA_VEC_SEAL_AAD));
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_mock_live);
}

/* Unseals blob under vector 1's key; asserts the refusal and no plaintext out. */
static void assert_refused(const uint8_t *blob, size_t blob_len, TEE_Result want_res,
                           enum tfw_blob_refusal want)
{
    uint8_t plaintext[64];
    uint8_t untouched[64];
    size_t plaintext_len = sizeof(plaintext);
    enum tfw_blob_refusal refusal = TFW_BLOB_ACCEPTED;

    memset(plaintext, 0xA5, sizeof(plaintext));
    memset(untouched, 0xA5, sizeof(untouched));
    TEST_ASSERT_EQUAL_HEX32(want_res,
                            tfw_blob_unseal(TFW_TA_VEC_SEAL1_KEY, TFW_BLOB_KEY_LEN,
                                            TFW_TA_VEC_SEAL_UUID,
                                            sizeof(TFW_TA_VEC_SEAL_UUID), blob, blob_len,
                                            plaintext, &plaintext_len, &refusal));
    TEST_ASSERT_EQUAL_INT(want, refusal);
    if (want == TFW_BLOB_AUTHENTICATION) {
        memset(untouched, 0, sizeof(untouched));
    }
    TEST_ASSERT_EQUAL_HEX8_ARRAY(untouched, plaintext, sizeof(plaintext));
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_mock_live);
}

static void vector1_copy(uint8_t blob[sizeof(TFW_TA_VEC_SEAL1_BLOB) + 1u])
{
    memset(blob, 0, sizeof(TFW_TA_VEC_SEAL1_BLOB) + 1u);
    memcpy(blob, TFW_TA_VEC_SEAL1_BLOB, sizeof(TFW_TA_VEC_SEAL1_BLOB));
}

static void test_seal_vector_1_is_go_xkms_blob(void)
{
    assert_seals_to(TFW_TA_VEC_SEAL1_KEY, TFW_TA_VEC_SEAL1_NONCE, TFW_TA_VEC_SEAL1_PLAINTEXT,
                    sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT), TFW_TA_VEC_SEAL1_BLOB,
                    sizeof(TFW_TA_VEC_SEAL1_BLOB));
}

static void test_seal_vector_2_is_the_tag_alone(void)
{
    assert_seals_to(TFW_TA_VEC_SEAL2_KEY, TFW_TA_VEC_SEAL2_NONCE, NULL, 0u,
                    TFW_TA_VEC_SEAL2_BLOB, sizeof(TFW_TA_VEC_SEAL2_BLOB));
}

static void test_unseal_vector_1_is_its_plaintext(void)
{
    assert_unseals_to(TFW_TA_VEC_SEAL1_KEY, TFW_TA_VEC_SEAL1_BLOB,
                      sizeof(TFW_TA_VEC_SEAL1_BLOB), TFW_TA_VEC_SEAL1_PLAINTEXT,
                      sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT));
}

static void test_unseal_vector_2_is_empty(void)
{
    assert_unseals_to(TFW_TA_VEC_SEAL2_KEY, TFW_TA_VEC_SEAL2_BLOB,
                      sizeof(TFW_TA_VEC_SEAL2_BLOB), NULL, 0u);
}

static void test_a_key_not_32_bytes_is_refused(void)
{
    uint8_t blob[128];
    size_t blob_len = sizeof(blob);
    uint8_t plaintext[64];
    size_t plaintext_len = sizeof(plaintext);
    enum tfw_blob_refusal refusal = TFW_BLOB_ACCEPTED;

    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_BAD_PARAMETERS,
                            tfw_blob_seal(TFW_TA_VEC_SEAL1_KEY, TFW_BLOB_KEY_LEN - 1u,
                                          TFW_TA_VEC_SEAL_UUID, sizeof(TFW_TA_VEC_SEAL_UUID),
                                          TFW_TA_VEC_SEAL1_NONCE, NULL, 0u, blob, &blob_len));
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_BAD_PARAMETERS,
                            tfw_blob_unseal(TFW_TA_VEC_SEAL1_KEY, TFW_BLOB_KEY_LEN - 1u,
                                            TFW_TA_VEC_SEAL_UUID,
                                            sizeof(TFW_TA_VEC_SEAL_UUID),
                                            TFW_TA_VEC_SEAL1_BLOB,
                                            sizeof(TFW_TA_VEC_SEAL1_BLOB), plaintext,
                                            &plaintext_len, &refusal));
    TEST_ASSERT_EQUAL_INT(TFW_BLOB_KEY_LENGTH, refusal);
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_mock_live);
}

static void test_a_short_buffer_answers_the_size_needed(void)
{
    uint8_t blob[sizeof(TFW_TA_VEC_SEAL1_BLOB)];
    size_t blob_len = sizeof(blob) - 1u;
    uint8_t plaintext[sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT)];
    size_t plaintext_len = sizeof(plaintext) - 1u;
    enum tfw_blob_refusal refusal = TFW_BLOB_HEADER;

    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_SHORT_BUFFER,
                            tfw_blob_seal(TFW_TA_VEC_SEAL1_KEY, TFW_BLOB_KEY_LEN,
                                          TFW_TA_VEC_SEAL_UUID, sizeof(TFW_TA_VEC_SEAL_UUID),
                                          TFW_TA_VEC_SEAL1_NONCE, TFW_TA_VEC_SEAL1_PLAINTEXT,
                                          sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT), blob,
                                          &blob_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(TFW_TA_VEC_SEAL1_BLOB), blob_len);
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_SHORT_BUFFER,
                            tfw_blob_unseal(TFW_TA_VEC_SEAL1_KEY, TFW_BLOB_KEY_LEN,
                                            TFW_TA_VEC_SEAL_UUID,
                                            sizeof(TFW_TA_VEC_SEAL_UUID),
                                            TFW_TA_VEC_SEAL1_BLOB,
                                            sizeof(TFW_TA_VEC_SEAL1_BLOB), plaintext,
                                            &plaintext_len, &refusal));
    TEST_ASSERT_EQUAL_size_t(sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT), plaintext_len);
    TEST_ASSERT_EQUAL_INT(TFW_BLOB_ACCEPTED, refusal);
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_mock_live);
}

static void test_a_truncated_blob_is_refused(void)
{
    uint8_t blob[sizeof(TFW_TA_VEC_SEAL2_BLOB)];

    assert_refused(TFW_TA_VEC_SEAL1_BLOB, sizeof(TFW_TA_VEC_SEAL1_BLOB) - 1u,
                   TEE_ERROR_BAD_FORMAT, TFW_BLOB_TRUNCATED);
    /* Complete, but its ciphertext is a byte short of the tag. */
    memcpy(blob, TFW_TA_VEC_SEAL2_BLOB, sizeof(blob));
    blob[27] = 0x4F;
    assert_refused(blob, sizeof(blob) - 1u, TEE_ERROR_BAD_FORMAT, TFW_BLOB_TRUNCATED);
}

static void test_a_malformed_blob_is_refused(void)
{
    uint8_t blob[sizeof(TFW_TA_VEC_SEAL1_BLOB) + 1u];

    /* A trailing byte. */
    vector1_copy(blob);
    assert_refused(blob, sizeof(blob), TEE_ERROR_BAD_FORMAT, TFW_BLOB_MALFORMED);
    /* Tag 17, not 16. */
    vector1_copy(blob);
    blob[0] = 0xD1;
    assert_refused(blob, sizeof(TFW_TA_VEC_SEAL1_BLOB), TEE_ERROR_BAD_FORMAT,
                   TFW_BLOB_MALFORMED);
}

static void test_a_version_not_1_is_refused(void)
{
    uint8_t blob[sizeof(TFW_TA_VEC_SEAL1_BLOB) + 1u];

    vector1_copy(blob);
    blob[11] = 0x02;
    assert_refused(blob, sizeof(TFW_TA_VEC_SEAL1_BLOB), TEE_ERROR_BAD_FORMAT,
                   TFW_BLOB_VERSION);
}

static void test_an_algorithm_not_a256gcm_is_refused(void)
{
    uint8_t blob[sizeof(TFW_TA_VEC_SEAL1_BLOB) + 1u];

    vector1_copy(blob);
    blob[5] = 0x01;
    assert_refused(blob, sizeof(TFW_TA_VEC_SEAL1_BLOB), TEE_ERROR_BAD_FORMAT,
                   TFW_BLOB_ALGORITHM);
}

static void test_a_header_not_the_bytes_of_section_2_is_refused(void)
{
    static const uint8_t reordered[9] = {0xa2, 0x3a, 0x00, 0x01, 0x00,
                                         0x00, 0x01, 0x01, 0x03};
    uint8_t blob[sizeof(TFW_TA_VEC_SEAL1_BLOB) + 1u];

    /* The version and the algorithm, right, in the other order. */
    vector1_copy(blob);
    memcpy(blob + 3, reordered, sizeof(reordered));
    assert_refused(blob, sizeof(TFW_TA_VEC_SEAL1_BLOB), TEE_ERROR_BAD_FORMAT,
                   TFW_BLOB_HEADER);
    /* The unprotected header's IV under label 6, not 5. */
    vector1_copy(blob);
    blob[13] = 0x06;
    assert_refused(blob, sizeof(TFW_TA_VEC_SEAL1_BLOB), TEE_ERROR_BAD_FORMAT,
                   TFW_BLOB_HEADER);
}

static void test_a_non_minimal_head_is_refused(void)
{
    uint8_t blob[sizeof(TFW_TA_VEC_SEAL1_BLOB) + 1u];

    /* The ciphertext's head 58 37 written as 59 00 37. */
    blob[27] = 0x59;
    blob[28] = 0x00;
    memcpy(blob, TFW_TA_VEC_SEAL1_BLOB, 27u);
    memcpy(blob + 29, TFW_TA_VEC_SEAL1_BLOB + 28, sizeof(TFW_TA_VEC_SEAL1_BLOB) - 28u);
    assert_refused(blob, sizeof(blob), TEE_ERROR_BAD_FORMAT, TFW_BLOB_MALFORMED);
}

static void test_a_flipped_tag_byte_is_refused(void)
{
    uint8_t blob[sizeof(TFW_TA_VEC_SEAL1_BLOB) + 1u];

    vector1_copy(blob);
    blob[sizeof(TFW_TA_VEC_SEAL1_BLOB) - 1u] ^= 0x01u;
    assert_refused(blob, sizeof(TFW_TA_VEC_SEAL1_BLOB), TEE_ERROR_MAC_INVALID,
                   TFW_BLOB_AUTHENTICATION);
}

static void test_another_tas_uuid_does_not_authenticate(void)
{
    uint8_t uuid[sizeof(TFW_TA_VEC_SEAL_UUID)];
    uint8_t plaintext[64];
    size_t plaintext_len = sizeof(plaintext);
    enum tfw_blob_refusal refusal = TFW_BLOB_ACCEPTED;

    memcpy(uuid, TFW_TA_VEC_SEAL_UUID, sizeof(uuid));
    uuid[15] ^= 0x01u;
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_MAC_INVALID,
                            tfw_blob_unseal(TFW_TA_VEC_SEAL1_KEY, TFW_BLOB_KEY_LEN, uuid,
                                            sizeof(uuid), TFW_TA_VEC_SEAL1_BLOB,
                                            sizeof(TFW_TA_VEC_SEAL1_BLOB), plaintext,
                                            &plaintext_len, &refusal));
    TEST_ASSERT_EQUAL_INT(TFW_BLOB_AUTHENTICATION, refusal);
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_mock_live);
}

/* Test input only: the TA's own extra_data comes from go-xkms #1783. */
static const uint8_t derive_extra[5] = { 0x01, 0x02, 0x03, 0x04, 0x05 };

/* pta_system.h's PTA_SYSTEM_UUID at OP-TEE 4.11.0, as a literal. */
static void assert_system_pta_opened(void)
{
    static const uint8_t node[8] = { 0x9c, 0x2d, 0xfa, 0x7a, 0xe0, 0x1b, 0xbe, 0xbc };

    TEST_ASSERT_EQUAL_UINT(1u, tfw_tee_session_mock.opens);
    TEST_ASSERT_EQUAL_HEX32(0x3a2f8978u, tfw_tee_session_mock.uuid.timeLow);
    TEST_ASSERT_EQUAL_HEX16(0x5dc0u, tfw_tee_session_mock.uuid.timeMid);
    TEST_ASSERT_EQUAL_HEX16(0x11e8u, tfw_tee_session_mock.uuid.timeHiAndVersion);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(node, tfw_tee_session_mock.uuid.clockSeqAndNode, 8);
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, tfw_tee_session_mock.open_param_types);
}

static void assert_derive_refused_before_the_call(const uint8_t *extra, size_t extra_len,
                                                  size_t key_len)
{
    uint8_t key[40];

    memset(key, 0xA5, sizeof(key));
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_BAD_PARAMETERS,
                            tfw_derive_ta_unique_key(extra, extra_len, key, key_len));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_tee_session_mock.opens);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_tee_session_mock.invokes);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, key, sizeof(key));
}

static void test_derive_invokes_the_system_pta(void)
{
    uint8_t key[32];
    uint8_t want[32];
    size_t i;

    for (i = 0u; i < sizeof(want); i++)
        want[i] = (uint8_t)(0x40u + i);
    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS, tfw_derive_ta_unique_key(derive_extra,
                                                                  sizeof(derive_extra), key,
                                                                  sizeof(key)));
    assert_system_pta_opened();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_tee_session_mock.invokes);
    TEST_ASSERT_EQUAL_HEX32(1u, tfw_tee_session_mock.command_id);
    /* params[0] MEMREF_INPUT (5), params[1] MEMREF_OUTPUT (6), [2] and [3] NONE. */
    TEST_ASSERT_EQUAL_HEX32(0x00000065u, tfw_tee_session_mock.param_types);
    TEST_ASSERT_EQUAL_size_t(sizeof(derive_extra), tfw_tee_session_mock.extra_data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(derive_extra, tfw_tee_session_mock.extra_data,
                                 sizeof(derive_extra));
    TEST_ASSERT_EQUAL_size_t(32u, tfw_tee_session_mock.key_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, key, sizeof(key));
    TEST_ASSERT_EQUAL_UINT(1u, tfw_tee_session_mock.closes);
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_session_mock.live);
}

static void test_derive_takes_1024_bytes_of_extra_data(void)
{
    static uint8_t extra[1024];
    uint8_t key[16];

    memset(extra, 0x5A, sizeof(extra));
    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS,
                            tfw_derive_ta_unique_key(extra, sizeof(extra), key, sizeof(key)));
    TEST_ASSERT_EQUAL_size_t(1024u, tfw_tee_session_mock.extra_data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(extra, tfw_tee_session_mock.extra_data, sizeof(extra));
    TEST_ASSERT_EQUAL_size_t(16u, tfw_tee_session_mock.key_len);
    TEST_ASSERT_EQUAL_UINT(1u, tfw_tee_session_mock.closes);
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_session_mock.live);
}

static void test_derive_refuses_1025_bytes_of_extra_data(void)
{
    static uint8_t extra[1025];

    assert_derive_refused_before_the_call(extra, sizeof(extra), 32u);
}

static void test_derive_refuses_a_key_of_15_bytes(void)
{
    assert_derive_refused_before_the_call(derive_extra, sizeof(derive_extra), 15u);
}

static void test_derive_refuses_a_key_of_33_bytes(void)
{
    assert_derive_refused_before_the_call(derive_extra, sizeof(derive_extra), 33u);
}

static void test_derive_returns_the_ptas_error_unchanged(void)
{
    uint8_t key[32];

    tfw_tee_session_mock.invoke_result = TEE_ERROR_NOT_SUPPORTED;
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_NOT_SUPPORTED,
                            tfw_derive_ta_unique_key(derive_extra, sizeof(derive_extra), key,
                                                     sizeof(key)));
    assert_system_pta_opened();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_tee_session_mock.invokes);
    TEST_ASSERT_EACH_EQUAL_HEX8(0x00, key, sizeof(key));
    TEST_ASSERT_EQUAL_UINT(1u, tfw_tee_session_mock.closes);
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_session_mock.live);
}

static void test_derive_returns_the_open_error_unchanged(void)
{
    uint8_t key[32];

    tfw_tee_session_mock.open_result = TEE_ERROR_ITEM_NOT_FOUND;
    memset(key, 0xA5, sizeof(key));
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_ITEM_NOT_FOUND,
                            tfw_derive_ta_unique_key(derive_extra, sizeof(derive_extra), key,
                                                     sizeof(key)));
    assert_system_pta_opened();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_tee_session_mock.invokes);
    TEST_ASSERT_EACH_EQUAL_HEX8(0x00, key, sizeof(key));
    /* No session opened, so none to close. */
    TEST_ASSERT_EQUAL_UINT(0u, tfw_tee_session_mock.closes);
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_session_mock.live);
}

static void assert_hkdf_gives(const uint8_t *ta_unique_key, const uint8_t *want)
{
    uint8_t okm[32];

    memset(okm, 0xAA, sizeof(okm));
    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS,
                            tfw_hkdf_sha256(ta_unique_key, 32u, TFW_TA_VEC_SEAL_KDF_INFO,
                                            sizeof(TFW_TA_VEC_SEAL_KDF_INFO), okm));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, okm, sizeof(okm));
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_mock_live);
}

static void test_hkdf_of_the_zero_ta_key_is_seal_vector_1_key(void)
{
    assert_hkdf_gives(TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY, TFW_TA_VEC_SEAL1_KEY);
}

static void test_hkdf_of_the_counting_ta_key_is_seal_vector_2_key(void)
{
    assert_hkdf_gives(TFW_TA_VEC_SEAL2_TA_UNIQUE_KEY, TFW_TA_VEC_SEAL2_KEY);
}

static void assert_blob_key_is(const uint8_t *ta_unique_key, const uint8_t *want)
{
    uint8_t key[32];

    memset(key, 0xAA, sizeof(key));
    tfw_tee_session_mock.answer = ta_unique_key;
    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS, tfw_blob_key(TFW_TA_VEC_SEAL_UUID, key));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, key, sizeof(key));
    /* The PTA is asked once, with the label alone as extra_data and 32 bytes out. */
    TEST_ASSERT_EQUAL_UINT(1u, tfw_tee_session_mock.invokes);
    TEST_ASSERT_EQUAL_HEX32(1u, tfw_tee_session_mock.command_id);
    TEST_ASSERT_EQUAL_size_t(28u, tfw_tee_session_mock.extra_data_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TFW_TA_VEC_SEAL_KDF_INFO, tfw_tee_session_mock.extra_data, 28u);
    TEST_ASSERT_EQUAL_size_t(32u, tfw_tee_session_mock.key_len);
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_session_mock.live);
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_mock_live);
}

static void test_the_blob_key_of_the_zero_ta_key_is_seal_vector_1_key(void)
{
    assert_blob_key_is(TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY, TFW_TA_VEC_SEAL1_KEY);
}

static void test_the_blob_key_of_the_counting_ta_key_is_seal_vector_2_key(void)
{
    assert_blob_key_is(TFW_TA_VEC_SEAL2_TA_UNIQUE_KEY, TFW_TA_VEC_SEAL2_KEY);
}

static void test_another_tas_uuid_derives_another_key(void)
{
    uint8_t other[16];
    uint8_t key[32];

    memcpy(other, TFW_TA_VEC_SEAL_UUID, sizeof(other));
    other[15] ^= 0x01u;
    tfw_tee_session_mock.answer = TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY;
    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS, tfw_blob_key(other, key));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(key, TFW_TA_VEC_SEAL1_KEY, sizeof(key)));
}

static void test_a_failed_ta_unique_key_leaves_no_blob_key(void)
{
    uint8_t key[32];
    uint8_t zero[32];

    memset(key, 0xAA, sizeof(key));
    memset(zero, 0, sizeof(zero));
    tfw_tee_session_mock.invoke_result = TEE_ERROR_GENERIC;
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_GENERIC, tfw_blob_key(TFW_TA_VEC_SEAL_UUID, key));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(zero, key, sizeof(key));
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_session_mock.live);
}

/* --- SEAL and UNSEAL through the command entry point (#288) ----------- */

/* README, "The trusted application": MEMREF_INPUT, MEMREF_INPUT, MEMREF_OUTPUT, NONE. */
#define SEAL_PT 0x00000655u

/* The TA is the vector's: its appID is the vector's UUID, its PTA gives that vector's key. */
static void be_the_vectors_ta(const uint8_t *ta_unique_key)
{
    const uint8_t *u = TFW_TA_VEC_SEAL_UUID;
    TEE_UUID *id = &tfw_tee_session_mock.app_id;

    id->timeLow = ((uint32_t)u[0] << 24) | ((uint32_t)u[1] << 16) | ((uint32_t)u[2] << 8) | u[3];
    id->timeMid = (uint16_t)(((uint32_t)u[4] << 8) | u[5]);
    id->timeHiAndVersion = (uint16_t)(((uint32_t)u[6] << 8) | u[7]);
    memcpy(id->clockSeqAndNode, u + 8, 8u);
    tfw_tee_session_mock.answer = ta_unique_key;
}

/* One SEAL or UNSEAL: in and out are params 0 and 2, aad_len bytes are param 1. */
static TEE_Result run_seal_cmd(uint32_t cmd, const void *in, size_t in_len, size_t aad_len,
                               void *out, size_t *out_len)
{
    static const uint8_t aad[4] = { 1, 2, 3, 4 };
    TEE_Param p[4];
    TEE_Result res;

    memset(p, 0, sizeof(p));
    p[0].memref.buffer = (void *)(uintptr_t)in;
    p[0].memref.size = in_len;
    p[1].memref.buffer = (void *)(uintptr_t)aad;
    p[1].memref.size = aad_len;
    p[2].memref.buffer = out;
    p[2].memref.size = *out_len;
    res = invoke(cmd, SEAL_PT, p);
    *out_len = p[2].memref.size;
    return res;
}

static void assert_nothing_left_open(void)
{
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_session_mock.live);
    TEST_ASSERT_EQUAL_INT(0, tfw_tee_mock_live);
}

static void assert_command_unseals_to(const uint8_t *ta_unique_key, const uint8_t *blob,
                                      size_t blob_len, const uint8_t *want, size_t want_len)
{
    uint8_t out[64];
    size_t out_len = sizeof(out);

    be_the_vectors_ta(ta_unique_key);
    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS,
                            run_seal_cmd(TFW_TA_CMD_UNSEAL, blob, blob_len, 0u, out, &out_len));
    TEST_ASSERT_EQUAL_size_t(want_len, out_len);
    if (want_len != 0u)
        TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, want_len);
    assert_nothing_left_open();
}

static void test_the_seal_ids_and_shapes_are_the_vectors(void)
{
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_CMD_SEAL, TFW_TA_CMD_SEAL);
    TEST_ASSERT_EQUAL_HEX32(TFW_TA_VEC_CMD_UNSEAL, TFW_TA_CMD_UNSEAL);
    TEST_ASSERT_EQUAL_HEX32(SEAL_PT,
                            TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT, TEE_PARAM_TYPE_MEMREF_INPUT,
                                            TEE_PARAM_TYPE_MEMREF_OUTPUT, TEE_PARAM_TYPE_NONE));
}

static void test_unseal_of_vector_1_returns_its_plaintext(void)
{
    assert_command_unseals_to(TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY, TFW_TA_VEC_SEAL1_BLOB,
                              sizeof(TFW_TA_VEC_SEAL1_BLOB), TFW_TA_VEC_SEAL1_PLAINTEXT,
                              sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT));
}

static void test_unseal_of_vector_2_returns_the_empty_plaintext(void)
{
    assert_command_unseals_to(TFW_TA_VEC_SEAL2_TA_UNIQUE_KEY, TFW_TA_VEC_SEAL2_BLOB,
                              sizeof(TFW_TA_VEC_SEAL2_BLOB), NULL, 0u);
}

static void test_a_seal_then_an_unseal_returns_the_input(void)
{
    uint8_t blob[128];
    size_t blob_len = sizeof(blob);
    uint8_t out[64];
    size_t out_len = sizeof(out);

    be_the_vectors_ta(TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY);
    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS,
                            run_seal_cmd(TFW_TA_CMD_SEAL, TFW_TA_VEC_SEAL1_PLAINTEXT,
                                         sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT), 0u, blob, &blob_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(TFW_TA_VEC_SEAL1_BLOB), blob_len);
    /* §4: the nonce is a fresh draw of 12 bytes. */
    TEST_ASSERT_EQUAL_size_t(12u, tfw_gp_mock.random_bytes);
    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS,
                            run_seal_cmd(TFW_TA_CMD_UNSEAL, blob, blob_len, 0u, out, &out_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT), out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TFW_TA_VEC_SEAL1_PLAINTEXT, out, out_len);
    assert_nothing_left_open();
}

static void test_a_seal_of_nothing_then_an_unseal_returns_nothing(void)
{
    uint8_t blob[64];
    size_t blob_len = sizeof(blob);
    uint8_t out[8];
    size_t out_len = sizeof(out);

    be_the_vectors_ta(TFW_TA_VEC_SEAL2_TA_UNIQUE_KEY);
    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS,
                            run_seal_cmd(TFW_TA_CMD_SEAL, NULL, 0u, 0u, blob, &blob_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(TFW_TA_VEC_SEAL2_BLOB), blob_len);
    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS,
                            run_seal_cmd(TFW_TA_CMD_UNSEAL, blob, blob_len, 0u, out, &out_len));
    TEST_ASSERT_EQUAL_size_t(0u, out_len);
    assert_nothing_left_open();
}

static void test_every_seal_draws_its_own_nonce(void)
{
    uint8_t first[128];
    uint8_t second[128];
    size_t first_len = sizeof(first);
    size_t second_len = sizeof(second);

    be_the_vectors_ta(TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY);
    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS,
                            run_seal_cmd(TFW_TA_CMD_SEAL, TFW_TA_VEC_SEAL1_PLAINTEXT,
                                         sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT), 0u, first,
                                         &first_len));
    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS,
                            run_seal_cmd(TFW_TA_CMD_SEAL, TFW_TA_VEC_SEAL1_PLAINTEXT,
                                         sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT), 0u, second,
                                         &second_len));
    TEST_ASSERT_EQUAL_size_t(24u, tfw_gp_mock.random_bytes);
    /* §2: the IV is the 12 bytes at offset 15. */
    TEST_ASSERT_NOT_EQUAL(0, memcmp(first + 15, second + 15, TFW_BLOB_NONCE_LEN));
}

static void test_a_short_seal_buffer_answers_the_size_needed(void)
{
    uint8_t blob[128];
    size_t blob_len = sizeof(TFW_TA_VEC_SEAL1_BLOB) - 1u;

    be_the_vectors_ta(TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY);
    memset(blob, 0xA5, sizeof(blob));
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_SHORT_BUFFER,
                            run_seal_cmd(TFW_TA_CMD_SEAL, TFW_TA_VEC_SEAL1_PLAINTEXT,
                                         sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT), 0u, blob, &blob_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(TFW_TA_VEC_SEAL1_BLOB), blob_len);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, blob, sizeof(blob));
    /* An empty output asks for the size alone. */
    blob_len = 0u;
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_SHORT_BUFFER,
                            run_seal_cmd(TFW_TA_CMD_SEAL, TFW_TA_VEC_SEAL1_PLAINTEXT,
                                         sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT), 0u, NULL, &blob_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(TFW_TA_VEC_SEAL1_BLOB), blob_len);
    assert_nothing_left_open();
}

static void test_a_short_unseal_buffer_answers_the_size_needed(void)
{
    uint8_t out[64];
    size_t out_len = sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT) - 1u;

    be_the_vectors_ta(TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY);
    memset(out, 0xA5, sizeof(out));
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_SHORT_BUFFER,
                            run_seal_cmd(TFW_TA_CMD_UNSEAL, TFW_TA_VEC_SEAL1_BLOB,
                                         sizeof(TFW_TA_VEC_SEAL1_BLOB), 0u, out, &out_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT), out_len);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, out, sizeof(out));
    assert_nothing_left_open();
}

static void test_a_flipped_tag_byte_answers_mac_invalid_and_an_untouched_output(void)
{
    uint8_t blob[sizeof(TFW_TA_VEC_SEAL1_BLOB)];
    uint8_t out[64];
    size_t out_len = sizeof(out);

    be_the_vectors_ta(TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY);
    memcpy(blob, TFW_TA_VEC_SEAL1_BLOB, sizeof(blob));
    blob[sizeof(blob) - 1u] ^= 0x01u;
    memset(out, 0xA5, sizeof(out));
    TEST_ASSERT_EQUAL_HEX32(0xFFFF3071u,
                            run_seal_cmd(TFW_TA_CMD_UNSEAL, blob, sizeof(blob), 0u, out, &out_len));
    /* The output is shared memory: no plaintext reaches it before the tag verifies. */
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, out, sizeof(out));
    assert_nothing_left_open();
}

static uint8_t shared_blob[sizeof(TFW_TA_VEC_SEAL1_BLOB)];

static void overwrite_the_shared_blob(void)
{
    memset(shared_blob, 0xFF, sizeof(shared_blob));
}

static void test_a_blob_changed_during_decryption_still_unseals_as_parsed(void)
{
    uint8_t out[64];
    size_t out_len = sizeof(out);

    be_the_vectors_ta(TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY);
    memcpy(shared_blob, TFW_TA_VEC_SEAL1_BLOB, sizeof(shared_blob));
    tfw_tee_mock_on_decrypt = overwrite_the_shared_blob;
    TEST_ASSERT_EQUAL_HEX32(TEE_SUCCESS, run_seal_cmd(TFW_TA_CMD_UNSEAL, shared_blob,
                                                      sizeof(shared_blob), 0u, out, &out_len));
    TEST_ASSERT_EQUAL_size_t(sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT), out_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(TFW_TA_VEC_SEAL1_PLAINTEXT, out, out_len);
    assert_nothing_left_open();
}

static void test_a_flipped_ciphertext_byte_answers_mac_invalid(void)
{
    uint8_t blob[sizeof(TFW_TA_VEC_SEAL1_BLOB)];
    uint8_t out[64];
    size_t out_len = sizeof(out);

    be_the_vectors_ta(TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY);
    memcpy(blob, TFW_TA_VEC_SEAL1_BLOB, sizeof(blob));
    blob[29] ^= 0x80u;
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_MAC_INVALID,
                            run_seal_cmd(TFW_TA_CMD_UNSEAL, blob, sizeof(blob), 0u, out, &out_len));
}

static void test_a_blob_that_is_not_a_blob_answers_bad_format(void)
{
    uint8_t out[64];
    size_t out_len = sizeof(out);

    be_the_vectors_ta(TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY);
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_BAD_FORMAT,
                            run_seal_cmd(TFW_TA_CMD_UNSEAL, TFW_TA_VEC_SEAL1_BLOB,
                                         sizeof(TFW_TA_VEC_SEAL1_BLOB) - 1u, 0u, out, &out_len));
    out_len = sizeof(out);
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_BAD_FORMAT,
                            run_seal_cmd(TFW_TA_CMD_UNSEAL, TFW_TA_VEC_SEAL1_BLOB, 0u, 0u, out,
                                         &out_len));
    assert_nothing_left_open();
}

static void test_another_tas_blob_does_not_unseal(void)
{
    uint8_t out[64];
    size_t out_len = sizeof(out);

    be_the_vectors_ta(TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY);
    tfw_tee_session_mock.app_id.clockSeqAndNode[7] ^= 0x01u;
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_MAC_INVALID,
                            run_seal_cmd(TFW_TA_CMD_UNSEAL, TFW_TA_VEC_SEAL1_BLOB,
                                         sizeof(TFW_TA_VEC_SEAL1_BLOB), 0u, out, &out_len));
    assert_nothing_left_open();
}

/* A refusal before any key: no session opened, nothing drawn, the output untouched. */
static void assert_seal_cmd_refused(uint32_t cmd, uint32_t types, size_t aad_len)
{
    uint8_t in[16];
    uint8_t out[16];
    TEE_Param p[4];

    memset(in, 0x11, sizeof(in));
    memset(out, 0xA5, sizeof(out));
    memset(p, 0, sizeof(p));
    p[0].memref.buffer = in;
    p[0].memref.size = sizeof(in);
    p[1].memref.buffer = in;
    p[1].memref.size = aad_len;
    p[2].memref.buffer = out;
    p[2].memref.size = sizeof(out);
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_BAD_PARAMETERS, invoke(cmd, types, p));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_tee_session_mock.opens);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_tee_session_mock.app_id_reads);
    TEST_ASSERT_EQUAL_size_t(0u, tfw_gp_mock.random_bytes);
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(sizeof(out), p[2].memref.size);
}

static void test_seal_and_unseal_refuse_the_wrong_param_types(void)
{
    static const uint32_t cmds[] = { TFW_TA_CMD_SEAL, TFW_TA_CMD_UNSEAL };
    static const uint32_t types[] = {
        TFW_TA_VEC_PT_NONE, TFW_TA_VEC_PT_GET_RANDOM, TFW_TA_VEC_PT_GET_INFO,
        0x00000555u, /* the output as an input */
        0x00000656u, /* the first as an output */
        0x00000065u, /* the output missing */
        0x00006655u, /* a fourth parameter */
        0x00000612u, /* values */
        0x00000657u, /* the output in and out */
        0x00000755u, /* a memref in and out */
    };
    size_t i;
    size_t j;

    for (i = 0u; i < sizeof(cmds) / sizeof(cmds[0]); i++)
        for (j = 0u; j < sizeof(types) / sizeof(types[0]); j++)
            assert_seal_cmd_refused(cmds[i], types[j], 0u);
}

static void test_seal_and_unseal_refuse_associated_data(void)
{
    /* go-xkms's format has no field for it, so data a caller thinks is bound is refused. */
    assert_seal_cmd_refused(TFW_TA_CMD_SEAL, SEAL_PT, 1u);
    assert_seal_cmd_refused(TFW_TA_CMD_UNSEAL, SEAL_PT, 1u);
}

static void test_seal_and_unseal_refuse_a_missing_buffer(void)
{
    uint8_t buf[16];
    size_t len = sizeof(buf);

    memset(buf, 0, sizeof(buf));
    be_the_vectors_ta(TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY);
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_BAD_PARAMETERS,
                            run_seal_cmd(TFW_TA_CMD_SEAL, NULL, 5u, 0u, buf, &len));
    len = sizeof(buf);
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_BAD_PARAMETERS,
                            run_seal_cmd(TFW_TA_CMD_SEAL, buf, 5u, 0u, NULL, &len));
    len = sizeof(buf);
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_BAD_PARAMETERS,
                            run_seal_cmd(TFW_TA_CMD_UNSEAL, NULL, 0u, 0u, buf, &len));
    len = sizeof(buf);
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_BAD_PARAMETERS,
                            run_seal_cmd(TFW_TA_CMD_UNSEAL, buf, 5u, 0u, NULL, &len));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_tee_session_mock.opens);
    TEST_ASSERT_EQUAL_size_t(0u, tfw_gp_mock.random_bytes);
}

static void test_a_failed_ta_unique_key_fails_both_commands_with_its_error(void)
{
    uint8_t out[128];
    size_t out_len = sizeof(out);

    be_the_vectors_ta(TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY);
    tfw_tee_session_mock.invoke_result = TEE_ERROR_NOT_SUPPORTED;
    memset(out, 0xA5, sizeof(out));
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_NOT_SUPPORTED,
                            run_seal_cmd(TFW_TA_CMD_SEAL, TFW_TA_VEC_SEAL1_PLAINTEXT,
                                         sizeof(TFW_TA_VEC_SEAL1_PLAINTEXT), 0u, out, &out_len));
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_NOT_SUPPORTED,
                            run_seal_cmd(TFW_TA_CMD_UNSEAL, TFW_TA_VEC_SEAL1_BLOB,
                                         sizeof(TFW_TA_VEC_SEAL1_BLOB), 0u, out, &out_len));
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(0u, tfw_gp_mock.random_bytes);
    assert_nothing_left_open();
}

static void test_a_ta_with_no_app_id_fails_both_commands_with_its_error(void)
{
    uint8_t out[128];
    size_t out_len = sizeof(out);

    be_the_vectors_ta(TFW_TA_VEC_SEAL1_TA_UNIQUE_KEY);
    tfw_tee_session_mock.app_id_result = TEE_ERROR_ITEM_NOT_FOUND;
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_ITEM_NOT_FOUND,
                            run_seal_cmd(TFW_TA_CMD_SEAL, NULL, 0u, 0u, out, &out_len));
    TEST_ASSERT_EQUAL_HEX32(TEE_ERROR_ITEM_NOT_FOUND,
                            run_seal_cmd(TFW_TA_CMD_UNSEAL, TFW_TA_VEC_SEAL2_BLOB,
                                         sizeof(TFW_TA_VEC_SEAL2_BLOB), 0u, out, &out_len));
    TEST_ASSERT_EQUAL_UINT(0u, tfw_tee_session_mock.opens);
    TEST_ASSERT_EQUAL_size_t(0u, tfw_gp_mock.random_bytes);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_uuid_is_the_vector_in_rfc9562_order);
    RUN_TEST(test_the_command_ids_and_bounds_are_the_vectors);
    RUN_TEST(test_the_gp_numbers_are_the_vectors);
    RUN_TEST(test_a_session_opens_with_no_parameter_and_no_context);
    RUN_TEST(test_an_open_with_a_parameter_is_refused);
    RUN_TEST(test_get_info_answers_version_1_and_no_flags);
    RUN_TEST(test_get_info_with_the_wrong_types_is_refused_untouched);
    RUN_TEST(test_get_random_fills_every_size_from_1_to_4096);
    RUN_TEST(test_get_random_refuses_a_size_outside_the_bounds_and_draws_nothing);
    RUN_TEST(test_get_random_with_the_wrong_types_is_refused);
    RUN_TEST(test_every_other_id_is_not_supported);
    RUN_TEST(test_the_check_passes_every_case_against_the_ta);
    RUN_TEST(test_no_context_is_reported_and_nothing_else_runs);
    RUN_TEST(test_no_session_is_reported_and_the_context_finalised);
    RUN_TEST(test_a_wrong_version_fails_get_info_only);
    RUN_TEST(test_a_stuck_generator_fails_get_random);
    RUN_TEST(test_an_error_not_from_the_ta_fails);
    RUN_TEST(test_a_failed_draw_fails_get_random);
    RUN_TEST(test_an_open_the_ta_should_refuse_fails_and_is_closed);
    RUN_TEST(test_seal_vector_1_is_go_xkms_blob);
    RUN_TEST(test_seal_vector_2_is_the_tag_alone);
    RUN_TEST(test_unseal_vector_1_is_its_plaintext);
    RUN_TEST(test_unseal_vector_2_is_empty);
    RUN_TEST(test_a_key_not_32_bytes_is_refused);
    RUN_TEST(test_a_short_buffer_answers_the_size_needed);
    RUN_TEST(test_a_truncated_blob_is_refused);
    RUN_TEST(test_a_malformed_blob_is_refused);
    RUN_TEST(test_a_version_not_1_is_refused);
    RUN_TEST(test_an_algorithm_not_a256gcm_is_refused);
    RUN_TEST(test_a_header_not_the_bytes_of_section_2_is_refused);
    RUN_TEST(test_a_non_minimal_head_is_refused);
    RUN_TEST(test_a_flipped_tag_byte_is_refused);
    RUN_TEST(test_another_tas_uuid_does_not_authenticate);
    RUN_TEST(test_derive_invokes_the_system_pta);
    RUN_TEST(test_derive_takes_1024_bytes_of_extra_data);
    RUN_TEST(test_derive_refuses_1025_bytes_of_extra_data);
    RUN_TEST(test_derive_refuses_a_key_of_15_bytes);
    RUN_TEST(test_derive_refuses_a_key_of_33_bytes);
    RUN_TEST(test_derive_returns_the_ptas_error_unchanged);
    RUN_TEST(test_derive_returns_the_open_error_unchanged);
    RUN_TEST(test_hkdf_of_the_zero_ta_key_is_seal_vector_1_key);
    RUN_TEST(test_hkdf_of_the_counting_ta_key_is_seal_vector_2_key);
    RUN_TEST(test_the_blob_key_of_the_zero_ta_key_is_seal_vector_1_key);
    RUN_TEST(test_the_blob_key_of_the_counting_ta_key_is_seal_vector_2_key);
    RUN_TEST(test_another_tas_uuid_derives_another_key);
    RUN_TEST(test_a_failed_ta_unique_key_leaves_no_blob_key);
    RUN_TEST(test_the_seal_ids_and_shapes_are_the_vectors);
    RUN_TEST(test_unseal_of_vector_1_returns_its_plaintext);
    RUN_TEST(test_unseal_of_vector_2_returns_the_empty_plaintext);
    RUN_TEST(test_a_seal_then_an_unseal_returns_the_input);
    RUN_TEST(test_a_seal_of_nothing_then_an_unseal_returns_nothing);
    RUN_TEST(test_every_seal_draws_its_own_nonce);
    RUN_TEST(test_a_short_seal_buffer_answers_the_size_needed);
    RUN_TEST(test_a_short_unseal_buffer_answers_the_size_needed);
    RUN_TEST(test_a_flipped_tag_byte_answers_mac_invalid_and_an_untouched_output);
    RUN_TEST(test_a_blob_changed_during_decryption_still_unseals_as_parsed);
    RUN_TEST(test_a_flipped_ciphertext_byte_answers_mac_invalid);
    RUN_TEST(test_a_blob_that_is_not_a_blob_answers_bad_format);
    RUN_TEST(test_another_tas_blob_does_not_unseal);
    RUN_TEST(test_seal_and_unseal_refuse_the_wrong_param_types);
    RUN_TEST(test_seal_and_unseal_refuse_associated_data);
    RUN_TEST(test_seal_and_unseal_refuse_a_missing_buffer);
    RUN_TEST(test_a_failed_ta_unique_key_fails_both_commands_with_its_error);
    RUN_TEST(test_a_ta_with_no_app_id_fails_both_commands_with_its_error);
    return UNITY_END();
}
