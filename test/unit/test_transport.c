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
#include "crc16.h"
#include "dispatch.h"
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
    TEST_ASSERT_EQUAL_HEX8(7u, tfw_frame_seq(&rxf));
    TEST_ASSERT_EQUAL_HEX8(TFW_CMD_SYS_INFO | TFW_OP_RESPONSE_BIT, tfw_frame_op(&rxf));
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
    TEST_ASSERT_EQUAL_HEX8(4u, tfw_frame_seq(&rxf));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_frame_status(&rxf));
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
    tfw_transport_poll(&t);
    TEST_ASSERT_NOT_EQUAL(0u, tfw_mock_tx_pending());

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

/* Issue #163: the mock's receive buffer gave back no byte it had handed out,
 * so one link went deaf after 4 KiB from the host in total, however promptly
 * each frame was read. tfwsim is this mock behind a pipe, so a long session
 * hung on its 63rd 60-byte PING. 16 KiB here, well past the old limit. */
static void test_a_long_session_is_answered_past_four_kib_from_the_host(void)
{
    uint8_t  msg[60];
    unsigned i;

    for (i = 0; i < sizeof msg; i++) { msg[i] = (uint8_t)(i * 5u + 1u); }
    for (i = 0; i < 256u; i++) {
        msg[0] = (uint8_t)i;
        TEST_ASSERT_TRUE_MESSAGE(exchange((uint8_t)i, TFW_CMD_PING, msg, sizeof msg),
                                 "a PING went unanswered");
        TEST_ASSERT_EQUAL_HEX8((uint8_t)i, tfw_frame_seq(&rxf));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, tfw_frame_payload(&rxf), sizeof msg);
    }
}

/* One reply as the host decodes it, an event with TFW_OK skipped (api.md
 * "Events"); the 0x7F refusal shares the event OP but not its status. */
typedef struct {
    uint8_t seq, op, status, len;
    uint8_t pay[8];
} reply_t;

static size_t feed_and_collect(const uint8_t *in, size_t n, reply_t *out, size_t max)
{
    uint8_t back[TFW_MAX_FRAME * 2u];
    size_t  got, i, j, k = 0u;

    tfw_mock_feed_rx(in, n);
    for (i = 0; i < 512u; i++) {
        tfw_transport_poll(&t);
        got = tfw_mock_drain_tx(back, sizeof back);
        for (j = 0; j < got; j++) {
            if (tfw_framer_push(&rxf, back[j]) != TFW_FRAME_READY) { continue; }
            if (tfw_frame_op(&rxf) == TFW_OP_EVENT && tfw_frame_status(&rxf) == TFW_OK) { continue; }
            TEST_ASSERT_TRUE_MESSAGE(k < max, "more replies than requests");
            out[k].seq    = tfw_frame_seq(&rxf);
            out[k].op     = tfw_frame_op(&rxf);
            out[k].status = (uint8_t)tfw_frame_status(&rxf);
            out[k].len    = tfw_frame_payload_len(&rxf);
            memcpy(out[k].pay, tfw_frame_payload(&rxf),
                   out[k].len < sizeof out[k].pay ? out[k].len : sizeof out[k].pay);
            k++;
        }
    }
    return k;
}

static uint16_t ping(uint8_t *wire, uint8_t seq, const char *msg)
{
    uint16_t n = tfw_frame_encode_request(wire, TFW_MAX_FRAME, seq, TFW_CMD_PING,
                                          (const uint8_t *)msg, (uint8_t)strlen(msg));
    TEST_ASSERT_NOT_EQUAL(0u, n);
    return n;
}

static void assert_pong(const reply_t *r, uint8_t seq, const char *msg)
{
    TEST_ASSERT_EQUAL_HEX8(seq, r->seq);
    TEST_ASSERT_EQUAL_HEX8(TFW_CMD_PING | TFW_OP_RESPONSE_BIT, r->op);
    TEST_ASSERT_EQUAL(TFW_OK, r->status);
    TEST_ASSERT_EQUAL_UINT8(strlen(msg), r->len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(msg, r->pay, strlen(msg));
}

/* Every request id 0x00..0x7F with an empty payload is answered once, its SEQ
 * echoed and OP = id|0x80: TFW_ERR_UNKNOWN_CMD exactly for an id the table
 * does not have, and a refusal carries no payload. 0x7F is refused in a frame
 * whose OP is 0xFF. */
static void test_every_request_id_is_answered_under_its_seq_and_op(void)
{
    unsigned id;
    reply_t  r[2];
    uint8_t  wire[TFW_MAX_FRAME];

    for (id = 0x00u; id <= 0x7Fu; id++) {
        uint16_t n = tfw_frame_encode_request(wire, sizeof wire, (uint8_t)(id + 1u), (uint8_t)id, 0, 0u);
        TEST_ASSERT_NOT_EQUAL(0u, n);
        TEST_ASSERT_EQUAL_UINT(1u, feed_and_collect(wire, n, r, 2u));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(id + 1u), r[0].seq);
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(id | TFW_OP_RESPONSE_BIT), r[0].op);
        if (tfw_dispatch_knows((uint8_t)id)) {
            TEST_ASSERT_NOT_EQUAL(TFW_ERR_UNKNOWN_CMD, r[0].status);
        } else {
            TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, r[0].status);
        }
        if (r[0].status != TFW_OK) {
            TEST_ASSERT_EQUAL_UINT8(0u, r[0].len);
        }
    }
    TEST_ASSERT_EQUAL_HEX8(TFW_OP_EVENT, r[0].op);
}

/* A corrupted frame is answered TFW_ERR_CRC under its own SEQ with no payload;
 * a LEN above TFW_MAX_LEN or below TFW_LEN_MIN is dropped unanswered, and the
 * byte that failed as LEN is tried again as a start byte; garbage between
 * frames is dropped. Each time the next good frame is the next one answered,
 * and the framer's counters, which SYS_INFO reports, say what happened. */
static void test_the_link_resynchronises_and_counts_what_it_refused(void)
{
    static const uint8_t junk[5] = { 0x00u, 0xFFu, 0x5Au, 0x81u, 0x13u };
    uint8_t  in[TFW_MAX_FRAME * 2u + 200u];
    reply_t  r[3];
    uint16_t n;
    unsigned i;

    n = ping(in, 0x31u, "abc");
    in[5] ^= 0x01u;                                  /* a payload byte */
    TEST_ASSERT_EQUAL_UINT(1u, feed_and_collect(in, n, r, 3u));
    TEST_ASSERT_EQUAL_HEX8(0x31u, r[0].seq);
    TEST_ASSERT_EQUAL_HEX8(TFW_CMD_PING | TFW_OP_RESPONSE_BIT, r[0].op);
    TEST_ASSERT_EQUAL(TFW_ERR_CRC, r[0].status);
    TEST_ASSERT_EQUAL_UINT8(0u, r[0].len);

    /* A lone start byte: the real SOF is read as an over-length LEN, refused,
     * and reprocessed as the frame's start. */
    in[0] = TFW_SOF;
    n = ping(&in[1], 0x32u, "over");
    TEST_ASSERT_EQUAL_UINT(1u, feed_and_collect(in, n + 1u, r, 3u));
    assert_pong(&r[0], 0x32u, "over");

    /* LEN 1, below the minimum, then the frame. */
    in[0] = TFW_SOF;
    in[1] = 0x01u;
    n = ping(&in[2], 0x33u, "under");
    TEST_ASSERT_EQUAL_UINT(1u, feed_and_collect(in, n + 2u, r, 3u));
    assert_pong(&r[0], 0x33u, "under");

    /* Garbage with no start byte in it, between two frames. */
    n = ping(in, 0x34u, "one");
    for (i = 0; i < 200u; i++) { in[n + i] = junk[i % sizeof junk]; }
    n = (uint16_t)(n + 200u);
    n = (uint16_t)(n + ping(&in[n], 0x35u, "two"));
    TEST_ASSERT_EQUAL_UINT(2u, feed_and_collect(in, n, r, 3u));
    assert_pong(&r[0], 0x34u, "one");
    assert_pong(&r[1], 0x35u, "two");

    TEST_ASSERT_EQUAL_UINT16(1u, t.framer.err_crc);
    TEST_ASSERT_EQUAL_UINT16(2u, t.framer.err_format);
}

/* The largest frame either side may send has LEN = TFW_MAX_LEN; one built by
 * hand with a payload byte more is dropped as over-length, not answered, and
 * not counted as a CRC error; the frame after it is answered. */
static void test_an_over_length_frame_is_dropped_and_the_next_answered(void)
{
    uint8_t  in[TFW_MAX_FRAME * 2u + 8u];
    reply_t  r[2];
    uint16_t crc, n = 0u;
    unsigned i;

    in[n++] = TFW_SOF;
    in[n++] = (uint8_t)(TFW_MAX_LEN + 1u);
    in[n++] = 0x42u;
    in[n++] = TFW_CMD_PING;
    for (i = 0; i < TFW_MAX_PAYLOAD + 1u; i++) { in[n++] = 0x00u; }
    crc = tfw_crc16(&in[1], (uint16_t)(n - 1u));
    in[n++] = (uint8_t)(crc & 0xFFu);
    in[n++] = (uint8_t)(crc >> 8);
    n = (uint16_t)(n + ping(&in[n], 0x43u, "after"));

    TEST_ASSERT_EQUAL_UINT(1u, feed_and_collect(in, n, r, 2u));
    assert_pong(&r[0], 0x43u, "after");
    TEST_ASSERT_EQUAL_UINT16(1u, t.framer.err_format);
    TEST_ASSERT_EQUAL_UINT16(0u, t.framer.err_crc);
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
    RUN_TEST(test_a_long_session_is_answered_past_four_kib_from_the_host);
    RUN_TEST(test_every_request_id_is_answered_under_its_seq_and_op);
    RUN_TEST(test_the_link_resynchronises_and_counts_what_it_refused);
    RUN_TEST(test_an_over_length_frame_is_dropped_and_the_next_answered);
    return UNITY_END();
}
