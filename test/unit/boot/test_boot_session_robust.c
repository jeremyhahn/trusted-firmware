/*
 * test_boot_session_robust.c -- the recovery session's request handler and the
 * boot request, fed hostile input from a fixed seed (row secure-boot, #163).
 *
 * test_boot_session.c holds the cases someone thought of. This suite drives
 * tfw_boot_session_handle with seeded sessions -- well-formed requests for the
 * state the session is in, the same requests truncated, extended, bit-flipped
 * or relabelled, and random command ids with random payloads -- and checks
 * every reply against the handler's contract. The PRNG is the test's own and
 * its seed is a literal, so every run is the same run and a failure names the
 * iteration that produced it.
 *
 * The level is the handler's: the framer that hands it a payload, and so the
 * one reply per frame, is recovery_ota.c over src/proto/frame.c, which the
 * pattern rule does not link (test_boot_recovery_session has its own rule).
 */

#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "boot_session.h"
#include "boot_ota.h"
#include "boot_request.h"
#include "boot_hal_mock.h"

#define SEED        0x5EED0163uL
#define SESSIONS    600u
#define STEPS       80u
#define ID_ROUNDS   16u
#define REQ_ROUNDS  20000u
#define IMG_MAX     (8u * TFW_OTA_PAGE_MAX)
#define CANARY      0xC3u

static uint32_t rng;

/* xorshift32: small, and the same sequence on every host. */
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

static tfw_ota_t     ota;
static uint8_t       shadow[IMG_MAX];  /* what the session wrote, by offset */
static uint8_t       model[IMG_MAX];   /* what the accepted chunks carried */
static uint32_t      next_page;        /* the offset the next page write must carry */
static unsigned long commits, refusals, accepted;

/* The page writer. Every page lands in order, inside what OTA_BEGIN declared:
 * the write cursor is never rewound and never runs past the image. */
static tfw_status_t sink(uint32_t addr, const uint8_t *data, uint16_t len)
{
    uint32_t off = addr - (uint32_t)TFW_OTA_STAGE_DEST;

    TEST_ASSERT_EQUAL_UINT16(ota.page_size, len);
    TEST_ASSERT_EQUAL_UINT32(next_page, off);
    TEST_ASSERT_TRUE(off + len <= ota.declared_len);
    if (off + len <= IMG_MAX) {
        memcpy(&shadow[off], data, len);
    }
    next_page = off + len;
    return TFW_OK;
}

typedef struct {
    uint8_t  state;
    uint32_t declared_len;
    uint32_t received;
    uint16_t buf_used;
    uint8_t  buf[TFW_OTA_PAGE_MAX];
    uint32_t next_page;
} snap_t;

static void take(snap_t *s)
{
    memset(s, 0, sizeof *s);
    s->state        = ota.state;
    s->declared_len = ota.declared_len;
    s->received     = ota.received;
    s->buf_used     = ota.buf_used;
    memcpy(s->buf, ota.buf, ota.buf_used);
    s->next_page    = next_page;
}

static void assert_same(const snap_t *a, const snap_t *b)
{
    TEST_ASSERT_EQUAL_UINT8(a->state, b->state);
    TEST_ASSERT_EQUAL_UINT32(a->declared_len, b->declared_len);
    TEST_ASSERT_EQUAL_UINT32(a->received, b->received);
    TEST_ASSERT_EQUAL_UINT16(a->buf_used, b->buf_used);
    TEST_ASSERT_EQUAL_MEMORY(a->buf, b->buf, sizeof a->buf);
    TEST_ASSERT_EQUAL_UINT32(a->next_page, b->next_page);
}

static int known_cmd(uint8_t cmd)
{
    return cmd == TFW_CMD_PING || cmd == TFW_CMD_RESET || cmd == TFW_CMD_OTA_BEGIN
           || cmd == TFW_CMD_OTA_DATA || cmd == TFW_CMD_OTA_END
           || cmd == TFW_CMD_OTA_STATUS;
}

/* What this handler returns. Write failures are not injected here, so
 * TFW_ERR_PROTECTED_REGION and the writer's own statuses cannot occur. */
static int known_status(tfw_status_t st)
{
    return st == TFW_OK || st == TFW_ERR_BAD_ARGC || st == TFW_ERR_RANGE
           || st == TFW_ERR_BUSY || st == TFW_ERR_FRAMING
           || st == TFW_ERR_UNKNOWN_CMD;
}

static uint32_t begin_len(void)
{
    const uint32_t page = ota.page_size;

    switch (rnd() % 8u) {
    case 0:  return 0u;
    case 1:  return (uint32_t)TFW_BOOT_TRAILER_LEN;
    case 2:  return (uint32_t)TFW_BOOT_APP_SIZE + page;
    case 3:  return 0xFFFFFFFFuL;
    case 4:  return page * (1u + rnd() % 8u) + 1u + rnd() % (page - 1u);
    default: return page * (1u + rnd() % 8u);
    }
}

/* A request well-formed for the session's state, then perhaps mutated. */
static uint8_t make_request(uint8_t *cmd, uint8_t p[TFW_MAX_PAYLOAD])
{
    uint8_t len = 0u;

    switch (rnd() % 10u) {
    case 0:
        *cmd = TFW_CMD_OTA_BEGIN;
        wr_u32(p, begin_len());
        len = 4u;
        break;
    case 1: case 2: case 3: case 4: {
        uint32_t left = ota.declared_len - ota.received;
        uint32_t room = TFW_MAX_PAYLOAD - 4u;
        uint32_t n    = 1u + rnd() % (left != 0u && left < room ? left : room);

        *cmd = TFW_CMD_OTA_DATA;
        wr_u32(p, ota.received);
        for (uint32_t i = 0u; i < n; i++) {
            p[4u + i] = (uint8_t)rnd();
        }
        len = (uint8_t)(4u + n);
        break;
    }
    case 5:
        *cmd = TFW_CMD_OTA_END;
        break;
    case 6:
        *cmd = TFW_CMD_OTA_STATUS;
        break;
    case 7:
        *cmd = TFW_CMD_PING;
        len  = (uint8_t)(rnd() % (TFW_MAX_PAYLOAD + 1u));
        for (uint8_t i = 0u; i < len; i++) {
            p[i] = (uint8_t)rnd();
        }
        break;
    case 8:
        *cmd = TFW_CMD_RESET;
        p[0] = (uint8_t)rnd();
        len  = 1u;
        break;
    default:
        *cmd = (uint8_t)rnd();
        len  = (uint8_t)(rnd() % (TFW_MAX_PAYLOAD + 1u));
        for (uint8_t i = 0u; i < len; i++) {
            p[i] = (uint8_t)rnd();
        }
        break;
    }

    if (rnd() % 3u != 0u) {
        return len;
    }
    switch (rnd() % 5u) {
    case 0:                                         /* bit flips */
        if (len != 0u) {
            for (uint32_t k = 1u + rnd() % 4u; k > 0u; k--) {
                p[rnd() % len] ^= (uint8_t)(1u << (rnd() % 8u));
            }
        }
        break;
    case 1:                                         /* truncated */
        len = (uint8_t)(rnd() % (len + 1u));
        break;
    case 2:                                         /* over-long */
        if (len < TFW_MAX_PAYLOAD) {
            uint8_t to = (uint8_t)(len + 1u + rnd() % (TFW_MAX_PAYLOAD - len));
            for (uint8_t i = len; i < to; i++) {
                p[i] = (uint8_t)rnd();
            }
            len = to;
        }
        break;
    case 3:                                         /* relabelled */
        *cmd = (uint8_t)rnd();
        break;
    default:                                        /* offset or length edge */
        if (len >= 4u) {
            static const uint32_t edges[] = {
                0u, 1u, 0x7FFFFFFFuL, 0x80000000uL, 0xFFFFFFFFuL,
                (uint32_t)TFW_BOOT_APP_SIZE, (uint32_t)TFW_BOOT_APP_SIZE + 1u,
            };
            wr_u32(p, edges[rnd() % (sizeof edges / sizeof edges[0])]
                      + (uint32_t)(rnd() % 3u));
        }
        break;
    }
    return len;
}

/* One request, and every assertion its reply has to meet. */
static void step(uint8_t cmd, const uint8_t *src, uint8_t plen)
{
    /* Exactly plen bytes, and a response buffer the size the recovery loop
     * gives it: a read or write past either is an ASan report. */
    uint8_t                  *payload = malloc(plen != 0u ? plen : 1u);
    uint8_t                  *resp    = malloc(TFW_MAX_PAYLOAD);
    uint8_t                   rlen    = 0xEEu;
    tfw_boot_session_action_t action  = (tfw_boot_session_action_t)0x5A;
    snap_t                    before, after;
    tfw_status_t              st;

    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_NOT_NULL(resp);
    memcpy(payload, src, plen);
    memset(resp, CANARY, TFW_MAX_PAYLOAD);
    take(&before);

    st = tfw_boot_session_handle(&ota, cmd, payload, plen, resp, &rlen, &action);
    take(&after);

    TEST_ASSERT_TRUE(known_status(st));
    TEST_ASSERT_TRUE(rlen <= TFW_MAX_PAYLOAD);
    for (uint32_t i = rlen; i < TFW_MAX_PAYLOAD; i++) {
        TEST_ASSERT_EQUAL_HEX8(CANARY, resp[i]);
    }
    TEST_ASSERT_TRUE(after.state <= (uint8_t)TFW_OTA_FAILED);
    TEST_ASSERT_TRUE(after.received <= after.declared_len || after.declared_len == 0u);
    TEST_ASSERT_TRUE(after.buf_used < ota.page_size);

    if (!known_cmd(cmd)) {
        TEST_ASSERT_EQUAL(TFW_ERR_UNKNOWN_CMD, st);
    }

    if (st != TFW_OK) {
        refusals++;
        TEST_ASSERT_EQUAL_UINT8(0u, rlen);
        TEST_ASSERT_EQUAL(TFW_BOOT_SESSION_CONTINUE, action);
        /* A refusal leaves the session as it was. The one exception is
         * OTA_END on a short transfer, which fails the session on purpose
         * (boot_ota.c tfw_ota_end): it moves the state, and nothing else. */
        if (cmd == TFW_CMD_OTA_END && st == TFW_ERR_FRAMING) {
            TEST_ASSERT_EQUAL_UINT8((uint8_t)TFW_OTA_RECEIVING, before.state);
            TEST_ASSERT_EQUAL_UINT8((uint8_t)TFW_OTA_FAILED, after.state);
            before.state = after.state;
        }
        assert_same(&before, &after);
        free(payload);
        free(resp);
        return;
    }

    accepted++;
    if (cmd != TFW_CMD_OTA_END && cmd != TFW_CMD_RESET) {
        TEST_ASSERT_EQUAL(TFW_BOOT_SESSION_CONTINUE, action);
    }
    switch (cmd) {
    case TFW_CMD_PING:
        TEST_ASSERT_EQUAL_UINT8(plen, rlen);
        if (plen != 0u) {
            TEST_ASSERT_EQUAL_MEMORY(payload, resp, plen);
        }
        assert_same(&before, &after);
        break;
    case TFW_CMD_RESET:
        TEST_ASSERT_EQUAL_UINT8(1u, plen);
        TEST_ASSERT_EQUAL_UINT8(0u, rlen);
        TEST_ASSERT_EQUAL(TFW_BOOT_SESSION_RESET, action);
        assert_same(&before, &after);
        break;
    case TFW_CMD_OTA_BEGIN:
        TEST_ASSERT_EQUAL_UINT8(4u, plen);
        TEST_ASSERT_EQUAL_UINT8(0u, rlen);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)TFW_OTA_RECEIVING, after.state);
        TEST_ASSERT_EQUAL_UINT32(rd_u32(payload), after.declared_len);
        TEST_ASSERT_TRUE(after.declared_len > TFW_BOOT_TRAILER_LEN);
        TEST_ASSERT_TRUE(after.declared_len <= TFW_BOOT_APP_SIZE);
        TEST_ASSERT_EQUAL_UINT32(0u, after.declared_len % ota.page_size);
        TEST_ASSERT_EQUAL_UINT32(0u, after.received);
        TEST_ASSERT_EQUAL_UINT16(0u, after.buf_used);
        next_page = 0u;
        memset(model, 0xFF, sizeof model);
        memset(shadow, 0xFF, sizeof shadow);
        break;
    case TFW_CMD_OTA_DATA: {
        uint32_t off = rd_u32(payload);
        uint32_t n   = (uint32_t)plen - 4u;

        TEST_ASSERT_TRUE(plen >= 5u);
        TEST_ASSERT_EQUAL_UINT8(0u, rlen);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)TFW_OTA_RECEIVING, before.state);
        TEST_ASSERT_EQUAL_UINT32(before.received, off);
        TEST_ASSERT_EQUAL_UINT32(before.received + n, after.received);
        TEST_ASSERT_EQUAL_UINT32(before.declared_len, after.declared_len);
        if (off + n <= IMG_MAX) {
            memcpy(&model[off], &payload[4], n);
        }
        break;
    }
    case TFW_CMD_OTA_END:
        TEST_ASSERT_EQUAL_UINT8(0u, plen);
        TEST_ASSERT_EQUAL_UINT8(0u, rlen);
        TEST_ASSERT_EQUAL(TFW_BOOT_SESSION_COMMIT, action);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)TFW_OTA_COMPLETE, after.state);
        TEST_ASSERT_EQUAL_UINT32(after.declared_len, after.received);
        TEST_ASSERT_EQUAL_UINT32(after.declared_len, next_page);
        /* Every byte the session accepted is the byte it wrote, where the
         * chunk's offset said. */
        TEST_ASSERT_TRUE(after.declared_len <= IMG_MAX);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(model, shadow, after.declared_len);
        commits++;
        break;
    case TFW_CMD_OTA_STATUS:
        TEST_ASSERT_EQUAL_UINT8(0u, plen);
        TEST_ASSERT_EQUAL_UINT8(5u, rlen);
        TEST_ASSERT_EQUAL_UINT8(after.state, resp[0]);
        TEST_ASSERT_EQUAL_UINT32(after.received, rd_u32(&resp[1]));
        assert_same(&before, &after);
        break;
    default:
        TEST_FAIL_MESSAGE("an unknown command was accepted");
        break;
    }
    free(payload);
    free(resp);
}

static void open_session(void)
{
    static const uint16_t pages[] = { 32u, 64u, 128u, 256u };

    TEST_ASSERT_EQUAL(TFW_OK, tfw_ota_init(&ota, sink, pages[rnd() % 4u]));
    next_page = 0u;
    memset(model, 0xFF, sizeof model);
    memset(shadow, 0xFF, sizeof shadow);
}

void setUp(void)
{
    rng = SEED;
    commits = refusals = accepted = 0u;
    tfw_boot_mock_reset();
}

void tearDown(void) {}

static void test_seeded_sessions_keep_the_handler_contract(void)
{
    uint8_t p[TFW_MAX_PAYLOAD];

    for (uint32_t s = 0u; s < SESSIONS; s++) {
        open_session();
        for (uint32_t i = 0u; i < STEPS; i++) {
            uint8_t cmd;
            uint8_t len = make_request(&cmd, p);
            step(cmd, p, len);
        }
    }
    /* Proof the sessions reached both sides and some ran to the end. */
    TEST_ASSERT_GREATER_THAN_UINT32(0u, commits);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, refusals);
    TEST_ASSERT_GREATER_THAN_UINT32(0u, accepted);
}

/* Every command id, in every state a session can be in, with random payloads:
 * the boot's six are answered by their rules, every other id is unknown and
 * moves nothing. */
static void test_every_command_id_in_every_state(void)
{
    uint8_t p[TFW_MAX_PAYLOAD];

    for (uint32_t r = 0u; r < ID_ROUNDS; r++) {
        for (uint32_t id = 0u; id < 256u; id++) {
            uint8_t len = (uint8_t)(rnd() % (TFW_MAX_PAYLOAD + 1u));

            open_session();
            switch (r % 4u) {
            case 0:
                break;                                  /* IDLE */
            case 1:
                wr_u32(p, 8u * ota.page_size);          /* RECEIVING, part-way */
                step(TFW_CMD_OTA_BEGIN, p, 4u);
                wr_u32(p, 0u);
                memset(&p[4], 0x11, 7u);
                step(TFW_CMD_OTA_DATA, p, 11u);
                break;
            case 2:
                wr_u32(p, 8u * ota.page_size);          /* FAILED */
                step(TFW_CMD_OTA_BEGIN, p, 4u);
                step(TFW_CMD_OTA_END, p, 0u);
                TEST_ASSERT_EQUAL_UINT8((uint8_t)TFW_OTA_FAILED, ota.state);
                break;
            default: {
                uint32_t total = 2u * ota.page_size;     /* COMPLETE */
                if (total <= TFW_BOOT_TRAILER_LEN) {
                    total = 8u * ota.page_size;
                }
                wr_u32(p, total);
                step(TFW_CMD_OTA_BEGIN, p, 4u);
                for (uint32_t off = 0u; off < total; ) {
                    uint32_t n = total - off < 32u ? total - off : 32u;
                    wr_u32(p, off);
                    memset(&p[4], (int)(off & 0xFFu), n);
                    step(TFW_CMD_OTA_DATA, p, (uint8_t)(4u + n));
                    off += n;
                }
                step(TFW_CMD_OTA_END, p, 0u);
                TEST_ASSERT_EQUAL_UINT8((uint8_t)TFW_OTA_COMPLETE, ota.state);
                break;
            }
            }
            for (uint8_t i = 0u; i < len; i++) {
                p[i] = (uint8_t)rnd();
            }
            step((uint8_t)id, p, len);
        }
    }
}

/* Every single-bit flip of a valid request, under every reset cause: none is a
 * request. The unflipped one is a request only after a watchdog reset. */
static void test_every_bit_flip_of_a_request_is_refused(void)
{
    uint8_t req[TFW_BOOT_REQUEST_LEN];

    for (uint32_t cause = 0u; cause <= (uint32_t)TFW_RESET_PANIC; cause++) {
        tfw_boot_request_encode(req);
        TEST_ASSERT_EQUAL_UINT8(cause == (uint32_t)TFW_RESET_WATCHDOG ? 1u : 0u,
                                tfw_boot_request_valid(req, (tfw_reset_cause_t)cause));
        for (uint32_t bit = 0u; bit < 8u * TFW_BOOT_REQUEST_LEN; bit++) {
            tfw_boot_request_encode(req);
            req[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            TEST_ASSERT_EQUAL_UINT8(0u,
                tfw_boot_request_valid(req, (tfw_reset_cause_t)cause));
        }
    }
}

/* Random regions, mostly near a valid request, under random causes: valid
 * exactly when the bytes are the request and the reset was the watchdog. And
 * taking it, valid or not, leaves the region cleared. */
static void test_random_requests_are_taken_by_their_contract(void)
{
    uint8_t good[TFW_BOOT_REQUEST_LEN];
    unsigned long hits = 0u;

    tfw_boot_request_encode(good);
    for (uint32_t it = 0u; it < REQ_ROUNDS; it++) {
        uint8_t           *req   = malloc(TFW_BOOT_REQUEST_LEN);
        tfw_reset_cause_t  cause = (tfw_reset_cause_t)(rnd() % 8u);
        uint8_t            want, got;

        TEST_ASSERT_NOT_NULL(req);
        memcpy(req, good, TFW_BOOT_REQUEST_LEN);
        switch (rnd() % 4u) {
        case 0:
            for (uint32_t i = 0u; i < TFW_BOOT_REQUEST_LEN; i++) {
                req[i] = (uint8_t)rnd();
            }
            break;
        case 1:
            req[rnd() % TFW_BOOT_REQUEST_LEN] = (uint8_t)rnd();
            break;
        case 2:
            break;
        default:
            for (uint32_t k = 1u + rnd() % 6u; k > 0u; k--) {
                req[rnd() % TFW_BOOT_REQUEST_LEN] ^= (uint8_t)(1u << (rnd() % 8u));
            }
            break;
        }
        want = (uint8_t)(cause == TFW_RESET_WATCHDOG
                         && memcmp(req, good, TFW_BOOT_REQUEST_LEN) == 0);
        hits += want;
        TEST_ASSERT_EQUAL_UINT8(want, tfw_boot_request_valid(req, cause));

        tfw_boot_mock_set_handoff(req, TFW_BOOT_REQUEST_LEN);
        got = tfw_boot_request_take(cause);
        TEST_ASSERT_EQUAL_UINT8(want, got);
        for (uint32_t i = 0u; i < TFW_BOOT_REQUEST_LEN; i++) {
            TEST_ASSERT_EQUAL_HEX8(0u, tfw_boot_mock_handoff()[i]);
        }
        free(req);
    }
    TEST_ASSERT_GREATER_THAN_UINT32(0u, hits);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_seeded_sessions_keep_the_handler_contract);
    RUN_TEST(test_every_command_id_in_every_state);
    RUN_TEST(test_every_bit_flip_of_a_request_is_refused);
    RUN_TEST(test_random_requests_are_taken_by_their_contract);
    return UNITY_END();
}
