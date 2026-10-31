/*
 * The OPTIGA Trust M crypto tier (row optiga-trust-m): optiga_crypto.c on
 * the real command layer, optiga_cmd.c, with the transport faked here at the
 * APDU. Each request is the Solution Reference Manual's table, Revision 3.70
 * (OpenApplication §4.4.1.1 Table 40, GetDataObject §4.4.1.3 Table 42,
 * GetRandom §4.4.1.6 Table 45, CalcSign §4.4.1.12 Table 51, VerifySign
 * §4.4.1.13 Table 52), compared byte
 * for byte, so flipping any byte the provider or the layer sends fails.
 *
 * As in test_optiga_cmd.c, every Cmd sent is checked against the commands
 * that write no data object, and a call the tier does not support must send
 * nothing at all.
 */
#include <string.h>
#include "unity.h"
#include "optiga_cmd.h"
#include "optiga_i2c.h"
#include "tfw/crypto.h"
#include "tfw/protocol.h"
#include "../../src/core/handoff.h"
#include "../../src/core/secure_zero.h"

typedef struct {
    const uint8_t *q;
    uint16_t       qlen;
    const uint8_t *a;
    uint16_t       alen;
    tfw_status_t   st;
} exchange_t;

#define EX(q, a)     { (q), (uint16_t)sizeof (q), (a), (uint16_t)sizeof (a), TFW_OK }
#define EX_ST(q, st) { (q), (uint16_t)sizeof (q), 0, 0u, (st) }
#define PLAY(s)      play((s), (unsigned)(sizeof (s) / sizeof (s)[0]))

static const exchange_t *script;
static unsigned          steps;
static unsigned          at;
static unsigned          cmds_seen;
static tfw_status_t      open_st;
static unsigned          opens;
static int               handoff_has_cdi;
static unsigned          takes;
static unsigned          zero_calls;
static size_t            zero_len[8];
static uint8_t           zero_before[8][32];
static uint8_t           out[80];
static uint8_t           alg;

static const uint8_t allowed[][2] = {
    { 0xF0u, 0x00u },  /* OpenApplication, clean context */
    { 0x81u, 0x00u },  /* GetDataObject, data */
    { 0x01u, 0x00u },  /* GetDataObject of the last error code, no flush */
    { 0x8Cu, 0x00u },  /* GetRandom, TRNG */
    { 0xB1u, 0x11u },  /* CalcSign, ECDSA */
    { 0xB2u, 0x11u },  /* VerifySign, ECDSA */
};

tfw_status_t tfw_optiga_open(void)
{
    opens++;
    return open_st;
}

tfw_status_t tfw_optiga_transceive(const uint8_t *apdu, uint16_t alen,
                                   uint8_t *rsp, uint16_t rcap, uint16_t *rlen,
                                   uint16_t exec_ms)
{
    const exchange_t *e;
    unsigned          i;
    int               ok = 0;

    TEST_ASSERT_TRUE_MESSAGE(alen >= 4u, "an APDU shorter than its header");
    for (i = 0u; i < sizeof allowed / sizeof allowed[0]; i++) {
        if (apdu[0] == allowed[i][0] && apdu[1] == allowed[i][1]) {
            cmds_seen |= 1u << i;
            ok = 1;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(ok, "a command code or Param outside the read-only set");
    TEST_ASSERT_EQUAL_UINT16(TFW_OPTIGA_EXEC_MS, exec_ms);
    TEST_ASSERT_TRUE_MESSAGE(at < steps, "an APDU the script does not hold");
    e = &script[at++];
    TEST_ASSERT_EQUAL_UINT16(e->qlen, alen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(e->q, apdu, alen);
    if (e->st != TFW_OK) {
        return e->st;
    }
    if (e->alen > rcap) {
        return TFW_ERR_TOO_LONG;
    }
    memcpy(rsp, e->a, e->alen);
    *rlen = e->alen;
    return TFW_OK;
}

/* A CDI tfw-boot might have handed off; any value not all zero will do. */
uint8_t tfw_boot_handoff_take_cdi(uint8_t cdi[TFW_BOOT_MEASURE_LEN])
{
    takes++;
    if (!handoff_has_cdi) {
        return 0u;
    }
    memset(cdi, 0x5A, TFW_BOOT_MEASURE_LEN);
    return 1u;
}

void tfw_secure_zero(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    size_t            i;

    if (zero_calls < 8u) {
        zero_len[zero_calls] = n;
        memcpy(zero_before[zero_calls], p, n < 32u ? n : 32u);
    }
    zero_calls++;
    for (i = 0u; i < n; i++) {
        v[i] = 0u;
    }
}

static void play(const exchange_t *s, unsigned n)
{
    script = s;
    steps  = n;
    at     = 0u;
}

static void played(void)
{
    TEST_ASSERT_EQUAL_UINT_MESSAGE(steps, at, "the script was not played out");
}

static const uint8_t Q_OPEN[] = {
    0xF0, 0x00, 0x00, 0x10,
    0xD2, 0x76, 0x00, 0x00, 0x04,                                     /* RID */
    0x47, 0x65, 0x6E, 0x41, 0x75, 0x74, 0x68, 0x41, 0x70, 0x70, 0x6C, /* GenAuthAppl */
};
static const uint8_t Q_LASTERR[] = { 0x01, 0x00, 0x00, 0x06, 0xF1, 0xC2, 0x00, 0x00, 0x00, 0x01 };
static const uint8_t A_EMPTY[]   = { 0x00, 0x00, 0x00, 0x00 };
static const uint8_t A_FAIL[]    = { 0xFF, 0x00, 0x00, 0x00 };
static const uint8_t A_E07[]     = { 0x00, 0x00, 0x00, 0x01, 0x07 };
static const uint8_t A_E0B[]     = { 0x00, 0x00, 0x00, 0x01, 0x0B };

void setUp(void)
{
    play(0, 0u);
    open_st         = TFW_OK;
    opens           = 0u;
    handoff_has_cdi = 0;
    takes           = 0u;
    zero_calls      = 0u;
    memset(out, 0xA5, sizeof out);
    alg = 0xEEu;
}

void tearDown(void) {}

/* init with an element that answers. */
static void present(void)
{
    static const exchange_t s[] = { EX(Q_OPEN, A_EMPTY) };

    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_crypto_init());
    played();
    TEST_ASSERT_EQUAL_INT(TFW_CRYPTO_SECURE_ELEMENT, tfw_crypto_tier());
}

static const uint8_t DIGEST[32] = {
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
};

/* What a call needs from an absent element: nothing on the bus. */
static void refuses_everything_absent(void)
{
    uint32_t c = 0xDEADBEEFu;

    play(0, 0u);
    TEST_ASSERT_EQUAL_INT(TFW_CRYPTO_NONE, tfw_crypto_tier());
    TEST_ASSERT_EQUAL_HEX16(0u, tfw_crypto_algorithms());
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_crypto_random(out, 32u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_crypto_sign(DIGEST, out, &alg));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_crypto_counter(0u, &c));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(DIGEST, out, out));
    played();
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xEEu, alg);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, c);
}

static void test_init_opens_the_link_then_the_application(void)
{
    present();
    TEST_ASSERT_EQUAL_UINT(1u, opens);
    TEST_ASSERT_EQUAL_HEX16(TFW_ALGBIT_ECDSA_P256_SHA256, tfw_crypto_algorithms());
}

static void test_no_element_is_no_tier_and_sends_nothing(void)
{
    open_st = TFW_ERR_TIMEOUT;
    play(0, 0u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_crypto_init());
    played();
    refuses_everything_absent();
}

static void test_an_application_that_will_not_open_is_no_tier(void)
{
    static const exchange_t s1[] = { EX(Q_OPEN, A_FAIL), EX(Q_LASTERR, A_E0B) };
    static const exchange_t s2[] = { EX_ST(Q_OPEN, TFW_ERR_CRC) };

    present();
    PLAY(s1);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_crypto_init());
    played();
    refuses_everything_absent();
    PLAY(s2);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_crypto_init());
    played();
    refuses_everything_absent();
}

static void test_init_drains_and_clears_a_handed_off_cdi(void)
{
    static const uint8_t handed[32] = {
        0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A,
        0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A,
    };

    present();
    TEST_ASSERT_EQUAL_UINT(1u, takes);
    TEST_ASSERT_EQUAL_UINT(0u, zero_calls);
    handoff_has_cdi = 1;
    present();
    TEST_ASSERT_EQUAL_UINT(2u, takes);
    TEST_ASSERT_EQUAL_UINT(1u, zero_calls);
    TEST_ASSERT_EQUAL_size_t(32u, zero_len[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(handed, zero_before[0], 32u);
    /* Drained even when there is no element. */
    open_st = TFW_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_crypto_init());
    TEST_ASSERT_EQUAL_UINT(3u, takes);
    TEST_ASSERT_EQUAL_UINT(2u, zero_calls);
}

static const uint8_t Q_RANDOM_32[] = { 0x8C, 0x00, 0x00, 0x02, 0x00, 0x20 };
static const uint8_t Q_RANDOM_8[]  = { 0x8C, 0x00, 0x00, 0x02, 0x00, 0x08 };
static const uint8_t A_RANDOM_32[] = {
    0x00, 0x00, 0x00, 0x20,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
};
static const uint8_t A_RANDOM_8[] = { 0x00, 0x00, 0x00, 0x08, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78 };

static void test_random_is_get_random(void)
{
    static const exchange_t s[] = { EX(Q_RANDOM_32, A_RANDOM_32) };

    present();
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_crypto_random(out, 32u));
    played();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&A_RANDOM_32[4], out, 32u);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[32]);
    /* The copy on the stack is cleared. */
    TEST_ASSERT_EQUAL_UINT(1u, zero_calls);
    TEST_ASSERT_EQUAL_size_t(32u, zero_len[0]);
}

static void test_random_under_eight_asks_eight_and_hands_back_what_was_asked(void)
{
    static const exchange_t s[] = { EX(Q_RANDOM_8, A_RANDOM_8), EX(Q_RANDOM_8, A_RANDOM_8) };

    present();
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_crypto_random(out, 1u));
    TEST_ASSERT_EQUAL_HEX8(0x71u, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[1]);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_crypto_random(out, 8u));
    played();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&A_RANDOM_8[4], out, 8u);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[8]);
}

static void test_random_out_of_range_or_failing(void)
{
    static const uint8_t    short8[] = { 0x00, 0x00, 0x00, 0x07, 1, 2, 3, 4, 5, 6, 7 };
    static const exchange_t s1[]     = { EX(Q_RANDOM_32, A_FAIL), EX(Q_LASTERR, A_E07) };
    static const exchange_t s2[]     = { EX(Q_RANDOM_8, short8) };

    present();
    play(0, 0u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_crypto_random(out, 0u));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_crypto_random(out, 33u));
    played();
    PLAY(s1);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_crypto_random(out, 32u));
    played();
    PLAY(s2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_crypto_random(out, 4u));
    played();
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[0]);
}

/* A GetRandom that fails hands back nothing and still clears what it put
 * on the stack. */
static void test_a_failing_random_writes_nothing_and_clears_its_copy(void)
{
    static const exchange_t s[] = { EX_ST(Q_RANDOM_32, TFW_ERR_TIMEOUT) };

    present();
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_crypto_random(out, 32u));
    played();
    for (unsigned i = 0u; i < 33u; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xA5u, out[i]);
    }
    TEST_ASSERT_EQUAL_UINT(1u, zero_calls);
    TEST_ASSERT_EQUAL_size_t(32u, zero_len[0]);
}

static void test_sha256_is_software(void)
{
    /* FIPS 180-4's "abc". */
    static const uint8_t abc[]  = { 'a', 'b', 'c' };
    static const uint8_t want[] = {
        0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA, 0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
        0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C, 0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD,
    };

    play(0, 0u);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_crypto_sha256(abc, sizeof abc, out));
    played();
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, 32u);
}

static const uint8_t Q_SIGN[] = {
    0xB1, 0x11, 0x00, 0x28,
    0x01, 0x00, 0x20,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
    0x03, 0x00, 0x02, 0xE0, 0xF0,
};
/* r with its top bit set behind a sign byte, s of 31 bytes. */
static const uint8_t A_SIGN[] = {
    0x00, 0x00, 0x00, 0x44,
    0x02, 0x21, 0x00,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
    0x02, 0x1F,
    0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
    0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
};

static void test_sign_is_calc_sign_with_the_factory_key(void)
{
    static const exchange_t s[] = { EX(Q_SIGN, A_SIGN) };
    uint8_t                 i;

    present();
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_crypto_sign(DIGEST, out, &alg));
    played();
    TEST_ASSERT_EQUAL_HEX8(TFW_ALG_ECDSA_P256_SHA256, alg);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&A_SIGN[7], out, 32u);
    TEST_ASSERT_EQUAL_HEX8(0x00u, out[32]);
    for (i = 0u; i < 31u; i++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t)(0x41u + i), out[33u + i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[64]);
}

static void test_sign_failures_leave_the_algorithm_unset(void)
{
    static uint8_t          bad_r[sizeof A_SIGN];
    static uint8_t          bad_s[sizeof A_SIGN];
    static const exchange_t s1[] = { EX(Q_SIGN, A_FAIL), EX(Q_LASTERR, A_E07) };
    static const exchange_t s2[] = { EX(Q_SIGN, bad_r) };
    static const exchange_t s3[] = { EX(Q_SIGN, bad_s) };
    static const exchange_t s4[] = { EX(Q_SIGN, A_EMPTY) };
    static const exchange_t s5[] = { EX_ST(Q_SIGN, TFW_ERR_TIMEOUT) };

    memcpy(bad_r, A_SIGN, sizeof A_SIGN);
    bad_r[5] = 0x20u;   /* r's DER length one short */
    memcpy(bad_s, A_SIGN, sizeof A_SIGN);
    bad_s[40] = 0x20u;  /* s's DER length one long */
    present();
    PLAY(s1);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_crypto_sign(DIGEST, out, &alg));
    played();
    PLAY(s2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_crypto_sign(DIGEST, out, &alg));
    played();
    PLAY(s3);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_crypto_sign(DIGEST, out, &alg));
    played();
    PLAY(s4);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_crypto_sign(DIGEST, out, &alg));
    played();
    PLAY(s5);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_crypto_sign(DIGEST, out, &alg));
    played();
    TEST_ASSERT_EQUAL_HEX8(0xEEu, alg);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[0]);
}

/* GetDataObject on counter idx: offset 0, its four value bytes. */
static const uint8_t Q_CTR[4][10] = {
    { 0x81, 0x00, 0x00, 0x06, 0xE1, 0x20, 0x00, 0x00, 0x00, 0x04 },
    { 0x81, 0x00, 0x00, 0x06, 0xE1, 0x21, 0x00, 0x00, 0x00, 0x04 },
    { 0x81, 0x00, 0x00, 0x06, 0xE1, 0x22, 0x00, 0x00, 0x00, 0x04 },
    { 0x81, 0x00, 0x00, 0x06, 0xE1, 0x23, 0x00, 0x00, 0x00, 0x04 },
};

static void test_counter_reads_the_value_of_each_of_the_four(void)
{
    static const uint8_t a[] = { 0x00, 0x00, 0x00, 0x04, 0x01, 0x02, 0x03, 0x04 };
    uint8_t              i;
    uint32_t             c;

    present();
    for (i = 0u; i < 4u; i++) {
        const exchange_t s[] = { { Q_CTR[i], 10u, a, (uint16_t)sizeof a, TFW_OK } };

        c = 0u;
        PLAY(s);
        TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_crypto_counter(i, &c));
        played();
        TEST_ASSERT_EQUAL_HEX32(0x01020304u, c);
    }
}

static void test_counter_out_of_range_short_or_failing(void)
{
    static const uint8_t    a3[] = { 0x00, 0x00, 0x00, 0x03, 0x01, 0x02, 0x03 };
    static const exchange_t s1[] = { { Q_CTR[1], 10u, a3, (uint16_t)sizeof a3, TFW_OK } };
    static const exchange_t s2[] = {
        { Q_CTR[2], 10u, A_FAIL, (uint16_t)sizeof A_FAIL, TFW_OK }, EX(Q_LASTERR, A_E07),
    };
    static const exchange_t s3[] = { { Q_CTR[3], 10u, 0, 0u, TFW_ERR_CRC } };
    uint32_t                c    = 0xDEADBEEFu;

    present();
    play(0, 0u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_crypto_counter(4u, &c));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_RANGE, tfw_crypto_counter(0xFFu, &c));
    played();
    PLAY(s1);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_FRAMING, tfw_crypto_counter(1u, &c));
    played();
    PLAY(s2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_PROTECTED_REGION, tfw_crypto_counter(2u, &c));
    played();
    PLAY(s3);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRC, tfw_crypto_counter(3u, &c));
    played();
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, c);
}

/* VerifySign (§4.4.1.13, Table 52) of SIG over DIGEST under OP_KEY, the key
 * in the command (tags 0x05 and 0x06, Table 57's P-256, note 70's BIT
 * STRING), r and s each a DER INTEGER (note 68): r = 0x90.. has its top bit
 * set and takes a sign byte, s = 0x30.. does not. */
static const uint8_t OP_KEY[64] = {
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
};
static const uint8_t OTHER_KEY[64] = {
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
};
static const uint8_t SIG[64] = {
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
};
static const uint8_t Q_VERIFY[] = {
    0xB2, 0x11, 0x00, 0xB6,
    0x01, 0x00, 0x20,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
    0x02, 0x00, 0x45,
    0x02, 0x21, 0x00, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C,
    0x9D, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC,
    0xAD, 0xAE, 0xAF,
    0x02, 0x20, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D,
    0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D,
    0x4E, 0x4F,
    0x05, 0x00, 0x01, 0x03,
    0x06, 0x00, 0x44, 0x03, 0x42, 0x00, 0x04,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
};
/* Table 39's 0x2C, Signature verification failure, read back. */
static const uint8_t A_E2C[] = { 0x00, 0x00, 0x00, 0x01, 0x2C };

static void test_verify_operator_is_verify_sign_with_the_key_sent(void)
{
    static const exchange_t s[] = { EX(Q_VERIFY, A_EMPTY) };

    present();
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_OK, tfw_crypto_verify_operator(DIGEST, SIG, OP_KEY));
    played();
}

/* A signature the element does not verify is TFW_ERR_CRYPTO, the status
 * include/tfw/crypto.h gives every tier for it, and no new one. */
static void test_verify_operator_bad_signature_is_crypto(void)
{
    static uint8_t          q[sizeof Q_VERIFY];
    static const exchange_t s[] = {
        { q, (uint16_t)sizeof q, A_FAIL, (uint16_t)sizeof A_FAIL, TFW_OK },
        EX(Q_LASTERR, A_E2C),
    };
    uint8_t                 sig[64];

    /* The last byte of s one off: 0x4F to 0x4E, at the end of tag 0x02. */
    memcpy(sig, SIG, sizeof sig);
    sig[63] = 0x4Eu;
    memcpy(q, Q_VERIFY, sizeof q);
    q[110] = 0x4Eu;
    present();
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(DIGEST, sig, OP_KEY));
    played();
}

/* The same signature under another key: the request carries that key, byte
 * for byte, and the element's refusal is the same TFW_ERR_CRYPTO. */
static void test_verify_operator_wrong_key_is_crypto(void)
{
    static uint8_t          q[sizeof Q_VERIFY];
    static const exchange_t s[] = {
        { q, (uint16_t)sizeof q, A_FAIL, (uint16_t)sizeof A_FAIL, TFW_OK },
        EX(Q_LASTERR, A_E2C),
    };

    memcpy(q, Q_VERIFY, sizeof q);
    memcpy(&q[sizeof q - 64u], OTHER_KEY, 64u);
    present();
    PLAY(s);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRYPTO, tfw_crypto_verify_operator(DIGEST, SIG, OTHER_KEY));
    played();
}

static void test_verify_operator_transport_failure_is_returned(void)
{
    static const exchange_t s1[] = { EX_ST(Q_VERIFY, TFW_ERR_TIMEOUT) };
    static const exchange_t s2[] = { EX_ST(Q_VERIFY, TFW_ERR_CRC) };

    present();
    PLAY(s1);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_TIMEOUT, tfw_crypto_verify_operator(DIGEST, SIG, OP_KEY));
    played();
    PLAY(s2);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_CRC, tfw_crypto_verify_operator(DIGEST, SIG, OP_KEY));
    played();
}

static void test_the_unsupported_calls_send_nothing(void)
{
    static const uint8_t key[64] = { 0 };

    present();
    play(0, 0u);
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_crypto_hmac(0u, DIGEST, 32u, out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_crypto_pubkey(out));
    TEST_ASSERT_EQUAL_INT(TFW_ERR_UNSUPPORTED, tfw_crypto_verify_image(DIGEST, key));
    played();
    TEST_ASSERT_EQUAL_HEX8(0xA5u, out[0]);
}

/* Runs last: every allowed (Cmd, Param) pair was sent by some test above, and
 * the fake refused every other one in all of them. */
static void test_only_the_read_only_commands_were_ever_sent(void)
{
    TEST_ASSERT_EQUAL_HEX(0x3Fu, cmds_seen);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_opens_the_link_then_the_application);
    RUN_TEST(test_no_element_is_no_tier_and_sends_nothing);
    RUN_TEST(test_an_application_that_will_not_open_is_no_tier);
    RUN_TEST(test_init_drains_and_clears_a_handed_off_cdi);
    RUN_TEST(test_random_is_get_random);
    RUN_TEST(test_random_under_eight_asks_eight_and_hands_back_what_was_asked);
    RUN_TEST(test_random_out_of_range_or_failing);
    RUN_TEST(test_a_failing_random_writes_nothing_and_clears_its_copy);
    RUN_TEST(test_sha256_is_software);
    RUN_TEST(test_sign_is_calc_sign_with_the_factory_key);
    RUN_TEST(test_sign_failures_leave_the_algorithm_unset);
    RUN_TEST(test_counter_reads_the_value_of_each_of_the_four);
    RUN_TEST(test_counter_out_of_range_short_or_failing);
    RUN_TEST(test_verify_operator_is_verify_sign_with_the_key_sent);
    RUN_TEST(test_verify_operator_bad_signature_is_crypto);
    RUN_TEST(test_verify_operator_wrong_key_is_crypto);
    RUN_TEST(test_verify_operator_transport_failure_is_returned);
    RUN_TEST(test_the_unsupported_calls_send_nothing);
    RUN_TEST(test_only_the_read_only_commands_were_ever_sent);
    return UNITY_END();
}
