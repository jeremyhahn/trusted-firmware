/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * End to end: bytes onto the emulated link, bytes back off it, through
 * framing, dispatch and the capability modules. Nothing is stubbed but the
 * hardware itself.
 */

#include <string.h>
#include "unity.h"
#include "transport.h"
#include "sys.h"
#include "hal_mock.h"
#include "tfw/hal.h"
#include "tfw/protocol.h"

static tfw_transport_t t;
static tfw_framer_t    rxf;      /* the host's side: decodes what came back */

void setUp(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    tfw_transport_init(&t);
    tfw_framer_init(&rxf);
}
void tearDown(void) {}

/* Sends one request and decodes the reply. Returns 1 if a whole frame came
 * back and landed in rxf. */
static uint8_t exchange(uint8_t seq, uint8_t cmd, const uint8_t *pay, uint8_t n)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint8_t  back[TFW_MAX_FRAME * 2u];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, seq, cmd, pay, n);
    size_t   got, i;

    TEST_ASSERT_NOT_EQUAL(0u, len);
    tfw_mock_feed_rx(wire, len);

    for (i = 0; i < 8u; i++) { tfw_transport_poll(&t); }   /* drain fully */

    got = tfw_mock_drain_tx(back, sizeof back);
    for (i = 0; i < got; i++) {
        if (tfw_framer_push(&rxf, back[i]) == TFW_FRAME_READY) {
            return 1u;
        }
    }
    return 0u;
}

static void test_ping_round_trips_over_the_link(void)
{
    const uint8_t msg[4] = { 'p', 'o', 'n', 'g' };

    TEST_ASSERT_TRUE(exchange(0x21u, TFW_CMD_PING, msg, 4u));
    TEST_ASSERT_EQUAL_HEX8(0x21u, tfw_frame_seq(&rxf));
    TEST_ASSERT_EQUAL_HEX8(TFW_CMD_PING | TFW_OP_RESPONSE_BIT, tfw_frame_op(&rxf));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_frame_status(&rxf));
    TEST_ASSERT_EQUAL_UINT8(4u, tfw_frame_payload_len(&rxf));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, tfw_frame_payload(&rxf), 4u);
}

static void test_the_sequence_number_is_echoed_so_a_host_can_correlate(void)
{
    TEST_ASSERT_TRUE(exchange(0xABu, TFW_CMD_CAPS, 0, 0u));
    TEST_ASSERT_EQUAL_HEX8(0xABu, tfw_frame_seq(&rxf));
}

static void test_an_unknown_command_is_answered_rather_than_ignored(void)
{
    TEST_ASSERT_TRUE(exchange(1u, 0x7Eu, 0, 0u));
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, tfw_frame_status(&rxf));
    /* A failure reply carries no payload: a partial result beside a failure
     * status is something the host would have to guess about. */
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_frame_payload_len(&rxf));
}

static void test_a_corrupted_frame_is_answered_with_a_crc_status(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint8_t  back[TFW_MAX_FRAME * 2u];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, 3u, TFW_CMD_PING, 0, 0u);
    size_t   got, i;
    uint8_t  saw = 0u;

    wire[len - 1u] ^= 0xFFu;                 /* wreck the CRC */
    tfw_mock_feed_rx(wire, len);
    for (i = 0; i < 8u; i++) { tfw_transport_poll(&t); }

    got = tfw_mock_drain_tx(back, sizeof back);
    for (i = 0; i < got; i++) {
        if (tfw_framer_push(&rxf, back[i]) == TFW_FRAME_READY) { saw = 1u; }
    }
    TEST_ASSERT_TRUE(saw);
    TEST_ASSERT_EQUAL(TFW_ERR_CRC, tfw_frame_status(&rxf));
}

/* A frame split across many reads must decode identically. On a real link this
 * is the normal case, not the exception -- a UART delivers bytes one at a time
 * and the poll loop sees whatever happened to arrive. */
static void test_a_frame_arriving_one_byte_at_a_time_decodes_the_same(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint8_t  back[TFW_MAX_FRAME * 2u];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, 7u, TFW_CMD_SYS_INFO, 0, 0u);
    size_t   got, i;
    uint8_t  saw = 0u;

    for (i = 0; i < len; i++) {
        tfw_mock_feed_rx(&wire[i], 1u);
        tfw_transport_poll(&t);
    }
    for (i = 0; i < 4u; i++) { tfw_transport_poll(&t); }

    got = tfw_mock_drain_tx(back, sizeof back);
    for (i = 0; i < got; i++) {
        if (tfw_framer_push(&rxf, back[i]) == TFW_FRAME_READY) { saw = 1u; }
    }
    TEST_ASSERT_TRUE(saw);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_frame_status(&rxf));
}

/* BACKPRESSURE. A full TX ring is not an error and must not lose bytes: the
 * emit cursor holds its place and the next poll continues from there. Without
 * this the device would either block -- stalling the scheduler while a long
 * reply drains -- or silently truncate its own response. */
static void test_a_full_transmit_ring_defers_rather_than_dropping_bytes(void)
{
    uint8_t pay[32];
    uint8_t wire[TFW_MAX_FRAME];
    uint8_t back[TFW_MAX_FRAME * 2u];
    size_t  total = 0u, got, i;
    uint8_t saw = 0u;

    memset(pay, 0x5Au, sizeof pay);
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, 2u, TFW_CMD_PING, pay, (uint8_t)sizeof pay);

    tfw_mock_set_tx_capacity(4u);             /* a very small ring */
    tfw_mock_feed_rx(wire, len);

    /* Poll repeatedly, draining a little each time, as a real link would. */
    for (i = 0; i < 64u; i++) {
        tfw_transport_poll(&t);
        got = tfw_mock_drain_tx(back + total, sizeof back - total);
        total += got;
    }
    for (i = 0; i < total; i++) {
        if (tfw_framer_push(&rxf, back[i]) == TFW_FRAME_READY) { saw = 1u; }
    }
    TEST_ASSERT_TRUE(saw);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)sizeof pay, tfw_frame_payload_len(&rxf));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pay, tfw_frame_payload(&rxf), sizeof pay);
}

/* A partial frame that stops arriving must be abandoned, or it absorbs the
 * opening bytes of the next good frame and one lost frame becomes two. */
static void test_a_stalled_partial_frame_is_abandoned_after_the_idle_window(void)
{
    uint8_t wire[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, 4u, TFW_CMD_PING, 0, 0u);

    tfw_mock_feed_rx(wire, 3u);               /* a fragment, then silence */
    tfw_transport_poll(&t);
    TEST_ASSERT_TRUE(t.in_frame);

    tfw_mock_advance(TFW_FRAME_IDLE_MS + 1u);
    tfw_transport_poll(&t);
    TEST_ASSERT_FALSE(t.in_frame);

    /* a whole frame now succeeds */
    tfw_mock_feed_rx(wire, len);
    for (unsigned i = 0; i < 8u; i++) { tfw_transport_poll(&t); }
    uint8_t back[TFW_MAX_FRAME * 2u];
    size_t got = tfw_mock_drain_tx(back, sizeof back);
    uint8_t saw = 0u;
    for (size_t i = 0; i < got; i++) {
        if (tfw_framer_push(&rxf, back[i]) == TFW_FRAME_READY) { saw = 1u; }
    }
    TEST_ASSERT_TRUE(saw);
}

/* A malformed frame and the good frame that follows it are TWO events, not
 * one poll's work. Without this, a host bug or a noisy line that drops one
 * SOF byte would deliver a BAD_FORMAT event and dispatch the next request in
 * the very same tfw_transport_poll() call -- and anything that mirrors the
 * framer's counters once per poll (SYS_INFO's bridge in main.c) would encode
 * that reply one event stale, reporting the count as it stood before this
 * BAD_FORMAT rather than after. The resync trick from
 * test_a_false_sof_does_not_swallow_the_frame_that_follows_it builds a
 * BAD_FORMAT byte immediately followed, in the SAME feed, by a complete good
 * frame: one poll() must see only the BAD_FORMAT event and produce no
 * reply yet; a second must then produce the good frame's reply. */
static void test_a_bad_format_event_and_the_good_frame_after_it_are_two_polls(void)
{
    uint8_t  good[TFW_MAX_FRAME];
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t n = tfw_frame_encode_request(good, sizeof good, 0x44u, TFW_CMD_PING, 0, 0u);
    uint16_t i;

    wire[0] = TFW_SOF;                 /* a false SOF, swallowed as TFW_FRAME_NONE */
    wire[1] = TFW_SOF;                 /* implausible LEN, itself reprocessed as SOF */
    for (i = 1; i < n; i++) { wire[i + 1u] = good[i]; }

    tfw_mock_feed_rx(wire, (size_t)(n + 1u));

    tfw_transport_poll(&t);
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_tx_pending());

    for (i = 0; i < 8u; i++) { tfw_transport_poll(&t); }

    uint8_t back[TFW_MAX_FRAME * 2u];
    size_t  got = tfw_mock_drain_tx(back, sizeof back);
    uint8_t saw = 0u;
    for (i = 0; i < got; i++) {
        if (tfw_framer_push(&rxf, back[i]) == TFW_FRAME_READY) { saw = 1u; }
    }
    TEST_ASSERT_TRUE(saw);
    TEST_ASSERT_EQUAL_HEX8(0x44u, tfw_frame_seq(&rxf));
}

/* A response arriving at a device is not a request. Answering it would make
 * two devices sharing a line argue with each other forever. */
static void test_a_response_frame_sent_to_the_device_is_ignored(void)
{
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_response(wire, sizeof wire, 1u, TFW_CMD_PING, TFW_OK, 0, 0u);

    tfw_mock_feed_rx(wire, len);
    for (unsigned i = 0; i < 8u; i++) { tfw_transport_poll(&t); }
    TEST_ASSERT_EQUAL_UINT(0u, tfw_mock_tx_pending());
}

static void test_an_event_is_emitted_when_nothing_is_in_flight(void)
{
    uint8_t back[TFW_MAX_FRAME];
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_transport_send_event(&t, TFW_EVENT_BOOT));

    size_t got = tfw_mock_drain_tx(back, sizeof back);
    uint8_t saw = 0u;
    for (size_t i = 0; i < got; i++) {
        if (tfw_framer_push(&rxf, back[i]) == TFW_FRAME_READY) { saw = 1u; }
    }
    TEST_ASSERT_TRUE(saw);
    TEST_ASSERT_EQUAL_HEX8(TFW_OP_EVENT, tfw_frame_op(&rxf));
    TEST_ASSERT_EQUAL_HEX8(TFW_EVENT_BOOT, tfw_frame_payload(&rxf)[0]);
}

/* Events only go out when nothing else is. That is what lets the host stay a
 * strict request/response state machine rather than a reassembler that must
 * cope with an event landing mid-reply. */
static void test_an_event_is_refused_while_a_response_is_pending(void)
{
    uint8_t pay[32];
    uint8_t wire[TFW_MAX_FRAME];
    memset(pay, 0x11u, sizeof pay);
    uint16_t len = tfw_frame_encode_request(wire, sizeof wire, 1u, TFW_CMD_PING, pay, (uint8_t)sizeof pay);

    tfw_mock_set_tx_capacity(2u);             /* guarantee the reply cannot finish */
    tfw_mock_feed_rx(wire, len);
    tfw_transport_poll(&t);

    TEST_ASSERT_EQUAL_UINT8(0u, tfw_transport_send_event(&t, TFW_EVENT_DEADMAN));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ping_round_trips_over_the_link);
    RUN_TEST(test_the_sequence_number_is_echoed_so_a_host_can_correlate);
    RUN_TEST(test_an_unknown_command_is_answered_rather_than_ignored);
    RUN_TEST(test_a_corrupted_frame_is_answered_with_a_crc_status);
    RUN_TEST(test_a_frame_arriving_one_byte_at_a_time_decodes_the_same);
    RUN_TEST(test_a_full_transmit_ring_defers_rather_than_dropping_bytes);
    RUN_TEST(test_a_stalled_partial_frame_is_abandoned_after_the_idle_window);
    RUN_TEST(test_a_bad_format_event_and_the_good_frame_after_it_are_two_polls);
    RUN_TEST(test_a_response_frame_sent_to_the_device_is_ignored);
    RUN_TEST(test_an_event_is_emitted_when_nothing_is_in_flight);
    RUN_TEST(test_an_event_is_refused_while_a_response_is_pending);
    return UNITY_END();
}
