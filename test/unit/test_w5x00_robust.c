/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The W5100 / W5500 receive path (src/net/w5x00.c) into the framer
 * (src/proto/frame.c), as the link drives them, fed what a hostile peer or a
 * failing chip can produce: frames split at every byte, a read pointer that
 * wraps the ring at every offset inside a frame, reads of one byte and of
 * none, a received size the ring cannot hold, a frame cut by a hang-up, single
 * bit flips and runs of noise.
 *
 * Held for every input: no read or write leaves its buffer (make test-asan); a
 * frame is handed up only when its last byte is in and its CRC is good, and
 * then exactly as it was sent; every byte the chip received is read once, in
 * order, and handed back (RX_RD); and the next whole frame after any of it is
 * handed up, once the link has done what it does on a hang-up or an idle line
 * (tfw_framer_abort(): the reconnect backoff is at least 1 s, past
 * TFW_FRAME_IDLE_MS).
 *
 * The state that must survive is the framer's: a refused or broken frame
 * leaves it hunting for the next SOF, so the following frame is read whole.
 *
 * The generator is a fixed-seed xorshift, so a failure names its case and
 * iteration, and every run is that run.
 */
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "tfw/eth.h"
#include "tfw/protocol.h"
#include "frame.h"
#include "w5x00_model.h"

#define CHIP     ((uint8_t)W5X_TEST_CHIP)
#define SEED     0x2545F491u
#define RING     2048u
#define S_SR     0x03u
#define S_RX_RSR 0x26u
#define S_RX_RD  0x28u

static const tfw_netcfg_t cfg = {
    { 0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u },
    { 192u, 0u, 2u, 10u },
    { 255u, 255u, 255u, 0u },
    { 192u, 0u, 2u, 1u },
    { 192u, 0u, 2u, 20u },
    7020u,
    0u
};

static tfw_framer_t fr;
static uint32_t     rng;
static uint8_t      sent[TFW_MAX_FRAME];      /* the frame on the wire */
static uint16_t     sent_len;
static uint8_t      sent_seq, sent_op, sent_plen;
static uint8_t      sent_payload[TFW_MAX_PAYLOAD];
static uint16_t     rd0;                      /* RX_RD after the connect */
static uint32_t     delivered;                /* bytes the chip has received */
static char         where[80];

static uint32_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static uint32_t below(uint32_t n) { return next() % n; }

static void at(const char *what, uint32_t i)
{
    (void)snprintf(where, sizeof where, "seed 0x%08X, %s, iteration %lu",
                   SEED, what, (unsigned long)i);
}

/* A request with a random sequence, a random command id below the event and
 * response ranges, and a random payload of up to plen_max bytes. */
static void make_frame_of(uint8_t plen)
{
    uint8_t i;

    sent_seq  = (uint8_t)next();
    sent_op   = (uint8_t)below(0x7Fu);
    sent_plen = plen;
    for (i = 0u; i < sent_plen; i++) {
        sent_payload[i] = (uint8_t)next();
    }
    sent_len = tfw_frame_encode_request(sent, sizeof sent, sent_seq, sent_op,
                                        sent_payload, sent_plen);
    TEST_ASSERT_TRUE(sent_len >= TFW_FRAME_OVERHEAD + TFW_LEN_MIN);
}

static void make_frame(uint8_t plen_max)
{
    make_frame_of((uint8_t)below((uint32_t)plen_max + 1u));
}

static void connect(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(TFW_OK, tfw_eth_start(&cfg, 50000u), where);
    w5x_model_peer_accept();
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(TFW_ETH_UP, tfw_eth_state(), where);
    rd0       = w5x_model_sock16(S_RX_RD);
    delivered = 0u;
}

static void fresh(uint16_t pointers)
{
    w5x_model_reset(CHIP);
    w5x_model_seed_pointers(pointers);
    connect();
}

static void deliver(const uint8_t *b, uint16_t n)
{
    w5x_model_peer_send(b, n);
    delivered += n;
}

/* Reads until the chip is empty, pushing every byte into the framer. Counts
 * what the framer said; a READY must be the frame that was sent. Ends with a
 * read that finds nothing, and with every byte handed back to the chip. */
static uint16_t ready, bad;

static void drain(void)
{
    uint8_t  b;
    uint16_t guard = 0u;

    while (tfw_eth_rx(&b)) {
        tfw_frame_event_t ev = tfw_framer_push(&fr, b);

        TEST_ASSERT_TRUE_MESSAGE(++guard <= RING, where);
        if (ev == TFW_FRAME_READY) {
            ready++;
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(sent_seq, tfw_frame_seq(&fr), where);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(sent_op, tfw_frame_op(&fr), where);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(sent_plen, tfw_frame_payload_len(&fr), where);
            if (sent_plen) {
                TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(sent_payload, tfw_frame_payload(&fr),
                                                      sent_plen, where);
            }
        } else if (ev != TFW_FRAME_NONE) {
            bad++;
        }
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_eth_rx(&b), where);       /* a read of none */
    TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)(rd0 + delivered),
                                     w5x_model_sock16(S_RX_RD), where);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, w5x_model_sock16(S_RX_RSR), where);
}

/* The next whole frame is handed up, once and whole. */
static void resyncs(void)
{
    make_frame(TFW_MAX_PAYLOAD);
    ready = 0u;
    deliver(sent, sent_len);
    drain();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, ready, where);
}

void setUp(void)
{
    rng   = SEED;
    ready = 0u;
    bad   = 0u;
    tfw_framer_init(&fr);
    w5x_model_reset(CHIP);
}

void tearDown(void) {}

/* ----------------------------------------------------------------- tests */

/* Every cut point, two pieces: nothing until the last byte, then the frame. */
static void test_a_frame_split_at_every_byte_is_handed_up_once_when_whole(void)
{
    uint16_t k;

    fresh(0u);
    make_frame_of(TFW_MAX_PAYLOAD);
    for (k = 1u; k < sent_len; k++) {
        at("split in two", k);
        ready = 0u;
        deliver(sent, k);
        drain();
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, ready, where);
        deliver(sent + k, (uint16_t)(sent_len - k));
        drain();
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, ready, where);
    }
    TEST_ASSERT_EQUAL_UINT16(0u, bad);
}

/* Random frames in random pieces of zero bytes up, frames back to back. */
static void test_frames_in_random_pieces_are_each_handed_up_once(void)
{
    uint32_t i;
    uint16_t k, n;

    fresh((uint16_t)next());
    for (i = 0u; i < 400u; i++) {
        at("random pieces", i);
        make_frame(TFW_MAX_PAYLOAD);
        ready = 0u;
        for (k = 0u; k < sent_len; k = (uint16_t)(k + n)) {
            n = (uint16_t)below(9u);
            if (n > sent_len - k) {
                n = (uint16_t)(sent_len - k);
            }
            deliver(sent + k, n);
            drain();
            TEST_ASSERT_EQUAL_UINT16_MESSAGE((k + n == sent_len) ? 1u : 0u, ready, where);
        }
    }
    TEST_ASSERT_EQUAL_UINT16(0u, bad);
}

/* One byte at a time, each read alone and followed by a read of none. */
static void test_a_frame_read_a_byte_at_a_time_is_handed_up_on_its_last(void)
{
    uint16_t k;

    fresh(0x0400u);
    make_frame_of(TFW_MAX_PAYLOAD);
    at("a byte at a time", 0u);
    for (k = 0u; k < sent_len; k++) {
        deliver(sent + k, 1u);
        drain();
        TEST_ASSERT_EQUAL_UINT16_MESSAGE((k + 1u == sent_len) ? 1u : 0u, ready, where);
    }
}

/* The ring's end and the 16-bit pointer's wrap, each at every offset inside
 * a frame of the largest size, and a frame starting at every ring offset. */
static void wrap(uint16_t p, uint32_t i)
{
    at("pointer seeded", i);
    tfw_framer_init(&fr);
    fresh(p);
    rng = SEED ^ p;
    make_frame_of(TFW_MAX_PAYLOAD);
    ready = 0u;
    deliver(sent, sent_len);
    drain();
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, ready, where);
}

static void test_a_read_pointer_wrapping_the_ring_at_every_offset_loses_nothing(void)
{
    uint16_t k;

    for (k = 0u; k <= TFW_MAX_FRAME; k++) {
        wrap((uint16_t)(RING - k), k);                      /* the ring's end */
        wrap((uint16_t)(0x10000u - k), k);                  /* the pointer's wrap */
    }
    for (k = 0u; k < RING; k++) {                           /* and every start */
        wrap(k, k);
    }
}

/* A received size above the ring: nothing is handed up, the socket closes,
 * the link reads down; after the reconnect the next frame is read whole. */
static void test_a_received_size_the_ring_cannot_hold_drops_the_link_and_it_resyncs(void)
{
    static const uint16_t fixed[6] = { 0x0801u, 0x0802u, 0x0FFFu, 0x1000u, 0x8000u, 0xFFFFu };
    uint32_t i;
    uint16_t v, k;
    uint8_t  b;

    for (i = 0u; i < 6u + 40u; i++) {
        v = (i < 6u) ? fixed[i] : (uint16_t)(RING + 1u + below(0x10000u - RING - 1u));
        at("received size above the ring", i);
        tfw_framer_init(&fr);
        fresh((uint16_t)next());
        make_frame(TFW_MAX_PAYLOAD);
        ready = 0u;
        k = (uint16_t)below(sent_len);
        deliver(sent, k);                                   /* part of a frame, read */
        drain();
        deliver(sent + k, (uint16_t)(sent_len - k));
        w5x_model_rsr_reads(v);
        b = 0xEEu;
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_eth_rx(&b), where);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xEEu, b, where);    /* nothing handed up */
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00u, w5x_model_sock(S_SR), where);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_eth_rx(&b), where);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xEEu, b, where);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(TFW_ETH_DOWN, tfw_eth_state(), where);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, ready, where);
        w5x_model_rsr_reads(0u);
        tfw_framer_abort(&fr);
        connect();
        resyncs();
    }
}

/* A hang-up after every byte of a frame, with the rest of it already in the
 * chip unread: none of the old connection's bytes come out of the new one,
 * and its first frame is read whole. */
static void test_a_frame_cut_by_a_hang_up_at_every_byte_leaves_nothing_behind(void)
{
    uint16_t k;
    uint8_t  b;

    fresh(0x07C0u);
    for (k = 0u; k < TFW_MAX_FRAME; k++) {
        uint16_t i;

        at("hang-up after byte", k);
        make_frame_of(TFW_MAX_PAYLOAD);
        ready = 0u;
        deliver(sent, sent_len);
        for (i = 0u; i < k && i + 1u < sent_len; i++) {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(1u, tfw_eth_rx(&b), where);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(TFW_FRAME_NONE, tfw_framer_push(&fr, b), where);
        }
        w5x_model_peer_close();
        /* Down: the link reads no more (link_eth.c reads only while up). */
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(TFW_ETH_DOWN, tfw_eth_state(), where);
        tfw_framer_abort(&fr);
        connect();
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_eth_rx(&b), where);
        resyncs();
    }
}

/* Every bit of a frame flipped in turn: never handed up; the framer, aborted
 * as the idle guard would, reads the next frame whole. A flip that leaves the
 * length alone needs no abort: the frame ends where it said and fails its CRC. */
static void test_a_frame_with_any_one_bit_flipped_is_never_handed_up(void)
{
    uint8_t  bad_frame[TFW_MAX_FRAME];
    uint16_t bit;

    fresh(0x0123u);
    make_frame_of(TFW_MAX_PAYLOAD);
    for (bit = 0u; bit < (uint16_t)(sent_len * 8u); bit++) {
        uint16_t n = sent_len;

        at("one bit flipped", bit);
        memcpy(bad_frame, sent, n);
        bad_frame[bit >> 3] ^= (uint8_t)(1u << (bit & 7u));
        ready = 0u;
        deliver(bad_frame, n);
        drain();
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, ready, where);
        if ((bit >> 3) > TFW_HDR_LEN) {
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(TFW_FRAME_WAIT_SOF, fr.state, where);
        }
        tfw_framer_abort(&fr);
        deliver(sent, n);
        drain();
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, ready, where);
    }
}

/* Noise of every length up to a ring: never a frame of its own, and after it
 * (and the idle guard's abort) the next frame is read whole. Noise with no SOF
 * in it needs no abort at all. */
static void test_noise_is_never_a_frame_and_the_next_frame_is_read_whole(void)
{
    static uint8_t noise[RING];
    uint32_t i;
    uint16_t n, k;

    fresh((uint16_t)next());
    for (i = 0u; i < 300u; i++) {
        uint8_t no_sof = (uint8_t)(i & 1u);

        at(no_sof ? "noise, no SOF" : "noise", i);
        make_frame(TFW_MAX_PAYLOAD);
        n = (uint16_t)(1u + below((i < 290u) ? 200u : RING));
        for (k = 0u; k < n; k++) {
            noise[k] = (uint8_t)next();
            if (no_sof && noise[k] == TFW_SOF) {
                noise[k] = 0u;
            }
        }
        ready = 0u;
        deliver(noise, n);
        drain();
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0u, ready, where);
        if (!no_sof) {
            tfw_framer_abort(&fr);
        }
        deliver(sent, sent_len);
        drain();
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(1u, ready, where);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_frame_split_at_every_byte_is_handed_up_once_when_whole);
    RUN_TEST(test_frames_in_random_pieces_are_each_handed_up_once);
    RUN_TEST(test_a_frame_read_a_byte_at_a_time_is_handed_up_on_its_last);
    RUN_TEST(test_a_read_pointer_wrapping_the_ring_at_every_offset_loses_nothing);
    RUN_TEST(test_a_received_size_the_ring_cannot_hold_drops_the_link_and_it_resyncs);
    RUN_TEST(test_a_frame_cut_by_a_hang_up_at_every_byte_leaves_nothing_behind);
    RUN_TEST(test_a_frame_with_any_one_bit_flipped_is_never_handed_up);
    RUN_TEST(test_noise_is_never_a_frame_and_the_next_frame_is_read_whole);
    return UNITY_END();
}
