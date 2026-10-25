/*
 * Single-slot OTA (#163) under seeded mutation, linked as test_ota_single
 * is: the staging commands, OTA_AUTH and OTA_ACTIVATE through the dispatcher
 * against the software NOR part, with neither OTA_INFO nor OTA_CONFIRM in
 * the table.
 *
 * Requests go in any order, mutated: noise, a flipped bit, a request dropped,
 * repeated, swapped, cut short or grown, another command id, a chunk sent at
 * another offset. Every answer must keep the contracts test_ota_mutate keeps
 * for the staged build: a status status.h names, an oversize request refused
 * BAD_ARGC, OTA_ACTIVATE refused AUTH_REQUIRED without a held OTA_AUTH, the
 * MARK written by OTA_ACTIVATE alone, and an OK OTA_END or OTA_ACTIVATE only
 * over the image that was built, so a mutated image never activates. After
 * each run a clean sequence must still activate as test_ota_single pins.
 */
#include <stdio.h>
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
#include "handoff.h"
#include "tfw/protocol.h"
#include "tfw/status.h"
#include "dispatch.h"

#define SEED          0x51A60163u
#define RUNS          40u
#define STEPS         100u
#define STREAMS       1500u
#define TOTAL         512u
#define BODY          200u
#define TRAILER       128u
#define CHUNK         60u      /* TFW_MAX_PAYLOAD less the offset */
#define PAYLOAD_LIMIT (TFW_MAX_PAYLOAD + 1u)
#define AUTH_TABLE_MAX 64u     /* OTA_AUTH's bound in commands.def */
#define STREAM_MAX    20u

typedef struct {
    uint8_t  cmd;
    unsigned n;
    uint8_t  p[PAYLOAD_LIMIT];
} req_t;

static const uint8_t cmds[] = {
    TFW_CMD_OTA_BEGIN, TFW_CMD_OTA_DATA, TFW_CMD_OTA_END, TFW_CMD_OTA_STATUS,
    TFW_CMD_OTA_ACTIVATE, TFW_CMD_OTA_AUTH, TFW_CMD_OTA_INFO, TFW_CMD_OTA_CONFIRM,
};
#define NCMDS (sizeof cmds / sizeof cmds[0])

static uint8_t  img[TFW_OTA_STORE_STAGE_LEN];
static uint8_t  meas[32];
static uint32_t rng;
static uint8_t  held;                   /* an OTA_AUTH the firmware still holds */
static uint32_t fed, ok, refused, activated, unknown;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
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

/* The image test_ota_single builds: 512 bytes, a 200-byte body, the 328P. */
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

static void put_good_block(void)
{
    uint8_t             raw[TFW_BOOT_EE_BLOCK_LEN];
    tfw_boot_platform_t p;

    memset(&p, 0, sizeof p);
    p.rollback_floor = 1u;
    p.boot_state     = TFW_BOOT_STATE_GOOD;
    p.ota_state      = TFW_BOOT_OTA_IDLE;
    tfw_boot_platform_serialise(&p, raw);
    TEST_ASSERT_EQUAL(TFW_OK, tfw_hal_eeprom_write(TFW_BOOT_EE_BASE, raw, (uint8_t)TFW_BOOT_EE_BLOCK_LEN));
}

static int marked(void)
{
    const uint8_t *m = tfw_nor_model_mem() + TFW_OTA_STORE_MARK_ADDR;
    return m[0] == 0x54u && m[1] == 0x46u && m[2] == 0x53u && m[3] == 0x47u;   /* "TFSG" */
}

static void reset_all(void)
{
    tfw_mock_reset();
    tfw_nor_model_reset();
    tfw_sys_reset_state();
    tfw_boot_handoff_init();
    tfw_stage_init();
    put_good_block();
    (void)tfw_ota_auth_take();
    held = 0u;
}

void setUp(void)
{
    rng = SEED;
    fed = ok = refused = activated = unknown = 0u;
    build();
    reset_all();
}
void tearDown(void) {}

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

static int registered(uint8_t cmd)
{
    return cmd != TFW_CMD_OTA_INFO && cmd != TFW_CMD_OTA_CONFIRM;
}

static void dump(const req_t *q)
{
    unsigned i;

    printf("cmd 0x%02x len %u:", q->cmd, q->n);
    for (i = 0u; i < q->n; i++) { printf(" %02x", q->p[i]); }
    printf("\n");
}

static tfw_status_t feed(const req_t *q)
{
    uint8_t       in[PAYLOAD_LIMIT];
    uint8_t       out[TFW_MAX_PAYLOAD];
    const uint8_t was_held   = held;
    const int     was_marked = marked();
    const int     gated      = q->cmd == TFW_CMD_OTA_ACTIVATE && q->n == 1u && q->p[0] <= 1u;
    /* While an activation is pending, an OTA_ACTIVATE without OTA_AUTH is the
     * retry of that exchange or a refused new attempt (m1117 (1)). */
    const int     retry      = gated && !was_held && tfw_stage_install_pending() != 0u;
    tfw_reader_t  r;
    tfw_writer_t  w;
    tfw_status_t  st;
    const char   *broke = NULL;
    uint8_t       state;
    uint32_t      received;

    memcpy(in, q->p, q->n);
    tfw_reader_init(&r, in, (uint8_t)q->n);
    tfw_writer_init(&w, out, (uint8_t)sizeof out);
    st = tfw_dispatch(q->cmd, &r, &w);

    /* OTA_AUTH replaces the hold when its handler runs; OTA_ACTIVATE takes
     * it once the mode is in range. */
    if (q->cmd == TFW_CMD_OTA_AUTH && q->n <= AUTH_TABLE_MAX) { held = (uint8_t)(st == TFW_OK); }
    if (gated) { held = 0u; }

    if (!registered(q->cmd)) {
        if (st != TFW_ERR_UNKNOWN_CMD) { broke = "a command this link leaves out was not refused UNKNOWN_CMD"; }
    } else if ((unsigned)st > (unsigned)TFW_ERR_NO_STORE || st == TFW_ERR_UNKNOWN_CMD) {
        broke = "a status status.h does not name, or a known command reported unknown";
    } else if (q->n > TFW_MAX_PAYLOAD && st != TFW_ERR_BAD_ARGC) {
        broke = "an oversize request was not refused BAD_ARGC";
    } else if (retry && st != TFW_OK && st != TFW_ERR_BAD_STATE) {
        broke = "OTA_ACTIVATE while one was pending was neither its retry nor refused";
    } else if (gated && !was_held && !retry && st != TFW_ERR_AUTH_REQUIRED) {
        broke = "OTA_ACTIVATE without OTA_AUTH was not refused AUTH_REQUIRED";
    } else if (q->cmd == TFW_CMD_OTA_ACTIVATE && st == TFW_OK && !was_held && !retry) {
        broke = "OTA_ACTIVATE succeeded without OTA_AUTH";
    }
    if (!broke && tfw_writer_len(&w) > TFW_MAX_PAYLOAD) {
        broke = "the reply outgrew its buffer";
    }
    if (!broke && q->cmd != TFW_CMD_OTA_ACTIVATE && !was_marked && marked()) {
        broke = "something other than OTA_ACTIVATE wrote the MARK";
    }
    if (!broke && st == TFW_OK && (q->cmd == TFW_CMD_OTA_END || q->cmd == TFW_CMD_OTA_ACTIVATE)) {
        stage_status(&state, &received);
        if (!staged_is_the_valid_image(received)) {
            broke = "OTA_END or OTA_ACTIVATE took an image that is not the one built";
        } else if (q->cmd == TFW_CMD_OTA_ACTIVATE
                   && (!marked()
                       || memcmp(tfw_nor_model_mem() + TFW_OTA_STORE_MARK_ADDR + 8u, meas, 32u) != 0)) {
            broke = "OTA_ACTIVATE reported success without marking the image";
        }
    }
    if (!broke && tfw_nor_model_violations() != 0u) {
        broke = "the NOR part saw a protocol violation";
    }
    if (broke) {
        printf("status %u, writer %u, held %u\n", (unsigned)st, (unsigned)tfw_writer_len(&w), (unsigned)was_held);
        dump(q);
        TEST_FAIL_MESSAGE(broke);
    }
    TEST_ASSERT_EQUAL_UINT8(q->cmd, tfw_dispatch_last());

    fed++;
    if (st == TFW_OK) { ok++; } else { refused++; }
    if (st == TFW_ERR_UNKNOWN_CMD) { unknown++; }
    if (q->cmd == TFW_CMD_OTA_ACTIVATE && st == TFW_OK) { activated++; }
    if (tfw_sys_reset_pending()) { tfw_sys_reset_state(); }
    return st;
}

static void req_begin(req_t *q, uint32_t total)
{
    q->cmd = TFW_CMD_OTA_BEGIN;
    put32(q->p, total);
    q->n = 4u;
}

static void req_data(req_t *q, uint32_t off)
{
    const uint32_t at = off < TOTAL ? off : 0u;
    const unsigned n  = TOTAL - at < CHUNK ? TOTAL - at : CHUNK;

    q->cmd = TFW_CMD_OTA_DATA;
    put32(q->p, off);
    memcpy(&q->p[4], &img[at], n);
    q->n = 4u + n;
}

static void req_auth(req_t *q)
{
    q->cmd = TFW_CMD_OTA_AUTH;
    memset(q->p, 0xA5, TFW_OTA_AUTH_SIG_LEN);
    q->n = TFW_OTA_AUTH_SIG_LEN;
}

static void req_bare(req_t *q, uint8_t cmd, unsigned n, uint8_t first)
{
    q->cmd  = cmd;
    q->p[0] = first;
    q->n    = n;
}

/* BEGIN, the chunks in order, END, AUTH, ACTIVATE(0): test_ota_single's. */
static unsigned valid_stream(req_t *s)
{
    unsigned k = 0u;
    uint32_t off;

    req_begin(&s[k++], TOTAL);
    for (off = 0u; off < TOTAL; off += CHUNK) { req_data(&s[k++], off); }
    req_bare(&s[k++], TFW_CMD_OTA_END, 0u, 0u);
    req_auth(&s[k++]);
    req_bare(&s[k++], TFW_CMD_OTA_ACTIVATE, 1u, 0u);
    return k;
}

static void random_fill(uint8_t *p, unsigned n)
{
    unsigned i;

    for (i = 0u; i < n; i++) { p[i] = (uint8_t)rnd(); }
}

static void flip_bit(req_t *q)
{
    if (q->n) {
        const uint32_t at = rnd() % (q->n * 8u);
        q->p[at / 8u] ^= (uint8_t)(1u << (at % 8u));
    }
}

static void random_request(req_t *q, uint32_t next_off)
{
    const uint8_t cmd = cmds[rnd() % NCMDS];

    if (rnd() % 4u == 0u) {                     /* noise */
        q->cmd = cmd;
        q->n   = rnd() % (PAYLOAD_LIMIT + 1u);
        random_fill(q->p, q->n);
        return;
    }
    switch (cmd) {                              /* the request a host would send */
    case TFW_CMD_OTA_BEGIN:
        req_begin(q, rnd() % 4u ? TOTAL : (rnd() % 3u) * 256u);
        break;
    case TFW_CMD_OTA_DATA:
        req_data(q, rnd() % 4u ? next_off : (rnd() % 10u) * CHUNK);
        break;
    case TFW_CMD_OTA_AUTH:
        req_auth(q);
        break;
    case TFW_CMD_OTA_ACTIVATE:
    case TFW_CMD_OTA_INFO:
        req_bare(q, cmd, 1u, (uint8_t)(rnd() % 3u));
        break;
    default:
        req_bare(q, cmd, 0u, 0u);
        break;
    }
    if (rnd() % 3u == 0u) { flip_bit(q); }      /* and one bit of it flipped */
}

/* After any run: test_ota_single's setUp and its pinned sequence, with the
 * RAM state the run left behind. */
static void clean_sequence_activates(void)
{
    req_t    s[STREAM_MAX];
    unsigned k, n;
    uint32_t before = activated;

    tfw_nor_model_reset();
    tfw_stage_init();   /* a boot: the requests before may have spent the OTA_BEGIN budget (#232) */
    put_good_block();
    TEST_ASSERT_FALSE(marked());
    n = valid_stream(s);
    for (k = 0u; k < n; k++) {
        TEST_ASSERT_EQUAL(TFW_OK, feed(&s[k]));
        if (k + 3u == n) { TEST_ASSERT_FALSE(marked()); }   /* staged, not marked */
    }
    TEST_ASSERT_TRUE(marked());
    TEST_ASSERT_EQUAL_UINT32(before + 1u, activated);
}

static void test_requests_in_any_order_keep_the_contracts(void)
{
    uint32_t run, step;

    for (run = 0u; run < RUNS; run++) {
        uint32_t next_off = 0u;

        reset_all();
        for (step = 0u; step < STEPS; step++) {
            req_t        q;
            tfw_status_t st;

            random_request(&q, next_off);
            st = feed(&q);
            if (q.cmd == TFW_CMD_OTA_BEGIN && st == TFW_OK) { next_off = 0u; }
            if (q.cmd == TFW_CMD_OTA_DATA && st == TFW_OK) { next_off += q.n - 4u; }
        }
        clean_sequence_activates();
    }
    TEST_ASSERT_EQUAL_UINT32(RUNS * (STEPS + 13u), fed);
    TEST_ASSERT_TRUE(refused > RUNS * STEPS / 4u);
    TEST_ASSERT_TRUE(ok > RUNS * STEPS / 8u);
    TEST_ASSERT_TRUE(unknown > 0u);
}

/* One mutation of the valid stream; returns its new length. */
static unsigned mutate(req_t *s, unsigned n)
{
    const unsigned at = rnd() % n;
    unsigned       i;

    switch (rnd() % 8u) {
    case 0u:                                    /* a flipped bit */
        flip_bit(&s[at]);
        break;
    case 1u:                                    /* a request dropped */
        for (i = at; i + 1u < n; i++) { s[i] = s[i + 1u]; }
        return n - 1u;
    case 2u:                                    /* a request sent twice */
        if (n < STREAM_MAX) {
            for (i = n; i > at; i--) { s[i] = s[i - 1u]; }
            return n + 1u;
        }
        break;
    case 3u:                                    /* two neighbours swapped */
        if (at + 1u < n) {
            req_t t = s[at];
            s[at] = s[at + 1u];
            s[at + 1u] = t;
        }
        break;
    case 4u:                                    /* cut short */
        if (s[at].n) { s[at].n -= 1u + rnd() % s[at].n; }
        break;
    case 5u:                                    /* grown by a byte */
        if (s[at].n < PAYLOAD_LIMIT) { s[at].p[s[at].n++] = (uint8_t)rnd(); }
        break;
    case 6u:                                    /* another command id */
        s[at].cmd = cmds[rnd() % NCMDS];
        break;
    default:                                    /* a chunk sent at another offset */
        if (s[at].cmd == TFW_CMD_OTA_DATA) { put32(s[at].p, (rnd() % 10u) * CHUNK + rnd() % 2u); }
        else { flip_bit(&s[at]); }
        break;
    }
    return n;
}

static void test_a_mutated_stream_never_activates_another_image(void)
{
    uint32_t i, streams_activated = 0u, streams_refused = 0u;

    for (i = 0u; i < STREAMS; i++) {
        req_t    s[STREAM_MAX];
        unsigned n = valid_stream(s), k;
        uint32_t before = activated;

        n = mutate(s, n);
        if (rnd() % 4u == 0u) { n = mutate(s, n); }
        tfw_nor_model_reset();
        tfw_stage_init();   /* each stream a boot: the OTA_BEGIN budget is per boot (#232) */
        put_good_block();
        for (k = 0u; k < n; k++) { (void)feed(&s[k]); }
        if (activated != before) { streams_activated++; } else { streams_refused++; }
    }
    TEST_ASSERT_EQUAL_UINT32(STREAMS, streams_activated + streams_refused);
    TEST_ASSERT_TRUE(streams_activated > STREAMS / 10u);
    TEST_ASSERT_TRUE(streams_refused > STREAMS / 4u);
    clean_sequence_activates();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_requests_in_any_order_keep_the_contracts);
    RUN_TEST(test_a_mutated_stream_never_activates_another_image);
    return UNITY_END();
}
