/*
 * Seeded mutation of ATTEST_CSR's request (tfw_csr_cmd, src/caps/csr.c) on
 * test_csr's link: the software element as the I2C HAL, the ATECC provider as
 * the crypto tier. The request is the one test_csr sends -- the 32-byte b an
 * ATTEST_CERT was minted for -- bit-flipped with the certificate minted for
 * the flipped value and for the clean one, truncated and extended to every
 * length, all 0x00, all 0xFF and random, through writers of every capacity
 * with guard bytes behind them. tfw_alias_reset_state() runs between
 * iterations, so whether a certificate stands behind b is part of what is
 * mutated.
 *
 * The oracle is the command's contract as test_csr pins it: exactly
 * TFW_ALIAS_NONCE_LEN bytes naming the certificate last minted are signed, and
 * the signature is the one the element's DeviceID key makes over the request
 * info rebuilt from b, the DeviceID, claim 273 and the key; every other
 * request is TFW_ERR_BAD_ARGC. A refusal writes nothing; nothing is ever
 * written past the writer's capacity. Afterwards the clean request gives the
 * signature it gave before the mutations.
 *
 * xorshift32 from a literal seed, fixed counts; every loop asserts it ran.
 */
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "csr.h"
#include "alias.h"
#include "attest.h"
#include "uccs.h"
#include "uccs_vectors.h"
#include "vectors.h"
#include "atecc608a_model.h"
#include "tfw/config.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "tfw/types.h"
#include "tfw/hal.h"
#include "tfw/boot.h"
#include "handoff.h"
#include "platform_id.h"
#include "tfw/app.h"

/* The HAL and runner stand-ins test_csr links, as it defines them: no clock,
 * no application, a zeroed platform block and handoff. */
tfw_ms_t tfw_hal_millis(void) { return 0u; }

tfw_status_t tfw_app_runner_evidence(uint8_t out[TFW_APP_EVIDENCE_LEN])
{
    (void)out;
    return TFW_ERR_UNSUPPORTED;
}

static uint8_t test_eeprom[TFW_BOOT_EE_LEN];
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

#define GUARD_LEN    16u
#define GUARD        0xA5u
#define PREFIX       0x5Au
#define RANDOM_RUNS  3000u

static uint32_t rng = 0xC5A1F00Du;

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

/* The clean request's signature and claim 273, from the first clean pass. */
static uint8_t base_sig[TFW_SIG_LEN];
static uint8_t base_meas[TFW_SHA256_LEN];

static unsigned signed_runs;

/* Signatures over the clean nonce compared byte for byte. */
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
    signed_runs = 0u;
    pinned_runs = 0u;
}

void tearDown(void) {}

/* ATTEST_CERT for b, page 0 only: what stands a certificate behind b. */
static void mint(const uint8_t *b)
{
    static uint8_t nin[TFW_ALIAS_NONCE_LEN];
    uint8_t        page[TFW_MAX_PAYLOAD];
    tfw_reader_t   r;
    tfw_writer_t   w;

    memcpy(nin, b, sizeof nin);
    tfw_reader_init(&r, nin, sizeof nin);
    tfw_writer_init(&w, page, sizeof page);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_alias_cmd(&r, &w));
    memcpy(minted, b, sizeof minted);
    have_minted = 1u;
}

static void reset_between(void)
{
    tfw_alias_reset_state();
    have_minted = 0u;
}

/*
 * Sends the n bytes at req through a writer of capacity cap that already holds
 * pre bytes. Asserts nothing lands past the capacity and a refusal leaves the
 * length where it was; returns the status, the length and where the command's
 * own bytes start.
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
    st = tfw_csr_cmd(&r, &w);
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

/* One request against the oracle. A signature the writer had room for is
 * verified under the DeviceID key over the request info rebuilt from b, and
 * must be the deterministic one that key makes over it. */
static void check(const uint8_t *req, uint8_t n, uint8_t cap, uint8_t pre)
{
    uint8_t           copy[256];
    uint8_t           len = 0u, at = 0u;
    uint8_t           digest[TFW_SHA256_LEN];
    uint8_t           want_sig[TFW_SIG_LEN];
    tfw_alias_parts_t p;
    tfw_status_t      want = TFW_ERR_BAD_ARGC;

    if (n > 0u) {
        memcpy(copy, req, n);
    }
    if (n == TFW_ALIAS_NONCE_LEN && have_minted &&
        same(copy, minted)) {
        want = TFW_OK;
    }
    TEST_ASSERT_EQUAL(want, send(copy, n, cap, pre, &len, &at));
    if (want != TFW_OK) {
        return;
    }
    signed_runs++;
    if ((unsigned)pre + TFW_SIG_LEN > cap) {
        return;
    }
    TEST_ASSERT_EQUAL_UINT8(at + TFW_SIG_LEN, len);
    if (same(copy, NONCE)) {
        pinned_runs++;
        TEST_ASSERT_EQUAL_HEX8_ARRAY(base_sig, &out[at], TFW_SIG_LEN);
        return;
    }
    p.nonce   = copy;
    p.ueid    = tfw_attest_device_id();
    p.fw_hash = base_meas;
    p.pubkey  = tfw_model_deviceid_public();
    p.sig     = NULL;
    TEST_ASSERT_NOT_NULL(p.pubkey);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_csr_cri_digest(&p, digest));
    TEST_ASSERT_TRUE(tfw_model_p256_verify(p.pubkey, digest, &out[at]));
    TEST_ASSERT_TRUE(tfw_model_p256_sign(tfw_model_deviceid_private(), digest, want_sig));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want_sig, &out[at], TFW_SIG_LEN);
}

/* The clean request, as test_csr makes it: mint for NONCE, then sign. */
static void clean_pass(uint8_t sig[TFW_SIG_LEN])
{
    uint8_t len = 0u, at = 0u;

    reset_between();
    mint(NONCE);
    TEST_ASSERT_EQUAL(TFW_OK, send(NONCE, TFW_ALIAS_NONCE_LEN, TFW_MAX_PAYLOAD, 0u, &len, &at));
    TEST_ASSERT_EQUAL_UINT8(TFW_SIG_LEN, len);
    memcpy(sig, out, TFW_SIG_LEN);
}

static void baseline(void)
{
    tfw_alias_parts_t p;
    uint8_t           digest[TFW_SHA256_LEN];

    clean_pass(base_sig);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_alias_measurement_for(NONCE, base_meas));
    p.nonce   = NONCE;
    p.ueid    = tfw_attest_device_id();
    p.fw_hash = base_meas;
    p.pubkey  = tfw_model_deviceid_public();
    p.sig     = NULL;
    TEST_ASSERT_EQUAL(TFW_OK, tfw_csr_cri_digest(&p, digest));
    TEST_ASSERT_TRUE(tfw_model_p256_verify(p.pubkey, digest, base_sig));
    reset_between();
}

static void clean_afterwards(void)
{
    uint8_t sig[TFW_SIG_LEN];

    clean_pass(sig);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(base_sig, sig, sizeof sig);
}

/* ------------------------------------------------------------- the tests */

/* Every bit of b, with the certificate minted for the flipped value (signed)
 * and for the clean one (refused, and nothing signed). */
static void test_every_bit_flip_of_b(void)
{
    uint8_t  req[TFW_ALIAS_NONCE_LEN];
    unsigned bit, runs = 0u;

    baseline();
    for (bit = 0u; bit < TFW_ALIAS_NONCE_LEN * 8u; bit++) {
        memcpy(req, NONCE, sizeof req);
        req[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        reset_between();
        mint(req);
        check(req, TFW_ALIAS_NONCE_LEN, TFW_MAX_PAYLOAD, 0u);
        reset_between();
        mint(NONCE);
        check(req, TFW_ALIAS_NONCE_LEN, TFW_MAX_PAYLOAD, 0u);
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(TFW_ALIAS_NONCE_LEN * 8u, runs);
    TEST_ASSERT_EQUAL_UINT(TFW_ALIAS_NONCE_LEN * 8u, signed_runs);
    clean_afterwards();
}

/* Every length from 0 to 255, the clean b first and random bytes after it,
 * with the certificate behind it and without. */
static void test_every_truncation_and_extension(void)
{
    uint8_t  req[256];
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
                mint(NONCE);
            }
            check(req, (uint8_t)n, TFW_MAX_PAYLOAD, 0u);
            runs++;
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u * 256u, runs);
    TEST_ASSERT_EQUAL_UINT(1u, signed_runs);
    TEST_ASSERT_EQUAL_UINT(1u, pinned_runs);
    clean_afterwards();
}

/* All 0x00 and all 0xFF at every length to one past the payload, with no
 * certificate, the clean one, and one minted for the fill itself. */
static void test_all_zero_and_all_ff(void)
{
    uint8_t  req[TFW_MAX_PAYLOAD + 1u];
    unsigned fill, n, behind, runs = 0u;

    baseline();
    for (fill = 0u; fill < 2u; fill++) {
        memset(req, fill ? 0xFF : 0x00, sizeof req);
        for (behind = 0u; behind < 3u; behind++) {
            for (n = 0u; n <= sizeof req; n++) {
                reset_between();
                if (behind == 1u) {
                    mint(NONCE);
                } else if (behind == 2u) {
                    mint(req);
                }
                check(req, (uint8_t)n, TFW_MAX_PAYLOAD, 0u);
                runs++;
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT(2u * 3u * (TFW_MAX_PAYLOAD + 2u), runs);
    TEST_ASSERT_EQUAL_UINT(2u, signed_runs);
    clean_afterwards();
}

/* The clean request through every writer capacity from 0 to the payload, with
 * up to three bytes already in it. */
static void test_every_writer_capacity(void)
{
    unsigned cap, pre, runs = 0u;

    baseline();
    mint(NONCE);
    for (cap = 0u; cap <= TFW_MAX_PAYLOAD; cap++) {
        for (pre = 0u; pre < 4u; pre++) {
            check(NONCE, TFW_ALIAS_NONCE_LEN, (uint8_t)cap, (uint8_t)pre);
            runs++;
        }
    }
    TEST_ASSERT_EQUAL_UINT((TFW_MAX_PAYLOAD + 1u) * 4u, runs);
    TEST_ASSERT_EQUAL_UINT(runs, signed_runs);
    clean_afterwards();
}

/*
 * Random requests: a random length biased toward the one the command accepts,
 * random bytes, a random capacity and prefix, and no certificate, a clean one,
 * or one minted for the request itself.
 */
static void test_random_requests(void)
{
    uint8_t  req[256];
    unsigned i, k, runs = 0u;

    baseline();
    for (i = 0u; i < RANDOM_RUNS; i++) {
        uint32_t x = xorshift32();
        uint8_t  n = ((x & 1u) != 0u) ? (uint8_t)TFW_ALIAS_NONCE_LEN : (uint8_t)(x >> 8);

        for (k = 0u; k < n; k++) {
            req[k] = (uint8_t)xorshift32();
        }
        reset_between();
        switch ((x >> 1) & 15u) {
        case 0u:  mint(NONCE); break;
        case 1u:  if (n >= TFW_ALIAS_NONCE_LEN) { mint(req); } break;
        default:  break;
        }
        check(req, n, (uint8_t)((x >> 16) % (TFW_MAX_PAYLOAD + 1u)), (uint8_t)((x >> 24) & 3u));
        runs++;
    }
    TEST_ASSERT_EQUAL_UINT(RANDOM_RUNS, runs);
    TEST_ASSERT_TRUE(signed_runs > 0u);
    clean_afterwards();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_bit_flip_of_b);
    RUN_TEST(test_every_truncation_and_extension);
    RUN_TEST(test_all_zero_and_all_ff);
    RUN_TEST(test_every_writer_capacity);
    RUN_TEST(test_random_requests);
    return UNITY_END();
}
