/*
 * Mutation tests for boot/recovery/recovery_ota.c (#163): each test pins a
 * fault the session suite (test_boot_recovery_session.c) let through when it
 * was planted in the source -- a bound off by one, a zero limit read as a
 * limit, a progress command dropped, a page write's failure swallowed, the
 * STAGED write repeated per page, the floor not passed on, a refused update
 * not aborted or not stored. Linked as test_boot_recovery_session is.
 */
#include <setjmp.h>
#include <string.h>
#include "unity.h"
#include "boot_recovery.h"
#include "boot_policy.h"
#include "boot_hal_mock.h"
#include "boot_hal.h"
#include "boot_cdi.h"
#include "boot_ota.h"
#include "boot_platform.h"
#include "boot_store.h"
#include "boot_trial.h"
#include "frame.h"

#define IDLE  100u
#define NEVER 100000u   /* an escape no bounded session reaches */

static tfw_status_t evaluate_result;
static uint32_t     evaluated_floor;
static unsigned     evaluates;
static unsigned     hand_overs;
static tfw_status_t floor_read_result;
static uint32_t     el_counter;
static tfw_boot_platform_t plat;

static jmp_buf  escape;
static unsigned escape_at;
static unsigned feed_at;
static void   (*feed_fn)(void);

tfw_status_t tfw_boot_evaluate(uint32_t floor, tfw_boot_verdict_t *out)
{
    memset(out, 0, sizeof *out);
    evaluated_floor = floor;
    evaluates++;
    return evaluate_result;
}

void tfw_boot_hand_over(const tfw_boot_verdict_t *v)
{
    (void)v;
    hand_overs++;
    tfw_boot_jump_application();
}

tfw_status_t tfw_boot_floor_read(uint32_t *out)
{
    if (floor_read_result == TFW_OK) {
        *out = el_counter;
    }
    return floor_read_result;
}

tfw_status_t tfw_boot_floor_write(uint32_t value)
{
    if (floor_read_result == TFW_ERR_UNSUPPORTED) {
        return TFW_ERR_UNSUPPORTED;
    }
    el_counter = value;
    return TFW_OK;
}

/* A peer that speaks once at a chosen empty poll, and a way out of a session
 * that would otherwise never return (recovery, or a mutant's lost bound). */
static void idle_hook(unsigned empty_polls)
{
    if (feed_fn && empty_polls == feed_at) {
        void (*f)(void) = feed_fn;
        feed_fn = 0;
        f();
    }
    if (empty_polls >= escape_at) {
        longjmp(escape, 1);
    }
}

void setUp(void)
{
    tfw_boot_mock_reset();
    tfw_boot_mock_on_rx_idle(idle_hook);
    memset(&plat, 0, sizeof plat);
    evaluate_result = TFW_OK;
    evaluated_floor = 0xFFFFFFFFu;
    evaluates = 0u;
    hand_overs = 0u;
    floor_read_result = TFW_ERR_UNSUPPORTED;
    el_counter = 0u;
    escape_at = NEVER;
    feed_at = 0u;
    feed_fn = 0;
}

void tearDown(void) {}

static void feed_frame(uint8_t seq, uint8_t cmd, const uint8_t *p, uint8_t n)
{
    uint8_t  buf[TFW_MAX_FRAME];
    uint16_t len = tfw_frame_encode_request(buf, sizeof buf, seq, cmd, p, n);

    TEST_ASSERT_TRUE(len > 0u);
    tfw_boot_mock_feed_rx(buf, len);
}

static void feed_begin(void)
{
    static const uint8_t p[4] = { 0x00, 0x01, 0x00, 0x00 };   /* 256 bytes */
    feed_frame(1u, TFW_CMD_OTA_BEGIN, p, 4u);
}

static void feed_data_at(uint32_t off)
{
    uint8_t p[4u + 32u];

    memset(&p[4], 0xA5, 32u);
    p[0] = (uint8_t)off;         p[1] = (uint8_t)(off >> 8);
    p[2] = (uint8_t)(off >> 16); p[3] = (uint8_t)(off >> 24);
    feed_frame(2u, TFW_CMD_OTA_DATA, p, sizeof p);
}

static void feed_data(void) { feed_data_at(0u); }

static void feed_image(uint32_t total)
{
    uint32_t off;
    for (off = 0u; off < total; off += 32u) {
        feed_data_at(off);
    }
}

static void feed_end(void)  { feed_frame(3u, TFW_CMD_OTA_END, 0, 0u); }
static void feed_ping(void) { feed_frame(7u, TFW_CMD_PING, 0, 0u); }

static void feed_junk(size_t n)
{
    uint8_t junk[64];
    TEST_ASSERT_TRUE(n <= sizeof junk);
    memset(junk, 0xEE, n);
    tfw_boot_mock_feed_rx(junk, n);
}

/* 1 when the session was left through the escape rather than returning. */
static int session(uint32_t idle_polls, uint32_t idle_bytes,
                   uint32_t max_polls, uint32_t max_bytes)
{
    tfw_boot_session_limits_t lim;

    lim.idle_polls = idle_polls;
    lim.idle_bytes = idle_bytes;
    lim.max_polls  = max_polls;
    lim.max_bytes  = max_bytes;
    if (setjmp(escape) == 0) {
        tfw_boot_session_open(&plat, &lim);
        return 0;
    }
    return 1;
}

static void recovery(tfw_status_t why)
{
    if (setjmp(escape) == 0) {
        tfw_boot_recovery(&plat, why);
    }
}

typedef struct {
    unsigned     events;
    tfw_status_t first_event;
    unsigned     responses;
    tfw_status_t resp[16];
    uint8_t      last[8];   /* the last response's payload */
    uint8_t      last_len;
} tx_t;

static tx_t drain(void)
{
    uint8_t      raw[2048];
    size_t       n = tfw_boot_mock_drain_tx(raw, sizeof raw);
    tfw_framer_t f;
    tx_t         s;
    size_t       i;

    memset(&s, 0, sizeof s);
    tfw_framer_init(&f);
    for (i = 0u; i < n; i++) {
        if (tfw_framer_push(&f, raw[i]) != TFW_FRAME_READY) { continue; }
        if (tfw_frame_op(&f) == TFW_OP_EVENT) {
            if (s.events++ == 0u) { s.first_event = tfw_frame_status(&f); }
        } else if (s.responses < 16u) {
            s.resp[s.responses++] = tfw_frame_status(&f);
            s.last_len = tfw_frame_payload_len(&f);
            if (s.last_len > sizeof s.last) { s.last_len = sizeof s.last; }
            memcpy(s.last, tfw_frame_payload(&f), s.last_len);
        }
    }
    return s;
}

static void load_platform(uint8_t boot_state, uint8_t ota_state, uint32_t floor)
{
    uint8_t      ee[TFW_BOOT_EE_LEN];
    tfw_status_t st;

    memset(ee, 0xFF, sizeof ee);
    plat.boot_state     = boot_state;
    plat.ota_state      = ota_state;
    plat.rollback_floor = floor;
    tfw_boot_platform_serialise(&plat, ee);
    memcpy(tfw_boot_mock_nvm(), ee, TFW_BOOT_EE_LEN);
    memset(&plat, 0, sizeof plat);
    tfw_boot_platform_load(&plat, &st);
    TEST_ASSERT_EQUAL(TFW_OK, st);
}

static tfw_boot_platform_t stored(void)
{
    tfw_boot_platform_t p;
    tfw_status_t        st;

    tfw_boot_platform_load(&p, &st);
    TEST_ASSERT_EQUAL(TFW_OK, st);
    return p;
}

/* `++idle >= idle_polls`: the bound is reached at the bound, not one past. */
static void test_the_idle_bound_is_exact(void)
{
    TEST_ASSERT_EQUAL_INT(0, session(IDLE, 0u, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT(IDLE, tfw_boot_mock_rx_empty_polls());
}

/* `++total_polls >= max_polls` is counted before the poll: the session polls
 * max_polls - 1 times and returns at the next. */
static void test_the_poll_ceiling_is_exact(void)
{
    TEST_ASSERT_EQUAL_INT(0, session(0u, 0u, 37u, 0u));
    TEST_ASSERT_EQUAL_UINT(36u, tfw_boot_mock_rx_empty_polls());
}

static void test_the_idle_byte_budget_is_exact(void)
{
    feed_junk(40u);
    TEST_ASSERT_EQUAL_INT(0, session(0u, 30u, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT(30u, tfw_boot_mock_rx_consumed());
}

static void test_the_byte_ceiling_is_exact(void)
{
    feed_junk(40u);
    TEST_ASSERT_EQUAL_INT(0, session(0u, 0u, 0u, 30u));
    TEST_ASSERT_EQUAL_UINT(30u, tfw_boot_mock_rx_consumed());
}

/* A zero in any limit is no limit (boot_recovery.h): with all four zero the
 * session answers and keeps polling until the test leaves it. */
static void test_zero_limits_are_no_limits(void)
{
    tx_t s;

    escape_at = 50u;
    feed_ping();
    TEST_ASSERT_EQUAL_INT(1, session(0u, 0u, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT(50u, tfw_boot_mock_rx_empty_polls());
    s = drain();
    TEST_ASSERT_EQUAL_UINT(1u, s.responses);
    TEST_ASSERT_EQUAL(TFW_OK, s.resp[0]);
}

/* OTA_DATA accepted is progress as OTA_BEGIN is: it restarts the idle bound. */
static void test_accepted_data_restarts_the_idle_bound(void)
{
    feed_begin();
    feed_at = 60u;
    feed_fn = feed_data;
    TEST_ASSERT_EQUAL_INT(0, session(IDLE, 0u, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT(60u + IDLE, tfw_boot_mock_rx_empty_polls());
}

/* And it restarts the byte budget: a BEGIN, 40 junk bytes, a DATA, 40 more,
 * under a budget of 100, all reach the framer and the session ends idle. */
static void test_accepted_data_restarts_the_byte_budget(void)
{
    feed_begin();
    feed_junk(40u);
    feed_data();
    feed_junk(40u);
    TEST_ASSERT_EQUAL_INT(0, session(IDLE, 100u, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT(IDLE, tfw_boot_mock_rx_empty_polls());
}

static void test_recovery_announces_the_reason_it_was_entered(void)
{
    tx_t s;

    escape_at = 5u;
    recovery(TFW_ERR_CRYPTO);
    s = drain();
    TEST_ASSERT_EQUAL_UINT(1u, s.events);
    TEST_ASSERT_EQUAL(TFW_ERR_CRYPTO, s.first_event);
}

/* Only a refused OTA_END ends a requested session; any other refusal is
 * answered and the session goes on. */
static void test_a_refused_frame_other_than_end_does_not_end_the_session(void)
{
    static const uint8_t stray[5] = { 0, 0, 0, 0, 0xEE };
    tx_t s;

    feed_frame(8u, TFW_CMD_OTA_DATA, stray, sizeof stray);
    feed_ping();
    TEST_ASSERT_EQUAL_INT(0, session(IDLE, 0u, 0u, 0u));
    s = drain();
    TEST_ASSERT_EQUAL_UINT(2u, s.responses);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, s.resp[0]);
    TEST_ASSERT_EQUAL(TFW_OK, s.resp[1]);
}

/* In recovery a refused OTA_END does not end the loop. */
static void test_a_refused_end_does_not_end_recovery(void)
{
    tx_t s;

    escape_at = 10u;
    feed_end();
    feed_ping();
    recovery(TFW_ERR_CRYPTO);
    s = drain();
    TEST_ASSERT_EQUAL_UINT(2u, s.responses);
    TEST_ASSERT_NOT_EQUAL(TFW_OK, s.resp[0]);
    TEST_ASSERT_EQUAL(TFW_OK, s.resp[1]);
    TEST_ASSERT_EQUAL_UINT(10u, tfw_boot_mock_rx_empty_polls());
}

/* The floor evaluated against is the one the tier holds, not a constant. */
static void test_the_update_is_evaluated_against_the_tiers_floor(void)
{
    load_platform(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE, 7u);
    floor_read_result = TFW_OK;
    el_counter = 7u;
    feed_begin();
    feed_image(256u);
    feed_end();
    (void)session(IDLE, 0u, 0u, 0u);
    TEST_ASSERT_EQUAL_UINT(1u, evaluates);
    TEST_ASSERT_EQUAL_UINT32(7u, evaluated_floor);
    TEST_ASSERT_EQUAL_UINT(1u, hand_overs);
}

static void test_a_refused_update_is_stored_as_failed(void)
{
    load_platform(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE, 0u);
    evaluate_result = TFW_ERR_CRYPTO;
    feed_begin();
    feed_image(256u);
    feed_end();
    TEST_ASSERT_EQUAL_INT(0, session(IDLE, 0u, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT(0u, hand_overs);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, plat.ota_state);
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_OTA_FAILED, stored().ota_state);
}

/* A refused update aborts the transfer: OTA_STATUS in the same recovery
 * reads an idle transfer with nothing received, not a complete one. */
static void test_a_refused_update_aborts_the_transfer(void)
{
    tx_t s;

    load_platform(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE, 0u);
    evaluate_result = TFW_ERR_CRYPTO;
    escape_at = 10u;
    feed_begin();
    feed_image(256u);
    feed_end();
    feed_frame(4u, TFW_CMD_OTA_STATUS, 0, 0u);
    recovery(TFW_ERR_CRYPTO);
    TEST_ASSERT_EQUAL_UINT(1u, evaluates);
    TEST_ASSERT_EQUAL_UINT(0u, hand_overs);
    s = drain();
    TEST_ASSERT_EQUAL_UINT(1u + 8u + 2u, s.responses);
    TEST_ASSERT_EQUAL(TFW_OK, s.resp[10]);
    TEST_ASSERT_EQUAL_UINT8(5u, s.last_len);
    TEST_ASSERT_EQUAL_UINT8(TFW_OTA_IDLE, s.last[0]);
    TEST_ASSERT_EQUAL_UINT8(0u, s.last[1]);
    TEST_ASSERT_EQUAL_UINT8(0u, s.last[2]);
}

/* A page the flash refuses is the DATA frame's answer, not TFW_OK. */
static void test_a_page_the_flash_refuses_refuses_the_data(void)
{
    tx_t     s;
    unsigned i;
    unsigned refused = 0u;

    load_platform(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE, 0u);
    tfw_boot_mock_fail_writes(1u);
    feed_begin();
    feed_image(256u);
    TEST_ASSERT_EQUAL_INT(0, session(IDLE, 0u, 0u, 0u));
    s = drain();
    TEST_ASSERT_EQUAL_UINT(9u, s.responses);
    TEST_ASSERT_EQUAL(TFW_OK, s.resp[0]);
    for (i = 1u; i < s.responses; i++) {
        if (s.resp[i] != TFW_OK) { refused++; }
    }
    TEST_ASSERT_TRUE(refused > 0u);
}

/* STAGED is written once, at the first page, not at every page: with the
 * EEPROM torn at its second write, a transfer of two pages leaves the block
 * whole and STAGED. */
static void test_staged_is_written_once_per_transfer(void)
{
    load_platform(TFW_BOOT_STATE_GOOD, TFW_BOOT_OTA_IDLE, 0u);
    tfw_boot_mock_tear_nvm(1u, 0u, 0x5Au);
    feed_begin();
    feed_image(256u);
    TEST_ASSERT_EQUAL_INT(0, session(IDLE, 0u, 0u, 0u));
    TEST_ASSERT_EQUAL_UINT(2u, tfw_boot_mock_page_writes());
    TEST_ASSERT_EQUAL_UINT8(TFW_BOOT_STATE_STAGED, stored().boot_state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_idle_bound_is_exact);
    RUN_TEST(test_the_poll_ceiling_is_exact);
    RUN_TEST(test_the_idle_byte_budget_is_exact);
    RUN_TEST(test_the_byte_ceiling_is_exact);
    RUN_TEST(test_zero_limits_are_no_limits);
    RUN_TEST(test_accepted_data_restarts_the_idle_bound);
    RUN_TEST(test_accepted_data_restarts_the_byte_budget);
    RUN_TEST(test_recovery_announces_the_reason_it_was_entered);
    RUN_TEST(test_a_refused_frame_other_than_end_does_not_end_the_session);
    RUN_TEST(test_a_refused_end_does_not_end_recovery);
    RUN_TEST(test_the_update_is_evaluated_against_the_tiers_floor);
    RUN_TEST(test_a_refused_update_is_stored_as_failed);
    RUN_TEST(test_a_refused_update_aborts_the_transfer);
    RUN_TEST(test_a_page_the_flash_refuses_refuses_the_data);
    RUN_TEST(test_staged_is_written_once_per_transfer);
    return UNITY_END();
}
