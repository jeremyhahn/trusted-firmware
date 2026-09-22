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
    RUN_TEST(test_sys_info_reports_uptime_free_ram_and_reset_cause);
    RUN_TEST(test_uptime_survives_past_sixteen_bits);
    return UNITY_END();
}
