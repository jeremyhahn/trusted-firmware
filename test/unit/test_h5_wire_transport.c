/*
 * The STM32H5 tfw-wire TCP link (hal_eth_wire.c) under the real transport and
 * framer (#553): a connection that is replaced mid-frame must not leave its
 * partial frame, or a reply it was owed, to the connection that replaces it.
 * The listener seam is faked here; the UART is hal_mock's.
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "transport.h"
#include "dispatch.h"
#include "sys.h"
#include "hal_mock.h"
#include "tfw/hal.h"
#include "tfw/link.h"
#include "tfw/protocol.h"
#include "hal_eth_wire.h"

static tfw_transport_t t;
static tfw_framer_t    rxf;
static uint8_t         sent[TFW_MAX_FRAME * 4u];
static unsigned        sent_len;

void tfw_h5_wire_dial_poll(void) { }

uint16_t tfw_h5_wire_send(const uint8_t *buf, uint16_t len)
{
    if (sent_len + len > sizeof sent) {
        len = (uint16_t)(sizeof sent - sent_len);
    }
    memcpy(sent + sent_len, buf, len);
    sent_len += len;
    return len;
}

void tfw_h5_wire_recved(uint16_t n) { (void)n; }

void tfw_h5_wire_finish(void) { tfw_h5_wire_drop(); }
void tfw_h5_wire_abort(void) { tfw_h5_wire_drop(); }

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_h5_wire_drop();
    (void)tfw_link_take_reset();
    tfw_transport_init(&t);
    tfw_framer_init(&rxf);
    sent_len = 0u;
}

void tearDown(void) { }

/* 1 if what went back on TCP holds a reply under seq with status st. */
static uint8_t answered_with(uint8_t seq, uint8_t st)
{
    for (unsigned i = 0u; i < sent_len; i++) {
        if (tfw_framer_push(&rxf, sent[i]) == TFW_FRAME_READY &&
            tfw_frame_seq(&rxf) == seq && tfw_frame_status(&rxf) == st) {
            return 1u;
        }
    }
    return 0u;
}

/* 1 if what went back on TCP holds an OK reply under seq. */
static uint8_t answered(uint8_t seq)
{
    return answered_with(seq, TFW_OK);
}

/* Device control stays on serial: a reset asked for over TCP gets the answer
 * an absent command gets, and the part is not reset. */
static void test_a_reset_over_tcp_is_not_admitted(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, 9u, TFW_CMD_RESET, 0, 0u);

    tfw_h5_wire_open();
    TEST_ASSERT_EQUAL_UINT16(len, tfw_h5_wire_push(wire, len));
    for (unsigned i = 0u; i < 4u; i++) { tfw_transport_poll(&t); }

    TEST_ASSERT_TRUE(answered_with(9u, TFW_ERR_UNKNOWN_CMD));
    TEST_ASSERT_EQUAL_UINT(0, tfw_mock_reset_count());
}

/* The other edge of the same boundary: a RESET refused over TCP leaves the
 * UART carrying every command, so the same RESET arriving on serial is
 * admitted, answered TFW_OK on the UART and does reset the part. */
static uint8_t uart_answered(uint8_t seq);

static void test_a_reset_refused_over_tcp_is_carried_over_the_uart(void)
{
    static const uint8_t mode = 0u;
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, 9u, TFW_CMD_RESET, 0, 0u);

    tfw_h5_wire_open();
    TEST_ASSERT_EQUAL_UINT16(len, tfw_h5_wire_push(wire, len));
    for (unsigned i = 0u; i < 4u; i++) { tfw_transport_poll(&t); }
    TEST_ASSERT_TRUE(answered_with(9u, TFW_ERR_UNKNOWN_CMD));
    TEST_ASSERT_EQUAL_UINT(0, tfw_mock_reset_count());

    len = tfw_frame_encode_request(wire, sizeof wire, 11u, TFW_CMD_RESET, &mode, 1u);
    TEST_ASSERT_NOT_EQUAL(0u, len);
    tfw_mock_feed_rx(wire, len);
    for (unsigned k = 0u; k < 64u; k++) { tfw_transport_poll(&t); }

    TEST_ASSERT_TRUE(uart_answered(11u));
    TEST_ASSERT_TRUE(tfw_sys_reset_pending());
    tfw_sys_poll();
    TEST_ASSERT_EQUAL_UINT(1, tfw_mock_reset_count());
}

static void test_a_replacement_connection_is_not_read_as_the_old_frames_tail(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, 1u, TFW_CMD_CAPS, 0, 0u);

    TEST_ASSERT_NOT_EQUAL(0u, len);
    tfw_h5_wire_open();                                 /* connection A */
    TEST_ASSERT_EQUAL_UINT16(3u, tfw_h5_wire_push(wire, 3u));
    tfw_transport_poll(&t);
    TEST_ASSERT_TRUE(t.in_frame);

    tfw_h5_wire_drop();                                 /* B replaces A */
    tfw_h5_wire_open();
    len = tfw_frame_encode_request(wire, sizeof wire, 2u, TFW_CMD_CAPS, 0, 0u);
    TEST_ASSERT_EQUAL_UINT16(len, tfw_h5_wire_push(wire, len));
    for (unsigned i = 0u; i < 8u; i++) { tfw_transport_poll(&t); }

    TEST_ASSERT_TRUE(answered(2u));
}

static void test_a_reply_owed_to_the_old_connection_is_not_sent_to_the_new(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, 5u, TFW_CMD_CAPS, 0, 0u);

    tfw_h5_wire_open();
    (void)tfw_h5_wire_push(wire, len);
    tfw_transport_poll(&t);                             /* answered at once */
    TEST_ASSERT_TRUE(answered(5u));
    tfw_h5_wire_drop();
    tfw_h5_wire_open();
    t.out_len = (uint16_t)sent_len;                     /* as if still owed */
    t.out_pos = 0u;
    sent_len = 0u;
    tfw_framer_init(&rxf);
    len = tfw_frame_encode_request(wire, sizeof wire, 6u, TFW_CMD_CAPS, 0, 0u);
    (void)tfw_h5_wire_push(wire, len);
    for (unsigned i = 0u; i < 8u; i++) { tfw_transport_poll(&t); }

    TEST_ASSERT_TRUE(answered(6u));
    TEST_ASSERT_FALSE(answered(5u));
}

static uint8_t uart_answered(uint8_t seq)
{
    uint8_t     out[512];
    size_t      n = tfw_mock_drain_tx(out, sizeof out);
    tfw_framer_t f;

    tfw_framer_init(&f);
    for (size_t i = 0u; i < n; i++) {
        if (tfw_framer_push(&f, out[i]) == TFW_FRAME_READY && tfw_frame_seq(&f) == seq &&
            tfw_frame_status(&f) == TFW_OK) {
            return 1u;
        }
    }
    return 0u;
}

static void feed_uart_request(uint8_t seq)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, seq, TFW_CMD_CAPS, 0, 0u);

    TEST_ASSERT_NOT_EQUAL(0u, len);
    tfw_mock_feed_rx(wire, len);
}

/* Codex P1 on #584: a TCP peer sending a non-SOF byte every 100 ms must not
 * hold the UART off. The framer discards each byte, so no frame starts and
 * the hold never begins. */
static void test_tcp_noise_does_not_hold_off_a_uart_request(void)
{
    static const uint8_t noise = 0x00u;
    uint8_t answered_uart = 0u;

    tfw_h5_wire_open();
    TEST_ASSERT_EQUAL_UINT16(1u, tfw_h5_wire_push(&noise, 1u));
    tfw_transport_poll(&t);
    TEST_ASSERT_FALSE(t.in_frame);
    feed_uart_request(7u);

    /* Within the 250 ms idle bound, noise arriving all along. */
    for (unsigned i = 0u; i < 2u && !answered_uart; i++) {
        tfw_mock_advance(100u);
        TEST_ASSERT_EQUAL_UINT16(1u, tfw_h5_wire_push(&noise, 1u));
        for (unsigned k = 0u; k < 64u; k++) { tfw_transport_poll(&t); }
        answered_uart = uart_answered(7u);
    }
    TEST_ASSERT_TRUE(answered_uart);
}

/* A TCP frame really in progress still holds the line: the UART request
 * waits for it to complete, then is answered. */
static void test_a_tcp_frame_in_progress_holds_until_it_completes(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, 2u, TFW_CMD_CAPS, 0, 0u);

    TEST_ASSERT_NOT_EQUAL(0u, len);
    tfw_h5_wire_open();
    TEST_ASSERT_EQUAL_UINT16(3u, tfw_h5_wire_push(wire, 3u));
    tfw_transport_poll(&t);
    TEST_ASSERT_TRUE(t.in_frame);
    feed_uart_request(7u);

    tfw_mock_advance(200u);
    for (unsigned k = 0u; k < 16u; k++) { tfw_transport_poll(&t); }
    TEST_ASSERT_FALSE(uart_answered(7u));
    TEST_ASSERT_TRUE(t.in_frame);

    TEST_ASSERT_EQUAL_UINT16((uint16_t)(len - 3u), tfw_h5_wire_push(wire + 3u, (uint16_t)(len - 3u)));
    for (unsigned k = 0u; k < 64u; k++) { tfw_transport_poll(&t); }
    TEST_ASSERT_TRUE(answered(2u));
    TEST_ASSERT_TRUE(uart_answered(7u));
}

/* ... or until the 250 ms idle timeout abandons it. */
static void test_a_stalled_tcp_frame_holds_until_the_idle_timeout(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, 2u, TFW_CMD_CAPS, 0, 0u);

    TEST_ASSERT_NOT_EQUAL(0u, len);
    tfw_h5_wire_open();
    TEST_ASSERT_EQUAL_UINT16(3u, tfw_h5_wire_push(wire, 3u));
    tfw_transport_poll(&t);
    feed_uart_request(7u);

    tfw_mock_advance(TFW_FRAME_IDLE_MS);
    for (unsigned k = 0u; k < 16u; k++) { tfw_transport_poll(&t); }
    TEST_ASSERT_FALSE(uart_answered(7u));

    tfw_mock_advance(1u);
    for (unsigned k = 0u; k < 64u; k++) { tfw_transport_poll(&t); }
    TEST_ASSERT_TRUE(uart_answered(7u));
    TEST_ASSERT_FALSE(answered(2u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_replacement_connection_is_not_read_as_the_old_frames_tail);
    RUN_TEST(test_a_reply_owed_to_the_old_connection_is_not_sent_to_the_new);
    RUN_TEST(test_a_reset_over_tcp_is_not_admitted);
    RUN_TEST(test_a_reset_refused_over_tcp_is_carried_over_the_uart);
    RUN_TEST(test_tcp_noise_does_not_hold_off_a_uart_request);
    RUN_TEST(test_a_tcp_frame_in_progress_holds_until_it_completes);
    RUN_TEST(test_a_stalled_tcp_frame_holds_until_the_idle_timeout);
    return UNITY_END();
}
