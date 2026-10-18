/* test_provision_mutate.c -- the provisioning handlers fed mutated requests
 * and requests out of order, against the ATECC608A model, with
 * test_provision's link.
 *
 * Deterministic: xorshift32 from a literal seed, fixed counts. The handlers
 * are called directly, as test_provision calls them, and at every length to
 * one past TFW_MAX_PAYLOAD -- past commands.def's arity only the
 * status and the reply bound are checked: the dispatcher refuses those on the
 * wire, and an empty GENKEY reads slot 0 from the reader's underflow default. Each request
 * is copied to a heap buffer of exactly its length and answered into one of
 * exactly TFW_MAX_PAYLOAD, so under `make test-asan` an overrun is a report.
 *
 * What it pins, for every request:
 *   - the call returns, with a status include/tfw/status.h names;
 *   - PROV_INFO answers 38 bytes, GENKEY and PUBKEY 64, SEED_UDS none;
 *   - a write below config block 2 is PROTECTED_REGION, past block 8 RANGE,
 *     and no write succeeds once the config zone is locked;
 *   - a LOCK without a summary CRC never reaches the element;
 *   - GENKEY and SEED_UDS succeed only on their own slot.
 * After the run test_provision's procedure still succeeds. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "provision.h"
#include "atecc.h"
#include "atecc608a_model.h"
#include "tfw/protocol.h"
#include "tfw/config.h"
#include "tfw/status.h"

#define PAYLOAD_LIMIT  (TFW_MAX_PAYLOAD + 1u)
#define RANDOM_MUTANTS 200u     /* bit-flip mutants per command */
#define RANDOM_PER_LEN 2u       /* pure random payloads per length, per command */
#define WALKS          60u      /* random-order sequences from a fresh part */
#define WALK_STEPS     24u

typedef tfw_status_t (*handler_t)(tfw_reader_t *r, tfw_writer_t *w);

typedef struct {
    uint8_t   cmd;
    handler_t fn;
    unsigned  arity;            /* commands.def's, min = max for all six */
    unsigned  runs, ok, refused;
    uint32_t  seen;
} entry_t;

static entry_t table[] = {
    { TFW_CMD_PROV_INFO,         tfw_prov_cmd_info,          0u, 0u, 0u, 0u, 0u },
    { TFW_CMD_PROV_WRITE_CONFIG, tfw_prov_cmd_write_config, 33u, 0u, 0u, 0u, 0u },
    { TFW_CMD_PROV_LOCK,         tfw_prov_cmd_lock,          3u, 0u, 0u, 0u, 0u },
    { TFW_CMD_PROV_GENKEY,       tfw_prov_cmd_genkey,        1u, 0u, 0u, 0u, 0u },
    { TFW_CMD_PROV_PUBKEY,       tfw_prov_cmd_pubkey,        1u, 0u, 0u, 0u, 0u },
    { TFW_CMD_PROV_SEED_UDS,     tfw_prov_cmd_seed_uds,      1u, 0u, 0u, 0u, 0u },
};
#define NCMDS (sizeof table / sizeof table[0])

static uint32_t rng;

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

void setUp(void)    { tfw_model_reset(); }
void tearDown(void) { }

static void dump(uint8_t cmd, const uint8_t *p, unsigned n)
{
    unsigned i;
    printf("cmd 0x%02x payload[%u]:", cmd, n);
    for (i = 0u; i < n; i++) {
        printf(" %02x", p[i]);
    }
    printf("\n");
}

/* One request to its handler, checked against every contract above. */
static tfw_status_t feed(unsigned k, const uint8_t *payload, unsigned n)
{
    entry_t      *e   = &table[k];
    uint8_t      *in  = (uint8_t *)malloc(n ? n : 1u);
    uint8_t      *out = (uint8_t *)malloc(TFW_MAX_PAYLOAD);
    const uint8_t cfg_locked = tfw_model_config_locked();
    const uint8_t before_op  = tfw_model_last_opcode();
    tfw_reader_t  r;
    tfw_writer_t  w;
    tfw_status_t  st;
    unsigned      len;
    const char   *broke = NULL;

    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_NOT_NULL(out);
    if (n) { memcpy(in, payload, n); }
    tfw_reader_init(&r, in, (uint8_t)n);
    tfw_writer_init(&w, out, (uint8_t)TFW_MAX_PAYLOAD);

    st  = e->fn(&r, &w);
    len = tfw_writer_len(&w);

    if ((unsigned)st > (unsigned)TFW_ERR_BAD_STATE) {
        broke = "a status status.h does not name";
    } else if (len > TFW_MAX_PAYLOAD) {
        broke = "the reply outgrew its buffer";
    } else if (n != e->arity) {
        /* past commands.def's arity the dispatcher refuses it on the wire;
         * here only the status and the reply bound are the handler's */
    } else if (st == TFW_OK) {
        switch (e->cmd) {
        case TFW_CMD_PROV_INFO:
            if (len != 38u) { broke = "PROV_INFO answered other than 38 bytes"; }
            break;
        case TFW_CMD_PROV_WRITE_CONFIG:
            if (n < 33u || payload[0] < 2u || payload[0] > 8u) {
                broke = "a short, protected or out-of-zone config write succeeded";
            } else if (cfg_locked) {
                broke = "a config write succeeded on a locked config zone";
            }
            break;
        case TFW_CMD_PROV_LOCK:
            if (n < 3u || (payload[1] == 0u && payload[2] == 0u)) {
                broke = "a LOCK without a summary CRC succeeded";
            } else if (payload[0] != TFW_ATECC_LOCK_CONFIG && payload[0] != TFW_ATECC_LOCK_DATA) {
                broke = "a LOCK of a zone the part does not have succeeded";
            }
            break;
        case TFW_CMD_PROV_GENKEY:
            if (n < 1u || payload[0] != TFW_ATECC_SLOT_DEVICEID || len != 64u) {
                broke = "GENKEY succeeded off its slot or not with a 64-byte point";
            }
            break;
        case TFW_CMD_PROV_PUBKEY:
            if (n < 1u || len != 64u) { broke = "PUBKEY succeeded without a 64-byte point"; }
            break;
        default:
            if (n < 1u || payload[0] != TFW_ATECC_SLOT_UDS || len != 0u) {
                broke = "SEED_UDS succeeded off its slot or answered key material";
            }
            break;
        }
    } else if (e->cmd == TFW_CMD_PROV_WRITE_CONFIG && n >= 33u && payload[0] < 2u
               && st != TFW_ERR_PROTECTED_REGION) {
        broke = "a write below config block 2 was not refused PROTECTED_REGION";
    } else if (e->cmd == TFW_CMD_PROV_WRITE_CONFIG && n >= 33u && payload[0] > 8u
               && st != TFW_ERR_RANGE) {
        broke = "a write past config block 8 was not refused RANGE";
    } else if (e->cmd == TFW_CMD_PROV_LOCK && n >= 3u && payload[1] == 0u && payload[2] == 0u
               && (st != TFW_ERR_BAD_ARGC || tfw_model_last_opcode() != before_op)) {
        broke = "a LOCK without a summary CRC reached the element or was not BAD_ARGC";
    } else if ((e->cmd == TFW_CMD_PROV_GENKEY && payload[0] != TFW_ATECC_SLOT_DEVICEID)
               || (e->cmd == TFW_CMD_PROV_SEED_UDS && payload[0] != TFW_ATECC_SLOT_UDS)) {
        if (st != TFW_ERR_RANGE) { broke = "a request for another slot was not refused RANGE"; }
    }
    if (broke) {
        printf("status %u, writer %u, config locked %u\n", (unsigned)st, len, (unsigned)cfg_locked);
        dump(e->cmd, payload, n);
        TEST_FAIL_MESSAGE(broke);
    }

    e->runs++;
    e->seen |= (uint32_t)1u << (unsigned)st;
    if (st == TFW_OK) { e->ok++; } else { e->refused++; }
    free(in);
    free(out);
    return st;
}

/* Each command's valid request, and the part it is valid on. */
static unsigned seed_for(unsigned k, uint8_t *p)
{
    switch (table[k].cmd) {
    case TFW_CMD_PROV_INFO:         return 0u;
    case TFW_CMD_PROV_WRITE_CONFIG: memset(p, 0x5Au, 33u); p[0] = 2u; return 33u;
    case TFW_CMD_PROV_LOCK:         p[0] = TFW_ATECC_LOCK_CONFIG; p[1] = 0x34u; p[2] = 0x12u; return 3u;
    case TFW_CMD_PROV_GENKEY:       p[0] = TFW_ATECC_SLOT_DEVICEID; return 1u;
    case TFW_CMD_PROV_PUBKEY:       p[0] = TFW_ATECC_SLOT_DEVICEID; return 1u;
    default:                        p[0] = TFW_ATECC_SLOT_UDS; return 1u;
    }
}

static void fresh_for(unsigned k)
{
    tfw_model_reset();
    if (table[k].cmd == TFW_CMD_PROV_PUBKEY) { tfw_model_provision(); }
    if (rnd() % 16u == 0u) { tfw_model_set_absent(1u); }   /* a silent bus */
}

static void try_one(unsigned k, const uint8_t *p, unsigned n)
{
    fresh_for(k);
    feed(k, p, n);
}

static void mutate(unsigned k)
{
    uint8_t  seed[PAYLOAD_LIMIT];
    uint8_t  buf[PAYLOAD_LIMIT];
    unsigned sn = seed_for(k, seed);
    unsigned n;
    unsigned i;
    unsigned j;

    try_one(k, seed, sn);

    /* truncation at every length, then the seed extended to every length */
    for (n = 0u; n < sn; n++) {
        try_one(k, seed, n);
    }
    for (n = sn + 1u; n <= PAYLOAD_LIMIT; n++) {
        memcpy(buf, seed, sn);
        random_fill(buf + sn, n - sn);
        try_one(k, buf, n);
    }

    /* every byte set to 0, the bytes that follow, one more, the block and
     * slot edges and 0xFF */
    for (i = 0u; i < sn; i++) {
        const uint8_t vals[9] = { 0u, (uint8_t)(sn - i - 1u), (uint8_t)(sn - i), 1u, 2u, 8u, 9u,
                                  0x7Fu, 0xFFu };
        for (j = 0u; j < 9u; j++) {
            memcpy(buf, seed, sn);
            buf[i] = vals[j];
            try_one(k, buf, sn);
        }
    }

    /* bit flips, then a random cut or a random tail */
    for (j = 0u; j < RANDOM_MUTANTS; j++) {
        unsigned flips = 1u + rnd() % 4u;
        n = sn;
        memcpy(buf, seed, sn);
        while (flips-- && n) {
            const uint32_t at = rnd() % (n * 8u);
            buf[at / 8u] ^= (uint8_t)(1u << (at % 8u));
        }
        switch (rnd() % 3u) {
        case 0u:
            n = n ? rnd() % (n + 1u) : 0u;
            break;
        case 1u: {
            const unsigned add = 1u + rnd() % (PAYLOAD_LIMIT - n);
            random_fill(buf + n, add);
            n += add;
            break;
        }
        default:
            break;
        }
        try_one(k, buf, n);
    }

    /* pure random payloads of every length */
    for (n = 0u; n <= PAYLOAD_LIMIT; n++) {
        for (j = 0u; j < RANDOM_PER_LEN; j++) {
            random_fill(buf, n);
            try_one(k, buf, n);
        }
    }
}

static void report(void)
{
    unsigned i;
    for (i = 0u; i < NCMDS; i++) {
        printf("cmd 0x%02x: %u runs, %u ok, %u refused, statuses 0x%05lx\n", table[i].cmd,
               table[i].runs, table[i].ok, table[i].refused, (unsigned long)table[i].seen);
    }
}

static void reset_tally(void)
{
    unsigned i;
    for (i = 0u; i < NCMDS; i++) {
        table[i].runs = table[i].ok = table[i].refused = 0u;
        table[i].seen = 0u;
    }
}

static void check_both_ways(void)
{
    unsigned i;
    report();
    for (i = 0u; i < NCMDS; i++) {
        TEST_ASSERT_TRUE_MESSAGE(table[i].ok > 0u, "a provisioning command never accepted a request");
        TEST_ASSERT_TRUE_MESSAGE(table[i].refused > 0u, "a provisioning command never refused a request");
    }
}

static void test_mutated_requests_keep_the_contracts(void)
{
    unsigned k;

    rng = 0x51D3A7E9u;
    reset_tally();
    for (k = 0u; k < NCMDS; k++) {
        mutate(k);
    }
    check_both_ways();
}

/* The procedure's steps in any order, valid or one bit off, from a fresh
 * part: LOCK before WRITE_CONFIG, GENKEY before the config lock, SEED_UDS
 * twice, a write after the lock. */
static void test_the_procedure_in_any_order_keeps_the_contracts(void)
{
    uint8_t  p[PAYLOAD_LIMIT];
    unsigned walk;
    unsigned step;

    rng = 0x7E57AB1Eu;
    reset_tally();
    for (walk = 0u; walk < WALKS; walk++) {
        tfw_model_reset();
        if (walk % 3u == 0u) { tfw_model_provision(); }
        if (walk % 10u == 9u) { tfw_model_set_absent(1u); }
        for (step = 0u; step < WALK_STEPS; step++) {
            const unsigned k = rnd() % NCMDS;
            unsigned       n = seed_for(k, p);
            switch (rnd() % 4u) {
            case 0u:                                     /* noise */
                n = rnd() % (PAYLOAD_LIMIT + 1u);
                random_fill(p, n);
                break;
            case 1u:                                     /* one bit off */
                if (n) {
                    const uint32_t at = rnd() % (n * 8u);
                    p[at / 8u] ^= (uint8_t)(1u << (at % 8u));
                }
                break;
            default:                                     /* the valid request */
                if (table[k].cmd == TFW_CMD_PROV_LOCK && rnd() % 2u) {
                    p[0] = TFW_ATECC_LOCK_DATA;
                }
                if (table[k].cmd == TFW_CMD_PROV_WRITE_CONFIG) {
                    p[0] = (uint8_t)(2u + rnd() % 7u);
                }
                break;
            }
            feed(k, p, n);
        }
    }
    check_both_ways();
}

/* test_provision's procedure, after the run. */
static void test_the_procedure_still_succeeds_after_the_run(void)
{
    const uint8_t slot[1] = { TFW_ATECC_SLOT_DEVICEID };
    const uint8_t uds[1]  = { TFW_ATECC_SLOT_UDS };
    const uint8_t cfg[3]  = { TFW_ATECC_LOCK_CONFIG, 0x34u, 0x12u };
    const uint8_t dat[3]  = { TFW_ATECC_LOCK_DATA, 0x34u, 0x12u };
    uint8_t       blk[33] = { 2u };

    tfw_model_reset();
    TEST_ASSERT_EQUAL(TFW_OK, feed(0u, NULL, 0u));
    TEST_ASSERT_EQUAL(TFW_OK, feed(1u, blk, 33u));
    TEST_ASSERT_EQUAL(TFW_OK, feed(2u, cfg, 3u));
    TEST_ASSERT_TRUE(tfw_model_config_locked());
    TEST_ASSERT_EQUAL(TFW_OK, feed(3u, slot, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, feed(4u, slot, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, feed(5u, uds, 1u));
    TEST_ASSERT_EQUAL(TFW_OK, feed(2u, dat, 3u));
    TEST_ASSERT_TRUE(tfw_model_data_locked());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mutated_requests_keep_the_contracts);
    RUN_TEST(test_the_procedure_in_any_order_keeps_the_contracts);
    RUN_TEST(test_the_procedure_still_succeeds_after_the_run);
    return UNITY_END();
}
