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
/* By its path: the firmware's own testdata/vectors.h is on the -I list. */
#include "../../optee/testdata/vectors.h"

void setUp(void)
{
    tfw_gp_mock_reset();
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
        TFW_TA_VEC_CMD_RESERVED, TFW_TA_VEC_CMD_SEAL, TFW_TA_VEC_CMD_UNSEAL,
        0x00000005u, 0x80000001u, TFW_TA_VEC_CMD_LAST,
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
    assert_line("tfw-ta-check: ok unknown-0x00000000\n");
    assert_line("tfw-ta-check: ok unknown-0x00000003\n");
    assert_line("tfw-ta-check: ok unknown-0x00000004\n");
    assert_line("tfw-ta-check: ok unknown-0xffffffff\n");
    assert_line("tfw-ta-check: ok open-bad-parameters\n");
    assert_line("tfw-ta-check: PASS, 14 of 14 passed\n");
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
    assert_line("tfw-ta-check: FAIL, 13 of 14 passed\n");
}

static void test_a_stuck_generator_fails_get_random(void)
{
    tfw_gp_mock.stuck_random = 1;
    TEST_ASSERT_EQUAL_INT(1, run_check());
    assert_line("tfw-ta-check: FAIL get-random\n");
}

/* The right code from the wrong origin is not the TA's answer. */
static void test_an_error_not_from_the_ta_fails(void)
{
    tfw_gp_mock.force = 1;
    tfw_gp_mock.force_cmd = TFW_TA_VEC_CMD_SEAL;
    tfw_gp_mock.force_rc = TEEC_ERROR_NOT_SUPPORTED;
    tfw_gp_mock.force_origin = TEEC_ORIGIN_TEE;
    TEST_ASSERT_EQUAL_INT(1, run_check());
    assert_line("tfw-ta-check: FAIL unknown-0x00000003\n");
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
    return UNITY_END();
}
