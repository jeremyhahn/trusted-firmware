/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */

#include <string.h>
#include "unity.h"
#include "dispatch.h"
#include "sys.h"
#include "hal_mock.h"
#include "tfw/hal.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "vectors.h"
#include "tfw/boot.h"
#include "auth_session.h"
#include "boot_request.h"

static uint8_t     inbuf[TFW_MAX_PAYLOAD];
static uint8_t     outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    memset(inbuf, 0, sizeof inbuf);
    memset(outbuf, 0, sizeof outbuf);
}
void tearDown(void) {}

static tfw_status_t run(uint8_t cmd, const uint8_t *payload, uint8_t n)
{
    if (n) { memcpy(inbuf, payload, n); }
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return tfw_dispatch(cmd, &r, &w);
}

/* issue #4: allocated ids with no handler in ANY build get a one-line comment
 * in protocol.h rather than silence -- this is the regression that keeps
 * them answering TFW_ERR_UNKNOWN_CMD rather than someone quietly wiring one up
 * without updating the other. */
static void test_the_five_reserved_ids_have_no_handler(void)
{
    const uint8_t reserved[] = {
        TFW_CMD_WDT_SET, TFW_CMD_SLEEP,
        TFW_CMD_PULSE_COUNT_START, TFW_CMD_PULSE_COUNT_READ, TFW_CMD_PULSE_COUNT_STOP,
    };
    unsigned i;
    for (i = 0u; i < sizeof reserved; i++) {
        TEST_ASSERT_FALSE(tfw_dispatch_knows(reserved[i]));
        TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, run(reserved[i], 0, 0u));
    }
}

/* A request OP is 7 bits, and 0x7F's response OP would be 0xFF, the event
 * marker. No entry may sit at or above it, or its answer reads as an event. */
static void test_no_command_id_reaches_the_event_marker(void)
{
    unsigned id;
    for (id = 0x7Fu; id <= 0xFFu; id++) {
        TEST_ASSERT_FALSE(tfw_dispatch_knows((uint8_t)id));
    }
    TEST_ASSERT_EQUAL_HEX8(TFW_OP_EVENT, (uint8_t)(0x7Fu | TFW_OP_RESPONSE_BIT));
}

/* This binary is the soft-tier host build (HOST_EXTRA_DEFS does not set
 * TFW_FEATURE_AUTH_SESSION): AUTH_CHALLENGE/AUTH_RESPONSE must be absent here
 * exactly like any other feature this tier does not carry, not merely
 * refuse gracefully -- see docs/security/threat-model.md.
 * test_auth_session.c is the element-tier build these actually work in. */
static void test_auth_commands_are_absent_from_a_soft_tier_build(void)
{
    TEST_ASSERT_FALSE(tfw_dispatch_knows(TFW_CMD_AUTH_CHALLENGE));
    TEST_ASSERT_FALSE(tfw_dispatch_knows(TFW_CMD_AUTH_RESPONSE));
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, run(TFW_CMD_AUTH_CHALLENGE, 0, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, run(TFW_CMD_AUTH_RESPONSE, 0, 0u));
}

/* issue #30: this binary takes the 328P's profile, which leaves ATTEST_CSR out
 * because it puts the part over its flash budget. Unlinked, it must answer like
 * any command the build never heard of, and CAPS must say so before a host asks
 * -- bit 15 clear, the alias certificate's bit 13 still set beside it. */
static void test_attest_csr_is_absent_from_a_328p_build_and_caps_says_so(void)
{
    uint32_t feats;
    const uint8_t b[TFW_ALIAS_NONCE_LEN] = { 0u };

    TEST_ASSERT_FALSE(tfw_dispatch_knows(TFW_CMD_ATTEST_CSR));
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, run(TFW_CMD_ATTEST_CSR, b, (uint8_t)sizeof b));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_CAPS, 0, 0u));
    feats = (uint32_t)outbuf[10] | ((uint32_t)outbuf[11] << 8)
          | ((uint32_t)outbuf[12] << 16) | ((uint32_t)outbuf[13] << 24);
    TEST_ASSERT_EQUAL_HEX32(0u, feats & (1uL << 15));
    TEST_ASSERT_EQUAL_HEX32(1uL << 13, feats & (1uL << 13));
}

static void test_the_system_commands_are_all_present(void)
{
    TEST_ASSERT_TRUE(tfw_dispatch_knows(TFW_CMD_CAPS));
    TEST_ASSERT_TRUE(tfw_dispatch_knows(TFW_CMD_PING));
    TEST_ASSERT_TRUE(tfw_dispatch_knows(TFW_CMD_RESET));
    TEST_ASSERT_TRUE(tfw_dispatch_knows(TFW_CMD_SYS_INFO));
}

/* Arity is checked centrally, so a handler never sees a payload of the wrong
 * size. Both directions, because a too-long payload is as wrong as a
 * too-short one and only one of them is obvious. */
static void test_a_payload_shorter_than_the_minimum_is_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, run(TFW_CMD_RESET, 0, 0u)); /* needs 1 */
}

static void test_a_payload_longer_than_the_maximum_is_refused(void)
{
    const uint8_t two[2] = { 0u, 0u };
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, run(TFW_CMD_SYS_INFO, two, 2u)); /* takes 0 */
}

static void test_an_unknown_command_writes_nothing_and_is_recorded_as_refused(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PING, 0, 0u));
    TEST_ASSERT_TRUE(tfw_dispatch_last_ok());
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, run(0x7Eu, 0, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_UINT8(0x7Eu, tfw_dispatch_last());
    TEST_ASSERT_FALSE(tfw_dispatch_last_ok());
    TEST_ASSERT_FALSE(tfw_dispatch_knows(0x7Eu));
}

static void test_a_payload_outside_the_arity_never_reaches_the_handler(void)
{
    const uint8_t two[2] = { 1u, 1u };
    /* RESET takes exactly one byte; mode 1 would arm a bootloader request. */
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, run(TFW_CMD_RESET, two, 2u));
    TEST_ASSERT_FALSE(tfw_sys_reset_pending());
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_UINT8(TFW_CMD_RESET, tfw_dispatch_last());
    TEST_ASSERT_FALSE(tfw_dispatch_last_ok());
}

static void test_a_handler_refusal_is_returned_unchanged_and_recorded(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run(TFW_CMD_RESET, (const uint8_t *)"\x09", 1u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_UINT8(TFW_CMD_RESET, tfw_dispatch_last());
    TEST_ASSERT_FALSE(tfw_dispatch_last_ok());
}

static void test_a_reply_that_overflows_the_writer_is_refused_as_too_long(void)
{
    const uint8_t msg[4] = { 'p', 'i', 'n', 'g' };
    memcpy(inbuf, msg, sizeof msg);
    tfw_reader_init(&r, inbuf, (uint8_t)sizeof msg);
    tfw_writer_init(&w, outbuf, 3u);
    TEST_ASSERT_EQUAL(TFW_ERR_TOO_LONG, tfw_dispatch(TFW_CMD_PING, &r, &w));
    TEST_ASSERT_FALSE(tfw_writer_ok(&w));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    TEST_ASSERT_EACH_EQUAL_HEX8(0u, outbuf, sizeof outbuf);
    TEST_ASSERT_EQUAL_UINT8(TFW_CMD_PING, tfw_dispatch_last());
    TEST_ASSERT_FALSE(tfw_dispatch_last_ok());
}

static void test_a_refusal_before_dispatch_is_recorded_as_not_ok(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PING, 0, 0u));
    tfw_dispatch_refused(TFW_CMD_SYS_INFO);
    TEST_ASSERT_EQUAL_UINT8(TFW_CMD_SYS_INFO, tfw_dispatch_last());
    TEST_ASSERT_FALSE(tfw_dispatch_last_ok());
}

static void test_ping_echoes_its_payload(void)
{
    const uint8_t msg[5] = { 'h', 'e', 'l', 'l', 'o' };
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PING, msg, 5u));
    TEST_ASSERT_EQUAL_UINT8(5u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, outbuf, 5u);
}

static void test_ping_with_no_payload_replies_with_no_payload(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_PING, 0, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
}

static void test_caps_reports_the_protocol_version_and_feature_bitmap(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_CAPS, 0, 0u));
    TEST_ASSERT_EQUAL_HEX8(TFW_PROTO_VERSION_MAJOR, outbuf[0]);
    TEST_ASSERT_EQUAL_HEX8(TFW_PROTO_VERSION_MINOR, outbuf[1]);
    /* and those are the literal bytes the SDK refuses any other major against */
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)TFWV_CAPS_PROTO_REPLY, outbuf,
                                  TFWV_CAPS_PROTO_REPLY_LEN);
    /* the feature bitmap is the last four bytes, little-endian */
    uint32_t feats = (uint32_t)outbuf[10] | ((uint32_t)outbuf[11] << 8)
                   | ((uint32_t)outbuf[12] << 16) | ((uint32_t)outbuf[13] << 24);
    TEST_ASSERT_EQUAL_HEX32(TFW_FEATURE_BITMAP, feats);
}

/* The six-byte pin page (#12), against the literal the SDK parses. */
static void test_caps_pin_page_is_the_six_byte_wire_vector(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_CAPS, (const uint8_t *)"\x0d", 1u));
    TEST_ASSERT_EQUAL_UINT8(TFWV_CAPS_PIN13_REPLY_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)TFWV_CAPS_PIN13_REPLY, outbuf,
                                  TFWV_CAPS_PIN13_REPLY_LEN);
}

/* CAPS must report what was COMPILED IN. If this ever drifts from the gates in
 * config.h, a host is told the firmware has a capability it does not, and finds
 * out by issuing a command that returns TFW_ERR_UNKNOWN_CMD. */
static void test_caps_agrees_with_the_dispatch_table_about_attestation(void)
{
    uint32_t feats;
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_CAPS, 0, 0u));
    feats = (uint32_t)outbuf[10] | ((uint32_t)outbuf[11] << 8)
          | ((uint32_t)outbuf[12] << 16) | ((uint32_t)outbuf[13] << 24);
    TEST_ASSERT_EQUAL(((feats >> 10) & 1u) != 0u, tfw_dispatch_knows(TFW_CMD_ATTEST) != 0u);
}

/* RESET latches rather than resetting, so the acknowledgement can be framed
 * and sent first. The old firmware slept 100 ms and hoped. */
static void test_reset_defers_until_the_link_is_idle(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_RESET, (const uint8_t *)"\x00", 1u));
    TEST_ASSERT_TRUE(tfw_sys_reset_pending());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());   /* not yet */

    tfw_sys_poll();                                        /* link is idle */
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
}

static void test_reset_does_not_fire_while_the_response_is_still_draining(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_RESET, (const uint8_t *)"\x00", 1u));
    (void)tfw_hal_uart_tx_try(0xAAu);      /* a byte still in the ring */

    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());

    uint8_t sink[4];
    (void)tfw_mock_drain_tx(sink, sizeof sink);
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
}

static void test_reset_rejects_an_unknown_mode(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run(TFW_CMD_RESET, (const uint8_t *)"\x09", 1u));
    TEST_ASSERT_FALSE(tfw_sys_reset_pending());
}

/* ------------------------------------------------- RESET(1): the bootloader */

static uint8_t request_armed(void)
{
    return tfw_boot_request_valid(tfw_mock_handoff(), TFW_RESET_WATCHDOG);
}

/* The request is what makes mode 1 mean something: tfw-boot reads it after the
 * watchdog reset. Armed when the reset fires, so the bytes that tfw-boot finds
 * are exactly the ones the shared codec defines. */
static void test_reset_to_bootloader_leaves_a_request_then_resets(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_RESET, (const uint8_t *)"\x01", 1u));
    TEST_ASSERT_FALSE(request_armed());               /* not before the link drains */

    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
    TEST_ASSERT_TRUE(request_armed());
}

/* The soft and CRYPTO=none tiers link the permissive auth gate
 * (auth_session_none.c), so RESET(1) there needs no session. The element tier's
 * refusal is pinned in test_auth_session.c; this is the other half of that
 * contract, and the build that exercises it is the one every host test uses. */
static void test_reset_to_bootloader_is_not_gated_on_the_soft_tier(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_RESET, (const uint8_t *)"\x01", 1u));
    TEST_ASSERT_TRUE(tfw_sys_reset_pending());
}

/* Mode 0 is unchanged: a soft reset must never land in the bootloader. */
static void test_soft_reset_leaves_no_request(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_RESET, (const uint8_t *)"\x00", 1u));
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
    TEST_ASSERT_FALSE(request_armed());
}

/* The latest RESET wins. Arming at poll time rather than at command time is
 * what makes this true: a request armed by the first command would outlive the
 * second. */
static void test_a_later_soft_reset_cancels_a_pending_bootloader_request(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_RESET, (const uint8_t *)"\x01", 1u));
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_RESET, (const uint8_t *)"\x00", 1u));
    tfw_sys_poll();
    TEST_ASSERT_FALSE(request_armed());
}

/* Nothing arms while the response is still draining, mode 1 included. */
static void test_no_request_is_left_while_the_response_is_draining(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_RESET, (const uint8_t *)"\x01", 1u));
    (void)tfw_hal_uart_tx_try(0xAAu);
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
    TEST_ASSERT_FALSE(request_armed());
}

static void test_a_refused_reset_leaves_no_request(void)
{
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run(TFW_CMD_RESET, (const uint8_t *)"\x02", 1u));
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
    TEST_ASSERT_FALSE(request_armed());
}

static void test_sys_info_reports_uptime_free_ram_and_reset_cause(void)
{
    tfw_mock_set_millis(0x12345678u);
    tfw_mock_set_free_ram(0x0321u);
    tfw_mock_set_reset_cause(TFW_RESET_WATCHDOG);

    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_SYS_INFO, 0, 0u));
    TEST_ASSERT_EQUAL_HEX8(0x78u, outbuf[0]);   /* little-endian uptime */
    TEST_ASSERT_EQUAL_HEX8(0x12u, outbuf[3]);
    TEST_ASSERT_EQUAL_HEX8(0x21u, outbuf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x03u, outbuf[5]);
    TEST_ASSERT_EQUAL_HEX8(TFW_RESET_WATCHDOG, outbuf[6]);
}

/* #5: err_crc and err_format are APPENDED after reset_cause. The bytes are
 * checked against the published literal rather than reconstructed field by
 * field, so a change to the append that broke the prefix an older host reads
 * would fail here even if every individual field still decoded correctly in
 * isolation. */
static void test_sys_info_appends_the_framer_error_counters(void)
{
    tfw_mock_set_millis(0x12345678u);
    tfw_mock_set_free_ram(0x0321u);
    tfw_mock_set_reset_cause(TFW_RESET_WATCHDOG);
    tfw_sys_set_framer_errors(5u, 7u);

    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_SYS_INFO, 0, 0u));
    TEST_ASSERT_EQUAL_UINT8(TFWV_SYSINFO_WIDE_REPLY_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)TFWV_SYSINFO_WIDE_REPLY, outbuf,
                                  TFWV_SYSINFO_WIDE_REPLY_LEN);
    /* #12 appended free_ram's u32 after these, so the reply a host built for #5
     * reads is still its prefix. */
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)TFWV_SYSINFO_NEW_REPLY, outbuf,
                                  TFWV_SYSINFO_NEW_REPLY_LEN);
}

/* #48: the reset causes appended after TFW_RESET_WATCHDOG reach the wire at byte
 * 6, checked against literal replies. */
static void test_sys_info_reports_the_appended_reset_causes(void)
{
    tfw_mock_set_millis(0x12345678u);
    tfw_mock_set_free_ram(0x0321u);
    tfw_sys_set_framer_errors(5u, 7u);

    tfw_mock_set_reset_cause(TFW_RESET_SOFTWARE);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_SYS_INFO, 0, 0u));
    TEST_ASSERT_EQUAL_UINT8(TFWV_SYSINFO_SOFTWARE_REPLY_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)TFWV_SYSINFO_SOFTWARE_REPLY, outbuf,
                                  TFWV_SYSINFO_SOFTWARE_REPLY_LEN);

    tfw_mock_set_reset_cause(TFW_RESET_PANIC);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_SYS_INFO, 0, 0u));
    TEST_ASSERT_EQUAL_UINT8(TFWV_SYSINFO_PANIC_REPLY_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)TFWV_SYSINFO_PANIC_REPLY, outbuf,
                                  TFWV_SYSINFO_PANIC_REPLY_LEN);
}

/* #12: a free_ram the u16 cannot hold saturates there and arrives exact in the
 * appended u32. Wrapping would hand an older host a small, wrong number. */
static void test_sys_info_widens_free_ram_by_appending_a_u32(void)
{
    tfw_mock_set_millis(0x12345678u);
    tfw_mock_set_free_ram(0x00012345u);
    tfw_mock_set_reset_cause(TFW_RESET_WATCHDOG);
    tfw_sys_set_framer_errors(5u, 7u);

    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_SYS_INFO, 0, 0u));
    TEST_ASSERT_EQUAL_UINT8(TFWV_SYSINFO_BIG_REPLY_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY((const uint8_t *)TFWV_SYSINFO_BIG_REPLY, outbuf,
                                  TFWV_SYSINFO_BIG_REPLY_LEN);
}

/* A device that has seen no framing errors reports zero rather than something
 * left over from a previous command -- tfw_sys_reset_state() clears the
 * mirror, and setUp() calls it before every test in this file. */
static void test_sys_info_reports_zero_counters_when_nothing_has_gone_wrong(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_SYS_INFO, 0, 0u));
    TEST_ASSERT_EQUAL_HEX16(0u, (uint16_t)(outbuf[7] | (outbuf[8] << 8)));
    TEST_ASSERT_EQUAL_HEX16(0u, (uint16_t)(outbuf[9] | (outbuf[10] << 8)));
}

/* tfw_framer_t's counters are SATURATING (frame.c never wraps them past
 * 0xFFFF), so the one boundary tfw_sys_set_framer_errors() must not mangle is
 * the saturated value itself -- a setter that narrowed through a smaller type
 * along the way would wrap it back to zero silently. */
static void test_sys_info_does_not_truncate_saturated_counters(void)
{
    tfw_sys_set_framer_errors(0xFFFFu, 0xFFFFu);

    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_SYS_INFO, 0, 0u));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, (uint16_t)(outbuf[7] | (outbuf[8] << 8)));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, (uint16_t)(outbuf[9] | (outbuf[10] << 8)));
}

/* Uptime is a full 32 bits on the wire. The firmware being replaced pushed
 * millis() through itoa() into an int and wrapped every 65 seconds, so a device
 * up for an hour reported under a minute. */
static void test_uptime_survives_past_sixteen_bits(void)
{
    tfw_mock_set_millis(4000000000u);
    TEST_ASSERT_EQUAL(TFW_OK, run(TFW_CMD_SYS_INFO, 0, 0u));
    uint32_t up = (uint32_t)outbuf[0] | ((uint32_t)outbuf[1] << 8)
                | ((uint32_t)outbuf[2] << 16) | ((uint32_t)outbuf[3] << 24);
    TEST_ASSERT_EQUAL_UINT32(4000000000u, up);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_five_reserved_ids_have_no_handler);
    RUN_TEST(test_no_command_id_reaches_the_event_marker);
    RUN_TEST(test_auth_commands_are_absent_from_a_soft_tier_build);
    RUN_TEST(test_attest_csr_is_absent_from_a_328p_build_and_caps_says_so);
    RUN_TEST(test_the_system_commands_are_all_present);
    RUN_TEST(test_a_payload_shorter_than_the_minimum_is_refused);
    RUN_TEST(test_a_payload_longer_than_the_maximum_is_refused);
    RUN_TEST(test_an_unknown_command_writes_nothing_and_is_recorded_as_refused);
    RUN_TEST(test_a_payload_outside_the_arity_never_reaches_the_handler);
    RUN_TEST(test_a_handler_refusal_is_returned_unchanged_and_recorded);
    RUN_TEST(test_a_reply_that_overflows_the_writer_is_refused_as_too_long);
    RUN_TEST(test_a_refusal_before_dispatch_is_recorded_as_not_ok);
    RUN_TEST(test_ping_echoes_its_payload);
    RUN_TEST(test_ping_with_no_payload_replies_with_no_payload);
    RUN_TEST(test_caps_reports_the_protocol_version_and_feature_bitmap);
    RUN_TEST(test_caps_pin_page_is_the_six_byte_wire_vector);
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
    RUN_TEST(test_sys_info_widens_free_ram_by_appending_a_u32);
    RUN_TEST(test_sys_info_appends_the_framer_error_counters);
    RUN_TEST(test_sys_info_reports_the_appended_reset_causes);
    RUN_TEST(test_sys_info_reports_zero_counters_when_nothing_has_gone_wrong);
    RUN_TEST(test_sys_info_does_not_truncate_saturated_counters);
    RUN_TEST(test_uptime_survives_past_sixteen_bits);
    return UNITY_END();
}
