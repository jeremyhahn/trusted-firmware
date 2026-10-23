/* The serial link under mutation (issue #163): src/proto/link_serial.c fed
 * mutated, truncated, split and interleaved byte streams through the mock UART
 * and read back the way src/proto/transport.c reads it -- one byte at a time
 * into a framer, with hold set while a frame is partly received.
 *
 * What is pinned: every byte the UART took comes out of tfw_link_rx exactly
 * once, in order, whatever hold says; a bad frame is dropped and the good frame
 * after it is delivered whole; tfw_link_tx_try is the UART's own backpressure
 * and tfw_link_admits refuses no command on the serial wire.
 *
 * A truncated or mutated frame can only be dropped once the line has been idle,
 * which is the transport's TFW_FRAME_IDLE_MS abort: the suite models that gap
 * with tfw_framer_abort() at the point the transport's clock would run it.
 *
 * Deterministic: frames and splits come from xorshift32 seeded with a literal,
 * and a failure prints the seed and the case. Every stream is a heap buffer of
 * exactly its length, so a read past the end is an ASan report.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "frame.h"
#include "hal_mock.h"
#include "tfw/link.h"
#include "tfw/protocol.h"

#define SEED       0x16351E71u
#define RANDOM_RUNS 2000u
#define MAX_FRAMES  6u

static uint32_t rng;
static tfw_framer_t rxf;

/* What the link handed out and what the framer made of it. */
static uint8_t  got[8192];
static size_t   got_len;
static uint8_t  ready[MAX_FRAMES * 2u][TFW_MAX_LEN];
static uint8_t  ready_len[MAX_FRAMES * 2u];
static unsigned ready_n;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

void setUp(void)
{
    tfw_mock_reset();
    tfw_framer_init(&rxf);
    got_len = 0u;
    ready_n = 0u;
}

void tearDown(void) {}

/* Drains the link as the transport does: hold is set while the framer is
 * mid-frame. Returns when the UART has nothing waiting. */
static void pump(void)
{
    uint8_t b;

    while (tfw_link_rx(&b, (uint8_t)(rxf.state != TFW_FRAME_WAIT_SOF))) {
        TEST_ASSERT_TRUE_MESSAGE(got_len < sizeof got, "the link handed out more bytes than were fed");
        got[got_len++] = b;
        if (tfw_framer_push(&rxf, b) == TFW_FRAME_READY) {
            TEST_ASSERT_TRUE(ready_n < MAX_FRAMES * 2u);
            memcpy(ready[ready_n], rxf.body, rxf.want);
            ready_len[ready_n] = rxf.want;
            ready_n++;
        }
    }
}

/* Feeds n bytes from a heap copy of exactly n, so the mock reads nothing past. */
static void feed(const uint8_t *b, size_t n)
{
    uint8_t *h;

    if (n == 0u) {
        return;
    }
    h = (uint8_t *)malloc(n);
    TEST_ASSERT_NOT_NULL(h);
    memcpy(h, b, n);
    tfw_mock_feed_rx(h, n);
    free(h);
}

/* A request frame with a random payload; cmd below the response bit. */
static uint16_t make_frame(uint8_t *out, uint8_t seq)
{
    uint8_t  pay[TFW_MAX_PAYLOAD];
    uint8_t  plen = (uint8_t)(rnd() % (TFW_MAX_PAYLOAD + 1u));
    uint8_t  i;
    uint16_t n;

    for (i = 0u; i < plen; i++) {
        pay[i] = (uint8_t)rnd();
    }
    n = tfw_frame_encode_request(out, TFW_MAX_FRAME, seq, (uint8_t)(rnd() & 0x7Fu), pay, plen);
    TEST_ASSERT_NOT_EQUAL(0u, n);
    return n;
}

static void expect_frame(unsigned k, const uint8_t *frame, uint16_t n, const char *what, unsigned a)
{
    if (k >= ready_n || ready_len[k] != frame[TFW_HDR_LEN] ||
        memcmp(ready[k], &frame[TFW_HDR_SEQ], frame[TFW_HDR_LEN]) != 0) {
        printf("seed 0x%08lx: %s %u: frame %u of %u not delivered whole\n", (unsigned long)SEED,
               what, a, k, ready_n);
        TEST_FAIL_MESSAGE("a good frame was not delivered whole");
    }
    (void)n;
}

static void expect_bytes(const uint8_t *stream, size_t n, const char *what, unsigned a)
{
    if (got_len != n || memcmp(got, stream, n) != 0) {
        printf("seed 0x%08lx: %s %u: fed %u bytes, link gave %u\n", (unsigned long)SEED, what, a,
               (unsigned)n, (unsigned)got_len);
        TEST_FAIL_MESSAGE("the link did not hand out every byte exactly once");
    }
}

/* ------------------------------------------------------------------ cases */

static void test_link_serial_mutate_every_byte_value_passes_once_whatever_hold_says(void)
{
    uint8_t *all = (uint8_t *)malloc(256u);
    uint8_t  b;
    unsigned i;

    TEST_ASSERT_NOT_NULL(all);
    for (i = 0u; i < 256u; i++) {
        all[i] = (uint8_t)i;
    }
    tfw_mock_feed_rx(all, 256u);
    for (i = 0u; i < 256u; i++) {
        b = (uint8_t)~i;
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_link_rx(&b, (uint8_t)(i & 1u)));
        TEST_ASSERT_EQUAL_HEX8((uint8_t)i, b);
    }
    for (i = 0u; i < 4u; i++) {
        b = 0x5Au;
        TEST_ASSERT_EQUAL_UINT8(0u, tfw_link_rx(&b, (uint8_t)(i & 1u)));
        TEST_ASSERT_EQUAL_HEX8(0x5Au, b);
    }
    free(all);
}

static void test_link_serial_mutate_tx_is_the_uarts_backpressure_and_admits_every_command(void)
{
    uint8_t  out[8];
    unsigned c;

    tfw_mock_set_tx_capacity(3u);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_link_tx_try(0x11u, 0u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_link_tx_try(0x22u, 1u));
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_link_tx_try(0x33u, 0xFFu));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_link_tx_try(0x44u, 0u));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_link_tx_try(0x44u, 1u));
    tfw_link_tx_done(0u);
    tfw_link_tx_done(1u);
    TEST_ASSERT_EQUAL_UINT(3u, tfw_mock_drain_tx(out, sizeof out));
    TEST_ASSERT_EQUAL_HEX8(0x11u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22u, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x33u, out[2]);
    TEST_ASSERT_EQUAL_UINT8(1u, tfw_link_tx_try(0x44u, 0u));

    for (c = 0u; c < 256u; c++) {
        TEST_ASSERT_EQUAL_UINT8(1u, tfw_link_admits((uint8_t)c));
    }
}

/* Every bit of a good frame flipped in turn, then an idle gap, then a good
 * frame: the mutated one is never delivered, the next one is, whole. */
static void test_link_serial_mutate_every_bit_flip_drops_the_frame_and_keeps_the_next(void)
{
    uint8_t  a[TFW_MAX_FRAME], g[TFW_MAX_FRAME];
    uint16_t na, ng;
    unsigned bit;

    rng = SEED;
    na  = make_frame(a, 0x41u);
    ng  = make_frame(g, 0x42u);
    for (bit = 0u; bit < na * 8u; bit++) {
        setUp();
        a[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        feed(a, na);
        pump();
        if (ready_n != 0u) {
            printf("seed 0x%08lx: bit %u: a mutated frame was delivered\n", (unsigned long)SEED, bit);
            TEST_FAIL_MESSAGE("a mutated frame was delivered");
        }
        tfw_framer_abort(&rxf);
        feed(g, ng);
        pump();
        TEST_ASSERT_EQUAL_UINT(1u, ready_n);
        expect_frame(0u, g, ng, "bit", bit);
        TEST_ASSERT_EQUAL_UINT(na + ng, got_len);
        a[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
    }
}

/* Every prefix of a good frame, then an idle gap, then a good frame. */
static void test_link_serial_mutate_every_truncation_drops_the_frame_and_keeps_the_next(void)
{
    uint8_t  a[TFW_MAX_FRAME], g[TFW_MAX_FRAME];
    uint16_t na, ng, cut;

    rng = SEED ^ 0x7Bu;
    na  = make_frame(a, 0x51u);
    ng  = make_frame(g, 0x52u);
    for (cut = 0u; cut < na; cut++) {
        setUp();
        feed(a, cut);
        pump();
        TEST_ASSERT_EQUAL_UINT(0u, ready_n);
        tfw_framer_abort(&rxf);
        feed(g, ng);
        pump();
        TEST_ASSERT_EQUAL_UINT(1u, ready_n);
        expect_frame(0u, g, ng, "cut", cut);
        TEST_ASSERT_EQUAL_UINT((unsigned)cut + ng, got_len);
    }
}

/* Good frames split at every boundary and drained between the halves: one
 * delivery, the bytes once each. */
static void test_link_serial_mutate_a_frame_split_anywhere_is_delivered_once(void)
{
    uint8_t  a[TFW_MAX_FRAME];
    uint16_t na, at;

    rng = SEED ^ 0x5911u;
    na  = make_frame(a, 0x61u);
    for (at = 0u; at <= na; at++) {
        setUp();
        feed(a, at);
        pump();
        feed(&a[at], (size_t)(na - at));
        pump();
        TEST_ASSERT_EQUAL_UINT(1u, ready_n);
        expect_frame(0u, a, na, "split", at);
        expect_bytes(a, na, "split", at);
    }
}

/* Random runs: up to MAX_FRAMES frames, each kept, bit-flipped or cut, with
 * line noise that holds no SOF between them, fed in random chunks and drained
 * between chunks. An idle gap follows a damaged frame, as the transport's
 * timeout would. Every kept frame is delivered whole, in order; no damaged
 * frame is; the link hands out every byte exactly once. */
static void test_link_serial_mutate_random_streams_deliver_exactly_the_good_frames(void)
{
    uint8_t  fr[TFW_MAX_FRAME];
    uint8_t  good[MAX_FRAMES][TFW_MAX_FRAME];
    uint16_t good_len[MAX_FRAMES];
    uint8_t *all;
    size_t   all_len, pos, chunk;
    unsigned run, f, nf, kept, k, noise, damaged;
    uint16_t n;

    all = (uint8_t *)malloc(sizeof got);
    TEST_ASSERT_NOT_NULL(all);
    rng = SEED ^ 0xC0FFEEu;
    for (run = 0u; run < RANDOM_RUNS; run++) {
        setUp();
        all_len = 0u;
        kept    = 0u;
        nf      = 1u + (unsigned)(rnd() % MAX_FRAMES);
        for (f = 0u; f < nf; f++) {
            for (noise = (unsigned)(rnd() % 6u); noise > 0u; noise--) {
                uint8_t z = (uint8_t)rnd();
                if (z == TFW_SOF) {
                    z = 0x00u;
                }
                feed(&z, 1u);
                all[all_len++] = z;
            }
            n = make_frame(fr, (uint8_t)(run + f));
            damaged = 1u;
            switch (rnd() % 4u) {
            case 0u:                       /* one bit flipped */
                fr[rnd() % n] ^= (uint8_t)(1u << (rnd() % 8u));
                break;
            case 1u:                       /* cut short */
                n = (uint16_t)(rnd() % n);
                break;
            default:                       /* kept */
                memcpy(good[kept], fr, n);
                good_len[kept] = n;
                kept++;
                damaged = 0u;
                break;
            }
            for (pos = 0u; pos < n; pos += chunk) {
                chunk = 1u + (size_t)(rnd() % 24u);
                if (chunk > n - pos) {
                    chunk = n - pos;
                }
                feed(&fr[pos], chunk);
                memcpy(&all[all_len], &fr[pos], chunk);
                all_len += chunk;
                pump();
            }
            pump();
            if (damaged) {
                tfw_framer_abort(&rxf);      /* the idle gap after a damaged frame */
            }
        }
        if (ready_n != kept) {
            printf("seed 0x%08lx: run %u: %u frames kept, %u delivered\n", (unsigned long)SEED,
                   run, kept, ready_n);
            TEST_FAIL_MESSAGE("delivered frames are not exactly the good ones");
        }
        for (k = 0u; k < kept; k++) {
            expect_frame(k, good[k], good_len[k], "run", run);
        }
        expect_bytes(all, all_len, "run", run);
    }
    free(all);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_link_serial_mutate_every_byte_value_passes_once_whatever_hold_says);
    RUN_TEST(test_link_serial_mutate_tx_is_the_uarts_backpressure_and_admits_every_command);
    RUN_TEST(test_link_serial_mutate_every_bit_flip_drops_the_frame_and_keeps_the_next);
    RUN_TEST(test_link_serial_mutate_every_truncation_drops_the_frame_and_keeps_the_next);
    RUN_TEST(test_link_serial_mutate_a_frame_split_anywhere_is_delivered_once);
    RUN_TEST(test_link_serial_mutate_random_streams_deliver_exactly_the_good_frames);
    return UNITY_END();
}
