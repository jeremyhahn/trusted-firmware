/* test_auth_robust.c -- seeded hostile AUTH session sequences, with
 * test_auth_session's link, each request checked for the exact status the
 * wire-protocol row names (docs/wire-protocol/api.md, AUTH_CHALLENGE 0x61 and
 * AUTH_RESPONSE 0x62; docs/security/threat-model.md for what the session gates).
 *
 * Every request goes through request() below, which does what dispatch.c's
 * run() does for these four ids: the commands.def arity check first (BAD_ARGC,
 * handler never called), then the handler, then the writer's sticky overflow
 * (TOO_LONG) and the reader's (BAD_ARGC). dispatch.c itself is not linked: its
 * table names every handler in the image, and this suite links only the auth,
 * storage and sys handlers. The arity below is commands.def's for the ids.
 *
 * Deterministic: xorshift32, one literal seed per walk derived from SEED and
 * the walk index, printed with the step on any failure. Each request is copied
 * to a heap buffer of exactly its length and answered into one of exactly
 * TFW_MAX_PAYLOAD, so under `make test-asan` an overrun is a report.
 *
 * What it pins, after every request:
 *   - the status is the one the row names for the session state the oracle
 *     tracks (no challenge, challenge outstanding, session granted);
 *   - the gate is open exactly when a response the operator signed over the
 *     outstanding challenge was granted since the last challenge or reset;
 *   - a refused response, a replay, a second response, a response with no
 *     challenge, and every malformed request leave no session open;
 *   - a guarded request (EEPROM_READ below the reserved end, RESET mode 1) runs
 *     only inside that session, and a refused one schedules no reset;
 *   - a RESET that runs ends the session, as the device restarting does. */

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
#include "reader.h"
#include "writer.h"

#define SEED        0xA17E5EEDu
#define WALKS       150u
#define WALK_STEPS  48u
#define EE_CHUNK    8u      /* storage.c's TFW_EEPROM_CHUNK: the most one read returns */

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
static uint8_t operator_priv[32];
static uint8_t stranger_priv[32];
static uint8_t stranger_pub[TFW_BOOT_OPERATOR_PUBKEY_LEN];
static uint8_t operator_pub[TFW_BOOT_OPERATOR_PUBKEY_LEN];
static uint8_t pristine[TFW_EEPROM_SIZE];

/* commands.def's arity for the four ids this suite sends. */
typedef struct {
    uint8_t id, lo, hi;
} arity_t;
static const arity_t ARITY[] = {
    { TFW_CMD_AUTH_CHALLENGE, 0u, 0u },
    { TFW_CMD_AUTH_RESPONSE,  TFW_AUTH_SIG_LEN, TFW_AUTH_SIG_LEN },
    { TFW_CMD_EEPROM_READ,    3u, 3u },
    { TFW_CMD_RESET,          1u, 1u },
};

/* The oracle: what the row says the device holds, from the requests alone. */
static uint8_t  session;                        /* granted since the last challenge/reset */
static uint8_t  pending;                        /* a challenge is outstanding ... */
static uint8_t  nonce[TFW_AUTH_NONCE_LEN];      /* ... over this nonce */
static uint8_t  old_nonce[TFW_AUTH_NONCE_LEN];  /* the one before it */
static uint8_t  have_old;
static uint8_t  granted_sig[TFW_AUTH_SIG_LEN];  /* the last response that opened a session */
static uint8_t  have_granted;

/* The genuine signature over the outstanding nonce, made once per nonce. */
static uint8_t  genuine_nonce[TFW_AUTH_NONCE_LEN];
static uint8_t  genuine_sig[TFW_AUTH_SIG_LEN];
static uint8_t  have_genuine;

static uint32_t rng;
static uint32_t walk_seed;
static unsigned walk_no, step_no;
static const char *op_name;

enum { O_CHALLENGE, O_CHALLENGE_ARGS, O_CHALLENGE_SILENT, O_GENUINE, O_FLIPPED, O_ZERO,
       O_OTHER_NONCE, O_STRANGER, O_REPLAY, O_SECOND, O_BAD_LEN, O_READ_GUARDED,
       O_READ_ANY, O_READ_ARGS, O_RESET_1, O_RESET_0, O_RESET_BAD, O_REINIT, NOPS };
static unsigned hits[NOPS];
static unsigned grants, guarded_runs, guarded_refusals;

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

    if (priv == operator_priv && have_genuine && memcmp(genuine_nonce, n, sizeof genuine_nonce) == 0) {
        memcpy(sig, genuine_sig, TFW_AUTH_SIG_LEN);
        return;
    }
    auth_digest(n, digest);
    TEST_ASSERT_TRUE(tfw_model_p256_sign(priv, digest, sig));
    if (priv == operator_priv) {
        memcpy(genuine_nonce, n, sizeof genuine_nonce);
        memcpy(genuine_sig, sig, TFW_AUTH_SIG_LEN);
        have_genuine = 1u;
    }
}

static void fail(const char *why, unsigned want, unsigned got)
{
    printf("seed 0x%08lX walk %u step %u op %s: expected %u, got %u "
           "(session %u, pending %u)\n",
           (unsigned long)walk_seed, walk_no, step_no, op_name, want, got,
           (unsigned)session, (unsigned)pending);
    TEST_FAIL_MESSAGE(why);
}

/* dispatch.c's run() for one of ARITY's ids; the reply's length in *out_len. */
static tfw_status_t request(uint8_t id, const uint8_t *payload, unsigned n,
                            uint8_t *reply, unsigned *out_len)
{
    uint8_t     *in  = (uint8_t *)calloc(n ? n : 1u, 1u);
    uint8_t     *out = (uint8_t *)malloc(TFW_MAX_PAYLOAD);
    const arity_t *a = NULL;
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_status_t st;
    unsigned     i;

    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_TRUE(n <= TFW_MAX_PAYLOAD);
    for (i = 0u; i < sizeof ARITY / sizeof ARITY[0]; i++) {
        if (ARITY[i].id == id) { a = &ARITY[i]; }
    }
    TEST_ASSERT_NOT_NULL(a);
    if (n) { memcpy(in, payload, n); }
    tfw_reader_init(&r, in, (uint8_t)n);
    tfw_writer_init(&w, out, (uint8_t)TFW_MAX_PAYLOAD);

    if (n < a->lo || n > a->hi) {
        st = TFW_ERR_BAD_ARGC;
    } else {
        switch (id) {
        case TFW_CMD_AUTH_CHALLENGE: st = tfw_auth_cmd_challenge(&r, &w); break;
        case TFW_CMD_AUTH_RESPONSE:  st = tfw_auth_cmd_response(&r, &w);  break;
        case TFW_CMD_EEPROM_READ:    st = tfw_storage_cmd_read(&r, &w);   break;
        default:                     st = tfw_sys_cmd_reset(&r, &w);      break;
        }
        if (st == TFW_OK && !tfw_writer_ok(&w)) {
            st = TFW_ERR_TOO_LONG;
        } else if (st == TFW_OK && !tfw_reader_ok(&r)) {
            st = TFW_ERR_BAD_ARGC;
        }
    }
    *out_len = st == TFW_OK ? tfw_writer_len(&w) : 0u;
    if (*out_len > TFW_MAX_PAYLOAD) {
        fail("the reply outgrew its buffer", TFW_MAX_PAYLOAD, *out_len);
    }
    if (reply && *out_len) { memcpy(reply, out, *out_len); }
    free(in);
    free(out);
    return st;
}

static void expect(tfw_status_t want, tfw_status_t got)
{
    if (want != got) {
        fail("the reply status is not the one the row names", (unsigned)want, (unsigned)got);
    }
}

/* What a device restart does to the session: tfw_auth_session_init(). */
static void device_reset(void)
{
    tfw_sys_reset_state();
    tfw_auth_session_init();
    session = 0u;
    pending = 0u;
}

static void fresh(void)
{
    tfw_model_reset();
    tfw_model_provision();
    memcpy(test_eeprom, pristine, sizeof test_eeprom);
    tfw_platform_id_init();
    memset(test_handoff, 0, sizeof test_handoff);
    tfw_boot_handoff_init();
    tfw_crypto_init();
    device_reset();
    have_old = 0u;
    have_granted = 0u;
}

void setUp(void)
{
    tfw_boot_platform_t plat;

    tfw_model_reset();
    tfw_model_p256_key("operator", operator_priv, operator_pub);
    tfw_model_p256_key("stranger", stranger_priv, stranger_pub);
    memset(&plat, 0, sizeof plat);
    memcpy(plat.serial, SERIAL, sizeof SERIAL);
    memcpy(plat.operator_pubkey, operator_pub, sizeof operator_pub);
    memset(pristine, 0, sizeof pristine);
    tfw_boot_platform_serialise(&plat, pristine);
    fresh();
}

void tearDown(void) {}

static void challenge(void)
{
    uint8_t  reply[TFW_MAX_PAYLOAD];
    unsigned len;

    expect(TFW_OK, request(TFW_CMD_AUTH_CHALLENGE, NULL, 0u, reply, &len));
    if (len != TFW_AUTH_NONCE_LEN) {
        fail("the challenge is not a nonce of the wire width", TFW_AUTH_NONCE_LEN, len);
    }
    if (pending) { memcpy(old_nonce, nonce, sizeof nonce); have_old = 1u; }
    memcpy(nonce, reply, sizeof nonce);
    pending = 1u;
    session = 0u;                                   /* a fresh challenge drops the grant */
}

/* A 64-byte AUTH_RESPONSE: granted only for the operator's signature over the
 * outstanding nonce. A malleated signature the operator's key still verifies
 * is the operator's; one it does not verify that opened the gate is the fault. */
static void response(const uint8_t sig[TFW_AUTH_SIG_LEN])
{
    uint8_t      digest[32];
    uint8_t      genuine[TFW_AUTH_SIG_LEN];
    uint8_t      want_ok = 0u;
    tfw_status_t st;
    unsigned     len;

    if (pending) {
        sign(operator_priv, nonce, genuine);        /* the cached one, or made now */
        want_ok = (uint8_t)(memcmp(sig, genuine, TFW_AUTH_SIG_LEN) == 0);
    }
    st = request(TFW_CMD_AUTH_RESPONSE, sig, TFW_AUTH_SIG_LEN, NULL, &len);
    if (st == TFW_OK && !want_ok) {
        auth_digest(nonce, digest);
        if (!pending || !tfw_model_p256_verify(operator_pub, digest, sig)) {
            fail("a session was granted for a response the operator did not sign "
                 "over the outstanding challenge", TFW_ERR_CRYPTO, TFW_OK);
        }
        want_ok = 1u;
    }
    expect(want_ok ? TFW_OK : TFW_ERR_CRYPTO, st);
    if (pending) {                                  /* single-use, granted or not */
        memcpy(old_nonce, nonce, sizeof nonce);
        have_old = 1u;
        pending = 0u;
    }
    if (st == TFW_OK) {
        session = 1u;
        memcpy(granted_sig, sig, TFW_AUTH_SIG_LEN);
        have_granted = 1u;
        grants++;
    }
}

static uint8_t reserved(uint16_t addr, uint8_t len)
{
    return (uint8_t)(addr < TFW_BOOT_EE_LEN
                     || (addr < TFW_BOOT_EE_EXTRA_BASE + TFW_BOOT_EE_EXTRA_LEN
                         && (uint16_t)(addr + len) > TFW_BOOT_EE_EXTRA_BASE));
}

static void read_eeprom(uint16_t addr, uint8_t len)
{
    uint8_t      p[3];
    uint8_t      reply[TFW_MAX_PAYLOAD];
    uint8_t      guarded;
    tfw_status_t want;
    tfw_status_t st;
    unsigned     got;

    p[0] = (uint8_t)addr;
    p[1] = (uint8_t)(addr >> 8);
    p[2] = len;
    guarded = 0u;
    if (len > EE_CHUNK || addr >= TFW_EEPROM_SIZE || len > (uint16_t)(TFW_EEPROM_SIZE - addr)) {
        want = TFW_ERR_RANGE;
    } else if (reserved(addr, len)) {
        guarded = 1u;
        want = session ? TFW_OK : TFW_ERR_AUTH_REQUIRED;
    } else {
        want = TFW_OK;
    }
    st = request(TFW_CMD_EEPROM_READ, p, 3u, reply, &got);
    expect(want, st);
    if (st == TFW_OK && (got != len || memcmp(reply, &test_eeprom[addr], len) != 0)) {
        fail("EEPROM_READ answered other bytes than the part holds", len, got);
    }
    if (guarded) {
        if (st == TFW_OK) { guarded_runs++; } else { guarded_refusals++; }
    }
}

static void reset(uint8_t mode)
{
    tfw_status_t want;
    tfw_status_t st;
    unsigned     len;

    want = mode > 1u ? TFW_ERR_RANGE
         : (mode == 1u && !session) ? TFW_ERR_AUTH_REQUIRED
         : TFW_OK;
    st = request(TFW_CMD_RESET, &mode, 1u, NULL, &len);
    expect(want, st);
    if ((st == TFW_OK) != (tfw_sys_reset_pending() != 0u)) {
        fail("a reset was scheduled by a refused RESET, or not by an accepted one",
             st == TFW_OK ? 1u : 0u, (unsigned)tfw_sys_reset_pending());
    }
    if (mode == 1u && st == TFW_OK) { guarded_runs++; }
    if (mode == 1u && st != TFW_OK) { guarded_refusals++; }
    if (st == TFW_OK) { device_reset(); }
}

static void step(void)
{
    uint8_t  p[TFW_MAX_PAYLOAD];
    uint8_t  other[TFW_AUTH_NONCE_LEN];
    unsigned op = rnd() % NOPS;
    unsigned n;
    unsigned len;

    static const char *const NAMES[NOPS] = {
        "challenge", "challenge+args", "challenge, element silent", "genuine response",
        "one bit flipped", "all-zero response", "response over another nonce",
        "stranger's response", "replay of an earlier grant", "second response",
        "response of the wrong length", "guarded read", "read anywhere",
        "read of the wrong length", "RESET 1", "RESET 0", "RESET bad", "re-init"
    };
    op_name = NAMES[op];
    hits[op]++;

    switch (op) {
    case O_CHALLENGE:
        challenge();
        break;
    case O_CHALLENGE_ARGS:
        n = 1u + rnd() % TFW_MAX_PAYLOAD;
        random_fill(p, n);
        expect(TFW_ERR_BAD_ARGC, request(TFW_CMD_AUTH_CHALLENGE, p, n, NULL, &len));
        break;
    case O_CHALLENGE_SILENT:                        /* the element does not answer */
        tfw_model_set_absent(1u);
        expect(TFW_ERR_CRYPTO, request(TFW_CMD_AUTH_CHALLENGE, NULL, 0u, NULL, &len));
        tfw_model_set_absent(0u);
        if (pending) { memcpy(old_nonce, nonce, sizeof nonce); have_old = 1u; }
        pending = 0u;                               /* auth_session.c: no nonce, no challenge */
        session = 0u;                               /* and the request still drops the grant */
        break;
    case O_GENUINE:
    case O_SECOND:                                  /* genuine twice in a row */
        sign(operator_priv, pending ? nonce : (have_old ? old_nonce : nonce), p);
        response(p);
        if (op == O_SECOND) { response(p); }
        break;
    case O_FLIPPED:
        sign(operator_priv, nonce, p);
        n = rnd() % (TFW_AUTH_SIG_LEN * 8u);
        p[n / 8u] ^= (uint8_t)(1u << (n % 8u));
        response(p);
        break;
    case O_ZERO:
        memset(p, 0, TFW_AUTH_SIG_LEN);
        response(p);
        break;
    case O_OTHER_NONCE:
        if (have_old && rnd() % 2u) {
            memcpy(other, old_nonce, sizeof other);
        } else {
            random_fill(other, sizeof other);
        }
        sign(operator_priv, other, p);
        response(p);
        break;
    case O_STRANGER:
        sign(stranger_priv, nonce, p);
        response(p);
        break;
    case O_REPLAY:
        if (have_granted) {
            memcpy(p, granted_sig, TFW_AUTH_SIG_LEN);
        } else {
            random_fill(p, TFW_AUTH_SIG_LEN);
        }
        response(p);
        break;
    case O_BAD_LEN:                                 /* truncated or extended genuine */
        sign(operator_priv, nonce, p);
        do { n = rnd() % (TFW_MAX_PAYLOAD + 1u); } while (n == TFW_AUTH_SIG_LEN);
        if (n > TFW_AUTH_SIG_LEN) { random_fill(p + TFW_AUTH_SIG_LEN, n - TFW_AUTH_SIG_LEN); }
        expect(TFW_ERR_BAD_ARGC, request(TFW_CMD_AUTH_RESPONSE, p, n, NULL, &len));
        break;                                      /* refused by arity: the challenge stands */
    case O_READ_GUARDED:
        read_eeprom((uint16_t)(rnd() % TFW_BOOT_EE_LEN), (uint8_t)(1u + rnd() % EE_CHUNK));
        break;
    case O_READ_ANY:
        read_eeprom((uint16_t)(rnd() % (TFW_EEPROM_SIZE + 16u)), (uint8_t)(rnd() % (EE_CHUNK + 4u)));
        break;
    case O_READ_ARGS:
        do { n = rnd() % (TFW_MAX_PAYLOAD + 1u); } while (n == 3u);
        random_fill(p, n);
        expect(TFW_ERR_BAD_ARGC, request(TFW_CMD_EEPROM_READ, p, n, NULL, &len));
        break;
    case O_RESET_1:
        reset(1u);
        break;
    case O_RESET_0:
        reset(0u);
        break;
    case O_RESET_BAD:
        if (rnd() % 2u) {
            reset((uint8_t)(2u + rnd() % 254u));
        } else {
            do { n = rnd() % (TFW_MAX_PAYLOAD + 1u); } while (n == 1u);
            memset(p, 1, n);                        /* mode 1 at a wrong length */
            expect(TFW_ERR_BAD_ARGC, request(TFW_CMD_RESET, p, n, NULL, &len));
            if (tfw_sys_reset_pending()) {
                fail("a malformed RESET scheduled a reset", 0u, 1u);
            }
        }
        break;
    default:
        device_reset();
        break;
    }

    if (tfw_auth_gate() != (session ? TFW_OK : TFW_ERR_AUTH_REQUIRED)) {
        fail("the gate does not match the session the row allows",
             session ? TFW_OK : TFW_ERR_AUTH_REQUIRED, (unsigned)tfw_auth_gate());
    }
}

static void test_hostile_session_sequences_keep_the_gate(void)
{
    unsigned i;

    memset(hits, 0, sizeof hits);
    grants = guarded_runs = guarded_refusals = 0u;
    for (walk_no = 0u; walk_no < WALKS; walk_no++) {
        walk_seed = SEED ^ ((uint32_t)walk_no * 0x9E3779B9u);
        rng = walk_seed ? walk_seed : 1u;
        fresh();
        for (step_no = 0u; step_no < WALK_STEPS; step_no++) {
            step();
        }
    }
    printf("%u walks x %u steps: %u grants, %u guarded runs, %u guarded refusals\n",
           WALKS, WALK_STEPS, grants, guarded_runs, guarded_refusals);
    for (i = 0u; i < NOPS; i++) {
        TEST_ASSERT_TRUE_MESSAGE(hits[i] > 0u, "an operation was never generated");
    }
    TEST_ASSERT_TRUE_MESSAGE(grants > 0u, "no session was ever granted");
    TEST_ASSERT_TRUE_MESSAGE(guarded_runs > 0u, "no guarded request ever ran");
    TEST_ASSERT_TRUE_MESSAGE(guarded_refusals > 0u, "no guarded request was ever refused");
}

/* The fixed sequences the walk reaches only by chance, each step's status named. */
static void test_named_hostile_sequences(void)
{
    uint8_t  p[TFW_AUTH_SIG_LEN];
    uint8_t  first[TFW_AUTH_NONCE_LEN];
    unsigned len;

    walk_seed = 0u;
    walk_no = 0u;
    step_no = 0u;
    op_name = "named";

    /* A response before any challenge: refused, nothing opened. */
    random_fill(p, sizeof p);
    response(p);
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());

    /* A second challenge mid-session kills the first's nonce. */
    challenge();
    memcpy(first, nonce, sizeof first);
    challenge();
    sign(operator_priv, first, p);
    response(p);                                    /* CRYPTO unless the nonces collide */
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());

    /* Granted, then a second response to the same challenge changes nothing. */
    challenge();
    sign(operator_priv, nonce, p);
    response(p);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());
    expect(TFW_ERR_CRYPTO, request(TFW_CMD_AUTH_RESPONSE, p, sizeof p, NULL, &len));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());

    /* A challenge drops the grant; the old response does not restore it. */
    challenge();
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
    response(granted_sig);
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());

    /* A truncated response leaves the challenge standing; the genuine one
     * then still opens the gate. */
    challenge();
    sign(operator_priv, nonce, p);
    expect(TFW_ERR_BAD_ARGC, request(TFW_CMD_AUTH_RESPONSE, p, TFW_AUTH_SIG_LEN - 1u, NULL, &len));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
    response(p);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());
    reset(1u);                                      /* runs, and the restart ends the session */
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
    reset(1u);                                      /* AUTH_REQUIRED, nothing scheduled */
}

static void test_failed_challenge_drops_the_session(void)
{
    uint8_t  p[TFW_AUTH_SIG_LEN];
    uint8_t  first[TFW_AUTH_NONCE_LEN];
    unsigned len;

    walk_seed = 0u;
    walk_no = 0u;
    step_no = 0u;
    op_name = "failed challenge";

    challenge();
    memcpy(first, nonce, sizeof first);
    sign(operator_priv, first, p);
    response(p);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());

    /* The element does not answer the next challenge: the request still
     * ends the session, and the old nonce opens nothing. */
    tfw_model_set_absent(1u);
    expect(TFW_ERR_CRYPTO, request(TFW_CMD_AUTH_CHALLENGE, NULL, 0u, NULL, &len));
    tfw_model_set_absent(0u);
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
    expect(TFW_ERR_CRYPTO, request(TFW_CMD_AUTH_RESPONSE, p, sizeof p, NULL, &len));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

/* ---- each refusal, called directly: the status, and nothing past it ---- */

static tfw_status_t direct_challenge(uint8_t out[TFW_AUTH_NONCE_LEN], unsigned *written)
{
    uint8_t      buf[TFW_MAX_PAYLOAD];
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_status_t st;

    memset(buf, 0xEEu, sizeof buf);
    tfw_reader_init(&r, buf, 0u);
    tfw_writer_init(&w, buf, (uint8_t)sizeof buf);
    st = tfw_auth_cmd_challenge(&r, &w);
    *written = tfw_writer_len(&w);
    if (out) { memcpy(out, buf, TFW_AUTH_NONCE_LEN); }
    return st;
}

static tfw_status_t direct_response(const uint8_t *sig, uint8_t n, unsigned *written)
{
    uint8_t      in[TFW_AUTH_SIG_LEN];
    uint8_t      out[TFW_MAX_PAYLOAD];
    tfw_reader_t r;
    tfw_writer_t w;
    tfw_status_t st;

    memset(in, 0, sizeof in);
    memcpy(in, sig, n);
    tfw_reader_init(&r, in, n);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    st = tfw_auth_cmd_response(&r, &w);
    *written = tfw_writer_len(&w);
    return st;
}

/* A challenge the element will not answer writes no nonce: a host must never
 * be handed bytes that merely look like one. */
static void test_a_challenge_the_element_refuses_writes_no_nonce(void)
{
    unsigned written = 99u;

    tfw_model_set_absent(1u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, direct_challenge(NULL, &written));
    tfw_model_set_absent(0u);
    TEST_ASSERT_EQUAL_UINT(0u, written);
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

/* A response too short for a signature is refused before it touches the
 * challenge: the outstanding nonce stays answerable, and nothing is written. */
static void test_a_short_response_is_refused_and_the_challenge_stays_outstanding(void)
{
    uint8_t  n[TFW_AUTH_NONCE_LEN];
    uint8_t  sig[TFW_AUTH_SIG_LEN];
    unsigned written = 99u;

    TEST_ASSERT_EQUAL(TFW_OK, direct_challenge(n, &written));
    sign(operator_priv, n, sig);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, direct_response(sig, TFW_AUTH_SIG_LEN - 1u, &written));
    TEST_ASSERT_EQUAL_UINT(0u, written);
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
    TEST_ASSERT_EQUAL(TFW_OK, direct_response(sig, TFW_AUTH_SIG_LEN, &written));
    TEST_ASSERT_EQUAL(TFW_OK, tfw_auth_gate());
}

/* A signature the element rejects is refused, writes nothing, and spends the
 * challenge: the genuine signature over the same nonce opens nothing after. */
static void test_a_rejected_signature_writes_nothing_and_spends_the_challenge(void)
{
    uint8_t  n[TFW_AUTH_NONCE_LEN];
    uint8_t  bad[TFW_AUTH_SIG_LEN];
    uint8_t  good[TFW_AUTH_SIG_LEN];
    unsigned written = 99u;

    TEST_ASSERT_EQUAL(TFW_OK, direct_challenge(n, &written));
    sign(stranger_priv, n, bad);
    sign(operator_priv, n, good);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, direct_response(bad, TFW_AUTH_SIG_LEN, &written));
    TEST_ASSERT_EQUAL_UINT(0u, written);
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, direct_response(good, TFW_AUTH_SIG_LEN, &written));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

/* An element that stops answering between challenge and response fails the
 * verify: the status is the element's, the gate stays shut, and the challenge
 * is spent all the same. */
static void test_an_element_lost_before_the_verify_refuses_and_spends_the_challenge(void)
{
    uint8_t      n[TFW_AUTH_NONCE_LEN];
    uint8_t      sig[TFW_AUTH_SIG_LEN];
    unsigned     written = 99u;
    tfw_status_t st;

    TEST_ASSERT_EQUAL(TFW_OK, direct_challenge(n, &written));
    sign(operator_priv, n, sig);
    tfw_model_set_absent(1u);
    st = direct_response(sig, TFW_AUTH_SIG_LEN, &written);
    tfw_model_set_absent(0u);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, st);
    TEST_ASSERT_EQUAL_UINT(0u, written);
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, direct_response(sig, TFW_AUTH_SIG_LEN, &written));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, tfw_auth_gate());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_named_hostile_sequences);
    RUN_TEST(test_failed_challenge_drops_the_session);
    RUN_TEST(test_hostile_session_sequences_keep_the_gate);
    RUN_TEST(test_a_challenge_the_element_refuses_writes_no_nonce);
    RUN_TEST(test_a_short_response_is_refused_and_the_challenge_stays_outstanding);
    RUN_TEST(test_a_rejected_signature_writes_nothing_and_spends_the_challenge);
    RUN_TEST(test_an_element_lost_before_the_verify_refuses_and_spends_the_challenge);
    return UNITY_END();
}
