/* test_ota_mutate.c -- the OTA command handlers fed mutated requests and
 * requests out of order, through the dispatcher, with test_ota_stage's link.
 *
 * Deterministic: xorshift32 from a literal seed, fixed counts. Each request is
 * copied to a heap buffer of exactly its length and answered into one of
 * exactly TFW_MAX_PAYLOAD, so under `make test-asan` a read past the request or
 * a write past the reply is a sanitizer report, not a silent pass.
 *
 * What it pins, for every request:
 *   - the call returns, with a status include/tfw/status.h names;
 *   - OTA_END and OTA_ACTIVATE succeed only for the valid image: the staged
 *     body hashes to the reference measurement and the trailer carries it;
 *   - OTA_ACTIVATE and OTA_CONFIRM without an OTA_AUTH immediately before are
 *     refused AUTH_REQUIRED (ota_activate.c, ota_ctl.c), never anything that
 *     touched the MARK or wrote a confirm request.
 * After the run the valid sequence test_ota_stage pins still succeeds. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "tfw/config.h"
#include "tfw/board.h"
#include "hal_mock.h"
#include "spi_nor_model.h"
#include "ota_stage.h"
#include "ota_store.h"
#include "ota_ctl.h"
#include "ota_auth.h"
#include "boot_platform.h"
#include "tfw/hal.h"
#include "caps/sys.h"
#include "sha256.h"
#include "crc16.h"
#include "handoff.h"
#include "boot_measure.h"
#include "tfw/protocol.h"
#include "tfw/status.h"
#include "dispatch.h"

#define TRAILER        128u
#define CHUNK          60u      /* TFW_MAX_PAYLOAD less the offset */
#define TOTAL          512u     /* the valid image's declared length */
#define BODY           200u     /* and its measured body */
#define PAYLOAD_LIMIT  (TFW_MAX_PAYLOAD + 1u)
#define RANDOM_MUTANTS 300u     /* bit-flip mutants per command */
#define RANDOM_PER_LEN 2u       /* pure random payloads per length, per command */
#define WALKS          120u     /* random-order sequences */
#define WALK_STEPS     40u
#define NOISY_RUNS     150u     /* the valid sequence with one step perturbed */

static uint32_t rng;
static uint8_t  img[2u * TOTAL];
static uint8_t  meas[32];
static uint8_t  authed;        /* the last dispatched request was an OTA_AUTH that took */
static uint8_t  prev_cmd;      /* the request before, for the failure report */
static unsigned prev_len;
static tfw_status_t prev_st;

typedef enum { CTX_FRESH, CTX_BEGUN, CTX_MID, CTX_STREAMED, CTX_ENDED, CTX_MARKED, CTX_TRIAL } ctx_t;

static const uint8_t cmds[] = {
    TFW_CMD_OTA_BEGIN, TFW_CMD_OTA_DATA, TFW_CMD_OTA_END, TFW_CMD_OTA_STATUS,
    TFW_CMD_OTA_INFO, TFW_CMD_OTA_ACTIVATE, TFW_CMD_OTA_AUTH, TFW_CMD_OTA_CONFIRM
};
#define NCMDS (sizeof cmds / sizeof cmds[0])

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

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void hash(const uint8_t *p, uint32_t n, uint8_t out[32])
{
    tfw_sha256_t c;
    uint32_t     i;

    tfw_sha256_init(&c);
    for (i = 0u; i < n; i += 1024u) {
        tfw_sha256_update(&c, &p[i], (uint16_t)(n - i < 1024u ? n - i : 1024u));
    }
    tfw_sha256_final(&c, out);
}

/* test_ota_stage.c's build(TOTAL, BODY): the one valid image. */
static void build(void)
{
    uint8_t *t = &img[TOTAL - TRAILER];
    uint32_t i;

    memset(img, 0xFF, sizeof img);
    for (i = 0u; i < BODY; i++) { img[i] = (uint8_t)(i * 7u + 3u); }
    hash(img, BODY, meas);
    memset(t, 0, TRAILER);
    t[0] = 0x54u; t[1] = 0x46u; t[2] = 0x49u; t[3] = 0x4Du;   /* "TFIM" */
    t[4] = 0x01u; t[5] = 0x00u;                               /* version 1 */
    t[6] = 0x01u;                                             /* the 328P */
    put32(&t[8], 3u);
    put32(&t[12], BODY);
    memcpy(&t[16], meas, 32u);
}

static void put_block(uint8_t boot_state)
{
    uint8_t             raw[TFW_BOOT_EE_LEN];
    tfw_boot_platform_t p;

    memset(&p, 0, sizeof p);
    p.rollback_floor = 7u;
    p.boot_state     = boot_state;
    p.ota_state      = TFW_BOOT_OTA_IDLE;
    tfw_boot_platform_serialise(&p, raw);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, raw, (uint8_t)TFW_BOOT_EE_LEN));
}

static void reset_all(void)
{
    tfw_mock_reset();
    tfw_nor_model_reset();
    tfw_sys_reset_state();
    tfw_boot_handoff_init();
    tfw_stage_init();
    put_block(TFW_BOOT_STATE_GOOD);
    (void)tfw_ota_auth_take();   /* a hold from the last sequence is dropped */
    authed = 0u;
}

void setUp(void)    { reset_all(); }
void tearDown(void) { }

static unsigned idx_of(uint8_t cmd)
{
    unsigned i;
    for (i = 0u; i < NCMDS; i++) {
        if (cmds[i] == cmd) { return i; }
    }
    TEST_FAIL_MESSAGE("not an OTA command");
    return 0u;
}

static int marked(void)
{
    const uint8_t *m = tfw_nor_model_mem() + TFW_OTA_STORE_MARK_ADDR;
    return m[0] == 0x54u && m[1] == 0x46u && m[2] == 0x53u && m[3] == 0x47u;   /* "TFSG" */
}

/* The stager's own view of what it holds, read without the dispatcher so the
 * OTA_AUTH a sequence is holding is not dropped. */
static void stage_status(uint8_t *state, uint32_t *received)
{
    uint8_t      out[8];
    tfw_reader_t r;
    tfw_writer_t w;

    tfw_reader_init(&r, NULL, 0u);
    tfw_writer_init(&w, out, sizeof out);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_stage_cmd_status(&r, &w));
    *state    = out[0];
    *received = get32(&out[1]);
}

/* Independently of the stager: the bytes in STAGE, `total` long, carry a
 * trailer whose measurement is the reference one, over a body that hashes to
 * it. Anything else reported as staged is an image nobody signed. */
static int staged_is_the_valid_image(uint32_t total)
{
    const uint8_t *s = tfw_nor_model_mem() + TFW_OTA_STORE_STAGE_ADDR;
    const uint8_t *t;
    uint32_t       body;
    uint8_t        h[32];

    if (total < TRAILER || total > TFW_OTA_STORE_STAGE_LEN) { return 0; }
    t    = s + total - TRAILER;
    body = get32(&t[12]);
    if (memcmp(t, "TFIM", 4u) != 0 || body > total - TRAILER) { return 0; }
    if (memcmp(&t[16], meas, 32u) != 0) { return 0; }
    hash(s, body, h);
    return memcmp(h, meas, 32u) == 0;
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

/* One request through the dispatcher, checked against every contract above. */
static tfw_status_t feed(uint8_t cmd, const uint8_t *payload, unsigned n)
{
    uint8_t      *in  = (uint8_t *)malloc(n ? n : 1u);
    uint8_t      *out = (uint8_t *)malloc(TFW_MAX_PAYLOAD);
    const uint8_t was_authed = authed;
    const int     was_marked = marked();
    /* While an activation is pending, an OTA_ACTIVATE without OTA_AUTH is the
     * retry of that exchange or a refused new attempt (m1117 (1)): it answers
     * OK or BAD_STATE, and moves nothing either way. */
    const int     retry = cmd == TFW_CMD_OTA_ACTIVATE && tfw_stage_install_pending() != 0u;
    const uint8_t confirm_before = tfw_mock_eeprom()[TFW_BOOT_CONFIRM_ADDR];
    uint8_t       mark_before[40];   /* the MARK's header and hash */
    uint8_t       confirm_rec[TFW_BOOT_CONFIRM_LEN];
    tfw_reader_t  r;
    tfw_writer_t  w;
    tfw_status_t  st;
    const char   *broke = NULL;
    unsigned      k = idx_of(cmd);
    uint8_t       state;
    uint32_t      received;

    TEST_ASSERT_NOT_NULL(in);
    TEST_ASSERT_NOT_NULL(out);
    if (n) { memcpy(in, payload, n); }
    memcpy(mark_before, tfw_nor_model_mem() + TFW_OTA_STORE_MARK_ADDR, sizeof mark_before);
    memcpy(confirm_rec, tfw_mock_eeprom() + TFW_BOOT_CONFIRM_ADDR, sizeof confirm_rec);
    tfw_reader_init(&r, in, (uint8_t)n);
    tfw_writer_init(&w, out, (uint8_t)TFW_MAX_PAYLOAD);

    st = tfw_dispatch(cmd, &r, &w);
    authed = (uint8_t)(cmd == TFW_CMD_OTA_AUTH && st == TFW_OK);

    if ((unsigned)st > (unsigned)TFW_ERR_NO_STORE || st == TFW_ERR_UNKNOWN_CMD) {
        broke = "a status status.h does not name, or a known command reported unknown";
    } else if (tfw_writer_len(&w) > TFW_MAX_PAYLOAD) {
        broke = "the reply outgrew its buffer";
    } else if (n > TFW_MAX_PAYLOAD && st != TFW_ERR_BAD_ARGC) {
        broke = "an oversize request was not refused BAD_ARGC";
    } else if ((cmd == TFW_CMD_OTA_ACTIVATE || cmd == TFW_CMD_OTA_CONFIRM) && !was_authed
               && n == (cmd == TFW_CMD_OTA_ACTIVATE ? 1u : 0u)
               && !(cmd == TFW_CMD_OTA_ACTIVATE && payload[0] > 1u)
               && !retry && st != TFW_ERR_AUTH_REQUIRED) {
        broke = "a gated command without OTA_AUTH was not refused AUTH_REQUIRED";
    } else if (retry && st != TFW_OK && st != TFW_ERR_BAD_STATE && st != TFW_ERR_RANGE
               && st != TFW_ERR_BAD_ARGC) {
        broke = "an OTA_ACTIVATE while one was pending was neither its retry nor refused";
    } else if ((cmd == TFW_CMD_OTA_ACTIVATE || cmd == TFW_CMD_OTA_CONFIRM) && !was_authed
               && !retry && st == TFW_OK) {
        broke = "a gated command succeeded without OTA_AUTH";
    } else if ((cmd == TFW_CMD_OTA_ACTIVATE || cmd == TFW_CMD_OTA_CONFIRM) && !was_authed
               && (marked() != was_marked
                   || memcmp(mark_before, tfw_nor_model_mem() + TFW_OTA_STORE_MARK_ADDR, sizeof mark_before) != 0
                   || memcmp(confirm_rec, tfw_mock_eeprom() + TFW_BOOT_CONFIRM_ADDR, sizeof confirm_rec) != 0)) {
        broke = "a gated command without OTA_AUTH touched the MARK or the confirm request";
    } else if (cmd != TFW_CMD_OTA_ACTIVATE && !was_marked && marked()) {
        broke = "something other than OTA_ACTIVATE wrote the MARK";
    } else if (cmd != TFW_CMD_OTA_CONFIRM
               && tfw_mock_eeprom()[TFW_BOOT_CONFIRM_ADDR] != confirm_before) {
        broke = "something other than OTA_CONFIRM wrote the confirm request";
    }
    if (!broke && st == TFW_OK && (cmd == TFW_CMD_OTA_END || cmd == TFW_CMD_OTA_ACTIVATE)) {
        stage_status(&state, &received);
        if (!staged_is_the_valid_image(received)) {
            broke = "OTA_END or OTA_ACTIVATE took an image that is not the signed one";
        } else if (cmd == TFW_CMD_OTA_ACTIVATE
                   && (!marked()
                       || memcmp(tfw_nor_model_mem() + TFW_OTA_STORE_MARK_ADDR + 8u, meas, 32u) != 0)) {
            broke = "OTA_ACTIVATE reported success without marking the valid image";
        }
    }
    if (!broke && tfw_nor_model_violations() != 0u) {
        broke = "the NOR part saw a protocol violation";
    }
    if (broke) {
        printf("status %u, writer %u, authed %u; before it cmd 0x%02x len %u status %u\n",
               (unsigned)st, (unsigned)tfw_writer_len(&w), (unsigned)was_authed,
               prev_cmd, prev_len, (unsigned)prev_st);
        dump(cmd, payload, n);
        TEST_FAIL_MESSAGE(broke);
    }
    TEST_ASSERT_EQUAL_UINT8(cmd, tfw_dispatch_last());

    prev_cmd = cmd;
    prev_len = n;
    prev_st  = st;
    tally[k].runs++;
    tally[k].seen |= (uint32_t)1u << (unsigned)st;
    if (st == TFW_OK) { tally[k].ok++; } else { tally[k].refused++; }
    if (tfw_sys_reset_pending()) { tfw_sys_reset_state(); }
    free(in);
    free(out);
    return st;
}

/* ---------------------------------------------------------------- requests */

static unsigned req_begin(uint8_t *p, uint32_t total) { put32(p, total); return 4u; }

static unsigned req_data(uint8_t *p, uint32_t off, unsigned n)
{
    put32(p, off);
    memcpy(&p[4], &img[off < sizeof img - CHUNK ? off : 0u], n);
    return 4u + n;
}

static unsigned req_auth(uint8_t *p) { memset(p, 0xA5, TFW_OTA_AUTH_SIG_LEN); return TFW_OTA_AUTH_SIG_LEN; }

static void valid_stream(uint32_t from, uint32_t to)
{
    uint8_t  p[PAYLOAD_LIMIT];
    uint32_t off;
    for (off = from; off < to; off += CHUNK) {
        const unsigned n = (unsigned)(to - off < CHUNK ? to - off : CHUNK);
        TEST_ASSERT_EQUAL(TFW_OK, feed(TFW_CMD_OTA_DATA, p, req_data(p, off, n)));
    }
}

static void ok(uint8_t cmd, const uint8_t *p, unsigned n)
{
    TEST_ASSERT_EQUAL(TFW_OK, feed(cmd, p, n));
}

/* The state a mutated request lands in, built with valid requests. */
static void enter(ctx_t ctx)
{
    uint8_t p[PAYLOAD_LIMIT];

    reset_all();
    if (ctx == CTX_TRIAL) {
        uint8_t block[TFW_BOOT_HANDOFF_LEN], cdi[32] = { 0 }, m[32];
        memset(m, 0x3Cu, sizeof m);
        put_block(TFW_BOOT_STATE_TRIAL);
        tfw_mock_eeprom()[TFW_BOOT_CONFIRM_ADDR] = 0xFFu;
        tfw_boot_handoff_build(block, 0u, cdi, m, 3u, 0u);
        memcpy(tfw_mock_handoff(), block, sizeof block);
        tfw_boot_handoff_init();
        return;
    }
    if (ctx == CTX_FRESH) { return; }
    ok(TFW_CMD_OTA_BEGIN, p, req_begin(p, TOTAL));
    if (ctx == CTX_BEGUN) { return; }
    valid_stream(0u, ctx == CTX_MID ? 3u * CHUNK : TOTAL);
    if (ctx == CTX_MID || ctx == CTX_STREAMED) { return; }
    ok(TFW_CMD_OTA_END, p, 0u);
    if (ctx == CTX_ENDED) { return; }
    ok(TFW_CMD_OTA_AUTH, p, req_auth(p));
    p[0] = 0u;
    ok(TFW_CMD_OTA_ACTIVATE, p, 1u);
}

/* The context each command's mutants land in, and its valid request there.
 * OTA_ACTIVATE and OTA_CONFIRM get an OTA_AUTH first so the mutation reaches
 * past the gate; feed() still checks the gate on every one it was not given. */
static ctx_t seed_for(uint8_t cmd, uint8_t *p, unsigned *n)
{
    switch (cmd) {
    case TFW_CMD_OTA_BEGIN:    *n = req_begin(p, TOTAL);           return CTX_FRESH;
    case TFW_CMD_OTA_DATA:     *n = req_data(p, 3u * CHUNK, CHUNK); return CTX_MID;
    case TFW_CMD_OTA_END:      *n = 0u;                             return CTX_STREAMED;
    case TFW_CMD_OTA_STATUS:   *n = 0u;                             return CTX_MID;
    case TFW_CMD_OTA_INFO:     p[0] = 1u; *n = 1u;                  return CTX_MARKED;
    case TFW_CMD_OTA_ACTIVATE: p[0] = 0u; *n = 1u;                  return CTX_ENDED;
    case TFW_CMD_OTA_AUTH:     *n = req_auth(p);                    return CTX_ENDED;
    default:                   *n = 0u;                             return CTX_TRIAL;
    }
}

static void enter_for(uint8_t cmd, ctx_t ctx)
{
    uint8_t p[TFW_OTA_AUTH_SIG_LEN];
    enter(ctx);
    if (cmd == TFW_CMD_OTA_ACTIVATE || cmd == TFW_CMD_OTA_CONFIRM) {
        ok(TFW_CMD_OTA_AUTH, p, req_auth(p));
    }
}

static void try_one(uint8_t cmd, ctx_t ctx, const uint8_t *p, unsigned n)
{
    enter_for(cmd, ctx);
    feed(cmd, p, n);
}

static void mutate(uint8_t cmd)
{
    uint8_t  seed[PAYLOAD_LIMIT];
    uint8_t  buf[PAYLOAD_LIMIT];
    unsigned sn;
    unsigned n;
    unsigned i;
    unsigned k;
    ctx_t    ctx = seed_for(cmd, seed, &sn);

    try_one(cmd, ctx, seed, sn);

    /* truncation at every length, then the seed extended to every length */
    for (n = 0u; n < sn; n++) {
        try_one(cmd, ctx, seed, n);
    }
    for (n = sn + 1u; n <= PAYLOAD_LIMIT; n++) {
        memcpy(buf, seed, sn);
        random_fill(buf + sn, n - sn);
        try_one(cmd, ctx, buf, n);
    }

    /* every byte, and every little-endian u16 and u32 from it, set to the
     * values a length or offset goes wrong at: 0, max and max+1 of the field,
     * the bytes that follow, the declared total and the stage either side */
    for (i = 0u; i < sn; i++) {
        const uint32_t rest = sn - i - 1u;
        const uint32_t vals[10] = { 0u, rest, rest + 1u, 0xFFu, 0x100u, 0xFFFFu, 0x10000u,
                                    TOTAL, TFW_OTA_STORE_STAGE_LEN, 0xFFFFFFFFu };
        for (k = 0u; k < 10u; k++) {
            memcpy(buf, seed, sn);
            buf[i] = (uint8_t)vals[k];
            try_one(cmd, ctx, buf, sn);
            if (i + 1u < sn) {
                buf[i + 1u] = (uint8_t)(vals[k] >> 8);
                try_one(cmd, ctx, buf, sn);
            }
            if (i + 3u < sn) {
                put32(&buf[i], vals[k]);
                try_one(cmd, ctx, buf, sn);
                put32(&buf[i], vals[k] + 1u);
                try_one(cmd, ctx, buf, sn);
            }
        }
    }

    /* bit flips, then a random cut or a random tail */
    for (k = 0u; k < RANDOM_MUTANTS; k++) {
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
            const unsigned add = 1u + rnd() % (PAYLOAD_LIMIT - n + (n == PAYLOAD_LIMIT));
            if (n + add <= PAYLOAD_LIMIT) {
                random_fill(buf + n, add);
                n += add;
            }
            break;
        }
        default:
            break;
        }
        try_one(cmd, ctx, buf, n);
    }

    /* pure random payloads of every length, in every context */
    for (n = 0u; n <= PAYLOAD_LIMIT; n++) {
        for (k = 0u; k < RANDOM_PER_LEN; k++) {
            random_fill(buf, n);
            try_one(cmd, (ctx_t)(rnd() % 7u), buf, n);
        }
    }
}

static void report(void)
{
    unsigned i;
    for (i = 0u; i < NCMDS; i++) {
        printf("cmd 0x%02x: %u runs, %u ok, %u refused, statuses 0x%05lx\n", cmds[i],
               tally[i].runs, tally[i].ok, tally[i].refused, (unsigned long)tally[i].seen);
    }
}

static void test_mutated_requests_never_overrun_or_take_an_unsigned_image(void)
{
    unsigned i;

    rng = 0x6F7A1C35u;
    memset(tally, 0, sizeof tally);
    build();
    for (i = 0u; i < NCMDS; i++) {
        TEST_ASSERT_TRUE(tfw_dispatch_knows(cmds[i]));
        mutate(cmds[i]);
    }
    report();
    for (i = 0u; i < NCMDS; i++) {
        TEST_ASSERT_TRUE_MESSAGE(tally[i].ok > 0u, "an OTA command never accepted a request");
        TEST_ASSERT_TRUE_MESSAGE(tally[i].refused > 0u, "an OTA command never refused a request");
    }
}

/* A random request for a random command: valid for where a stream would be,
 * mutated, or noise. */
static void random_request(uint32_t *next_off)
{
    uint8_t        p[PAYLOAD_LIMIT];
    const uint8_t  cmd = cmds[rnd() % NCMDS];
    unsigned       n;
    tfw_status_t   st;

    switch (rnd() % 4u) {
    case 0u:                                    /* noise */
        n = rnd() % (PAYLOAD_LIMIT + 1u);
        random_fill(p, n);
        break;
    default:                                    /* the request a host would send */
        switch (cmd) {
        case TFW_CMD_OTA_BEGIN:
            n = req_begin(p, rnd() % 4u ? TOTAL : (rnd() % 3u) * 256u);
            break;
        case TFW_CMD_OTA_DATA: {
            const uint32_t off = rnd() % 4u ? *next_off : (rnd() % 10u) * CHUNK;
            n = req_data(p, off, off >= TOTAL ? CHUNK : (TOTAL - off < CHUNK ? TOTAL - off : CHUNK));
            break;
        }
        case TFW_CMD_OTA_INFO:
        case TFW_CMD_OTA_ACTIVATE:
            p[0] = (uint8_t)(rnd() % 3u);
            n = 1u;
            break;
        case TFW_CMD_OTA_AUTH:
            n = req_auth(p);
            break;
        default:
            n = 0u;
            break;
        }
        if (rnd() % 3u == 0u && n) {            /* and one bit of it flipped */
            const uint32_t at = rnd() % (n * 8u);
            p[at / 8u] ^= (uint8_t)(1u << (at % 8u));
        }
        break;
    }
    st = feed(cmd, p, n);
    if (cmd == TFW_CMD_OTA_BEGIN && st == TFW_OK) { *next_off = 0u; }
    if (cmd == TFW_CMD_OTA_DATA && st == TFW_OK) { *next_off += n - 4u; }
}

static void test_requests_in_any_order_keep_the_contracts(void)
{
    uint8_t  p[PAYLOAD_LIMIT];
    unsigned walk;
    unsigned step;
    uint32_t next_off;

    rng = 0x0B5E55EDu;
    memset(tally, 0, sizeof tally);
    build();

    /* the named misorderings first */
    reset_all();
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, feed(TFW_CMD_OTA_DATA, p, req_data(p, 0u, CHUNK)));   /* DATA before BEGIN */
    TEST_ASSERT_EQUAL(TFW_ERR_BUSY, feed(TFW_CMD_OTA_END, p, 0u));                         /* END before BEGIN */
    ok(TFW_CMD_OTA_AUTH, p, req_auth(p));
    p[0] = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, feed(TFW_CMD_OTA_ACTIVATE, p, 1u));               /* nothing staged */
    ok(TFW_CMD_OTA_AUTH, p, req_auth(p));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_STATE, feed(TFW_CMD_OTA_CONFIRM, p, 0u));                /* nothing on trial */
    TEST_ASSERT_FALSE(marked());
    enter(CTX_STREAMED);
    TEST_ASSERT_EQUAL(TFW_ERR_RANGE, feed(TFW_CMD_OTA_DATA, p, req_data(p, TOTAL, CHUNK))); /* past the length */
    ok(TFW_CMD_OTA_END, p, 0u);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, feed(TFW_CMD_OTA_END, p, 0u));                           /* END twice */
    enter(CTX_ENDED);
    p[0] = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, feed(TFW_CMD_OTA_ACTIVATE, p, 1u));
    TEST_ASSERT_FALSE(marked());

    /* a refused request between OTA_AUTH and ACTIVATE drops the hold (#137),
     * whatever refused it */
    enter(CTX_ENDED);
    ok(TFW_CMD_OTA_AUTH, p, req_auth(p));
    memset(p, 0xA5, 65u);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, feed(TFW_CMD_OTA_AUTH, p, 65u));            /* the table's length */
    p[0] = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, feed(TFW_CMD_OTA_ACTIVATE, p, 1u));
    TEST_ASSERT_FALSE(marked());
    enter(CTX_ENDED);
    ok(TFW_CMD_OTA_AUTH, p, req_auth(p));
    {
        tfw_reader_t r;
        tfw_writer_t w;
        uint8_t      out[TFW_MAX_PAYLOAD];
        TEST_ASSERT_FALSE(tfw_dispatch_knows(0xEEu));
        tfw_reader_init(&r, p, 0u);
        tfw_writer_init(&w, out, (uint8_t)sizeof out);
        TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, tfw_dispatch(0xEEu, &r, &w));       /* an unknown id */
    }
    p[0] = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, feed(TFW_CMD_OTA_ACTIVATE, p, 1u));
    TEST_ASSERT_FALSE(marked());
    enter(CTX_ENDED);
    ok(TFW_CMD_OTA_AUTH, p, req_auth(p));
    memset(p, 0xA5, 65u);
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, feed(TFW_CMD_OTA_AUTH, p, 65u));
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, feed(TFW_CMD_OTA_CONFIRM, p, 0u));      /* the auth gate */
    p[0] = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, feed(TFW_CMD_OTA_ACTIVATE, p, 1u));
    TEST_ASSERT_FALSE(marked());
    enter(CTX_ENDED);
    ok(TFW_CMD_OTA_AUTH, p, req_auth(p));
    TEST_ASSERT_EQUAL(TFW_ERR_BAD_ARGC, feed(TFW_CMD_OTA_AUTH, p, 10u));            /* the handler */
    p[0] = 0u;
    TEST_ASSERT_EQUAL(TFW_ERR_AUTH_REQUIRED, feed(TFW_CMD_OTA_ACTIVATE, p, 1u));
    TEST_ASSERT_FALSE(marked());

    /* random walks from every starting state */
    for (walk = 0u; walk < WALKS; walk++) {
        enter((ctx_t)(walk % 7u));
        next_off = walk % 7u == CTX_MID ? 3u * CHUNK : (walk % 7u == CTX_BEGUN ? 0u : TOTAL);
        for (step = 0u; step < WALK_STEPS; step++) {
            random_request(&next_off);
        }
    }

    /* the valid sequence, with one random step perturbed or a random request
     * slipped in before it */
    for (walk = 0u; walk < NOISY_RUNS; walk++) {
        const unsigned at = rnd() % 13u;   /* BEGIN, 9 DATA, END, AUTH, ACTIVATE */
        uint32_t       off = 0u;
        reset_all();
        next_off = 0u;
        for (step = 0u; step < 13u; step++) {
            unsigned n;
            uint8_t  cmd;
            if (step == at && rnd() % 2u) {
                random_request(&next_off);
            }
            if (step == 0u) {
                cmd = TFW_CMD_OTA_BEGIN; n = req_begin(p, TOTAL);
            } else if (step <= 9u) {
                cmd = TFW_CMD_OTA_DATA;
                n = req_data(p, off, TOTAL - off < CHUNK ? TOTAL - off : CHUNK);
            } else if (step == 10u) {
                cmd = TFW_CMD_OTA_END; n = 0u;
            } else if (step == 11u) {
                cmd = TFW_CMD_OTA_AUTH; n = req_auth(p);
            } else {
                cmd = TFW_CMD_OTA_ACTIVATE; p[0] = 0u; n = 1u;
            }
            if (step == at && n) {
                const uint32_t bit = rnd() % (n * 8u);
                p[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            }
            if (feed(cmd, p, n) == TFW_OK && cmd == TFW_CMD_OTA_DATA) {
                off += n - 4u;
            }
        }
    }
    report();
    for (step = 0u; step < NCMDS; step++) {
        TEST_ASSERT_TRUE_MESSAGE(tally[step].ok > 0u, "an OTA command never accepted a request");
        TEST_ASSERT_TRUE_MESSAGE(tally[step].refused > 0u, "an OTA command never refused a request");
    }
}

/* test_ota_stage's valid sequences, after the run. */
static void test_the_valid_sequences_still_succeed_after_the_run(void)
{
    uint8_t  p[PAYLOAD_LIMIT];
    uint8_t  state;
    uint32_t received;
    uint8_t *rec;

    build();
    reset_all();
    ok(TFW_CMD_OTA_BEGIN, p, req_begin(p, TOTAL));
    valid_stream(0u, TOTAL);
    ok(TFW_CMD_OTA_END, p, 0u);
    stage_status(&state, &received);
    TEST_ASSERT_EQUAL_UINT8(TFW_STAGE_COMPLETE, state);
    TEST_ASSERT_EQUAL_UINT32(TOTAL, received);
    TEST_ASSERT_FALSE(marked());
    ok(TFW_CMD_OTA_AUTH, p, req_auth(p));
    p[0] = 0u;
    ok(TFW_CMD_OTA_ACTIVATE, p, 1u);
    TEST_ASSERT_TRUE(marked());
    TEST_ASSERT_EQUAL_MEMORY(img, tfw_nor_model_mem() + TFW_OTA_STORE_STAGE_ADDR, TOTAL);
    TEST_ASSERT_EQUAL_MEMORY(meas, tfw_nor_model_mem() + TFW_OTA_STORE_MARK_ADDR + 8u, 32u);

    enter(CTX_TRIAL);
    rec = tfw_mock_eeprom() + TFW_BOOT_CONFIRM_ADDR;
    ok(TFW_CMD_OTA_AUTH, p, req_auth(p));
    ok(TFW_CMD_OTA_CONFIRM, p, 0u);
    TEST_ASSERT_EQUAL_HEX8(TFW_BOOT_CONFIRM_MAGIC, rec[0]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mutated_requests_never_overrun_or_take_an_unsigned_image);
    RUN_TEST(test_requests_in_any_order_keep_the_contracts);
    RUN_TEST(test_the_valid_sequences_still_succeed_after_the_run);
    return UNITY_END();
}
