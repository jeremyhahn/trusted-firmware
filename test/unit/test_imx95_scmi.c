/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The SCMI messages the i.MX 95 M7 sends its System Manager
 * (src/hal/cortex-m/imx95/scmi.c), against literals written out from Arm
 * DEN0056E (sections 4.1 to 4.8, cited in scmi.h): the message header's fields (message_id 7:0, message_type 9:8,
 * protocol_id 17:10, token 27:18), the protocol and message identifiers, each
 * command's parameter words and the status codes. None is computed from the
 * code under test.
 */
#include <string.h>

#include "unity.h"
#include "scmi.h"

void setUp(void) {}
void tearDown(void) {}

static struct tfw_scmi_msg cmd, rsp;

static void rsp_of(uint32_t header, const uint8_t *payload, uint32_t len)
{
    memset(&rsp, 0, sizeof rsp);
    rsp.header = header;
    rsp.len    = len;
    memcpy(rsp.payload, payload, len);
}

static void test_the_header_places_each_field(void)
{
    /* protocol 0x10, message 0x00, command, token 0 */
    TEST_ASSERT_EQUAL_HEX32(0x00004000u, tfw_scmi_header(0x10u, 0x00u, 0u, 0u));
    /* protocol 0x14, message 0x07, delayed response, token 0x155 */
    TEST_ASSERT_EQUAL_HEX32(0x05545207u, tfw_scmi_header(0x14u, 0x07u, 2u, 0x155u));
    /* every field at its widest: bits 31 to 28 stay zero */
    TEST_ASSERT_EQUAL_HEX32(0x0FFFFFFFu, tfw_scmi_header(0xFFu, 0xFFu, 3u, 0x3FFu));
    /* a token past ten bits wraps; it never reaches the reserved bits */
    TEST_ASSERT_EQUAL_HEX32(0x00004000u, tfw_scmi_header(0x10u, 0x00u, 0u, 0x400u));
}

static void test_the_header_splits_into_its_fields(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x07u, TFW_SCMI_HDR_MSG_ID(0x05545207u));
    TEST_ASSERT_EQUAL_UINT32(2u, TFW_SCMI_HDR_TYPE(0x05545207u));
    TEST_ASSERT_EQUAL_UINT32(0x14u, TFW_SCMI_HDR_PROTO(0x05545207u));
    TEST_ASSERT_EQUAL_UINT32(0x155u, TFW_SCMI_HDR_TOKEN(0x05545207u));
}

static void test_the_identifiers_are_the_specifications(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x10u, TFW_SCMI_PROTO_BASE);
    TEST_ASSERT_EQUAL_HEX32(0x11u, TFW_SCMI_PROTO_POWER);
    TEST_ASSERT_EQUAL_HEX32(0x14u, TFW_SCMI_PROTO_CLOCK);
    TEST_ASSERT_EQUAL_HEX32(0x16u, TFW_SCMI_PROTO_RESET);
    TEST_ASSERT_EQUAL_HEX32(0x0u, TFW_SCMI_PROTOCOL_VERSION);
    TEST_ASSERT_EQUAL_HEX32(0x1u, TFW_SCMI_PROTOCOL_ATTRIBUTES);
    TEST_ASSERT_EQUAL_HEX32(0x4u, TFW_SCMI_POWER_STATE_SET);
    TEST_ASSERT_EQUAL_HEX32(0x6u, TFW_SCMI_CLOCK_RATE_GET);
    TEST_ASSERT_EQUAL_HEX32(0x7u, TFW_SCMI_CLOCK_CONFIG_SET);
    TEST_ASSERT_EQUAL_HEX32(0x4u, TFW_SCMI_RESET);
}

static void test_base_protocol_version_has_no_parameters(void)
{
    tfw_scmi_protocol_version(&cmd, TFW_SCMI_PROTO_BASE, 1u);
    TEST_ASSERT_EQUAL_HEX32(0x00044000u, cmd.header);
    TEST_ASSERT_EQUAL_UINT32(0u, cmd.len);
}

static void test_base_protocol_attributes_has_no_parameters(void)
{
    tfw_scmi_protocol_attributes(&cmd, TFW_SCMI_PROTO_BASE, 0u);
    TEST_ASSERT_EQUAL_HEX32(0x00004001u, cmd.header);
    TEST_ASSERT_EQUAL_UINT32(0u, cmd.len);
}

static void test_clock_config_set_v3_carries_extended_config_val(void)
{
    static const uint8_t want[12] = {
        0x2A, 0x00, 0x00, 0x00,   /* clock_id 42 */
        0x01, 0x00, 0x00, 0x00,   /* attributes: state 1, enabled */
        0x00, 0x00, 0x00, 0x00,   /* extended_config_val */
    };

    tfw_scmi_clock_config_set(&cmd, 5u, 0x00030000u, 42u, 1u);
    TEST_ASSERT_EQUAL_HEX32(0x00145007u, cmd.header);
    TEST_ASSERT_EQUAL_UINT32(12u, cmd.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, cmd.payload, 12);
}

static void test_clock_config_set_before_v3_is_two_words(void)
{
    static const uint8_t want[8] = {
        0x04, 0x03, 0x02, 0x01,   /* clock_id 0x01020304 */
        0x00, 0x00, 0x00, 0x00,   /* attributes: disabled */
    };

    tfw_scmi_clock_config_set(&cmd, 5u, 0x00020000u, 0x01020304u, 0u);
    TEST_ASSERT_EQUAL_UINT32(8u, cmd.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, cmd.payload, 8);
}

static void test_clock_rate_get_is_the_clock_id(void)
{
    static const uint8_t want[4] = { 0x2A, 0x00, 0x00, 0x00 };

    tfw_scmi_clock_rate_get(&cmd, 0x3FFu, 42u);
    TEST_ASSERT_EQUAL_HEX32(0x0FFC5006u, cmd.header);
    TEST_ASSERT_EQUAL_UINT32(4u, cmd.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, cmd.payload, 4);
}

static void test_power_state_set_is_flags_domain_state(void)
{
    static const uint8_t want[12] = {
        0x00, 0x00, 0x00, 0x00,   /* flags: synchronous */
        0x07, 0x00, 0x00, 0x00,   /* domain_id 7 */
        0x00, 0x00, 0x00, 0x40,   /* power_state: generic OFF */
    };

    tfw_scmi_power_state_set(&cmd, 2u, 7u, TFW_SCMI_POWER_OFF);
    TEST_ASSERT_EQUAL_HEX32(0x00084404u, cmd.header);
    TEST_ASSERT_EQUAL_UINT32(12u, cmd.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, cmd.payload, 12);
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, TFW_SCMI_POWER_ON);
}

static void test_reset_is_domain_flags_state(void)
{
    static const uint8_t want[12] = {
        0x03, 0x00, 0x00, 0x00,   /* domain_id 3 */
        0x01, 0x00, 0x00, 0x00,   /* flags: autonomous */
        0x00, 0x00, 0x00, 0x00,   /* reset_state: cold */
    };

    tfw_scmi_reset(&cmd, 3u, 3u, TFW_SCMI_RESET_AUTONOMOUS, TFW_SCMI_RESET_COLD);
    TEST_ASSERT_EQUAL_HEX32(0x000C5804u, cmd.header);
    TEST_ASSERT_EQUAL_UINT32(12u, cmd.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, cmd.payload, 12);
    TEST_ASSERT_EQUAL_HEX32(0x2u, TFW_SCMI_RESET_ASSERT);
    TEST_ASSERT_EQUAL_HEX32(0x4u, TFW_SCMI_RESET_ASYNC);
}

static void test_every_status_code_reads_back_as_numbered(void)
{
    static const int32_t codes[12] = { 0, -1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11 };
    uint8_t p[4];
    uint32_t i, w;

    tfw_scmi_protocol_version(&cmd, TFW_SCMI_PROTO_BASE, 9u);
    for (i = 0u; i < 12u; i++) {
        w    = (uint32_t)codes[i];
        p[0] = (uint8_t)w; p[1] = (uint8_t)(w >> 8); p[2] = (uint8_t)(w >> 16); p[3] = (uint8_t)(w >> 24);
        rsp_of(0x00244000u, p, 4u);
        TEST_ASSERT_EQUAL_INT32(codes[i], tfw_scmi_check(&cmd, &rsp));
        TEST_ASSERT_TRUE(tfw_scmi_status_known(codes[i]));
    }
    TEST_ASSERT_EQUAL_INT32(0, TFW_SCMI_SUCCESS);
    TEST_ASSERT_EQUAL_INT32(-1, TFW_SCMI_NOT_SUPPORTED);
    TEST_ASSERT_EQUAL_INT32(-2, TFW_SCMI_INVALID_PARAMETERS);
    TEST_ASSERT_EQUAL_INT32(-3, TFW_SCMI_DENIED);
    TEST_ASSERT_EQUAL_INT32(-4, TFW_SCMI_NOT_FOUND);
    TEST_ASSERT_EQUAL_INT32(-5, TFW_SCMI_OUT_OF_RANGE);
    TEST_ASSERT_EQUAL_INT32(-6, TFW_SCMI_BUSY);
    TEST_ASSERT_EQUAL_INT32(-7, TFW_SCMI_COMMS_ERROR);
    TEST_ASSERT_EQUAL_INT32(-8, TFW_SCMI_GENERIC_ERROR);
    TEST_ASSERT_EQUAL_INT32(-9, TFW_SCMI_HARDWARE_ERROR);
    TEST_ASSERT_EQUAL_INT32(-10, TFW_SCMI_PROTOCOL_ERROR);
    TEST_ASSERT_EQUAL_INT32(-11, TFW_SCMI_IN_USE);
}

static void test_an_unnumbered_status_is_unrecognised(void)
{
    static const uint8_t p[4] = { 0xF4, 0xFF, 0xFF, 0xFF };   /* -12 */

    tfw_scmi_protocol_version(&cmd, TFW_SCMI_PROTO_BASE, 9u);
    rsp_of(0x00244000u, p, 4u);
    TEST_ASSERT_EQUAL_INT32(-12, tfw_scmi_check(&cmd, &rsp));
    TEST_ASSERT_FALSE(tfw_scmi_status_known(-12));
    TEST_ASSERT_FALSE(tfw_scmi_status_known(1));
}

static void test_a_wrong_token_is_refused(void)
{
    static const uint8_t p[4] = { 0, 0, 0, 0 };

    tfw_scmi_protocol_version(&cmd, TFW_SCMI_PROTO_BASE, 9u);
    rsp_of(0x00284000u, p, 4u);   /* token 10 */
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_HEADER, tfw_scmi_check(&cmd, &rsp));
}

static void test_a_wrong_protocol_is_refused(void)
{
    static const uint8_t p[4] = { 0, 0, 0, 0 };

    tfw_scmi_protocol_version(&cmd, TFW_SCMI_PROTO_BASE, 9u);
    rsp_of(0x00245000u, p, 4u);   /* protocol 0x14 */
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_HEADER, tfw_scmi_check(&cmd, &rsp));
}

static void test_a_wrong_message_or_type_is_refused(void)
{
    static const uint8_t p[4] = { 0, 0, 0, 0 };

    tfw_scmi_protocol_version(&cmd, TFW_SCMI_PROTO_BASE, 9u);
    rsp_of(0x00244001u, p, 4u);   /* message 1 */
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_HEADER, tfw_scmi_check(&cmd, &rsp));
    rsp_of(0x00244200u, p, 4u);   /* a delayed response */
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_HEADER, tfw_scmi_check(&cmd, &rsp));
}

static void test_a_response_without_a_status_is_short(void)
{
    static const uint8_t p[4] = { 0, 0, 0, 0 };

    tfw_scmi_protocol_version(&cmd, TFW_SCMI_PROTO_BASE, 9u);
    rsp_of(0x00244000u, p, 0u);
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_SHORT, tfw_scmi_check(&cmd, &rsp));
}

static void test_protocol_version_reads_major_and_minor(void)
{
    static const uint8_t p[8] = { 0, 0, 0, 0, 0x02, 0x00, 0x02, 0x00 };   /* 2.2 */
    uint32_t v = 0u;

    rsp_of(0x00004000u, p, 8u);
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_SUCCESS, tfw_scmi_word(&rsp, 1u, &v));
    TEST_ASSERT_EQUAL_HEX32(0x00020002u, v);
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_SHORT, tfw_scmi_word(&rsp, 2u, &v));
}

static void test_base_attributes_reads_protocols_and_agents(void)
{
    static const uint8_t p[8] = { 0, 0, 0, 0, 0x09, 0x04, 0xAA, 0xBB };
    uint32_t protocols = 0u, agents = 0u;

    rsp_of(0x00004001u, p, 8u);
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_SUCCESS, tfw_scmi_base_attributes(&rsp, &protocols, &agents));
    TEST_ASSERT_EQUAL_UINT32(9u, protocols);
    TEST_ASSERT_EQUAL_UINT32(4u, agents);
    rsp.len = 4u;
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_SHORT, tfw_scmi_base_attributes(&rsp, &protocols, &agents));
}

static void test_clock_rate_is_low_word_first(void)
{
    static const uint8_t p[12] = {
        0x00, 0x00, 0x00, 0x00,   /* SUCCESS */
        0x00, 0x36, 0x6E, 0x01,   /* rate_lower: 24 000 000 */
        0x01, 0x00, 0x00, 0x00,   /* rate_upper: 1 */
    };
    uint64_t hz = 0u;

    rsp_of(0x00005006u, p, 12u);
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_SUCCESS, tfw_scmi_clock_rate(&rsp, &hz));
    TEST_ASSERT_TRUE(hz == 0x00000001016E3600ull);
    rsp.len = 8u;
    TEST_ASSERT_EQUAL_INT32(TFW_SCMI_E_SHORT, tfw_scmi_clock_rate(&rsp, &hz));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_header_places_each_field);
    RUN_TEST(test_the_header_splits_into_its_fields);
    RUN_TEST(test_the_identifiers_are_the_specifications);
    RUN_TEST(test_base_protocol_version_has_no_parameters);
    RUN_TEST(test_base_protocol_attributes_has_no_parameters);
    RUN_TEST(test_clock_config_set_v3_carries_extended_config_val);
    RUN_TEST(test_clock_config_set_before_v3_is_two_words);
    RUN_TEST(test_clock_rate_get_is_the_clock_id);
    RUN_TEST(test_power_state_set_is_flags_domain_state);
    RUN_TEST(test_reset_is_domain_flags_state);
    RUN_TEST(test_every_status_code_reads_back_as_numbered);
    RUN_TEST(test_an_unnumbered_status_is_unrecognised);
    RUN_TEST(test_a_wrong_token_is_refused);
    RUN_TEST(test_a_wrong_protocol_is_refused);
    RUN_TEST(test_a_wrong_message_or_type_is_refused);
    RUN_TEST(test_a_response_without_a_status_is_short);
    RUN_TEST(test_protocol_version_reads_major_and_minor);
    RUN_TEST(test_base_attributes_reads_protocols_and_agents);
    RUN_TEST(test_clock_rate_is_low_word_first);
    return UNITY_END();
}
