/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation of the byte stream into the transport (src/proto/transport.c),
 * on the emulated link test_transport drives. The streams are built from the
 * frames that suite sends, mutated: every bit flip, truncation at every
 * length, a split at every byte boundary, every byte duplicated and every
 * byte dropped, all 0x00, all 0xFF, LEN at 0, 1, max and max + 1 with the CRC
 * left and resealed, random bytes, and random stacks of those edits.
 *
 * The oracle is the frame layout as bytes: SOF, LEN in [TFW_LEN_MIN,
 * TFW_MAX_LEN], LEN body bytes [SEQ][OP][payload], and a little-endian CRC16
 * over LEN and the body. A reply other than the CRC status means a request was
 * dispatched, and every one must name a frame that is somewhere in the stream
 * whole, with its CRC right and OP a request; a PING reply must echo that
 * frame's payload. The transport sits between guard bytes, which no run may
 * touch, and after every run, once the idle window has passed, a clean PING
 * gets byte for byte the reply it got before any mutation.
 *
 * FOUND, left as it is (a test-only branch changes no code): a request with
 * CMD 0x7F, which the wire allows (requests are 0x00..0x7F), is answered with
 * OP 0x7F | 0x80 = 0xFF, the event OP that include/tfw/protocol.h says is
 * "never a response" -- from handle() for a good frame and from the BAD_CRC
 * reply_status() for a bad one. A host reads that reply as an event. The
 * oracle below accepts it, as what the code does today, and counts it.
 *
 * xorshift32 from a literal seed, fixed counts; every loop asserts it ran.
 */
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "transport.h"
#include "sys.h"
#include "crc16.h"
#include "hal_mock.h"
#include "tfw/hal.h"
#include "tfw/protocol.h"

#define RANDOM_RUNS 3000u
#define STACK_RUNS  3000u
#define GUARD       16u
#define GUARD_BYTE  0xC3u
#define STREAM_MAX  (2u * TFW_MAX_FRAME + 8u)
#define BACK_MAX    (8u * TFW_MAX_FRAME)
#define NFRAMES     7u

/* The transport between guard bytes: a write past either end of it lands in
 * one of them. */
static struct {
    uint8_t         pre[GUARD];
    tfw_transport_t t;
    uint8_t         post[GUARD];
} g;

typedef struct {
    uint8_t  wire[TFW_MAX_FRAME];
    uint16_t len;
    uint8_t  back[BACK_MAX];      /* the reply the clean frame gets */
    size_t   back_len;
} seed_t;

static seed_t   seeds[NFRAMES];
static uint32_t rng = 0x5EC0A11Bu;
static unsigned runs, dispatched, crc_replies, split_runs, event_op_replies;

static uint32_t xorshift32(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void fresh(void)
{
    tfw_mock_reset();
    tfw_sys_reset_state();
    memset(g.pre, GUARD_BYTE, sizeof g.pre);
    memset(g.post, GUARD_BYTE, sizeof g.post);
    tfw_transport_init(&g.t);
}

static void check_guards(void)
{
    unsigned i;

    for (i = 0u; i < GUARD; i++) {
        TEST_ASSERT_EQUAL_HEX8(GUARD_BYTE, g.pre[i]);
        TEST_ASSERT_EQUAL_HEX8(GUARD_BYTE, g.post[i]);
    }
    TEST_ASSERT_TRUE(g.t.out_len <= sizeof g.t.out);
    TEST_ASSERT_TRUE(g.t.out_pos <= g.t.out_len);
    TEST_ASSERT_TRUE(g.t.framer.want <= TFW_MAX_LEN);
    TEST_ASSERT_TRUE(g.t.framer.got <= g.t.framer.want);
}

static void poll_n(size_t n)
{
    size_t i;

    for (i = 0u; i < n; i++) {
        tfw_transport_poll(&g.t);
        check_guards();
    }
}

/* Feeds a stream, polls until it is consumed, and returns what came back. */
static size_t feed(const uint8_t *s, size_t n, uint8_t *back)
{
    tfw_mock_feed_rx(s, n);
    poll_n(n + 8u);
    return tfw_mock_drain_tx(back, BACK_MAX);
}

/* The frames test_transport sends. */
static void build_seeds(void)
{
    static const uint8_t pong[4] = { 'p', 'o', 'n', 'g' };
    uint8_t  pay[32];
    unsigned i;

    memset(pay, 0x5Au, sizeof pay);
    seeds[0].len = tfw_frame_encode_request(seeds[0].wire, TFW_MAX_FRAME, 0x21u, TFW_CMD_PING, pong, 4u);
    seeds[1].len = tfw_frame_encode_request(seeds[1].wire, TFW_MAX_FRAME, 0xABu, TFW_CMD_CAPS, 0, 0u);
    seeds[2].len = tfw_frame_encode_request(seeds[2].wire, TFW_MAX_FRAME, 1u, 0x7Eu, 0, 0u);
    seeds[3].len = tfw_frame_encode_request(seeds[3].wire, TFW_MAX_FRAME, 3u, TFW_CMD_PING, 0, 0u);
    seeds[4].len = tfw_frame_encode_request(seeds[4].wire, TFW_MAX_FRAME, 7u, TFW_CMD_SYS_INFO, 0, 0u);
    seeds[5].len = tfw_frame_encode_request(seeds[5].wire, TFW_MAX_FRAME, 2u, TFW_CMD_PING, pay, (uint8_t)sizeof pay);
    seeds[6].len = tfw_frame_encode_request(seeds[6].wire, TFW_MAX_FRAME, 0x44u, TFW_CMD_PING, 0, 0u);
    for (i = 0u; i < NFRAMES; i++) {
        TEST_ASSERT_NOT_EQUAL(0u, seeds[i].len);
        fresh();
        seeds[i].back_len = feed(seeds[i].wire, seeds[i].len, seeds[i].back);
        TEST_ASSERT_NOT_EQUAL(0u, seeds[i].back_len);
    }
}

/* 1 and the frame's seq, op and payload if a whole, well-formed request with a
 * right CRC starts at s[i]. */
static int frame_at(const uint8_t *s, size_t n, size_t i,
                    uint8_t *seq, uint8_t *op, const uint8_t **pay, uint8_t *plen)
{
    uint8_t  len;
    uint16_t crc;

    if (s[i] != TFW_SOF || i + 1u >= n) {
        return 0;
    }
    len = s[i + 1u];
    if (len < TFW_LEN_MIN || len > TFW_MAX_LEN || i + 2u + len + 2u > n) {
        return 0;
    }
    crc = tfw_crc16(&s[i + 1u], (uint16_t)(len + 1u));
    if (crc != (uint16_t)(s[i + 2u + len] | (uint16_t)s[i + 3u + len] << 8)) {
        return 0;
    }
    *seq  = s[i + 2u];
    *op   = s[i + 3u];
    *pay  = &s[i + 4u];
    *plen = (uint8_t)(len - 2u);
    return (*op & TFW_OP_RESPONSE_BIT) == 0u;
}

/* Every reply that says a request was dispatched names a frame in the stream. */
static void check_replies(const uint8_t *s, size_t n, const uint8_t *back, size_t got)
{
    tfw_framer_t rxf;
    size_t       k, i;

    tfw_framer_init(&rxf);
    for (k = 0u; k < got; k++) {
        if (tfw_framer_push(&rxf, back[k]) != TFW_FRAME_READY) {
            continue;
        }
        TEST_ASSERT_TRUE(tfw_frame_op(&rxf) & TFW_OP_RESPONSE_BIT);
        if (tfw_frame_op(&rxf) == TFW_OP_EVENT) {
            event_op_replies++;           /* CMD 0x7F's reply: see the header */
        }
        if (tfw_frame_status(&rxf) == TFW_ERR_CRC) {
            crc_replies++;
            continue;
        }
        dispatched++;
        for (i = 0u; i < n; i++) {
            uint8_t        seq, op, plen;
            const uint8_t *pay;

            if (!frame_at(s, n, i, &seq, &op, &pay, &plen)) {
                continue;
            }
            if (seq != tfw_frame_seq(&rxf) || (uint8_t)(op | TFW_OP_RESPONSE_BIT) != tfw_frame_op(&rxf)) {
                continue;
            }
            if (op == TFW_CMD_PING && tfw_frame_status(&rxf) == TFW_OK &&
                (plen != tfw_frame_payload_len(&rxf) ||
                 memcmp(pay, tfw_frame_payload(&rxf), plen) != 0)) {
                continue;
            }
            break;
        }
        TEST_ASSERT_TRUE_MESSAGE(i < n, "a reply names no whole frame in the stream");
    }
}

/* After the idle window a clean PING gets what it got before any mutation. */
static void check_clean_after(void)
{
    static uint8_t back[BACK_MAX];
    size_t         got;

    tfw_mock_advance(TFW_FRAME_IDLE_MS + 1u);
    poll_n(1u);
    TEST_ASSERT_FALSE(g.t.in_frame);
    got = feed(seeds[0].wire, seeds[0].len, back);
    TEST_ASSERT_EQUAL_UINT(seeds[0].back_len, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(seeds[0].back, back, got);
}

static void run(const uint8_t *s, size_t n)
{
    static uint8_t back[BACK_MAX];
    size_t         got;

    fresh();
    got = feed(s, n, back);
    check_replies(s, n, back, got);
    check_clean_after();
    runs++;
}

static void test_the_clean_frames_are_answered_as_test_transport_pins(void)
{
    static const uint8_t pong[4] = { 'p', 'o', 'n', 'g' };
    tfw_framer_t rxf;
    size_t       k;
    uint8_t      saw = 0u;

    tfw_framer_init(&rxf);
    for (k = 0u; k < seeds[0].back_len; k++) {
        if (tfw_framer_push(&rxf, seeds[0].back[k]) == TFW_FRAME_READY) { saw++; }
    }
    TEST_ASSERT_EQUAL_UINT8(1u, saw);
    TEST_ASSERT_EQUAL_HEX8(0x21u, tfw_frame_seq(&rxf));
    TEST_ASSERT_EQUAL_HEX8(TFW_CMD_PING | TFW_OP_RESPONSE_BIT, tfw_frame_op(&rxf));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_frame_status(&rxf));
    TEST_ASSERT_EQUAL_UINT8(4u, tfw_frame_payload_len(&rxf));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pong, tfw_frame_payload(&rxf), 4u);

    tfw_framer_init(&rxf);
    for (k = 0u; k < seeds[2].back_len; k++) {
        (void)tfw_framer_push(&rxf, seeds[2].back[k]);
    }
    TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, tfw_frame_status(&rxf));
    TEST_ASSERT_EQUAL_UINT8(0u, tfw_frame_payload_len(&rxf));
}

static void test_every_bit_flip(void)
{
    uint8_t  s[TFW_MAX_FRAME];
    unsigned f, bit, want = 0u;

    runs = dispatched = crc_replies = 0u;
    for (f = 0u; f < NFRAMES; f++) {
        for (bit = 0u; bit < seeds[f].len * 8u; bit++) {
            memcpy(s, seeds[f].wire, seeds[f].len);
            s[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            run(s, seeds[f].len);
        }
        want += seeds[f].len * 8u;
    }
    TEST_ASSERT_EQUAL_UINT(want, runs);
    /* One to three flipped bits are always caught by the CRC (HD=4). */
    TEST_ASSERT_EQUAL_UINT(0u, dispatched);
    TEST_ASSERT_NOT_EQUAL(0u, crc_replies);
}

static void test_every_truncation(void)
{
    unsigned f, n, want = 0u;

    runs = dispatched = 0u;
    for (f = 0u; f < NFRAMES; f++) {
        for (n = 1u; n < seeds[f].len; n++) {
            run(seeds[f].wire, n);
        }
        want += seeds[f].len - 1u;
    }
    TEST_ASSERT_EQUAL_UINT(want, runs);
    TEST_ASSERT_EQUAL_UINT(0u, dispatched);
}

/* A frame split at any byte, polled between the halves, is answered byte for
 * byte as the whole frame is. */
static void test_a_split_at_every_byte_boundary(void)
{
    static uint8_t back[BACK_MAX];
    unsigned       f, cut, want = 0u;
    size_t         got;

    split_runs = 0u;
    for (f = 0u; f < NFRAMES; f++) {
        for (cut = 1u; cut < seeds[f].len; cut++) {
            fresh();
            tfw_mock_feed_rx(seeds[f].wire, cut);
            poll_n(cut + 4u);
            got = feed(&seeds[f].wire[cut], seeds[f].len - cut, back);
            TEST_ASSERT_EQUAL_UINT(seeds[f].back_len, got);
            TEST_ASSERT_EQUAL_HEX8_ARRAY(seeds[f].back, back, got);
            check_clean_after();
            split_runs++;
        }
        want += seeds[f].len - 1u;
    }
    TEST_ASSERT_EQUAL_UINT(want, split_runs);
}

static void test_every_byte_duplicated_and_dropped(void)
{
    uint8_t  s[TFW_MAX_FRAME + 1u];
    unsigned f, i, want = 0u;

    runs = 0u;
    for (f = 0u; f < NFRAMES; f++) {
        for (i = 0u; i < seeds[f].len; i++) {
            memcpy(s, seeds[f].wire, i + 1u);
            memcpy(&s[i + 1u], &seeds[f].wire[i], seeds[f].len - i);
            run(s, seeds[f].len + 1u);

            memcpy(s, seeds[f].wire, i);
            memcpy(&s[i], &seeds[f].wire[i + 1u], seeds[f].len - i - 1u);
            run(s, seeds[f].len - 1u);
        }
        want += 2u * seeds[f].len;
    }
    TEST_ASSERT_EQUAL_UINT(want, runs);
}

static void test_all_zero_and_all_ones(void)
{
    uint8_t  s[STREAM_MAX];
    unsigned n;

    runs = dispatched = 0u;
    for (n = 1u; n <= STREAM_MAX; n++) {
        memset(s, 0x00, n);
        run(s, n);
        memset(s, 0xFF, n);
        run(s, n);
    }
    TEST_ASSERT_EQUAL_UINT(2u * STREAM_MAX, runs);
    TEST_ASSERT_EQUAL_UINT(0u, dispatched);
}

/* LEN at 0, 1, max and max + 1, and 0xFF: once with the bytes as they were and
 * once resealed, the body cut or padded to what LEN says and the CRC right. */
static void test_length_field_edges(void)
{
    static const uint8_t lens[] = { 0u, 1u, TFW_MAX_LEN, (uint8_t)(TFW_MAX_LEN + 1u), 0xFFu };
    uint8_t  s[4u + 256u];
    unsigned f, k, want = 0u;
    uint16_t crc;

    runs = dispatched = 0u;
    for (f = 0u; f < NFRAMES; f++) {
        for (k = 0u; k < sizeof lens; k++) {
            uint8_t len = lens[k];

            memcpy(s, seeds[f].wire, seeds[f].len);
            s[1] = len;
            run(s, seeds[f].len);

            memset(s, 0x5Au, sizeof s);
            memcpy(s, seeds[f].wire, seeds[f].len - 2u);
            s[1] = len;
            crc = tfw_crc16(&s[1], (uint16_t)(len + 1u));
            s[2u + len] = (uint8_t)crc;
            s[3u + len] = (uint8_t)(crc >> 8);
            run(s, 4u + len);
            want += 2u;
        }
    }
    TEST_ASSERT_EQUAL_UINT(want, runs);
    /* Only LEN = max resealed is a frame: one per seed. */
    TEST_ASSERT_EQUAL_UINT(NFRAMES, dispatched);
}

/* A resealed OP that claims a STATUS byte the body is too short for. */
static void test_an_event_op_with_no_room_for_a_status(void)
{
    uint8_t  s[6] = { TFW_SOF, 2u, 0x09u, TFW_OP_EVENT, 0u, 0u };
    uint16_t crc = tfw_crc16(&s[1], 3u);

    s[4] = (uint8_t)crc;
    s[5] = (uint8_t)(crc >> 8);
    runs = dispatched = crc_replies = 0u;
    run(s, sizeof s);
    TEST_ASSERT_EQUAL_UINT(1u, runs);
    TEST_ASSERT_EQUAL_UINT(0u, dispatched);
    TEST_ASSERT_EQUAL_UINT(0u, crc_replies);
}

static void test_random_bytes(void)
{
    uint8_t  s[STREAM_MAX];
    unsigned i, j, n;

    runs = 0u;
    for (i = 0u; i < RANDOM_RUNS; i++) {
        n = 1u + xorshift32() % STREAM_MAX;
        for (j = 0u; j < n; j++) {
            s[j] = (uint8_t)xorshift32();
        }
        /* Half of them open with SOF and a plausible LEN, so the framer gets
         * past the hunt. */
        if (i & 1u) {
            s[0] = TFW_SOF;
            if (n > 1u) {
                s[1] = (uint8_t)(TFW_LEN_MIN + xorshift32() % (TFW_MAX_LEN - TFW_LEN_MIN + 1u));
            }
        }
        run(s, n);
    }
    TEST_ASSERT_EQUAL_UINT(RANDOM_RUNS, runs);
}

/* Two seed frames back to back, then a random stack of flips, overwrites,
 * duplications, drops and a truncation. */
static void test_random_stacks_of_edits(void)
{
    uint8_t  s[STREAM_MAX];
    unsigned i, e, edits, a, b;
    size_t   n, p;

    runs = 0u;
    for (i = 0u; i < STACK_RUNS; i++) {
        a = xorshift32() % NFRAMES;
        b = xorshift32() % NFRAMES;
        memcpy(s, seeds[a].wire, seeds[a].len);
        memcpy(&s[seeds[a].len], seeds[b].wire, seeds[b].len);
        n = (size_t)seeds[a].len + seeds[b].len;
        edits = 1u + xorshift32() % 4u;
        for (e = 0u; e < edits && n > 1u; e++) {
            p = xorshift32() % n;
            switch (xorshift32() % 5u) {
            case 0u:
                s[p] ^= (uint8_t)(1u << (xorshift32() % 8u));
                break;
            case 1u:
                s[p] = (uint8_t)xorshift32();
                break;
            case 2u:
                if (n < STREAM_MAX) {
                    memmove(&s[p + 1u], &s[p], n - p);
                    n++;
                }
                break;
            case 3u:
                memmove(&s[p], &s[p + 1u], n - p - 1u);
                n--;
                break;
            default:
                n = p + 1u;
                break;
            }
        }
        run(s, n);
    }
    TEST_ASSERT_EQUAL_UINT(STACK_RUNS, runs);
}

void setUp(void) { build_seeds(); }
void tearDown(void) { }

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_clean_frames_are_answered_as_test_transport_pins);
    RUN_TEST(test_every_bit_flip);
    RUN_TEST(test_every_truncation);
    RUN_TEST(test_a_split_at_every_byte_boundary);
    RUN_TEST(test_every_byte_duplicated_and_dropped);
    RUN_TEST(test_all_zero_and_all_ones);
    RUN_TEST(test_length_field_edges);
    RUN_TEST(test_an_event_op_with_no_room_for_a_status);
    RUN_TEST(test_random_bytes);
    RUN_TEST(test_random_stacks_of_edits);
    RUN_TEST(test_the_clean_frames_are_answered_as_test_transport_pins);
    return UNITY_END();
}
