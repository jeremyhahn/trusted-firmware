/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The sys capability, through its own entry points rather than dispatch.
 *
 * Linked against the default host library: auth_session_none.c, so
 * tfw_auth_gate() is permissive, and the real update_request.c, so this board
 * has a boot section. test_sys_noboot covers the other side of the latter.
 */

#include <string.h>
#include "unity.h"
#include "sys.h"
#include "hal_mock.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/board.h"
#include "tfw/boot.h"

static uint8_t      inbuf[TFW_MAX_PAYLOAD];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    memset(outbuf, 0xEE, sizeof outbuf);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
}
void tearDown(void) {}

static void args(const uint8_t *b, uint8_t n)
{
    if (n) {
        memcpy(inbuf, b, n);
    }
    tfw_reader_init(&r, inbuf, n);
}

static uint8_t handoff_touched(void)
{
    uint8_t i;

    for (i = 0u; i < TFW_BOOT_REQUEST_LEN; i++) {
        if (tfw_mock_handoff()[i] != 0u) {
            return 1u;
        }
    }
    return 0u;
}

static void test_caps_without_an_argument_returns_the_board_header(void)
{
    uint32_t bitmap = TFW_FEATURE_BITMAP;
    uint16_t algs   = tfw_crypto_algorithms();
    uint8_t  want[16];

    want[0]  = TFW_PROTO_VERSION_MAJOR;
    want[1]  = TFW_PROTO_VERSION_MINOR;
    want[2]  = 0u;                       /* TFW_FW_MAJOR, sys.c:16 */
    want[3]  = 1u;                       /* TFW_FW_MINOR, sys.c:17 */
    want[4]  = 0u;                       /* TFW_FW_PATCH, sys.c:18 */
    want[5]  = TFW_BOARD_ID;
    want[6]  = TFW_PIN_COUNT;
    want[7]  = (uint8_t)TFW_SCHED_SLOTS;
    want[8]  = (uint8_t)TFW_MAX_PAYLOAD;
    want[9]  = (uint8_t)tfw_crypto_tier();
    want[10] = (uint8_t)(bitmap & 0xFFu);
    want[11] = (uint8_t)(bitmap >> 8);
    want[12] = (uint8_t)(bitmap >> 16);
    want[13] = (uint8_t)(bitmap >> 24);
    want[14] = (uint8_t)(algs & 0xFFu);
    want[15] = (uint8_t)(algs >> 8);

    args(NULL, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sys_cmd_caps(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(sizeof want, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, outbuf, sizeof want);
}

static void test_caps_with_a_pin_returns_that_pins_page(void)
{
    const uint8_t pin = 0u;
    uint8_t       want[TFW_PIN_PAGE_LEN];

    TEST_ASSERT_EQUAL(TFW_OK, tfw_board_pin_page(pin, want));
    TEST_ASSERT_EQUAL_UINT8(pin, want[0]);

    args(&pin, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sys_cmd_caps(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(TFW_PIN_PAGE_LEN, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, outbuf, TFW_PIN_PAGE_LEN);
}

static void test_caps_refuses_a_pin_past_the_board_and_writes_nothing(void)
{
    const uint8_t pin = TFW_PIN_COUNT;

    args(&pin, 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_PIN, tfw_sys_cmd_caps(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
}

static void test_ping_echoes_its_payload(void)
{
    const uint8_t msg[] = { 0x00u, 0x7Fu, 0x80u, 0xFFu, 0x55u };

    args(msg, (uint8_t)sizeof msg);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sys_cmd_ping(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(sizeof msg, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, outbuf, sizeof msg);
}

static void test_ping_with_no_payload_replies_empty(void)
{
    args(NULL, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sys_cmd_ping(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
}

static void test_reset_refuses_a_mode_above_one_and_latches_nothing(void)
{
    const uint8_t mode = 2u;

    args(&mode, 1u);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, tfw_sys_cmd_reset(&r, &w));
    TEST_ASSERT_FALSE(tfw_sys_reset_pending());
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
}

static void test_reset_mode_0_latches_and_replies_empty(void)
{
    const uint8_t mode = 0u;

    args(&mode, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sys_cmd_reset(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    TEST_ASSERT_TRUE(tfw_sys_reset_pending());
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
}

/* sys.c:179: mode 1 asks tfw_auth_gate(), and this link's gate is
 * auth_session_none.c, which lets every caller through. So mode 1 is accepted
 * with no auth session open -- the soft/CRYPTO=none gap sys.c:174 documents. */
static void test_reset_mode_1_needs_no_auth_session_in_the_permissive_link(void)
{
    const uint8_t mode = 1u;

    args(&mode, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sys_cmd_reset(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_writer_len(&w));
    TEST_ASSERT_TRUE(tfw_sys_reset_pending());
}

static void test_poll_does_nothing_without_a_pending_reset(void)
{
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
    TEST_ASSERT_FALSE(handoff_touched());
}

static void test_poll_after_mode_0_resets_without_a_boot_request(void)
{
    const uint8_t mode = 0u;

    args(&mode, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sys_cmd_reset(&r, &w));
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
    TEST_ASSERT_FALSE(handoff_touched());
}

static void test_poll_after_mode_1_arms_the_boot_request_then_resets(void)
{
    const uint8_t mode = 1u;

    args(&mode, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sys_cmd_reset(&r, &w));
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
    TEST_ASSERT_TRUE(handoff_touched());
}

/* sys.c:55: reset_soon forces mode 0, so it supersedes an earlier RESET(1). */
static void test_reset_soon_is_mode_0_even_after_a_mode_1_request(void)
{
    const uint8_t mode = 1u;

    args(&mode, 1u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sys_cmd_reset(&r, &w));
    tfw_sys_reset_soon();
    TEST_ASSERT_TRUE(tfw_sys_reset_pending());
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_reset_count());
    TEST_ASSERT_FALSE(handoff_touched());
}

static void test_reset_state_clears_the_latch(void)
{
    tfw_sys_reset_soon();
    tfw_sys_reset_state();
    TEST_ASSERT_FALSE(tfw_sys_reset_pending());
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_reset_count());
}

static void test_sys_info_saturates_the_u16_and_appends_framer_counters_and_u32_ram(void)
{
    const uint8_t want[15] = {
        0x78u, 0x56u, 0x34u, 0x12u,               /* millis */
        0xFFu, 0xFFu,                             /* free RAM, saturated */
        (uint8_t)TFW_RESET_WATCHDOG,              /* reset cause */
        0x34u, 0x12u, 0xCDu, 0xABu,               /* err_crc, err_format */
        0x45u, 0x23u, 0x01u, 0x00u,               /* free RAM, exact */
    };

    tfw_mock_set_millis(0x12345678u);
    tfw_mock_set_free_ram(0x00012345u);
    tfw_mock_set_reset_cause(TFW_RESET_WATCHDOG);
    tfw_sys_set_framer_errors(0x1234u, 0xABCDu);

    args(NULL, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sys_cmd_sys_info(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(sizeof want, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, outbuf, sizeof want);
}

static void test_sys_info_after_reset_state_reports_zero_framer_counters(void)
{
    const uint8_t want[15] = {
        0x01u, 0x00u, 0x00u, 0x00u,
        0x21u, 0x03u,
        (uint8_t)TFW_RESET_SOFTWARE,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x21u, 0x03u, 0x00u, 0x00u,
    };

    tfw_sys_set_framer_errors(0xFFFFu, 0xFFFFu);
    tfw_sys_reset_state();
    tfw_mock_set_millis(1u);
    tfw_mock_set_free_ram(0x0321u);
    tfw_mock_set_reset_cause(TFW_RESET_SOFTWARE);

    args(NULL, 0u);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_sys_cmd_sys_info(&r, &w));
    TEST_ASSERT_EQUAL_UINT8(sizeof want, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, outbuf, sizeof want);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_caps_without_an_argument_returns_the_board_header);
    RUN_TEST(test_caps_with_a_pin_returns_that_pins_page);
    RUN_TEST(test_caps_refuses_a_pin_past_the_board_and_writes_nothing);
    RUN_TEST(test_ping_echoes_its_payload);
    RUN_TEST(test_ping_with_no_payload_replies_empty);
    RUN_TEST(test_reset_refuses_a_mode_above_one_and_latches_nothing);
    RUN_TEST(test_reset_mode_0_latches_and_replies_empty);
    RUN_TEST(test_reset_mode_1_needs_no_auth_session_in_the_permissive_link);
    RUN_TEST(test_poll_does_nothing_without_a_pending_reset);
    RUN_TEST(test_poll_after_mode_0_resets_without_a_boot_request);
    RUN_TEST(test_poll_after_mode_1_arms_the_boot_request_then_resets);
    RUN_TEST(test_reset_soon_is_mode_0_even_after_a_mode_1_request);
    RUN_TEST(test_reset_state_clears_the_latch);
    RUN_TEST(test_sys_info_saturates_the_u16_and_appends_framer_counters_and_u32_ram);
    RUN_TEST(test_sys_info_after_reset_state_reports_zero_framer_counters);
    return UNITY_END();
}
