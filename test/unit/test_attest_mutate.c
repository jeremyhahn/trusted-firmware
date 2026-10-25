/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for the ATTEST command.
 *
 * test_attest pins what a clean nonce and a clean page request do. This feeds
 * the same requests, through the same dispatcher, mutated: bit flips,
 * truncation at every length, all 0x00, all 0xFF, random bytes, and every page
 * swept through 0, 1, the last, one past it and wrapped. Each request runs on
 * three devices -- provisioned with no evidence yet, provisioned with evidence
 * for the test_attest nonce, and unprovisioned. Each payload sits in a heap
 * buffer of exactly its length; each reply goes to a writer of a chosen
 * capacity, some bytes already in it, with guard bytes after it. Every call is
 * held to:
 *
 *   - nothing past the writer's capacity, nothing over the bytes it held;
 *   - a refusal leaves the writer's length where it was;
 *   - only a 16-byte nonce makes evidence, and it echoes that nonce;
 *   - a page answers only evidence that exists, exactly the bytes of it the
 *     page names, never past its 164 bytes;
 *   - an unprovisioned device answers nothing at all.
 *
 * Deterministic: xorshift32 from a literal seed, literal counts. A failure
 * prints the device, the command and the payload so the case can be pinned.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "attest.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "uds_mock.h"
#include "boot_soft_mock.h"
#include "handoff.h"

#define GUARD       16u
#define GUARD_BYTE  0xA5u
#define PRE_BYTE    0xC3u
#define LIMIT       (TFW_MAX_PAYLOAD + 1u)
#define RANDOM_RUNS 1000u   /* per seed */
#define DEVICES     3u      /* fresh, primed, unprovisioned */
#define FEEDS       (DEVICES * 2u)

enum { FRESH = 0, PRIMED = 1, UNPROVISIONED = 2 };

static const uint8_t TEST_UDS[32] = {
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,
    0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF
};

/* The seeds are the requests test_attest sends. */
static const uint8_t NONCE[TFW_ATTEST_NONCE_LEN] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
    0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
};
static const uint8_t page_seed[1] = { 1u };

static uint32_t rng;
static uint8_t  evidence[TFW_ATTEST_LEN];   /* what NONCE's evidence is, read clean */
static unsigned ran;
static unsigned ok_runs;
static unsigned refused_runs;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void device(unsigned kind)
{
    tfw_mock_reset();
    tfw_attest_reset_state();
    tfw_mock_set_uds(0);
    if (kind == UNPROVISIONED) {
        tfw_boot_handoff_init();                /* the reset block: no handoff */
    } else {
        tfw_mock_boot_soft(TEST_UDS, TEST_UDS); /* the seed in tfw-boot's handoff */
    }
    tfw_crypto_init();
}

void setUp(void)    { device(FRESH); }
void tearDown(void) {}

static void start(uint32_t seed)
{
    rng          = seed;
    ran          = 0u;
    ok_runs      = 0u;
    refused_runs = 0u;
}

static void report(const char *what)
{
    printf("%s: %u requests, %u ok, %u refused\n", what, ran, ok_runs, refused_runs);
}

static void dump(unsigned kind, const uint8_t *p, unsigned n, unsigned cap, unsigned pre)
{
    unsigned i;
    printf("device %u cap %u pre %u payload[%u]:", kind, cap, pre, n);
    for (i = 0u; i < n; i++) {
        printf(" %02x", p[i]);
    }
    printf("\n");
}

static tfw_status_t run(const uint8_t *p, uint8_t n, uint8_t *out, uint8_t *len)
{
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_status_t st;

    tfw_reader_init(&r, p, n);
    tfw_writer_init(&w, out, TFW_MAX_PAYLOAD);
    st   = tfw_dispatch(TFW_CMD_ATTEST, &r, &w);
    *len = tfw_writer_len(&w);
    return st;
}

/* The evidence for NONCE, reassembled from its pages on a clean device. */
static void read_evidence(void)
{
    uint8_t out[TFW_MAX_PAYLOAD];
    uint8_t len;
    uint8_t got;
    uint8_t page;

    device(FRESH);
    TEST_ASSERT_EQUAL(TFW_OK, run(NONCE, TFW_ATTEST_NONCE_LEN, out, &len));
    memcpy(evidence, &out[2], (size_t)(len - 2u));
    got = (uint8_t)(len - 2u);
    for (page = 1u; got < TFW_ATTEST_LEN; page++) {
        TEST_ASSERT_EQUAL(TFW_OK, run(&page, 1u, out, &len));
        memcpy(&evidence[got], &out[2], (size_t)(len - 2u));
        got = (uint8_t)(got + (len - 2u));
    }
    TEST_ASSERT_EQUAL_UINT8(TFW_ATTEST_LEN, got);
}

/* A reply of exactly want bytes, or TOO_LONG because they did not fit. */
static const char *check_len(tfw_status_t st, unsigned rlen, unsigned room, unsigned want)
{
    if (st == TFW_ERR_TOO_LONG) {
        return want > room ? NULL : "a reply that fitted was called too long";
    }
    return rlen == want ? NULL : "a reply of the wrong length";
}

/* What the call must have done, given the device, the request and its status;
 * NULL when it did. */
static const char *check(unsigned kind, const uint8_t *p, unsigned n, tfw_status_t st,
                         const uint8_t *reply, unsigned rlen, unsigned room)
{
    const uint8_t taken = (uint8_t)(st == TFW_OK || st == TFW_ERR_TOO_LONG);
    const char   *fail;

    if (!taken) {
        if (kind == UNPROVISIONED && n == TFW_ATTEST_NONCE_LEN && st != TFW_ERR_NOT_PROVISIONED) {
            return "an unprovisioned device refused a nonce for another reason";
        }
        return NULL;
    }
    if (kind == UNPROVISIONED) {
        return "an unprovisioned device answered";
    }
    if (n == TFW_ATTEST_NONCE_LEN) {
        if ((fail = check_len(st, rlen, room, 2u + TFW_ATTEST_PAGE)) != NULL || st != TFW_OK) {
            return fail;
        }
        if (reply[0] != 0u || reply[1] != TFW_ATTEST_LEN || reply[2] != TFW_ATTEST_VERSION) {
            return "fresh evidence did not open page 0 of the evidence";
        }
        return memcmp(&reply[3], p, TFW_ATTEST_NONCE_LEN) == 0
               ? NULL : "fresh evidence did not echo the nonce";
    }
    if (n == 1u) {
        const unsigned off = (unsigned)p[0] * TFW_ATTEST_PAGE;
        const unsigned len = TFW_ATTEST_LEN - off < TFW_ATTEST_PAGE ? TFW_ATTEST_LEN - off
                                                                    : TFW_ATTEST_PAGE;
        if (kind != PRIMED) {
            return "a page with no evidence behind it was answered";
        }
        if (off >= TFW_ATTEST_LEN) {
            return "a page past the evidence was answered";
        }
        if ((fail = check_len(st, rlen, room, 2u + len)) != NULL || st != TFW_OK) {
            return fail;
        }
        if (reply[0] != p[0] || reply[1] != TFW_ATTEST_LEN) {
            return "a page answered another page's header";
        }
        return memcmp(&reply[2], &evidence[off], len) == 0
               ? NULL : "a page answered bytes the evidence does not hold there";
    }
    return "a request neither a nonce nor a page was answered";
}

static tfw_status_t feed_at(unsigned kind, const uint8_t *p, unsigned n, unsigned cap, unsigned pre)
{
    uint8_t      *in  = (uint8_t *)malloc(n ? n : 1u);
    uint8_t      *out = (uint8_t *)malloc(cap + GUARD);
    uint8_t       scratch[TFW_MAX_PAYLOAD];
    uint8_t       slen;
    tfw_reader_t  r;
    tfw_writer_t  w;
    tfw_status_t  st;
    const char   *fail = NULL;
    unsigned      len;
    unsigned      i;

    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_NOT_NULL(out);
    if (n) { memcpy(in, p, n); }
    memset(out, GUARD_BYTE, cap + GUARD);
    device(kind);
    if (kind == PRIMED) {
        TEST_ASSERT_EQUAL(TFW_OK, run(NONCE, TFW_ATTEST_NONCE_LEN, scratch, &slen));
    }

    tfw_reader_init(&r, in, (uint8_t)n);
    tfw_writer_init(&w, out, (uint8_t)cap);
    for (i = 0u; i < pre; i++) {
        tfw_write_u8(&w, PRE_BYTE);
    }
    st  = tfw_dispatch(TFW_CMD_ATTEST, &r, &w);
    len = tfw_writer_len(&w);

    for (i = cap; i < cap + GUARD && !fail; i++) {
        if (out[i] != GUARD_BYTE) {
            fail = "wrote past the writer's capacity";
        }
    }
    for (i = 0u; i < pre && !fail; i++) {
        if (out[i] != PRE_BYTE) {
            fail = "overwrote bytes the writer already held";
        }
    }
    if (!fail && len > cap) {
        fail = "the writer's length ran past its capacity";
    }
    if (!fail && (unsigned)st > (unsigned)TFW_ERR_NO_STORE) {
        fail = "an unknown status";
    }
    if (!fail && st != TFW_OK && st != TFW_ERR_TOO_LONG && len != pre) {
        fail = "a refused request moved the writer";
    }
    if (!fail && n > TFW_MAX_PAYLOAD && st != TFW_ERR_BAD_ARGC) {
        fail = "a request past the frame maximum was not refused";
    }
    if (!fail) {
        fail = check(kind, p, n, st, out + pre, len - pre, cap - pre);
    }
    if (fail) {
        printf("status %u, writer %u\n", (unsigned)st, len);
        dump(kind, p, n, cap, pre);
        TEST_FAIL_MESSAGE(fail);
    }
    ran++;
    if (st == TFW_OK) {
        ok_runs++;
    } else {
        refused_runs++;
    }
    free(in);
    free(out);
    return st;
}

/* Each request on each device twice: into a whole reply buffer, then into a
 * random shorter one that already holds a byte or two. */
static void feed(const uint8_t *p, unsigned n)
{
    unsigned kind;

    for (kind = 0u; kind < DEVICES; kind++) {
        const unsigned cap = rnd() % (TFW_MAX_PAYLOAD + 1u);
        (void)feed_at(kind, p, n, TFW_MAX_PAYLOAD, 0u);
        (void)feed_at(kind, p, n, cap, cap ? rnd() % (cap < 3u ? cap + 1u : 3u) : 0u);
    }
}

static void random_fill(uint8_t *p, unsigned n)
{
    unsigned i;
    for (i = 0u; i < n; i++) {
        p[i] = (uint8_t)rnd();
    }
}

static void test_every_page(void)
{
    uint8_t  p[1];
    unsigned page;

    read_evidence();
    start(0x9E3779B9u);
    for (page = 0u; page < 256u; page++) {
        p[0] = (uint8_t)page;
        feed(p, 1u);
    }
    TEST_ASSERT_EQUAL_UINT(FEEDS * 256u, ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("attest page fields");
}

static void test_every_length_around_the_nonce(void)
{
    uint8_t  p[LIMIT];
    unsigned n;
    unsigned k;

    read_evidence();
    start(0x7F4A7C15u);
    for (n = 0u; n <= LIMIT; n++) {
        for (k = 0u; k < 4u; k++) {
            random_fill(p, n);
            feed(p, n);
        }
    }
    TEST_ASSERT_EQUAL_UINT(FEEDS * (LIMIT + 1u) * 4u, ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("attest length fields");
}

/* Every mutation of one seed; returns the number of requests it fed. */
static unsigned mutate(const uint8_t *seed, unsigned sn)
{
    uint8_t  buf[LIMIT];
    unsigned n;
    unsigned i;
    unsigned k;
    unsigned b;

    /* truncation at every length, and every length up to one past the frame */
    for (n = 0u; n <= LIMIT; n++) {
        memcpy(buf, seed, n < sn ? n : sn);
        if (n > sn) {
            random_fill(buf + sn, n - sn);
        }
        feed(buf, n);
    }
    /* all 0x00 and all 0xFF at every length */
    for (n = 0u; n <= LIMIT; n++) {
        memset(buf, 0x00, n);
        feed(buf, n);
        memset(buf, 0xFF, n);
        feed(buf, n);
    }
    /* every single bit flipped */
    for (b = 0u; b < sn * 8u; b++) {
        memcpy(buf, seed, sn);
        buf[b / 8u] ^= (uint8_t)(1u << (b % 8u));
        feed(buf, sn);
    }
    /* several bits, then a random cut or a random tail */
    for (k = 0u; k < RANDOM_RUNS; k++) {
        unsigned flips = 1u + rnd() % 4u;
        n = sn;
        memcpy(buf, seed, sn);
        while (flips--) {
            const uint32_t at = rnd() % (n * 8u);
            buf[at / 8u] ^= (uint8_t)(1u << (at % 8u));
        }
        switch (rnd() % 3u) {
        case 0u:
            n = rnd() % (n + 1u);
            break;
        case 1u:
            i = rnd() % (LIMIT - n + 1u);
            random_fill(buf + n, i);
            n += i;
            break;
        default:
            break;
        }
        feed(buf, n);
    }
    return FEEDS * ((LIMIT + 1u) * 3u + sn * 8u + RANDOM_RUNS);
}

static void test_mutated_attest_requests(void)
{
    unsigned want = 0u;

    read_evidence();
    start(0x85EBCA6Bu);
    want += mutate(NONCE, sizeof NONCE);
    want += mutate(page_seed, sizeof page_seed);
    TEST_ASSERT_EQUAL_UINT(want, ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("mutated attest requests");
}

/* After all that, the clean requests answer as test_attest pins them. */
static void test_the_clean_requests_afterwards(void)
{
    uint8_t out[TFW_MAX_PAYLOAD];
    uint8_t len;
    uint8_t page = 99u;

    device(UNPROVISIONED);
    TEST_ASSERT_EQUAL(TFW_ERR_NOT_PROVISIONED, run(NONCE, TFW_ATTEST_NONCE_LEN, out, &len));

    device(FRESH);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, run(page_seed, 1u, out, &len));
    TEST_ASSERT_EQUAL(TFW_OK, run(NONCE, TFW_ATTEST_NONCE_LEN, out, &len));
    TEST_ASSERT_EQUAL_UINT8(0u, out[0]);
    TEST_ASSERT_EQUAL_UINT8(TFW_ATTEST_LEN, out[1]);
    TEST_ASSERT_EQUAL_UINT8(TFW_ATTEST_VERSION, out[2]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NONCE, &out[3], TFW_ATTEST_NONCE_LEN);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, run(&page, 1u, out, &len));

    read_evidence();
    /* The host links the soft root's P-256 DeviceID (mk/host.mk SOFT_ID_SRCS). */
    /* ES256 under the soft root, so the AlgID is 0x11 (#130). */
    TEST_ASSERT_EQUAL_HEX8(0x11u, evidence[73]);   /* OFF_ALG */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_page);
    RUN_TEST(test_every_length_around_the_nonce);
    RUN_TEST(test_mutated_attest_requests);
    RUN_TEST(test_the_clean_requests_afterwards);
    return UNITY_END();
}
