/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The bootloader's four commands.
 *
 * Arity is checked per command rather than by a table -- with four commands a
 * table would be larger than the checks -- which means each check is a separate
 * line that can be separately wrong. So each command is tested twice: that it
 * works, and that a wrong payload length is refused rather than read past.
 */

#include <string.h>
#include "unity.h"
#include "boot_session.h"

void setUp(void) {}
void tearDown(void) {}

#define PAGE 128u

static tfw_ota_t ota;
static uint8_t  resp[TFW_MAX_PAYLOAD];
static uint8_t  rlen;
static tfw_boot_session_action_t action;

static tfw_status_t sink(uint32_t addr, const uint8_t *d, uint16_t n)
{
    (void)addr; (void)d; (void)n;
    return TFW_OK;
}

static tfw_status_t call(uint8_t cmd, const uint8_t *p, uint8_t plen)
{
    return tfw_boot_session_handle(&ota, cmd, p, plen, resp, &rlen, &action);
}

static void fresh(void)
{
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_ota_init(&ota, sink, PAGE));
    memset(resp, 0, sizeof resp);
    rlen = 0u;
    action = TFW_BOOT_SESSION_CONTINUE;
}

/* The only way a host can tell "in the bootloader, waiting" from "dead". Both
 * look identical otherwise: neither answers any application command. */
static void test_ping_echoes_its_payload(void)
{
    const uint8_t msg[4] = { 'b', 'o', 'o', 't' };
    fresh();
    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_PING, msg, 4u));
    TEST_ASSERT_EQUAL_UINT8(4u, rlen);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(msg, resp, 4);
    TEST_ASSERT_EQUAL_INT(TFW_BOOT_SESSION_CONTINUE, action);
}

static void test_ping_with_no_payload_is_accepted(void)
{
    fresh();
    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_PING, 0, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, rlen);
}

/* An application command reaching the bootloader tells the host, correctly,
 * that there is no application running. */
static void test_an_application_command_is_unknown_here(void)
{
    const uint8_t pin[2] = { 13u, 1u };
    fresh();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNKNOWN_CMD, call(TFW_CMD_DIGITAL_WRITE, pin, 2u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNKNOWN_CMD, call(TFW_CMD_CAPS, 0, 0u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNKNOWN_CMD, call(TFW_CMD_ATTEST, 0, 0u));
}

static void test_ota_begin_starts_a_session(void)
{
    const uint8_t len[4] = { 0x00u, 0x04u, 0x00u, 0x00u };   /* 1024 */
    fresh();
    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_OTA_BEGIN, len, 4u));
    TEST_ASSERT_EQUAL_UINT8(TFW_OTA_RECEIVING, tfw_ota_state(&ota));
}

static void test_ota_begin_with_a_wrong_payload_length_is_refused(void)
{
    const uint8_t len[5] = { 0x00u, 0x04u, 0x00u, 0x00u, 0x00u };
    fresh();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, call(TFW_CMD_OTA_BEGIN, len, 3u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, call(TFW_CMD_OTA_BEGIN, len, 5u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, call(TFW_CMD_OTA_BEGIN, 0, 0u));
}

static void test_ota_data_carries_its_offset(void)
{
    const uint8_t begin[4] = { 0x00u, 0x04u, 0x00u, 0x00u };
    uint8_t       chunk[20];
    fresh();
    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_OTA_BEGIN, begin, 4u));

    memset(chunk, 0, sizeof chunk);
    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_OTA_DATA, chunk, 20u));  /* offset 0 */

    chunk[0] = 16u;                                                   /* offset 16 */
    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_OTA_DATA, chunk, 20u));
    TEST_ASSERT_EQUAL_UINT32(32u, tfw_ota_received(&ota));
}

/* Fewer than five bytes cannot hold an offset AND a byte of data. Reading one
 * anyway would take four bytes of offset from a three-byte payload. */
static void test_ota_data_shorter_than_an_offset_is_refused(void)
{
    const uint8_t begin[4] = { 0x00u, 0x04u, 0x00u, 0x00u };
    const uint8_t stub[4]  = { 0u, 0u, 0u, 0u };
    fresh();
    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_OTA_BEGIN, begin, 4u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, call(TFW_CMD_OTA_DATA, stub, 4u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, call(TFW_CMD_OTA_DATA, stub, 0u));
}

/*
 * OTA_END succeeding means every BYTE arrived, not that anything was verified.
 * The action says COMMIT and the loop then measures and checks the signature --
 * a handler that reported plain success here would be reporting that an
 * unverified image had been accepted.
 */
static void test_ota_end_asks_for_a_commit_rather_than_claiming_success(void)
{
    const uint8_t begin[4] = { 0x80u, 0x00u, 0x00u, 0x00u };   /* 128 -> too short */
    const uint8_t begin2[4] = { 0x00u, 0x04u, 0x00u, 0x00u };  /* 1024 */
    uint8_t       chunk[36];
    fresh();

    /* too short to carry a trailer */
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, call(TFW_CMD_OTA_BEGIN, begin, 4u));

    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_OTA_BEGIN, begin2, 4u));
    memset(chunk, 0, sizeof chunk);
    for (uint32_t off = 0u; off < 1024u; off += 32u) {
        chunk[0] = (uint8_t)(off & 0xFFu);
        chunk[1] = (uint8_t)((off >> 8) & 0xFFu);
        TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_OTA_DATA, chunk, 36u));
    }
    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_OTA_END, 0, 0u));
    TEST_ASSERT_EQUAL_INT(TFW_BOOT_SESSION_COMMIT, action);
}

static void test_ota_end_with_a_payload_is_refused(void)
{
    const uint8_t junk[1] = { 0u };
    fresh();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, call(TFW_CMD_OTA_END, junk, 1u));
}

/* An interrupted session must be resumable or abandonable deliberately, and a
 * host cannot decide which without knowing where it got to. */
static void test_ota_status_reports_state_and_progress(void)
{
    const uint8_t begin[4] = { 0x00u, 0x04u, 0x00u, 0x00u };
    uint8_t       chunk[20];
    fresh();

    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_OTA_STATUS, 0, 0u));
    TEST_ASSERT_EQUAL_UINT8(5u, rlen);
    TEST_ASSERT_EQUAL_UINT8(TFW_OTA_IDLE, resp[0]);

    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_OTA_BEGIN, begin, 4u));
    memset(chunk, 0, sizeof chunk);
    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_OTA_DATA, chunk, 20u));

    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_OTA_STATUS, 0, 0u));
    TEST_ASSERT_EQUAL_UINT8(TFW_OTA_RECEIVING, resp[0]);
    TEST_ASSERT_EQUAL_UINT8(16u, resp[1]);
}

static void test_ota_status_with_a_payload_is_refused(void)
{
    const uint8_t junk[1] = { 0u };
    fresh();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, call(TFW_CMD_OTA_STATUS, junk, 1u));
}

static void test_reset_is_latched_as_an_action(void)
{
    const uint8_t mode[1] = { 0u };
    fresh();
    TEST_ASSERT_EQUAL_INT(TFW_OK, call(TFW_CMD_RESET, mode, 1u));
    TEST_ASSERT_EQUAL_INT(TFW_BOOT_SESSION_RESET, action);
}

static void test_reset_with_a_wrong_payload_length_is_refused(void)
{
    const uint8_t mode[2] = { 0u, 0u };
    fresh();
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, call(TFW_CMD_RESET, mode, 2u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_BAD_ARGC, call(TFW_CMD_RESET, 0, 0u));
    TEST_ASSERT_EQUAL_INT(TFW_BOOT_SESSION_CONTINUE, action);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ping_echoes_its_payload);
    RUN_TEST(test_ping_with_no_payload_is_accepted);
    RUN_TEST(test_an_application_command_is_unknown_here);
    RUN_TEST(test_ota_begin_starts_a_session);
    RUN_TEST(test_ota_begin_with_a_wrong_payload_length_is_refused);
    RUN_TEST(test_ota_data_carries_its_offset);
    RUN_TEST(test_ota_data_shorter_than_an_offset_is_refused);
    RUN_TEST(test_ota_end_asks_for_a_commit_rather_than_claiming_success);
    RUN_TEST(test_ota_end_with_a_payload_is_refused);
    RUN_TEST(test_ota_status_reports_state_and_progress);
    RUN_TEST(test_ota_status_with_a_payload_is_refused);
    RUN_TEST(test_reset_is_latched_as_an_action);
    RUN_TEST(test_reset_with_a_wrong_payload_length_is_refused);
    return UNITY_END();
}
