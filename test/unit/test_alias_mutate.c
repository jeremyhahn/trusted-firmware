/*
 * Seeded mutation of ATTEST_CERT's request (tfw_alias_cmd, src/caps/alias.c)
 * on test_alias's link: the software element as the I2C HAL, the ATECC
 * provider as the crypto tier. The requests are the ones test_alias sends --
 * a 32-byte nonce that mints and a one-byte page that fetches -- bit-flipped,
 * truncated and extended to every length, all 0x00, all 0xFF, random, and
 * every page byte, through writers of every capacity with guard bytes behind
 * them. tfw_alias_reset_state() runs between iterations, so whether a
 * certificate stands behind a request is part of what is mutated.
 *
 * The oracle is the command's contract as test_alias pins it: a nonce of
 * exactly TFW_ALIAS_NONCE_LEN mints and answers page 0, a single byte pages a
 * certificate that exists, every other request is TFW_ERR_BAD_ARGC, and a page
 * whose offset is at or past the end is TFW_ERR_RANGE. A refusal writes
 * nothing; nothing is ever written past the writer's capacity. Afterwards the
 * clean requests give the same certificate they gave before the mutations.
 *
 * xorshift32 from a literal seed, fixed counts; every loop asserts it ran.
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "alias.h"
#include "attest.h"
#include "uccs.h"
#include "uccs_vectors.h"
#include "vectors.h"
#include "tfw/app.h"
#include "atecc608a_model.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/types.h"
#include "tfw/hal.h"
#include "tfw/boot.h"
#include "boot_platform.h"
#include "handoff.h"
#include "platform_id.h"

/* The HAL and runner stand-ins test_alias links, as it defines them: no
 * clock, no application, a zeroed platform block and handoff. */
tfw_ms_t tfw_hal_millis(void) { return 0u; }

tfw_status_t tfw_app_runner_evidence(uint8_t out[TFW_APP_EVIDENCE_LEN])
{
    (void)out;
    return TFW_ERR_UNSUPPORTED;
}

static uint8_t test_eeprom[TFW_BOOT_EE_BLOCK_LEN];
static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];

tfw_status_t tfw_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if ((uint16_t)(addr + len) > sizeof test_eeprom) { return TFW_ERR_RANGE; }
    memcpy(buf, &test_eeprom[addr], len);
    return TFW_OK;
}

tfw_status_t tfw_hal_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    return tfw_hal_eeprom_read(addr, buf, len);
}

uint8_t *tfw_hal_handoff_block(void)
{
    return test_handoff;
}

static const uint8_t NONCE[] = TFWV_UCCS_NONCE;

#define CERT_MAX     512u
#define GUARD_LEN    16u
#define GUARD        0xA5u
#define PREFIX       0x5Au
#define RANDOM_RUNS  3000u

static uint32_t rng = 0x6A11A5C3u;

static uint32_t xorshift32(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static uint8_t in[256];
static uint8_t out[TFW_MAX_PAYLOAD + GUARD_LEN];

/* The nonce the standing certificate was minted for, if one stands. */
static uint8_t minted[TFW_ALIAS_NONCE_LEN];
static uint8_t have_minted;
static uint16_t minted_total;

/* The clean certificate and its claim 273, from the first clean pass. */
static uint8_t  base_doc[CERT_MAX];
static uint16_t base_total;
static uint8_t  base_meas[TFW_SHA256_LEN];

/* Windows of the clean certificate compared byte for byte. */
static unsigned pinned_runs;

/* Whether two nonces are the same, a byte at a time: the vector is a string
 * literal with NULs in it, which memcmp is not trusted to be folded over. */
static int same(const uint8_t *a, const uint8_t *b)
{
    unsigned i;

    for (i = 0u; i < TFW_ALIAS_NONCE_LEN; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

void setUp(void)
{
    tfw_model_reset();
    tfw_attest_reset_state();
    tfw_alias_reset_state();
    memset(test_eeprom, 0, sizeof test_eeprom);
    memset(test_handoff, 0, sizeof test_handoff);
    tfw_platform_id_init();
    tfw_boot_handoff_init();
    tfw_model_provision();
    tfw_crypto_init();
    have_minted = 0u;
    pinned_runs = 0u;
}

void tearDown(void) {}

/*
 * Sends the n bytes at req through a writer of capacity cap that already holds
 * pre bytes. Asserts the two contracts every request is held to -- nothing past
 * the capacity, and a refusal leaves the length where it was -- and returns the
 * status, the writer's length and where the command's own bytes start.
 */
static tfw_status_t send(const uint8_t *req, uint8_t n, uint8_t cap, uint8_t pre,
                         uint8_t *len, uint8_t *at)
{
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_status_t st;
    uint8_t      before;
    unsigned     i;

    memset(out, GUARD, sizeof out);
    if (n > 0u) {
        memmove(in, req, n);
    }
    tfw_reader_init(&r, in, n);
    tfw_writer_init(&w, out, cap);
    for (i = 0u; i < pre; i++) {
        tfw_write_u8(&w, PREFIX);
    }
    before = tfw_writer_len(&w);
    st = tfw_alias_cmd(&r, &w);
    for (i = cap; i < sizeof out; i++) {
        if (out[i] != GUARD) {
            TEST_FAIL_MESSAGE("a byte was written past the writer's capacity");
        }
    }
    if (st != TFW_OK) {
        TEST_ASSERT_EQUAL_UINT8(before, tfw_writer_len(&w));
    }
    *len = tfw_writer_len(&w);
    *at  = before;
    return st;
}

/* The status the command must give n bytes at req, from the contract above. */
static tfw_status_t expected(const uint8_t *req, uint8_t n)
{
    if (n == TFW_ALIAS_NONCE_LEN) {
        return TFW_OK;
    }
    if (n != 1u || !have_minted) {
        return TFW_ERR_BAD_ARGC;
    }
    if ((uint16_t)((uint16_t)req[0] * TFW_ALIAS_PAGE) >= minted_total) {
        return TFW_ERR_RANGE;
    }
    return TFW_OK;
}

/*
 * One request against the oracle. A mint or a page that the writer has room
 * for is checked byte for byte: the page byte, the whole length and the
 * window, which for the clean nonce is the clean certificate's.
 */
static void check(const uint8_t *req, uint8_t n, uint8_t cap, uint8_t pre)
{
    uint8_t      copy[256] = {0u};
    uint8_t      len = 0u, at = 0u, page = 0u, want_got;
    uint16_t     total, off;
    tfw_status_t want, st;

    if (n > 0u) {
        memcpy(copy, req, n);
    }
    want = expected(copy, n);
    st   = send(copy, n, cap, pre, &len, &at);
    TEST_ASSERT_EQUAL(want, st);
    if (st != TFW_OK) {
        if (n == TFW_ALIAS_NONCE_LEN) {
            have_minted = 0u;
        }
        return;
    }
    if (n == TFW_ALIAS_NONCE_LEN) {
        memcpy(minted, copy, sizeof minted);
        have_minted = 1u;
    } else {
        page = copy[0];
    }
    if ((unsigned)pre + 3u + TFW_ALIAS_PAGE > cap) {
        if (n == TFW_ALIAS_NONCE_LEN) {
            /* The total is not readable from a short writer; ask page 0 again
             * through a full one to learn it. */
            uint8_t zero = 0u;

            TEST_ASSERT_EQUAL(TFW_OK, send(&zero, 1u, TFW_MAX_PAYLOAD, 0u, &len, &at));
            minted_total = (uint16_t)((uint16_t)out[1] | ((uint16_t)out[2] << 8));
        }
        return;
    }
    total = (uint16_t)((uint16_t)out[at + 1u] | ((uint16_t)out[at + 2u] << 8));
    TEST_ASSERT_EQUAL_UINT8(page, out[at]);
    TEST_ASSERT_TRUE(total > 400u && total < CERT_MAX);
    if (n == TFW_ALIAS_NONCE_LEN) {
        minted_total = total;
    }
    TEST_ASSERT_EQUAL_UINT16(minted_total, total);
    off      = (uint16_t)((uint16_t)page * TFW_ALIAS_PAGE);
    want_got = TFW_ALIAS_PAGE;
    if ((uint16_t)(total - off) < TFW_ALIAS_PAGE) {
        want_got = (uint8_t)(total - off);
    }
    TEST_ASSERT_EQUAL_UINT8(at + 3u + want_got, len);
    if (same(minted, NONCE)) {
        pinned_runs++;
        TEST_ASSERT_EQUAL_UINT16(base_total, total);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(&base_doc[off], &out[at + 3u], want_got);
    }
}

/* claim 273 is served for the nonce minted and refused for every other. */
static void check_measurement(const uint8_t *b)
{
    uint8_t      m[TFW_SHA256_LEN];
    tfw_status_t want = TFW_ERR_BAD_ARGC;

    if (have_minted && same(b, minted)) {
        want = TFW_OK;
    }
    TEST_ASSERT_EQUAL(want, tfw_alias_measurement_for(b, m));
    if (want == TFW_OK) {
        TEST_ASSERT_EQUAL_HEX8_ARRAY(base_meas, m, sizeof m);
    }
}

static void reset_between(void)
{
    tfw_alias_reset_state();
    have_minted = 0u;
}

/* Mints the clean nonce and pages the whole certificate into doc. */
static uint16_t clean_pass(uint8_t *doc)
{
    uint8_t  len = 0u, at = 0u, page;
    uint16_t total, got;

    TEST_ASSERT_EQUAL(TFW_OK, send(NONCE, TFW_ALIAS_NONCE_LEN, TFW_MAX_PAYLOAD, 0u, &len, &at));
    TEST_ASSERT_EQUAL_UINT8(TFW_ALIAS_PAGE + 3u, len);
    TEST_ASSERT_EQUAL_UINT8(0u, out[0]);
    total = (uint16_t)((uint16_t)out[1] | ((uint16_t)out[2] << 8));
    TEST_ASSERT_TRUE(total > 400u && total < CERT_MAX);
    memcpy(doc, &out[3], (size_t)(len - 3u));
    got = (uint16_t)(len - 3u);
    for (page = 1u; got < total; page++) {
        TEST_ASSERT_EQUAL(TFW_OK, send(&page, 1u, TFW_MAX_PAYLOAD, 0u, &len, &at));
        TEST_ASSERT_EQUAL_UINT8(page, out[0]);
        TEST_ASSERT_EQUAL_UINT16(total, (uint16_t)out[1] | ((uint16_t)out[2] << 8));
        memcpy(&doc[got], &out[3], (size_t)(len - 3u));
        got = (uint16_t)(got + (len - 3u));
    }
    TEST_ASSERT_EQUAL_UINT16(total, got);
    /* one SEQUENCE whose length is the rest of the document */
    TEST_ASSERT_EQUAL_HEX8(0x30u, doc[0]);
    TEST_ASSERT_EQUAL_HEX8(0x82u, doc[1]);
    TEST_ASSERT_EQUAL_UINT16(total - 4u, ((uint16_t)doc[2] << 8) | doc[3]);
    memcpy(minted, NONCE, sizeof minted);
    have_minted  = 1u;
    minted_total = total;
    return total;
}

static void baseline(void)
{
    base_total = clean_pass(base_doc);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_alias_measurement_for(NONCE, base_meas));
    reset_between();
}

static void clean_afterwards(void)
{
    uint8_t  doc[CERT_MAX];
    uint16_t total;

    reset_between();
    total = clean_pass(doc);
    TEST_ASSERT_EQUAL_UINT16(base_total, total);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(base_doc, doc, total);
    check_measurement(NONCE);
}

/* ------------------------------------------------------------- the tests */

/* Every bit of the nonce: each still mints, each certificate is a different
 * one of the same shape, and claim 273 answers only the nonce minted. */
static void test_every_bit_flip_of_the_nonce(void)
{
    uint8_t  req[TFW_ALIAS_NONCE_LEN];
    uint8_t  page;
    unsigned bit, runs = 0u;

    baseline();
    for (bit = 0u; bit < TFW_ALIAS_NONCE_LEN * 8u; bit++) {
        reset_between();
        memcpy(req, NONCE, sizeof req);
        req[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        check(req, TFW_ALIAS_NONCE_LEN, TFW_MAX_PAYLOAD, 0u);
        TEST_ASSERT_TRUE(have_minted);
        check_measurement(req);
        check_measurement(NONCE);
        page = (uint8_t)(minted_total / TFW_ALIAS_PAGE);
        check(&page, 1u, TFW_MAX_PAYLOAD, 0u);
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(TFW_ALIAS_NONCE_LEN * 8u, runs);
    clean_afterwards();
}

/* Every length from 0 to 255, the clean nonce first and random bytes after
 * it, with a certificate behind the request and without one. */
static void test_every_truncation_and_extension(void)
{
    uint8_t  req[256];
    uint8_t  doc[CERT_MAX];
    unsigned n, behind, runs = 0u;

    baseline();
    for (n = 0u; n < sizeof req; n++) {
        req[n] = (uint8_t)xorshift32();
    }
    memcpy(req, NONCE, TFW_ALIAS_NONCE_LEN);
    for (behind = 0u; behind < 2u; behind++) {
        for (n = 0u; n < sizeof req; n++) {
            reset_between();
            if (behind) {
                (void)clean_pass(doc);
            }
            check(req, (uint8_t)n, TFW_MAX_PAYLOAD, 0u);
            check_measurement(req);
            runs++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u * 256u, runs);
    clean_afterwards();
}

/* All 0x00 and all 0xFF at every length up to one past the payload. */
static void test_all_zero_and_all_ff(void)
{
    uint8_t  req[TFW_MAX_PAYLOAD + 1u];
    unsigned fill, n, behind, runs = 0u;

    baseline();
    for (fill = 0u; fill < 2u; fill++) {
        memset(req, fill ? 0xFF : 0x00, sizeof req);
        for (behind = 0u; behind < 2u; behind++) {
            for (n = 0u; n <= sizeof req; n++) {
                reset_between();
                if (behind) {
                    uint8_t doc[CERT_MAX];
                    (void)clean_pass(doc);
                }
                check(req, (uint8_t)n, TFW_MAX_PAYLOAD, 0u);
                check_measurement(req);
                runs++;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u * 2u * (TFW_MAX_PAYLOAD + 2u), runs);
    clean_afterwards();
}

/*
 * Every page byte, with the certificate behind it and without: page 0, 1,
 * the last, one past it, the pages whose offset wraps an 8-bit product to 0
 * (64, 128, 192) and 255 are all among them. Past the end is a refusal with
 * no bytes, never a window.
 */
static void test_every_page(void)
{
    uint8_t  doc[CERT_MAX];
    unsigned p, behind, runs = 0u, served = 0u;

    baseline();
    for (behind = 0u; behind < 2u; behind++) {
        reset_between();
        if (behind) {
            (void)clean_pass(doc);
        }
        for (p = 0u; p < 256u; p++) {
            uint8_t page = (uint8_t)p;

            if (expected(&page, 1u) == TFW_OK) {
                served++;
            }
            check(&page, 1u, TFW_MAX_PAYLOAD, 0u);
            runs++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(512u, runs);
    TEST_ASSERT_EQUAL_UINT((base_total + TFW_ALIAS_PAGE - 1u) / TFW_ALIAS_PAGE, served);
    TEST_ASSERT_EQUAL_UINT(served, pinned_runs);
    clean_afterwards();
}

/* The clean mint and every clean page through every writer capacity from 0
 * to the payload, with up to three bytes already in it. */
static void test_every_writer_capacity(void)
{
    uint8_t  doc[CERT_MAX];
    unsigned cap, pre, runs = 0u;

    baseline();
    for (cap = 0u; cap <= TFW_MAX_PAYLOAD; cap++) {
        for (pre = 0u; pre < 4u; pre++) {
            uint8_t page = (uint8_t)(cap % 9u);

            reset_between();
            check(NONCE, TFW_ALIAS_NONCE_LEN, (uint8_t)cap, (uint8_t)pre);
            (void)clean_pass(doc);
            check(&page, 1u, (uint8_t)cap, (uint8_t)pre);
            runs++;
        }
    }
    TEST_ASSERT_EQUAL_UINT((TFW_MAX_PAYLOAD + 1u) * 4u, runs);
    clean_afterwards();
}

/*
 * Random requests: a random length biased toward the two the command accepts,
 * random bytes, a random capacity and prefix, and a certificate behind it or
 * not.
 */
static void test_random_requests(void)
{
    uint8_t  req[256];
    uint8_t  doc[CERT_MAX];
    unsigned i, k, runs = 0u, mints = 0u, pages = 0u;

    baseline();
    for (i = 0u; i < RANDOM_RUNS; i++) {
        uint32_t x = xorshift32();
        uint8_t  n;

        reset_between();
        if ((x & 7u) == 0u) {
            (void)clean_pass(doc);
        }
        switch ((x >> 3) & 3u) {
        case 0u:  n = TFW_ALIAS_NONCE_LEN; break;
        case 1u:  n = 1u; break;
        default:  n = (uint8_t)(x >> 8); break;
        }
        for (k = 0u; k < n; k++) {
            req[k] = (uint8_t)xorshift32();
        }
        if (n == 1u && have_minted) {
            req[0] = (uint8_t)(req[0] % 16u);
            pages++;
        }
        if (n == TFW_ALIAS_NONCE_LEN) {
            mints++;
        }
        check(req, n, (uint8_t)((x >> 16) % (TFW_MAX_PAYLOAD + 1u)), (uint8_t)((x >> 24) & 3u));
        check_measurement(n >= TFW_ALIAS_NONCE_LEN ? req : NONCE);
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(RANDOM_RUNS, runs);
    TEST_ASSERT_TRUE(mints > 0u && pages > 0u);
    clean_afterwards();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_bit_flip_of_the_nonce);
    RUN_TEST(test_every_truncation_and_extension);
    RUN_TEST(test_all_zero_and_all_ff);
    RUN_TEST(test_every_page);
    RUN_TEST(test_every_writer_capacity);
    RUN_TEST(test_random_requests);
    return UNITY_END();
}
