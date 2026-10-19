/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * Seeded mutation test for EEPROM_READ and EEPROM_WRITE.
 *
 * test_storage pins what a clean request does. This feeds the same requests,
 * through the same dispatcher, mutated: bit flips, truncation at every length,
 * all 0x00, all 0xFF, random bytes, and the address and length fields at 0, 1,
 * the maximum, one past it and wrapped. Each payload sits in a heap buffer of
 * exactly its length; each reply goes to a writer of a chosen capacity, some
 * bytes already in it, with guard bytes after it. Every call is held to:
 *
 *   - nothing past the writer's capacity, nothing over the bytes it held;
 *   - a refusal leaves the writer's length where it was and the part untouched;
 *   - a read answers exactly the bytes the part holds at that span, never more;
 *   - a write lands exactly where it said, never in the boot-owned region and
 *     never past the part.
 *
 * Deterministic: xorshift32 from a literal seed, literal counts. A failure
 * prints the command and the payload so the case can be pinned.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "storage.h"
#include "dispatch.h"
#include "hal_mock.h"
#include "tfw/boot.h"
#include "tfw/protocol.h"
#include "tfw/config.h"

#define GUARD       16u
#define GUARD_BYTE  0xA5u
#define PRE_BYTE    0xC3u
#define LIMIT       (TFW_MAX_PAYLOAD + 1u)
#define CHUNK       8u    /* TFW_EEPROM_CHUNK, private to src/caps/storage.c */
#define RANDOM_RUNS 2000u /* per command */

static uint32_t rng;
static uint8_t  before[TFW_EEPROM_SIZE];
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

static void fill_part(void)
{
    uint8_t *ee = tfw_mock_eeprom();
    unsigned i;

    for (i = 0u; i < TFW_EEPROM_SIZE; i++) {
        ee[i] = (uint8_t)(i * 7u + 3u);
    }
}

void setUp(void)
{
    tfw_mock_reset();
    fill_part();
}
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

static void dump(uint8_t cmd, const uint8_t *p, unsigned n, unsigned cap, unsigned pre)
{
    unsigned i;
    printf("cmd 0x%02x cap %u pre %u payload[%u]:", cmd, cap, pre, n);
    for (i = 0u; i < n; i++) {
        printf(" %02x", p[i]);
    }
    printf("\n");
}

/* What the part must hold after the call, given the request and its status;
 * NULL when it does. */
static const char *check_part(uint8_t cmd, const uint8_t *p, unsigned n, tfw_status_t st)
{
    const uint8_t *ee = tfw_mock_eeprom();
    uint16_t       lo;
    uint16_t       hi;
    size_t         count = tfw_mock_eeprom_written(&lo, &hi);
    unsigned       addr;
    unsigned       len;
    unsigned       i;

    if (memcmp(ee, before, TFW_BOOT_EE_LEN) != 0) {
        return "the boot-owned region changed";
    }
    if (count && (hi > TFW_EEPROM_SIZE || lo < TFW_BOOT_EE_LEN)) {
        return "the part was written outside the application's span";
    }
    if (cmd != TFW_CMD_EEPROM_WRITE || st != TFW_OK) {
        if (count || memcmp(ee, before, TFW_EEPROM_SIZE) != 0) {
            return "a call that wrote nothing changed the part";
        }
        return NULL;
    }
    addr = (unsigned)p[0] | ((unsigned)p[1] << 8);
    len  = n - 2u;
    if (len == 0u || len > CHUNK || addr < TFW_BOOT_EE_LEN || addr + len > TFW_EEPROM_SIZE) {
        return "a write outside the contract was accepted";
    }
    for (i = 0u; i < TFW_EEPROM_SIZE; i++) {
        const uint8_t want = (i >= addr && i < addr + len) ? p[2u + i - addr] : before[i];
        if (ee[i] != want) {
            return "the part does not hold what was written where it was written";
        }
    }
    return NULL;
}

/* What the reply must be; NULL when it is. */
static const char *check_reply(uint8_t cmd, const uint8_t *p, unsigned n, tfw_status_t st,
                               const uint8_t *reply, unsigned rlen, unsigned room)
{
    unsigned addr;
    unsigned len;

    if (cmd != TFW_CMD_EEPROM_READ) {
        return (st == TFW_OK && rlen != 0u) ? "a write answered bytes" : NULL;
    }
    if (st != TFW_OK && st != TFW_ERR_TOO_LONG) {
        return NULL;
    }
    if (n != 3u) {
        return "a read of the wrong arity was not refused";
    }
    addr = (unsigned)p[0] | ((unsigned)p[1] << 8);
    len  = p[2];
    if (len > CHUNK || addr >= TFW_EEPROM_SIZE || addr + len > TFW_EEPROM_SIZE) {
        return "a read outside the part was not refused";
    }
    if (st == TFW_ERR_TOO_LONG) {
        return len > room ? NULL : "a read that fitted was called too long";
    }
    if (rlen != len) {
        return "a read answered a different length than it asked";
    }
    if (rlen && memcmp(reply, &before[addr], len) != 0) {
        return "a read answered bytes the part does not hold there";
    }
    return NULL;
}

static tfw_status_t feed_at(uint8_t cmd, const uint8_t *p, unsigned n, unsigned cap, unsigned pre)
{
    uint8_t      *in  = (uint8_t *)malloc(n ? n : 1u);
    uint8_t      *out = (uint8_t *)malloc(cap + GUARD);
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
    setUp();
    memcpy(before, tfw_mock_eeprom(), TFW_EEPROM_SIZE);

    tfw_reader_init(&r, in, (uint8_t)n);
    tfw_writer_init(&w, out, (uint8_t)cap);
    for (i = 0u; i < pre; i++) {
        tfw_write_u8(&w, PRE_BYTE);
    }
    st  = tfw_dispatch(cmd, &r, &w);
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
        fail = check_reply(cmd, p, n, st, out + pre, len - pre, cap - pre);
    }
    if (!fail) {
        fail = check_part(cmd, p, n, st);
    }
    if (fail) {
        printf("status %u, writer %u\n", (unsigned)st, len);
        dump(cmd, p, n, cap, pre);
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

/* Each request twice: into a whole reply buffer, then into a short one that
 * already holds a byte or two. */
static tfw_status_t feed(uint8_t cmd, const uint8_t *p, unsigned n)
{
    const tfw_status_t st  = feed_at(cmd, p, n, TFW_MAX_PAYLOAD, 0u);
    const unsigned     cap = rnd() % (CHUNK + 3u);
    (void)feed_at(cmd, p, n, cap, cap ? rnd() % (cap < 3u ? cap + 1u : 3u) : 0u);
    return st;
}

static void random_fill(uint8_t *p, unsigned n)
{
    unsigned i;
    for (i = 0u; i < n; i++) {
        p[i] = (uint8_t)rnd();
    }
}

/* The seeds are the requests test_storage sends. */
static const uint8_t read_seed[3]  = { 0x00u, 0x01u, 4u };                           /* 0x100, 4 */
static const uint8_t write_seed[6] = { 0x00u, 0x01u, 0xDEu, 0xADu, 0xBEu, 0xEFu };   /* 0x100 */

/* Addresses around every edge the handler checks. */
static const unsigned addrs[] = {
    0u, 1u, TFW_BOOT_EE_LEN - 8u, TFW_BOOT_EE_LEN - 1u, TFW_BOOT_EE_LEN, TFW_BOOT_EE_LEN + 1u,
    0x100u, TFW_EEPROM_SIZE - CHUNK - 1u, TFW_EEPROM_SIZE - CHUNK, TFW_EEPROM_SIZE - 1u,
    TFW_EEPROM_SIZE, TFW_EEPROM_SIZE + 1u, 0x7FFFu, 0x8000u, 0xFFF8u, 0xFFFFu,
};
#define NADDRS (sizeof addrs / sizeof addrs[0])

static void test_every_address_and_length_on_a_read(void)
{
    uint8_t  p[3];
    unsigned a;
    unsigned len;

    start(0x9E3779B9u);
    for (a = 0u; a < NADDRS; a++) {
        for (len = 0u; len < 256u; len++) {
            p[0] = (uint8_t)(addrs[a] & 0xFFu);
            p[1] = (uint8_t)(addrs[a] >> 8);
            p[2] = (uint8_t)len;
            feed(TFW_CMD_EEPROM_READ, p, 3u);
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u * NADDRS * 256u, ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("read fields");
}

static void test_every_address_and_length_on_a_write(void)
{
    uint8_t  p[LIMIT + 2u];
    unsigned a;
    unsigned len;

    start(0x7F4A7C15u);
    for (a = 0u; a < NADDRS; a++) {
        for (len = 0u; len + 2u <= LIMIT; len++) {
            p[0] = (uint8_t)(addrs[a] & 0xFFu);
            p[1] = (uint8_t)(addrs[a] >> 8);
            random_fill(&p[2], len);
            feed(TFW_CMD_EEPROM_WRITE, p, 2u + len);
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u * NADDRS * (LIMIT - 1u), ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("write fields");
}

static void mutate(uint8_t cmd, const uint8_t *seed, unsigned sn)
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
        feed(cmd, buf, n);
    }
    /* all 0x00 and all 0xFF at every length */
    for (n = 0u; n <= LIMIT; n++) {
        memset(buf, 0x00, n);
        feed(cmd, buf, n);
        memset(buf, 0xFF, n);
        feed(cmd, buf, n);
    }
    /* every single bit flipped */
    for (b = 0u; b < sn * 8u; b++) {
        memcpy(buf, seed, sn);
        buf[b / 8u] ^= (uint8_t)(1u << (b % 8u));
        feed(cmd, buf, sn);
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
        feed(cmd, buf, n);
    }
    /* pure random payloads of every length */
    for (n = 0u; n <= LIMIT; n++) {
        for (k = 0u; k < 4u; k++) {
            random_fill(buf, n);
            feed(cmd, buf, n);
        }
    }
}

static void test_mutated_reads(void)
{
    start(0x2545F491u);
    mutate(TFW_CMD_EEPROM_READ, read_seed, sizeof read_seed);
    TEST_ASSERT_EQUAL_UINT(2u * ((LIMIT + 1u) * 3u + 24u + RANDOM_RUNS + (LIMIT + 1u) * 4u), ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("mutated READ");
}

static void test_mutated_writes(void)
{
    start(0x6C8E9CF5u);
    mutate(TFW_CMD_EEPROM_WRITE, write_seed, sizeof write_seed);
    TEST_ASSERT_EQUAL_UINT(2u * ((LIMIT + 1u) * 3u + 48u + RANDOM_RUNS + (LIMIT + 1u) * 4u), ran);
    TEST_ASSERT_TRUE(ok_runs > 0u);
    TEST_ASSERT_TRUE(refused_runs > 0u);
    report("mutated WRITE");
}

/* After all that, the clean requests answer as test_storage pins them. */
static void test_the_clean_requests_afterwards(void)
{
    const uint8_t data[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    uint8_t       out[TFW_MAX_PAYLOAD];
    tfw_reader_t  r;
    tfw_writer_t  w;

    tfw_mock_reset();
    tfw_reader_init(&r, write_seed, (uint8_t)sizeof write_seed);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dispatch(TFW_CMD_EEPROM_WRITE, &r, &w));
    tfw_reader_init(&r, read_seed, (uint8_t)sizeof read_seed);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_dispatch(TFW_CMD_EEPROM_READ, &r, &w));
    TEST_ASSERT_EQUAL_UINT8(4u, tfw_writer_len(&w));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, out, 4u);

    tfw_reader_init(&r, (const uint8_t[]){ 0xBFu, 0x00u, 1, 2, 3, 4, 5, 6, 7, 8 }, 10u);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    TEST_ASSERT_EQUAL(TFW_ERR_PROTECTED_REGION, tfw_dispatch(TFW_CMD_EEPROM_WRITE, &r, &w));
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_eeprom()[0x0BF]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, tfw_mock_eeprom()[0x0C0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_address_and_length_on_a_read);
    RUN_TEST(test_every_address_and_length_on_a_write);
    RUN_TEST(test_mutated_reads);
    RUN_TEST(test_mutated_writes);
    RUN_TEST(test_the_clean_requests_afterwards);
    return UNITY_END();
}
