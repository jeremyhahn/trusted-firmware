/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include "unity.h"
#include "dispatch.h"
#include "sys.h"
#include "hal_mock.h"
#include "ag/hal.h"
#include "ag/protocol.h"
#include "ag/config.h"
#include "vectors.h"
#include "ag/boot.h"
#include "reset_cause.h"
#include "auth_session.h"
#include "boot_request.h"

static uint8_t     inbuf[AG_MAX_PAYLOAD];
static uint8_t     outbuf[AG_MAX_PAYLOAD];
static ag_reader_t r;
static ag_writer_t w;

void setUp(void)
{
    ag_mock_reset();
    ag_sys_reset_state();
    memset(inbuf, 0, sizeof inbuf);
    memset(outbuf, 0, sizeof outbuf);
}
void tearDown(void) {}

static ag_status_t run(uint8_t cmd, const uint8_t *payload, uint8_t n)
{
    if (n) { memcpy(inbuf, payload, n); }
    ag_reader_init(&r, inbuf, n);
    ag_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return ag_dispatch(cmd, &r, &w);
}

static void test_an_unknown_command_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_UNKNOWN_CMD, run(0x7Eu, 0, 0u));
    TEST_ASSERT_FALSE(ag_dispatch_knows(0x7Eu));
}

/* issue #4: allocated ids with no handler in ANY build get a one-line comment
 * in protocol.h rather than silence -- this is the regression that keeps
 * them answering AG_ERR_UNKNOWN_CMD rather than someone quietly wiring one up
 * without updating the other. */
static void test_the_five_reserved_ids_have_no_handler(void)
{
    const uint8_t reserved[] = {
        AG_CMD_WDT_SET, AG_CMD_SLEEP,
        AG_CMD_PULSE_COUNT_START, AG_CMD_PULSE_COUNT_READ, AG_CMD_PULSE_COUNT_STOP,
    };
    unsigned i;
    for (i = 0u; i < sizeof reserved; i++) {
        TEST_ASSERT_FALSE(ag_dispatch_knows(reserved[i]));
        TEST_ASSERT_EQUAL(AG_ERR_UNKNOWN_CMD, run(reserved[i], 0, 0u));
    }
}

/* This binary is the soft-tier host build (HOST_EXTRA_DEFS does not set
 * AG_FEATURE_AUTH_SESSION): AUTH_CHALLENGE/AUTH_RESPONSE must be absent here
 * exactly like any other feature this tier does not carry, not merely
 * refuse gracefully -- see docs/security/threat-model.md.
 * test_auth_session.c is the element-tier build these actually work in. */
static void test_auth_commands_are_absent_from_a_soft_tier_build(void)
{
    TEST_ASSERT_FALSE(ag_dispatch_knows(AG_CMD_AUTH_CHALLENGE));
    TEST_ASSERT_FALSE(ag_dispatch_knows(AG_CMD_AUTH_RESPONSE));
    TEST_ASSERT_EQUAL(AG_ERR_UNKNOWN_CMD, run(AG_CMD_AUTH_CHALLENGE, 0, 0u));
    TEST_ASSERT_EQUAL(AG_ERR_UNKNOWN_CMD, run(AG_CMD_AUTH_RESPONSE, 0, 0u));
}

static void test_the_system_commands_are_all_present(void)
{
    TEST_ASSERT_TRUE(ag_dispatch_knows(AG_CMD_CAPS));
    TEST_ASSERT_TRUE(ag_dispatch_knows(AG_CMD_PING));
    TEST_ASSERT_TRUE(ag_dispatch_knows(AG_CMD_RESET));
    TEST_ASSERT_TRUE(ag_dispatch_knows(AG_CMD_SYS_INFO));
}

/* Arity is checked centrally, so a handler never sees a payload of the wrong
 * size. Both directions, because a too-long payload is as wrong as a
 * too-short one and only one of them is obvious. */
static void test_a_payload_shorter_than_the_minimum_is_refused(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_BAD_ARGC, run(AG_CMD_RESET, 0, 0u)); /* needs 1 */
}

static void test_a_payload_longer_than_the_maximum_is_refused(void)
{
    const uint8_t two[2] = { 0u, 0u };
    TEST_ASSERT_EQUAL(AG_ERR_BAD_ARGC, run(AG_CMD_SYS_INFO, two, 2u)); /* takes 0 */
}

static void test_ping_echoes_its_payload(void)
{
    const uint8_t msg[5] = { 'h', 'e', 'l', 'l', 'o' };
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_PING, msg, 5u));
    TEST_ASSERT_EQUAL_UINT8(5u, ag_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, outbuf, 5u);
}

static void test_ping_with_no_payload_replies_with_no_payload(void)
{
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_PING, 0, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, ag_writer_len(&w));
}

static void test_caps_reports_the_protocol_version_and_feature_bitmap(void)
{
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_CAPS, 0, 0u));
    TEST_ASSERT_EQUAL_HEX8(AG_PROTO_VERSION_MAJOR, outbuf[0]);
    TEST_ASSERT_EQUAL_HEX8(AG_PROTO_VERSION_MINOR, outbuf[1]);
    /* the feature bitmap is the last four bytes, little-endian */
    uint32_t feats = (uint32_t)outbuf[10] | ((uint32_t)outbuf[11] << 8)
                   | ((uint32_t)outbuf[12] << 16) | ((uint32_t)outbuf[13] << 24);
    TEST_ASSERT_EQUAL_HEX32(AG_FEATURE_BITMAP, feats);
}

/* CAPS must report what was COMPILED IN. If this ever drifts from the gates in
 * config.h, a host is told the firmware has a capability it does not, and finds
 * out by issuing a command that returns AG_ERR_UNKNOWN_CMD. */
static void test_caps_agrees_with_the_dispatch_table_about_attestation(void)
{
    uint32_t feats;
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_CAPS, 0, 0u));
    feats = (uint32_t)outbuf[10] | ((uint32_t)outbuf[11] << 8)
          | ((uint32_t)outbuf[12] << 16) | ((uint32_t)outbuf[13] << 24);
    TEST_ASSERT_EQUAL(((feats >> 10) & 1u) != 0u, ag_dispatch_knows(AG_CMD_ATTEST) != 0u);
}

/* RESET latches rather than resetting, so the acknowledgement can be framed
 * and sent first. The old firmware slept 100 ms and hoped. */
static void test_reset_defers_until_the_link_is_idle(void)
{
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_RESET, (const uint8_t *)"\x00", 1u));
    TEST_ASSERT_TRUE(ag_sys_reset_pending());
    TEST_ASSERT_EQUAL_UINT(0u, ag_mock_reset_count());   /* not yet */

    ag_sys_poll();                                        /* link is idle */
    TEST_ASSERT_EQUAL_UINT(1u, ag_mock_reset_count());
}

static void test_reset_does_not_fire_while_the_response_is_still_draining(void)
{
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_RESET, (const uint8_t *)"\x00", 1u));
    (void)ag_hal_uart_tx_try(0xAAu);      /* a byte still in the ring */

    ag_sys_poll();
    TEST_ASSERT_EQUAL_UINT(0u, ag_mock_reset_count());

    uint8_t sink[4];
    (void)ag_mock_drain_tx(sink, sizeof sink);
    ag_sys_poll();
    TEST_ASSERT_EQUAL_UINT(1u, ag_mock_reset_count());
}

static void test_reset_rejects_an_unknown_mode(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, run(AG_CMD_RESET, (const uint8_t *)"\x09", 1u));
    TEST_ASSERT_FALSE(ag_sys_reset_pending());
}

/* ------------------------------------------------- RESET(1): the bootloader */

static uint8_t request_armed(void)
{
    return ag_boot_request_valid(ag_mock_handoff(), AG_MCUSR_WDRF);
}

/* The request is what makes mode 1 mean something: ag-boot reads it after the
 * watchdog reset. Armed when the reset fires, so the bytes that ag-boot finds
 * are exactly the ones the shared codec defines. */
static void test_reset_to_bootloader_leaves_a_request_then_resets(void)
{
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_RESET, (const uint8_t *)"\x01", 1u));
    TEST_ASSERT_FALSE(request_armed());               /* not before the link drains */

    ag_sys_poll();
    TEST_ASSERT_EQUAL_UINT(1u, ag_mock_reset_count());
    TEST_ASSERT_TRUE(request_armed());
}

/* The soft and CRYPTO=none tiers link the permissive auth gate
 * (auth_session_none.c), so RESET(1) there needs no session. The element tier's
 * refusal is pinned in test_auth_session.c; this is the other half of that
 * contract, and the build that exercises it is the one every host test uses. */
static void test_reset_to_bootloader_is_not_gated_on_the_soft_tier(void)
{
    TEST_ASSERT_EQUAL(AG_OK, ag_auth_gate());
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_RESET, (const uint8_t *)"\x01", 1u));
    TEST_ASSERT_TRUE(ag_sys_reset_pending());
}

/* Mode 0 is unchanged: a soft reset must never land in the bootloader. */
static void test_soft_reset_leaves_no_request(void)
{
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_RESET, (const uint8_t *)"\x00", 1u));
    ag_sys_poll();
    TEST_ASSERT_EQUAL_UINT(1u, ag_mock_reset_count());
    TEST_ASSERT_FALSE(request_armed());
}

/* The latest RESET wins. Arming at poll time rather than at command time is
 * what makes this true: a request armed by the first command would outlive the
 * second. */
static void test_a_later_soft_reset_cancels_a_pending_bootloader_request(void)
{
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_RESET, (const uint8_t *)"\x01", 1u));
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_RESET, (const uint8_t *)"\x00", 1u));
    ag_sys_poll();
    TEST_ASSERT_FALSE(request_armed());
}

/* Nothing arms while the response is still draining, mode 1 included. */
static void test_no_request_is_left_while_the_response_is_draining(void)
{
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_RESET, (const uint8_t *)"\x01", 1u));
    (void)ag_hal_uart_tx_try(0xAAu);
    ag_sys_poll();
    TEST_ASSERT_EQUAL_UINT(0u, ag_mock_reset_count());
    TEST_ASSERT_FALSE(request_armed());
}

static void test_a_refused_reset_leaves_no_request(void)
{
    TEST_ASSERT_EQUAL(AG_ERR_RANGE, run(AG_CMD_RESET, (const uint8_t *)"\x02", 1u));
    ag_sys_poll();
    TEST_ASSERT_EQUAL_UINT(0u, ag_mock_reset_count());
    TEST_ASSERT_FALSE(request_armed());
}

static void test_sys_info_reports_uptime_free_ram_and_reset_cause(void)
{
    ag_mock_set_millis(0x12345678u);
    ag_mock_set_free_ram(0x0321u);
    ag_mock_set_reset_cause(AG_RESET_WATCHDOG);

    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_SYS_INFO, 0, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x78u, outbuf[0]);   /* little-endian uptime */
    TEST_ASSERT_EQUAL_HEX8(0x12u, outbuf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x21u, outbuf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x03u, outbuf[5]);
    TEST_ASSERT_EQUAL_HEX8(AG_RESET_WATCHDOG, outbuf[6]);
}

/* #5: err_crc and err_format are APPENDED after reset_cause. The bytes are
 * checked against the published literal rather than reconstructed field by
 * field, so a change to the append that broke the prefix an older host reads
 * would fail here even if every individual field still decoded correctly in
 * isolation. */
static void test_sys_info_appends_the_framer_error_counters(void)
{
    ag_mock_set_millis(0x12345678u);
    ag_mock_set_free_ram(0x0321u);
    ag_mock_set_reset_cause(AG_RESET_WATCHDOG);
    ag_sys_set_framer_errors(5u, 7u);

    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_SYS_INFO, 0, 0u));
    TEST_ASSERT_EQUAL_UINT8(AGV_SYSINFO_NEW_REPLY_LEN, ag_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)AGV_SYSINFO_NEW_REPLY, outbuf,
                                  AGV_SYSINFO_NEW_REPLY_LEN);
}

/* A device that has seen no framing errors reports zero rather than something
 * left over from a previous command -- ag_sys_reset_state() clears the
 * mirror, and setUp() calls it before every test in this file. */
static void test_sys_info_reports_zero_counters_when_nothing_has_gone_wrong(void)
{
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_SYS_INFO, 0, 0u));
    TEST_ASSERT_EQUAL_HEX16(0u, (uint16_t)(outbuf[7] | (outbuf[8] << 8)));
    TEST_ASSERT_EQUAL_HEX16(0u, (uint16_t)(outbuf[9] | (outbuf[10] << 8)));
}

/* ag_framer_t's counters are SATURATING (frame.c never wraps them past
 * 0xFFFF), so the one boundary ag_sys_set_framer_errors() must not mangle is
 * the saturated value itself -- a setter that narrowed through a smaller type
 * along the way would wrap it back to zero silently. */
static void test_sys_info_does_not_truncate_saturated_counters(void)
{
    ag_sys_set_framer_errors(0xFFFFu, 0xFFFFu);

    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_SYS_INFO, 0, 0u));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, (uint16_t)(outbuf[7] | (outbuf[8] << 8)));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, (uint16_t)(outbuf[9] | (outbuf[10] << 8)));
}

/* Uptime is a full 32 bits on the wire. The firmware being replaced pushed
 * millis() through itoa() into an int and wrapped every 65 seconds, so a device
 * up for an hour reported under a minute. */
static void test_uptime_survives_past_sixteen_bits(void)
{
    ag_mock_set_millis(4000000000u);
    TEST_ASSERT_EQUAL(AG_OK, run(AG_CMD_SYS_INFO, 0, 0u));
    uint32_t up = (uint32_t)outbuf[0] | ((uint32_t)outbuf[1] << 8)
                | ((uint32_t)outbuf[2] << 16) | ((uint32_t)outbuf[3] << 24);
    TEST_ASSERT_EQUAL_UINT32(4000000000u, up);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_unknown_command_is_refused);
    RUN_TEST(test_the_five_reserved_ids_have_no_handler);
    RUN_TEST(test_auth_commands_are_absent_from_a_soft_tier_build);
    RUN_TEST(test_the_system_commands_are_all_present);
    RUN_TEST(test_a_payload_shorter_than_the_minimum_is_refused);
    RUN_TEST(test_a_payload_longer_than_the_maximum_is_refused);
    RUN_TEST(test_ping_echoes_its_payload);
    RUN_TEST(test_ping_with_no_payload_replies_with_no_payload);
    RUN_TEST(test_caps_reports_the_protocol_version_and_feature_bitmap);
    RUN_TEST(test_caps_agrees_with_the_dispatch_table_about_attestation);
    RUN_TEST(test_reset_defers_until_the_link_is_idle);
    RUN_TEST(test_reset_does_not_fire_while_the_response_is_still_draining);
    RUN_TEST(test_reset_rejects_an_unknown_mode);
    RUN_TEST(test_reset_to_bootloader_leaves_a_request_then_resets);
    RUN_TEST(test_reset_to_bootloader_is_not_gated_on_the_soft_tier);
    RUN_TEST(test_soft_reset_leaves_no_request);
    RUN_TEST(test_a_later_soft_reset_cancels_a_pending_bootloader_request);
    RUN_TEST(test_no_request_is_left_while_the_response_is_draining);
    RUN_TEST(test_a_refused_reset_leaves_no_request);
    RUN_TEST(test_sys_info_reports_uptime_free_ram_and_reset_cause);
    RUN_TEST(test_sys_info_appends_the_framer_error_counters);
    RUN_TEST(test_sys_info_reports_zero_counters_when_nothing_has_gone_wrong);
    RUN_TEST(test_sys_info_does_not_truncate_saturated_counters);
    RUN_TEST(test_uptime_survives_past_sixteen_bits);
    return UNITY_END();
}
