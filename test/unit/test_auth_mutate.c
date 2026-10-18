/* test_auth_mutate.c -- the AUTH handlers and the commands they gate, fed
 * mutated requests and requests out of order, against the ATECC608A model,
 * with test_auth_session's link.
 *
 * Deterministic: xorshift32 from a literal seed, fixed counts. Handlers are
 * called directly, as test_auth_session calls them. Each request is copied to
 * a heap buffer of exactly its length and answered into one of exactly
 * TFW_MAX_PAYLOAD, so under `make test-asan` an overrun is a report.
 *
 * What it pins, for every request:
 *   - the call returns, with a status include/tfw/status.h names;
 *   - AUTH_RESPONSE grants a session only for a signature the operator's key
 *     verifies over the outstanding challenge -- checked with the model's own
 *     P-256 verify, so a malleated signature that still verifies is not a
 *     false alarm and one that does not is a fault;
 *   - without a session, a reserved-region EEPROM_READ and RESET mode 1 are
 *     refused AUTH_REQUIRED and no reset is scheduled; RESET mode 0 is never
 *     gated.
 * After the run test_auth_session's authentication still opens the gate. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "auth_session.h"
#include "storage.h"
#include "sys.h"
#include "atecc608a_model.h"
#include "tfw/config.h"
#include "tfw/protocol.h"
#include "tfw/status.h"
#include "tfw/hal.h"
#include "tfw/crypto.h"
#include "tfw/boot.h"
#include "boot_platform.h"
#include "platform_id.h"
#include "handoff.h"
#include "sha256.h"

#define PAYLOAD_LIMIT    (TFW_MAX_PAYLOAD + 1u)
#define RESPONSE_MUTANTS 300u   /* bit-flip mutants of a genuine response */
#define GATED_MUTANTS    300u   /* mutants of each gated request, per state */
#define RANDOM_PER_LEN   2u     /* pure random payloads per length, per command */
#define WALKS            40u    /* random-order sequences */
#define WALK_STEPS       30u

/* test_auth_session's HAL stand-ins: the element model is the I2C HAL here. */
tfw_ms_t     tfw_hal_millis(void)              { return 0u; }
uint32_t    tfw_hal_free_ram(void)            { return 0u; }
tfw_reset_cause_t tfw_hal_reset_cause(void)    { return TFW_RESET_UNKNOWN; }
uint8_t     tfw_hal_uart_tx_idle(void)        { return 1u; }
void        tfw_hal_reset(void)               {}

static uint8_t test_eeprom[TFW_EEPROM_SIZE];
static uint8_t test_handoff[TFW_BOOT_HANDOFF_LEN];

uint8_t *tfw_hal_handoff_block(void)
{
    return test_handoff;
}

tfw_status_t tfw_hal_eeprom_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    if ((uint16_t)(addr + len) > sizeof test_eeprom) { return TFW_ERR_RANGE; }
    memcpy(buf, &test_eeprom[addr], len);
    return TFW_OK;
}

tfw_status_t tfw_hal_eeprom_write(uint16_t addr, const uint8_t *data, uint8_t len)
{
    if ((uint16_t)(addr + len) > sizeof test_eeprom) { return TFW_ERR_RANGE; }
    memcpy(&test_eeprom[addr], data, len);
    return TFW_OK;
}

tfw_status_t tfw_hal_nvm_read(uint16_t addr, uint8_t *buf, uint8_t len)
{
    return tfw_hal_eeprom_read(addr, buf, len);
}

static const uint8_t SERIAL[TFW_BOOT_SERIAL_LEN] = {
    0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11,
    0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11
};
static const uint8_t OPERATOR_PRIV[32] = {
    0x4F,0x0C,0x91,0x37,0xA2,0x5E,0x68,0xD3,0x1B,0x74,0xC0,0x29,0xE8,0x56,0x3D,0xAA,
    0x02,0x9B,0x61,0xF4,0x8E,0x17,0xC5,0x73,0xDA,0x40,0x2C,0xB9,0x85,0x1E,0x6F,0x30
};
static const uint8_t STRANGER_PRIV[32] = {
    0x91,0x3A,0x5C,0xE7,0x08,0xB2,0x6D,0x44,0xF1,0x27,0x9E,0x80,0x35,0xCB,0x1A,0x62,
    0x7D,0x0F,0xA9,0x53,0xE4,0x16,0xBC,0x29,0x70,0x8A,0x3E,0xD5,0x41,0xF8,0x0B,0x96
};
static uint8_t operator_pub[TFW_BOOT_OPERATOR_PUBKEY_LEN];
static uint8_t pristine[TFW_EEPROM_SIZE];

/* What the oracle knows of the session, from the statuses alone. */
static uint8_t  session;        /* a response was granted since the last challenge */
static uint8_t  pending;        /* a challenge is outstanding, with this nonce */
static uint8_t  nonce[TFW_AUTH_NONCE_LEN];
static uint8_t  old_nonce[TFW_AUTH_NONCE_LEN];
static uint8_t  have_old;

/* The last signature made, by the nonce it was made over: the model's RNG
 * restarts on each reset, so nonces repeat and signing once is enough. */
static uint8_t  cached_nonce[TFW_AUTH_NONCE_LEN];
static uint8_t  cached_sig[TFW_AUTH_SIG_LEN];
static uint8_t  cached;

static uint32_t rng;

enum { K_CHALLENGE, K_RESPONSE, K_READ, K_RESET, NCMDS };
static const uint8_t ids[NCMDS] = {
    TFW_CMD_AUTH_CHALLENGE, TFW_CMD_AUTH_RESPONSE, TFW_CMD_EEPROM_READ, TFW_CMD_RESET
};
typedef struct {
    unsigned runs, ok, refused;
    uint32_t seen;
} tally_t;
static tally_t tally[NCMDS];

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void random_fill(uint8_t *p, unsigned n)
{
    unsigned i;
    for (i = 0u; i < n; i++) {
        p[i] = (uint8_t)rnd();
    }
}

/* setUp's state, without recomputing the operator key or the platform block. */
static void fresh(void)
{
    tfw_model_reset();
    tfw_model_provision();
    memcpy(test_eeprom, pristine, sizeof test_eeprom);
    tfw_platform_id_init();
    memset(test_handoff, 0, sizeof test_handoff);
    tfw_boot_handoff_init();
    tfw_crypto_init();
    tfw_auth_session_init();
    tfw_sys_reset_state();
    session = 0u;
    pending = 0u;
    have_old = 0u;
}

void setUp(void)
{
    tfw_boot_platform_t plat;

    tfw_model_reset();
    TEST_ASSERT_TRUE(tfw_model_p256_public(OPERATOR_PRIV, operator_pub));
    memset(&plat, 0, sizeof plat);
    memcpy(plat.serial, SERIAL, sizeof SERIAL);
    memcpy(plat.operator_pubkey, operator_pub, sizeof operator_pub);
    memset(pristine, 0, sizeof pristine);
    tfw_boot_platform_serialise(&plat, pristine);
    fresh();
}
void tearDown(void) {}

static void auth_digest(const uint8_t n[TFW_AUTH_NONCE_LEN], uint8_t out[32])
{
    static const char domain[] = "tfw-auth-v1";
    uint8_t           msg[sizeof domain - 1u + TFW_BOOT_SERIAL_LEN + TFW_AUTH_NONCE_LEN];

    memcpy(msg, domain, sizeof domain - 1u);
    memcpy(msg + sizeof domain - 1u, SERIAL, TFW_BOOT_SERIAL_LEN);
    memcpy(msg + sizeof domain - 1u + TFW_BOOT_SERIAL_LEN, n, TFW_AUTH_NONCE_LEN);
    tfw_sha256(msg, (uint16_t)sizeof msg, out);
}

static void sign(const uint8_t priv[32], const uint8_t n[TFW_AUTH_NONCE_LEN], uint8_t sig[TFW_AUTH_SIG_LEN])
{
    uint8_t digest[32];

    if (priv == OPERATOR_PRIV && cached && memcmp(cached_nonce, n, sizeof cached_nonce) == 0) {
        memcpy(sig, cached_sig, TFW_AUTH_SIG_LEN);
        return;
    }
    auth_digest(n, digest);
    TEST_ASSERT_TRUE(tfw_model_p256_sign(priv, digest, sig));
    if (priv == OPERATOR_PRIV) {
        memcpy(cached_nonce, n, sizeof cached_nonce);
        memcpy(cached_sig, sig, TFW_AUTH_SIG_LEN);
        cached = 1u;
    }
}

static void dump(uint8_t cmd, const uint8_t *p, unsigned n)
{
    unsigned i;
    printf("cmd 0x%02x payload[%u]:", cmd, n);
    for (i = 0u; i < n; i++) {
        printf(" %02x", p[i]);
    }
    printf("\n");
}

/* One request to its handler, checked against every contract above, and the
 * oracle's view of the session moved by its status. */
static tfw_status_t feed(unsigned k, const uint8_t *payload, unsigned n)
{
    uint8_t      *in  = (uint8_t *)malloc(n ? n : 1u);
    uint8_t      *out = (uint8_t *)malloc(TFW_MAX_PAYLOAD);
    const uint8_t was_session = session;
    const uint8_t was_pending = pending;
    tfw_reader_t  r;
    tfw_writer_t  w;
    tfw_status_t  st;
    unsigned      len;
    const char   *broke = NULL;
    uint8_t       digest[32];

    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_NOT_NULL(out);
    if (n) { memcpy(in, payload, n); }
    tfw_reader_init(&r, in, (uint8_t)n);
    tfw_writer_init(&w, out, (uint8_t)TFW_MAX_PAYLOAD);

    switch (k) {
    case K_CHALLENGE: st = tfw_auth_cmd_challenge(&r, &w); break;
    case K_RESPONSE:  st = tfw_auth_cmd_response(&r, &w);  break;
    case K_READ:      st = tfw_storage_cmd_read(&r, &w);   break;
    default:          st = tfw_sys_cmd_reset(&r, &w);      break;
    }
    len = tfw_writer_len(&w);

    if ((unsigned)st > (unsigned)TFW_ERR_NO_STORE) {
        broke = "a status status.h does not name";
    } else if (len > TFW_MAX_PAYLOAD) {
        broke = "the reply outgrew its buffer";
    } else if (k == K_RESPONSE && st == TFW_OK) {
        auth_digest(nonce, digest);
        if (!was_pending || n < TFW_AUTH_SIG_LEN
            || !tfw_model_p256_verify(operator_pub, digest, payload)) {
            broke = "a session was granted for a signature the operator did not make";
        }
    } else if (k == K_READ && !was_session && n >= 3u
               && ((unsigned)payload[0] | ((unsigned)payload[1] << 8)) < (unsigned)TFW_BOOT_EE_LEN
               && st == TFW_OK) {
        broke = "a reserved-region read succeeded without a session";
    } else if (k == K_RESET && n >= 1u && payload[0] == 1u && !was_session
               && (st != TFW_ERR_AUTH_REQUIRED || tfw_sys_reset_pending())) {
        broke = "RESET mode 1 without a session was not refused AUTH_REQUIRED";
    } else if (k == K_RESET && n >= 1u && payload[0] == 0u && st != TFW_OK) {
        broke = "RESET mode 0 was refused";
    }
    if (broke) {
        printf("status %u, writer %u, session %u, pending %u\n", (unsigned)st, len,
               (unsigned)was_session, (unsigned)was_pending);
        dump(ids[k], payload, n);
        TEST_FAIL_MESSAGE(broke);
    }
    if (!was_session && (k != K_RESPONSE || st != TFW_OK)
        && tfw_auth_gate() != TFW_ERR_AUTH_REQUIRED) {
        printf("gate %u after\n", (unsigned)tfw_auth_gate());
        dump(ids[k], payload, n);
        TEST_FAIL_MESSAGE("the gate opened without a granted response");
    }

    if (k == K_CHALLENGE && st == TFW_OK) {
        if (pending) { memcpy(old_nonce, nonce, sizeof nonce); have_old = 1u; }
        memcpy(nonce, out, sizeof nonce);
        pending = 1u;
        session = 0u;
    }
    if (k == K_RESPONSE && n >= TFW_AUTH_SIG_LEN && pending) {
        memcpy(old_nonce, nonce, sizeof nonce);
        have_old = 1u;
        pending = 0u;
    }
    if (k == K_RESPONSE && st == TFW_OK) { session = 1u; }
    if (tfw_sys_reset_pending()) { tfw_sys_reset_state(); }

    tally[k].runs++;
    tally[k].seen |= (uint32_t)1u << (unsigned)st;
    if (st == TFW_OK) { tally[k].ok++; } else { tally[k].refused++; }
    free(in);
    free(out);
    return st;
}

static void challenge(void)
{
    TEST_ASSERT_EQUAL(TFW_OK, feed(K_CHALLENGE, NULL, 0u));
}

/* The gated requests the contract names, with nothing granted. */
static void gate_is_closed(void)
{
    const uint8_t read0[3] = { 0u, 0u, 4u };
    const uint8_t mode1[1] = { 1u };

    TEST_ASSERT_FALSE(session);
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, feed(K_READ, read0, 3u));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, feed(K_RESET, mode1, 1u));
}

static void report(void);

/* A fresh part, a challenge, and the operator's genuine answer to it. */
static void genuine(uint8_t sig[TFW_AUTH_SIG_LEN])
{
    fresh();
    challenge();
    sign(OPERATOR_PRIV, nonce, sig);
}

/* A response to the outstanding challenge; if refused, the gate stays shut. */
static void try_response(const uint8_t *p, unsigned n)
{
    if (feed(K_RESPONSE, p, n) != TFW_OK) {
        gate_is_closed();
    }
}

static void test_mutated_responses_never_open_the_gate(void)
{
    uint8_t  sig[TFW_AUTH_SIG_LEN];
    uint8_t  buf[PAYLOAD_LIMIT];
    unsigned n;
    unsigned i;
    unsigned j;

    rng = 0xA17C0DE5u;
    memset(tally, 0, sizeof tally);

    /* the genuine response, each challenge from a fresh part */
    genuine(sig);
    TEST_ASSERT_EQUAL(TFW_OK, feed(K_RESPONSE, sig, TFW_AUTH_SIG_LEN));

    /* truncation at every length, then the signature with bytes appended */
    for (n = 0u; n < TFW_AUTH_SIG_LEN; n++) {
        genuine(sig);
        try_response(sig, n);
    }
    genuine(sig);
    memcpy(buf, sig, TFW_AUTH_SIG_LEN);
    buf[TFW_AUTH_SIG_LEN] = (uint8_t)rnd();
    try_response(buf, PAYLOAD_LIMIT);

    /* every byte set to 0, 0xFF and its own complement: r or s of zero, all
     * ones, or off by a byte */
    for (i = 0u; i < TFW_AUTH_SIG_LEN; i++) {
        const uint8_t vals[3] = { 0u, 0xFFu, 0u };
        for (j = 0u; j < 3u; j++) {
            genuine(sig);
            memcpy(buf, sig, TFW_AUTH_SIG_LEN);
            buf[i] = j == 2u ? (uint8_t)~sig[i] : vals[j];
            if (memcmp(buf, sig, TFW_AUTH_SIG_LEN) != 0) {
                try_response(buf, TFW_AUTH_SIG_LEN);
            }
        }
    }
    /* r or s all zero, all ones, swapped */
    for (j = 0u; j < 3u; j++) {
        genuine(sig);
        memcpy(buf, sig, TFW_AUTH_SIG_LEN);
        if (j == 0u) { memset(buf, 0, 32u); }
        if (j == 1u) { memset(buf + 32u, 0xFF, 32u); }
        if (j == 2u) { memcpy(buf, sig + 32u, 32u); memcpy(buf + 32u, sig, 32u); }
        try_response(buf, TFW_AUTH_SIG_LEN);
    }

    /* bit flips */
    for (j = 0u; j < RESPONSE_MUTANTS; j++) {
        unsigned flips = 1u + rnd() % 4u;
        genuine(sig);
        memcpy(buf, sig, TFW_AUTH_SIG_LEN);
        while (flips--) {
            const uint32_t at = rnd() % (TFW_AUTH_SIG_LEN * 8u);
            buf[at / 8u] ^= (uint8_t)(1u << (at % 8u));
        }
        try_response(buf, TFW_AUTH_SIG_LEN);
    }

    /* the stranger's signature over the right nonce, and the operator's over
     * the wrong one */
    genuine(sig);
    sign(STRANGER_PRIV, nonce, buf);
    try_response(buf, TFW_AUTH_SIG_LEN);
    genuine(sig);
    memcpy(sig, nonce, sizeof nonce);
    sig[0] ^= 1u;
    sign(OPERATOR_PRIV, sig, buf);
    try_response(buf, TFW_AUTH_SIG_LEN);

    /* pure random payloads of every length, to both AUTH commands */
    for (n = 0u; n <= PAYLOAD_LIMIT; n++) {
        for (j = 0u; j < RANDOM_PER_LEN; j++) {
            genuine(sig);
            random_fill(buf, n);
            try_response(buf, n);
            fresh();
            if (rnd() % 8u == 0u) { tfw_model_set_absent(1u); }   /* a silent bus */
            random_fill(buf, n);
            if (feed(K_CHALLENGE, buf, n) != TFW_OK) {
                TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
            }
        }
    }
    report();
    TEST_ASSERT_TRUE(tally[K_CHALLENGE].ok > 0u && tally[K_CHALLENGE].refused > 0u);
    TEST_ASSERT_TRUE(tally[K_RESPONSE].ok > 0u && tally[K_RESPONSE].refused > 0u);
}

static void mutate_gated(unsigned k, uint8_t with_session)
{
    uint8_t  seed[3];
    uint8_t  buf[PAYLOAD_LIMIT];
    uint8_t  sig[TFW_AUTH_SIG_LEN];
    unsigned sn = k == K_READ ? 3u : 1u;
    unsigned n;
    unsigned j;

    seed[0] = (uint8_t)(k == K_READ ? 0u : 1u);
    seed[1] = 0u;
    seed[2] = 4u;
    fresh();
    if (with_session) {
        challenge();
        sign(OPERATOR_PRIV, nonce, sig);
        TEST_ASSERT_EQUAL(TFW_OK, feed(K_RESPONSE, sig, TFW_AUTH_SIG_LEN));
    }

    /* truncation and extension at every length */
    for (n = 0u; n <= PAYLOAD_LIMIT; n++) {
        memcpy(buf, seed, sn);
        if (n > sn) { random_fill(buf + sn, n - sn); }
        feed(k, buf, n);
    }
    /* every byte to every value: every mode, every address byte, every length */
    for (j = 0u; j < sn * 256u; j++) {
        memcpy(buf, seed, sn);
        buf[j / 256u] = (uint8_t)j;
        feed(k, buf, sn);
    }
    /* the reserved region's edge, read across */
    if (k == K_READ) {
        for (j = 0u; j < 8u; j++) {
            buf[0] = (uint8_t)(TFW_BOOT_EE_LEN - 4u + j);
            buf[1] = (uint8_t)((TFW_BOOT_EE_LEN - 4u + j) >> 8);
            buf[2] = 4u;
            feed(k, buf, 3u);
        }
    }
    /* bit flips, and pure random requests */
    for (j = 0u; j < GATED_MUTANTS; j++) {
        const uint32_t at = rnd() % (sn * 8u);
        memcpy(buf, seed, sn);
        buf[at / 8u] ^= (uint8_t)(1u << (at % 8u));
        feed(k, buf, sn);
        n = rnd() % (PAYLOAD_LIMIT + 1u);
        random_fill(buf, n);
        feed(k, buf, n);
    }
}

static void report(void)
{
    unsigned i;
    for (i = 0u; i < NCMDS; i++) {
        printf("cmd 0x%02x: %u runs, %u ok, %u refused, statuses 0x%05lx\n", ids[i],
               tally[i].runs, tally[i].ok, tally[i].refused, (unsigned long)tally[i].seen);
    }
}

static void test_gated_requests_are_refused_without_a_session(void)
{
    rng = 0x9A7ED00Du;
    memset(tally, 0, sizeof tally);
    mutate_gated(K_READ, 0u);
    mutate_gated(K_READ, 1u);
    mutate_gated(K_RESET, 0u);
    mutate_gated(K_RESET, 1u);
    report();
    TEST_ASSERT_TRUE(tally[K_READ].ok > 0u && tally[K_READ].refused > 0u);
    TEST_ASSERT_TRUE(tally[K_RESET].ok > 0u && tally[K_RESET].refused > 0u);
}

/* CHALLENGE, genuine, stale, foreign and mutated RESPONSEs, gated reads and
 * resets, in any order: a response with no challenge, two responses to one,
 * a challenge between a response and its use. */
static void test_auth_in_any_order_keeps_the_gate(void)
{
    uint8_t  p[PAYLOAD_LIMIT];
    unsigned walk;
    unsigned step;
    unsigned n;

    rng = 0x0DDB1A5Eu;
    memset(tally, 0, sizeof tally);
    for (walk = 0u; walk < WALKS; walk++) {
        fresh();
        for (step = 0u; step < WALK_STEPS; step++) {
            switch (rnd() % 9u) {
            case 0u:
            case 1u:
                feed(K_CHALLENGE, NULL, 0u);
                break;
            case 2u:                                        /* genuine, or one bit off */
                sign(OPERATOR_PRIV, nonce, p);
                if (rnd() % 3u == 0u) { p[rnd() % TFW_AUTH_SIG_LEN] ^= (uint8_t)(1u << (rnd() % 8u)); }
                feed(K_RESPONSE, p, TFW_AUTH_SIG_LEN);
                break;
            case 3u:                                        /* stale */
                if (have_old) {
                    sign(OPERATOR_PRIV, old_nonce, p);
                    feed(K_RESPONSE, p, TFW_AUTH_SIG_LEN);
                }
                break;
            case 4u:                                        /* foreign, or noise */
                if (rnd() % 4u == 0u) {
                    sign(STRANGER_PRIV, nonce, p);
                    n = TFW_AUTH_SIG_LEN;
                } else {
                    n = rnd() % (PAYLOAD_LIMIT + 1u);
                    random_fill(p, n);
                }
                feed(K_RESPONSE, p, n);
                break;
            case 5u:
            case 6u: {                                      /* a read either side of the edge */
                const uint16_t a = (uint16_t)(rnd() % (2u * TFW_BOOT_EE_LEN));
                p[0] = (uint8_t)a; p[1] = (uint8_t)(a >> 8); p[2] = (uint8_t)(1u + rnd() % 8u);
                feed(K_READ, p, 3u);
                break;
            }
            default:
                p[0] = (uint8_t)(rnd() % 3u);
                feed(K_RESET, p, 1u);
                break;
            }
        }
        if (walk % 10u == 9u) {                             /* and a silent bus */
            tfw_model_set_absent(1u);
            TEST_ASSERT_NOT_EQUAL(TFW_OK, feed(K_CHALLENGE, NULL, 0u));
            feed(K_RESPONSE, p, TFW_AUTH_SIG_LEN);
        }
    }
    report();
    for (step = 0u; step < NCMDS; step++) {
        TEST_ASSERT_TRUE_MESSAGE(tally[step].ok > 0u, "a command never accepted a request");
        TEST_ASSERT_TRUE_MESSAGE(tally[step].refused > 0u, "a command never refused a request");
    }
}

/* test_auth_session's authentication, after the run. */
static void test_authentication_still_opens_the_gate_after_the_run(void)
{
    const uint8_t read0[3] = { 0u, 0u, 4u };
    uint8_t       sig[TFW_AUTH_SIG_LEN];

    gate_is_closed();
    challenge();
    sign(OPERATOR_PRIV, nonce, sig);
    TEST_ASSERT_EQUAL(TFW_OK, feed(K_RESPONSE, sig, TFW_AUTH_SIG_LEN));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());
    TEST_ASSERT_EQUAL(TFW_OK, feed(K_READ, read0, 3u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mutated_responses_never_open_the_gate);
    RUN_TEST(test_gated_requests_are_refused_without_a_session);
    RUN_TEST(test_auth_in_any_order_keeps_the_gate);
    RUN_TEST(test_authentication_still_opens_the_gate_after_the_run);
    return UNITY_END();
}
