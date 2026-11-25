/* SPDX-License-Identifier: AGPL-3.0-or-later */
/* SPDX-License-Identifier: commercial */
/*
 * The eFuse-UDS provisioning backend (src/caps/provision_uds.c) under mutated
 * and truncated requests, a burn that fails at every step, and a second
 * provisioning after the first (#163). Driven through the wire handlers of
 * src/caps/provision.c over the part of test/mock/prov_uds_mock.c.
 *
 * The oracle is a model of the part, written from the backend's contract:
 * GENKEY takes slot 0 only, refuses a part that already holds a UDS before the
 * random source is touched, and answers the DeviceID point of the UDS it
 * burned; PUBKEY answers the same point or NOT_PROVISIONED; INFO, WRITE_CONFIG,
 * LOCK and SEED_UDS are not this backend's and answer UNSUPPORTED.
 *
 * After every request, whatever its length or bytes: the part holds nothing
 * or exactly the UDS it drew, never anything else; no reply carries any
 * 8-byte run of that UDS; and no reply passes the wire's payload bound.
 *
 * A burn is one operation at the part's interface (tfw_prov_uds_burn), so a
 * write torn anywhere inside the key block reaches the backend only as a
 * failed burn; that is injected at each step of the sequence below. Tearing
 * within the block is the part's own contract (src/hal/esp32/prov_uds.c).
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "provision.h"
#include "prov_uds_mock.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "crypto/sha256.h"
#include "crypto/deviceid_p256.h"

#define SEED         0x163D05D5u
#define RANDOM_CASES 1500u

typedef tfw_status_t (*handler_t)(tfw_reader_t *, tfw_writer_t *);

typedef struct {
    const char *name;
    handler_t   fn;
    uint8_t     arity;
} cmd_t;

/* Arities as literals, from src/core/commands.def. */
static const cmd_t CMDS[6] = {
    { "info",         tfw_prov_cmd_info,         0u  },
    { "write_config", tfw_prov_cmd_write_config, 33u },
    { "lock",         tfw_prov_cmd_lock,         3u  },
    { "genkey",       tfw_prov_cmd_genkey,       1u  },
    { "pubkey",       tfw_prov_cmd_pubkey,       1u  },
    { "seed_uds",     tfw_prov_cmd_seed_uds,     1u  },
};
#define C_GENKEY 3
#define C_PUBKEY 4

/* The point the part's UDS 00 01 .. 1F pins (test_provision_uds.c). */
static const uint8_t UDS_00_1F_PUB[64] = {
    0xC4,0x57,0x5D,0x64,0xA4,0xD8,0x9A,0x2A,0x53,0x2B,0x03,0x82,0xF3,0xA0,0x02,0xA2,
    0xB7,0x8D,0x4E,0xCB,0xC1,0x70,0x50,0x7E,0xDE,0x97,0x91,0xEB,0xFC,0xC9,0x7F,0x0E,
    0x55,0x4D,0xFC,0xFD,0x69,0x4E,0x65,0x91,0x04,0xB0,0xDE,0xEE,0xAC,0xCE,0x0C,0x47,
    0x00,0x2A,0x51,0x6E,0xA8,0xE5,0xF0,0xCE,0x96,0x22,0x0E,0x84,0x1C,0xC7,0x5C,0xA6
};

static uint8_t      inbuf[TFW_MAX_PAYLOAD + 1u];
static uint8_t      outbuf[TFW_MAX_PAYLOAD];
static tfw_reader_t r;
static tfw_writer_t w;
static uint32_t     rng;

/* the model */
static uint8_t  m_uds[32];   /* what the part draws */
static uint8_t  m_pub[64];   /* its point */
static uint8_t  m_burned;
static unsigned m_draws, m_burns;
static tfw_status_t m_random_st, m_burn_st, m_hmac_st;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* A fresh part drawing fill, fill+1 .. fill+31. */
static void part(uint8_t fill)
{
    uint8_t seed[32];

    tfw_mock_prov_uds_reset(fill);
    for (unsigned i = 0u; i < 32u; i++) {
        m_uds[i] = (uint8_t)(fill + i);
    }
    if (fill == 0x00u) {
        memcpy(m_pub, UDS_00_1F_PUB, sizeof m_pub);
    } else {
        tfw_hmac_sha256(m_uds, 32u, (const uint8_t *)TFW_DEVICEID_LABEL,
                        (uint16_t)TFW_DEVICEID_LABEL_LEN, seed);
        tfw_deviceid_p256_public(seed, m_pub);
    }
    m_burned = 0u;
    m_draws = m_burns = 0u;
    m_random_st = m_burn_st = m_hmac_st = TFW_OK;
}

static void set_fail(tfw_status_t rs, tfw_status_t bs, tfw_status_t hs)
{
    m_random_st = rs; tfw_mock_prov_uds_fail_random(rs);
    m_burn_st   = bs; tfw_mock_prov_uds_fail_burn(bs);
    m_hmac_st   = hs; tfw_mock_prov_uds_fail_hmac(hs);
}

void setUp(void)    { part(0x00u); }
void tearDown(void) {}

static tfw_status_t run(int c, const uint8_t *p, uint8_t n)
{
    if (n) { memcpy(inbuf, p, n); }
    memset(outbuf, 0xA5, sizeof outbuf);
    tfw_reader_init(&r, inbuf, n);
    tfw_writer_init(&w, outbuf, (uint8_t)sizeof outbuf);
    return CMDS[c].fn(&r, &w);
}

/* 1 when any 8-byte run of the UDS appears anywhere in the reply buffer,
 * written or not. */
static int reply_carries(void)
{
    for (unsigned i = 0u; i + 8u <= 32u; i++) {
        for (unsigned j = 0u; j + 8u <= sizeof outbuf; j++) {
            if (memcmp(&outbuf[j], &m_uds[i], 8u) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* What must hold after any request at all. */
static void invariants(const char *msg)
{
    const uint8_t *held = tfw_mock_prov_uds_burned();

    if (held) {
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(m_uds, held, 32u, msg);
    }
    TEST_ASSERT_FALSE_MESSAGE(reply_carries(), msg);
    TEST_ASSERT_TRUE_MESSAGE(tfw_writer_len(&w) <= TFW_MAX_PAYLOAD, msg);
}

/* What must hold after a refusal or failure: nothing written, and the part
 * still in the state the model has. */
static void refused(const char *msg)
{
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, tfw_writer_len(&w), msg);
}

/* One request at its arity against the model. */
static void step(int c, const uint8_t *p, const char *msg)
{
    tfw_status_t exp = TFW_ERR_UNSUPPORTED;
    tfw_status_t got;

    if (c == C_GENKEY) {
        if (p[0] != 0u)         { exp = TFW_ERR_RANGE; }
        else if (m_burned)      { exp = TFW_ERR_PROTECTED_REGION; }
        else {
            m_draws++;
            if (m_random_st != TFW_OK) { exp = m_random_st; }
            else {
                m_burns++;
                if (m_burn_st != TFW_OK) { exp = m_burn_st; }
                else {
                    m_burned = 1u;
                    exp = m_hmac_st;
                }
            }
        }
    } else if (c == C_PUBKEY) {
        if (p[0] != 0u)         { exp = TFW_ERR_RANGE; }
        else if (!m_burned)     { exp = TFW_ERR_NOT_PROVISIONED; }
        else                    { exp = m_hmac_st; }
    }

    got = run(c, p, CMDS[c].arity);
    TEST_ASSERT_EQUAL_MESSAGE(exp, got, msg);
    if (exp == TFW_OK && (c == C_GENKEY || c == C_PUBKEY)) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(64u, tfw_writer_len(&w), msg);
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(m_pub, outbuf, 64u, msg);
    } else {
        refused(msg);
    }
    TEST_ASSERT_EQUAL_MESSAGE(m_burned, tfw_mock_prov_uds_burned() != 0, msg);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(m_draws, tfw_mock_prov_uds_draws(), msg);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(m_burns, tfw_mock_prov_uds_burns(), msg);
    invariants(msg);
}

/* Every slot byte for GENKEY and PUBKEY, unprovisioned and then provisioned:
 * only slot 0 is a slot, and only one GENKEY ever burns. */
static void test_every_slot_byte_before_and_after_provisioning(void)
{
    char msg[48];

    for (unsigned round = 0u; round < 2u; round++) {
        for (unsigned b = 1u; b < 256u; b++) {
            uint8_t slot = (uint8_t)b;

            snprintf(msg, sizeof msg, "round %u genkey slot %u", round, b);
            step(C_GENKEY, &slot, msg);
            snprintf(msg, sizeof msg, "round %u pubkey slot %u", round, b);
            step(C_PUBKEY, &slot, msg);
        }
        snprintf(msg, sizeof msg, "round %u pubkey slot 0", round);
        step(C_PUBKEY, (const uint8_t[]){ 0u }, msg);
        snprintf(msg, sizeof msg, "round %u genkey slot 0", round);
        step(C_GENKEY, (const uint8_t[]){ 0u }, msg);
    }
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_prov_uds_burns());
}

/* A second provisioning after the first, at every slot byte and with every
 * failure armed: refused, the random source untouched, nothing reburned, and
 * PUBKEY still answers the first point. */
static void test_a_second_provisioning_is_refused(void)
{
    static const tfw_status_t fails[] = { TFW_OK, TFW_ERR_TIMEOUT, TFW_ERR_CRYPTO };
    char msg[64];

    step(C_GENKEY, (const uint8_t[]){ 0u }, "first");
    for (size_t f = 0u; f < sizeof fails / sizeof fails[0]; f++) {
        set_fail(fails[f], fails[f], TFW_OK);
        for (unsigned b = 0u; b < 256u; b++) {
            uint8_t slot = (uint8_t)b;

            snprintf(msg, sizeof msg, "fail %d second genkey slot %u", (int)fails[f], b);
            step(C_GENKEY, &slot, msg);
        }
        step(C_PUBKEY, (const uint8_t[]){ 0u }, "pubkey after");
    }
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_prov_uds_draws());
    TEST_ASSERT_EQUAL_UINT(1u, tfw_mock_prov_uds_burns());
}

/* The sequence failing at each of its steps -- the draw, the burn, the
 * derivation -- with several statuses, then retried clean on the same part:
 * a failed draw or burn leaves it unprovisioned and a retry provisions it; a
 * failed derivation after the burn leaves it provisioned with the UDS drawn,
 * and the retry is a second provisioning. */
static void test_a_burn_failing_at_every_step_then_retried(void)
{
    static const tfw_status_t sts[] = {
        TFW_ERR_TIMEOUT, TFW_ERR_CRYPTO, TFW_ERR_INTERNAL, TFW_ERR_BAD_STATE
    };
    char msg[64];

    for (unsigned at = 0u; at < 3u; at++) {
        for (size_t k = 0u; k < sizeof sts / sizeof sts[0]; k++) {
            part((uint8_t)(0x40u + at * 16u + k));
            set_fail(at == 0u ? sts[k] : TFW_OK,
                     at == 1u ? sts[k] : TFW_OK,
                     at == 2u ? sts[k] : TFW_OK);
            snprintf(msg, sizeof msg, "fail at %u st %d", at, (int)sts[k]);
            step(C_GENKEY, (const uint8_t[]){ 0u }, msg);
            step(C_PUBKEY, (const uint8_t[]){ 0u }, msg);
            set_fail(TFW_OK, TFW_OK, TFW_OK);
            snprintf(msg, sizeof msg, "retry after %u st %d", at, (int)sts[k]);
            step(C_GENKEY, (const uint8_t[]){ 0u }, msg);
            step(C_PUBKEY, (const uint8_t[]){ 0u }, msg);
            TEST_ASSERT_NOT_NULL_MESSAGE(tfw_mock_prov_uds_burned(), msg);
        }
    }
}

/* Every handler at every length from 0 to the payload bound, each byte
 * mutated. Off its arity the dispatcher refuses the request on the wire
 * (commands.def), so only the invariants are the handler's: the part holds
 * nothing or what it drew, nothing is echoed, the reply stays in bound. At
 * its arity the model answers. Run unprovisioned and provisioned. */
static void test_every_length_and_mutated_byte_on_every_handler(void)
{
    static const uint8_t vals[] = { 0x00u, 0x01u, 0x7Fu, 0x80u, 0xFFu };
    uint8_t p[TFW_MAX_PAYLOAD + 1u];
    char    msg[64];

    for (unsigned prov = 0u; prov < 2u; prov++) {
        for (int c = 0; c < 6; c++) {
            for (unsigned n = 0u; n <= TFW_MAX_PAYLOAD; n++) {
                for (size_t v = 0u; v < sizeof vals; v++) {
                    part(0x00u);
                    if (prov) { step(C_GENKEY, (const uint8_t[]){ 0u }, "provision"); }
                    memset(p, vals[v], sizeof p);
                    snprintf(msg, sizeof msg, "prov %u %s len %u fill 0x%02x",
                             prov, CMDS[c].name, n, vals[v]);
                    if (n == CMDS[c].arity) {
                        step(c, p, msg);
                    } else {
                        (void)run(c, p, (uint8_t)n);
                        invariants(msg);
                    }
                }
            }
        }
    }
}

/* Random requests at their arity, with failures armed and disarmed and a
 * fresh part now and then, against the model. */
static void test_random_requests_with_a_fixed_seed(void)
{
    static const tfw_status_t sts[] = { TFW_OK, TFW_OK, TFW_OK, TFW_ERR_TIMEOUT, TFW_ERR_CRYPTO };
    uint8_t p[TFW_MAX_PAYLOAD];
    char    msg[48];

    rng = SEED;
    for (uint32_t i = 0u; i < RANDOM_CASES; i++) {
        uint32_t x = rnd();
        int      c = (x & 0x3u) ? ((x & 0x4u) ? C_GENKEY : C_PUBKEY) : (int)((x >> 3) % 6u);

        snprintf(msg, sizeof msg, "seed 0x%08lx case %lu",
                 (unsigned long)SEED, (unsigned long)i);
        if ((x >> 8) % 50u == 0u) {
            part((uint8_t)(x >> 16));
        }
        if ((x >> 14) % 8u == 0u) {
            set_fail(sts[(x >> 17) % 5u], sts[(x >> 20) % 5u], sts[(x >> 23) % 5u]);
        }
        for (unsigned k = 0u; k < sizeof p; k++) {
            p[k] = (uint8_t)rnd();
        }
        if (x & 0x80000000u) { p[0] = 0u; }
        step(c, p, msg);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_slot_byte_before_and_after_provisioning);
    RUN_TEST(test_a_second_provisioning_is_refused);
    RUN_TEST(test_a_burn_failing_at_every_step_then_retried);
    RUN_TEST(test_every_length_and_mutated_byte_on_every_handler);
    RUN_TEST(test_random_requests_with_a_fixed_seed);
    return UNITY_END();
}
